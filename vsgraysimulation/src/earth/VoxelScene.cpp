#include <vsgraysim/earth/VoxelScene.h>
#include <vsgraysim/earth/VoxelScenePackage.h>
#include <vsgraysim/earth/TerrainDisplay.h>
#include <vsgraysim/earth/VoxelEarthPatch.h>
#include <vsgraysim/earth/VoxelOcean.h>
#include "VoxelTerrain.h"
#include "VoxelSensorMaterials.h"
#include "VoxelModelWorker.h"
#include <vsgraysim/earth/SceneTasks.h>
#include <vsgraysim/earth/SurfaceMesh.h>
#include "VoxelTerrainPhysics.h"
#include "VoxelModelVoxelizer.h"
#include "VoxelDynamics.h"
#include <LinearMath/btScalar.h>
#include <simcore/Orientation.h>
#include <vsg/maths/transform.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <deque>

namespace vsgraysim::earth {
namespace {
using json=nlohmann::json;namespace fs=std::filesystem;
constexpr uint64_t maxTick=1'000'000'000'000ULL;
constexpr size_t maxObjects=256,maxEvents=65536,maxSceneCells=16'000'000,maxSceneVertices=8'000'000;
uint64_t integer(const json& j,uint64_t maximum,const char* name) {
    if(!j.is_number_integer()||j.get<double>()<0||j.get<double>()>maximum)
        throw std::invalid_argument(std::string("invalid ")+name);
    return j.get<uint64_t>();
}
double number(const json& j,double lo,double hi,const char* name) {
    const double value=j.get<double>();
    if(!std::isfinite(value)||value<lo||value>hi)throw std::invalid_argument(std::string("invalid ")+name);
    return value;
}
json utcOrigin(const json& j) {
    const int year=int(integer(j.value("year",2026),9999,"UTC year"));
    const int month=int(integer(j.value("month",9),12,"UTC month"));
    const int day=int(integer(j.value("day",10),31,"UTC day"));
    const double hours=number(j.value("hours_utc",4.),0,std::nextafter(24.,0.),"UTC hours");
    const bool leap=year%4==0&&(year%100!=0||year%400==0);
    const int lengths[]={31,leap?29:28,31,30,31,30,31,31,30,31,30,31};
    if(year<1900||year>2200||month<1||day<1||day>lengths[month-1])throw std::invalid_argument("invalid UTC calendar date (supported years 1900..2200)");
    return {{"year",year},{"month",month},{"day",day},{"hours_utc",hours},
        {"time_scale",number(j.value("time_scale",1.),0,3600,"UTC time scale")}};
}
vsg::dvec3 triple(const json& j,const char* name) {
    if(!j.is_array()||j.size()!=3)throw std::invalid_argument(std::string("invalid ")+name);
    return {number(j[0],-1e8,1e8,name),number(j[1],-1e8,1e8,name),number(j[2],-1e8,1e8,name)};
}
std::string identity(const json& j) {
    const auto value=j.get<std::string>();
    if(value.empty()||value.size()>128||value=="__earth_surface"||value.find_first_of("\r\n\t")!=value.npos)
        throw std::invalid_argument("invalid voxel object id");
    return value;
}
json material(const json& source) {return normalizeVoxelMaterial(source);}

vsg::dmat4 pose(const json& object) {
    auto position=triple(object.value("position_m",json::array({0,0,0})),"position_m");
    auto rotation=triple(object.value("rotation_deg",json::array({0,0,0})),"rotation_deg");
    return vsg::translate(position)*vsg::rotate(simcore::headingRotation(rotation.y,rotation.x,rotation.z));
}
json matrixJson(const vsg::dmat4& matrix) {
    auto values=json::array();for(int c=0;c<4;++c)for(int r=0;r<4;++r)values.push_back(matrix[c][r]);return values;
}
vsg::dmat4 matrix(const json& j) {
    if(!j.is_array()||j.size()!=16)throw std::invalid_argument("pose matrix requires 16 values");
    vsg::dmat4 m;for(int c=0;c<4;++c)for(int r=0;r<4;++r)m[c][r]=number(j[c*4+r],-1e8,1e8,"pose matrix");
    for(int c=0;c<4;++c)if(std::abs(m[c][3]-(c==3?1.:0.))>1e-10)throw std::invalid_argument("non-affine voxel pose");
    vsg::dvec3 axis[3];for(int c=0;c<3;++c)axis[c]={m[c][0],m[c][1],m[c][2]};
    for(int a=0;a<3;++a)for(int b=0;b<3;++b)
        if(std::abs(vsg::dot(axis[a],axis[b])-(a==b?1.:0.))>1e-8)throw std::invalid_argument("voxel pose must be a rigid rotation");
    if(vsg::dot(vsg::cross(axis[0],axis[1]),axis[2])<.999999)throw std::invalid_argument("reflected voxel pose");
    return m;
}
void budgets(VoxelSceneSnapshot& scene) {
    if(scene.objects.size()>maxObjects)throw std::invalid_argument("voxel scene exceeds 256 objects");
    size_t cells=0,vertices=0,earthBytes=0;std::set<const VoxelEarthPatch*> patches;
    for(auto& [id,o]:scene.objects) {
        refreshVoxelEarthObject(o);
        cells+=o.volume->cellCount();vertices+=o.surface().vertices().size();
        earthBytes+=o.surface().imagery_rgba.size();
        if(o.earth&&patches.insert(o.earth.get()).second)earthBytes+=o.earth->residentBytes();
        if(earthBytes>128ULL*1024*1024)throw std::invalid_argument("voxel scene exceeds 128 MiB Earth height/imagery budget");
        if(!o.collision)o.collision=std::make_shared<VoxelTerrainPhysics>(o.volume);
        if(cells>maxSceneCells||vertices>maxSceneVertices)throw std::invalid_argument("voxel scene exceeds cell/mesh budget");
        if(o.motionOriginTick>scene.tick)throw std::invalid_argument("motion epoch is in the future");
        matrix(matrixJson(o.pose(scene.tick,scene.fixedStep)));
        if(o.earth&&o.body.motion=="static"&&o.parentId.empty())for(int c=0;c<4;++c)for(int r=0;r<4;++r)
            if(std::abs(o.basePose[c][r]-(c==r?1.:0.))>1e-10)throw std::invalid_argument("georeferenced Earth roots must stay in the scene chart; change geo_anchor and reload data to relocate the working set");
        if(o.body.motion=="static"&&(vsg::length(o.velocity)>0||vsg::length(o.angularVelocity)>0))throw std::invalid_argument("static voxel body has velocity");
        if(o.body.motion=="dynamic"&&o.motionOriginTick!=scene.tick)throw std::invalid_argument("dynamic voxel pose must be at the current tick");
    }
}
using Cache=std::map<std::string,std::shared_ptr<const VoxelTerrain>>;
using EarthPatches=std::map<std::string,std::shared_ptr<const VoxelEarthPatch>>;
VoxelSceneObject objectImpl(const json& j,const fs::path& directory,Cache& cache,uint64_t tick,bool checkpoint,const simcore::geo::GeoAnchor& anchor,const EarthPatches& patches={},const json& appearances=json::object(),const json& policy=json::object()) {
    VoxelSceneObject result;result.id=identity(j.at("id"));result.role=j.value("role",std::string("target"));
    if(result.role!="target"&&result.role!="terrain"&&result.role!="structure")throw std::invalid_argument("unknown voxel object role");
    result.material=material(j.value("material",json::object()));result.basePose=checkpoint?matrix(j.at("base_pose")):pose(j);
    result.velocity=triple(j.value("velocity_mps",json::array({0,0,0})),"velocity_mps");
    result.angularVelocity=triple(j.value("angular_velocity_rad_s",json::array({0,0,0})),"angular velocity");
    result.motionOriginTick=checkpoint?integer(j.at("motion_origin_tick"),tick,"motion origin tick"):tick;
    if(checkpoint) {
        auto volume=j.at("volume");
        if(volume.contains("surface_appearance_ref")) {
            const auto reference=identity(volume.at("surface_appearance_ref"));volume.erase("surface_appearance_ref");
            auto found=cache.find("appearance:"+reference);
            if(found==cache.end()){volume["surface_appearance"]=appearances.at(reference);result.volume=VoxelTerrain::decodeVolume(volume);cache["appearance:"+reference]=result.volume;}
            else {auto decoded=VoxelTerrain::decodeVolume(volume);if(found->second->appearance()->cellSize!=decoded->resolution_m)throw std::invalid_argument("shared surface resolution mismatch");decoded->setAppearance(found->second->appearance());result.volume=decoded;}
        } else result.volume=VoxelTerrain::decodeVolume(volume);
        result.provenance=j.value("provenance",json::object());
        if(j.contains("earth_patch_ref"))result.earth=patches.at(j.at("earth_patch_ref").get<std::string>());
        else if(j.contains("earth_patch"))result.earth=VoxelEarthPatch::decode(j.at("earth_patch"));
        if(result.earth)result.terrainMode=j.at("terrain_mode").get<std::string>();
        validateVoxelBody(result,j,true);return result;
    }
    const auto& source=j.at("source");const auto type=source.at("type").get<std::string>();
    result.provenance=source;auto cacheKey=source.dump();
    if(type=="mesh") {
        auto path=fs::path(source.at("path").get<std::string>());if(path.is_relative())path=directory/path;
        path=fs::weakly_canonical(path);if(!fs::is_regular_file(path))throw std::invalid_argument("missing voxel asset: "+path.string());
        result.provenance["path"]=path.string();result.provenance["size_bytes"]=fs::file_size(path);
        result.provenance["modified_time"]=fs::last_write_time(path).time_since_epoch().count();
        cacheKey=result.provenance.dump();
        auto found=cache.find(cacheKey);if(found!=cache.end()){result.volume=found->second;if(result.volume->appearance()&&!j.value("material",json::object()).contains("color_rgba"))result.material["color_rgba"]={1,1,1,1};validateVoxelBody(result,j,false);return result;}
        VoxelModelConfig config;config.maxDimension=int(integer(source.value("max_dimension",64),512,"max dimension"));
        config.voxelSize=number(source.value("voxel_size_m",0.),0,1e6,"voxel size");
        config.margin=number(source.value("margin_m",.02),0,1e6,"voxel margin");config.fillInterior=source.value("fill_interior",true);
        config.unitsToMetres=number(source.value("units_to_metres",1.),1e-9,1e9,"model unit scale");config.upAxis=source.value("up_axis",std::string("y"));
        const auto surface=source.value("surface_detail",std::string("corner"));if(surface!="corner"&&surface!="source_clipped")throw std::invalid_argument("unknown model surface_detail");config.sourceSurface=surface=="source_clipped";
        config.workers=unsigned(integer(source.value("import_workers",4),8,"import workers"));if(!config.workers)throw std::invalid_argument("import workers must be 1..8");
        if(source.value("isolate_import",policy.value("isolate_prepare",false))){
            const auto timeout=unsigned(integer(source.value("import_timeout_ms",policy.value("timeout_ms",300000)),600000,"import timeout"));if(timeout<100)throw std::invalid_argument("import timeout must be 100..600000 ms");
            result.volume=isolatedVoxelModel(path.string(),config,source.value("import_worker",policy.value("worker",std::string())),timeout);
        }else result.volume=voxelizeFile(path.string(),config).terrain;
        if(result.volume->appearance()&&!j.value("material",json::object()).contains("color_rgba"))result.material["color_rgba"]={1,1,1,1};
    } else if(type=="box") {
        const auto& n=source.at("dimensions");if(!n.is_array()||n.size()!=3)throw std::invalid_argument("box requires three grid dimensions");
        int dims[3];size_t count=1;
        for(int a=0;a<3;++a) {
            dims[a]=int(integer(n[a],4'000'000,"box dimension"));
            if(!dims[a]||count>4'000'000ULL/dims[a])throw std::invalid_argument("box exceeds 4M cells");count*=dims[a];
        }
        const auto origin=triple(source.value("origin_m",json::array({0,0,0})),"box origin");
        const double cell=number(source.at("cell_size_m"),1e-6,1e6,"cell size");
        auto found=cache.find(cacheKey);if(found!=cache.end()){result.volume=found->second;if(result.volume->appearance()&&!j.value("material",json::object()).contains("color_rgba"))result.material["color_rgba"]={1,1,1,1};validateVoxelBody(result,j,false);return result;}
        result.volume=VoxelTerrain::fromOccupancy(origin,cell,dims[0],dims[1],dims[2],std::vector<bool>(count,true),false);
    } else if(type=="earth") {
        if(policy.value("isolate_prepare",false))result.earth=VoxelEarthPatch::decode(isolatedSceneAsset("terrain",source,anchor,(directory/"scene.json").string(),policy.value("worker",std::string()),unsigned(policy.value("timeout_ms",300000))));
        else result.earth=VoxelEarthPatch::load(source,anchor,(directory/"scene.json").string());result.volume=result.earth->initialVolume();
        result.terrainMode=source.value("mode",std::string("hd"));result.role="terrain";
        result.provenance=result.earth->provenance();
        auto supplied=j;const auto& defaults=result.earth->materials();
        json mat={{"color_rgba",{1,1,1,1}},{"temperature_k",defaults.at("lwir_params").at("base_temp")},
            {"emissivity",defaults.at("lwir_params").at("emissivity")},{"rho_tv",defaults.at("vis_params").at("albedo")}};
        mat.update(j.value("material",json::object()));result.material=material(mat);
        if(!supplied.contains("physics"))supplied["physics"]={{"motion","static"},{"fracture",true},{"support","base"}};
        validateVoxelBody(result,supplied,false);return result;
    } else if(type=="volume")result.volume=VoxelTerrain::decodeVolume(source.at("volume"));
    else throw std::invalid_argument("unknown voxel source type: "+type);
    cache[cacheKey]=result.volume;validateVoxelBody(result,j,false);return result;
}
VoxelSceneObject object(const json& j,const fs::path& directory,Cache& cache,uint64_t tick,bool checkpoint,const simcore::geo::GeoAnchor& anchor,const EarthPatches& patches={},const json& appearances=json::object(),const json& policy=json::object()) {
    auto out=objectImpl(j,directory,cache,tick,checkpoint,anchor,patches,appearances,policy);
    if(j.contains("sensor_materials"))out.sensorMaterials=VoxelSensorMaterials::load(j.at("sensor_materials"),*out.volume,out.material,checkpoint);
    return out;
}
json encodedObject(const VoxelSceneObject& o,const std::string& earthRef) {
    json result={{"id",o.id},{"role",o.role},{"base_pose",matrixJson(o.basePose)},
        {"velocity_mps",{o.velocity.x,o.velocity.y,o.velocity.z}},{"motion_origin_tick",o.motionOriginTick},
        {"angular_velocity_rad_s",{o.angularVelocity.x,o.angularVelocity.y,o.angularVelocity.z}},
        {"physics",encodeVoxelBody(o)},{"anchor_cells",o.anchors},{"broken_bonds",o.brokenBonds},{"parent_id",o.parentId},
        {"material",o.material},{"provenance",o.provenance},{"volume",o.volume->encodeVolume()}};
    if(o.sensorMaterials)result["sensor_materials"]=o.sensorMaterials->encode();
    if(o.earth){result["earth_patch_ref"]=earthRef;result["terrain_mode"]=o.terrainMode;}
    return result;
}
struct Event {uint64_t tick,sequence;json value;};
Event event(const json& j,const VoxelSceneSnapshot& scene,uint64_t sequence) {
    Event result{integer(j.at("tick"),maxTick,"event tick"),sequence,j};
    result.value["sequence"]=sequence;
    if(result.tick<=scene.tick)throw std::invalid_argument("voxel events must be in the future");
    const auto id=identity(j.at("object"));if(!scene.objects.count(id))throw std::invalid_argument("unknown voxel event object: "+id);
    const auto kind=j.at("kind").get<std::string>();
    if(kind=="damage") {
        triple(j.at("point_m"),"damage point");
        const auto space=j.value("space",std::string("scene"));if(space!="scene"&&space!="object")throw std::invalid_argument("invalid damage space");
        const double energy=number(j.value("energy_j",0.),0,1e18,"damage energy");
        const double radius=number(j.value("radius_m",0.),0,1e6,"damage radius");
        number(j.value("blast_impulse_ns",0.),0,1e12,"blast impulse");
        if(energy==0&&radius==0)throw std::invalid_argument("damage requires energy or explicit radius");
    } else if(kind=="fracture"||kind=="impulse") {
        triple(j.at("point_m"),"event point");
        const auto space=j.value("space",std::string("object"));if(space!="scene"&&space!="object")throw std::invalid_argument("invalid event space");
        if(kind=="fracture") {
            const auto normal=triple(j.at("normal"),"fracture normal");if(vsg::length(normal)<1e-12)throw std::invalid_argument("zero fracture normal");
            number(j.at("radius_m"),1e-6,1e6,"fracture radius");number(j.at("energy_j"),1e-12,1e18,"fracture energy");
            if(!scene.objects.at(id).body.fracture)throw std::invalid_argument("target does not enable face-bond fracture");
        } else triple(j.at("impulse_ns"),"impulse");
    } else if(kind=="terrain_mode") {
        const auto& object=scene.objects.at(id);const auto mode=j.at("mode").get<std::string>();
        if(!object.earth||(mode!="hd"&&mode!="voxel"))throw std::invalid_argument("terrain_mode requires Earth terrain and hd/voxel mode");
        if(mode=="hd"&&object.body.motion!="static")throw std::invalid_argument("moving terrain fragments require voxel mode");
    } else if(kind=="settle") {
        const auto& body=scene.objects.at(id).body;
        if(body.motion!="prescribed"||body.fracture)throw std::invalid_argument("rigid voxel bodies settle through contact physics, not column compaction");
    } else throw std::invalid_argument("unknown voxel event kind: "+kind);
    return result;
}
void sort(std::vector<Event>& events){std::sort(events.begin(),events.end(),[](const auto& a,const auto& b){return std::tie(a.tick,a.sequence)<std::tie(b.tick,b.sequence);});}
}

vsg::dmat4 VoxelSceneObject::pose(uint64_t tick,double step) const {
    if(tick<motionOriginTick||!std::isfinite(step)||step<=0)throw std::invalid_argument("invalid voxel motion time");
    auto result=basePose;const auto displacement=velocity*(double(tick-motionOriginTick)*step);
    result[3][0]+=displacement.x;result[3][1]+=displacement.y;result[3][2]+=displacement.z;return result;
}
vsg::dbox VoxelSceneObject::localBounds() const {
    const auto& s=surface();return {{s.min_x,s.min_height,s.min_z},{s.max_x,s.max_height,s.max_z}};
}
const EarthSurfaceSnapshot& VoxelSceneObject::surface()const{return geometry?*geometry:*volume;}
double VoxelSceneObject::massKg() const {const auto cell=volume->resolution_m;return double(volume->occupiedCount())*cell*cell*cell*material.at("density_kg_m3").get<double>()+body.ballastMassKg;}
struct VoxelScene::Impl {
    std::shared_ptr<const VoxelSceneSnapshot> current;
    std::vector<Event> pending;uint64_t nextSequence=1;bool paused=true;
    fs::path directory;Cache assets;
    std::thread::id owner=std::this_thread::get_id();
    std::function<void(VoxelSceneSnapshotPtr)> prepare;bool preparing=false;
    VoxelDynamics dynamics;
    std::map<std::string,std::function<void(VoxelSceneSnapshotPtr)>> participants;
    mutable std::mutex historyMutex;std::deque<VoxelSceneSnapshotPtr> history;
    void own() const {if(std::this_thread::get_id()!=owner)throw std::logic_error("voxel scene mutations require the owner thread");if(preparing)throw std::logic_error("voxel publication callback cannot mutate the scene");}
    void publish(std::shared_ptr<VoxelSceneSnapshot> next) {
        budgets(*next);
        if(next->revision>maxTick||next->contentRevision>maxTick)throw std::overflow_error("voxel scene revision exhausted");
        std::deque<VoxelSceneSnapshotPtr> stagedHistory;
        {std::lock_guard<std::mutex> lock(historyMutex);stagedHistory=history;}
        if(!stagedHistory.empty()&&stagedHistory.back()->tick==next->tick)stagedHistory.clear();
        stagedHistory.push_back(next);
        while(stagedHistory.size()>2&&(stagedHistory.size()>512||stagedHistory[1]->timeSeconds()<next->timeSeconds()-2.))stagedHistory.pop_front();
        preparing=true;
        try {if(prepare)prepare(next);for(const auto& [key,participant]:participants)participant(next);}
        catch(...){preparing=false;throw;}
        preparing=false;
        std::shared_ptr<const VoxelSceneSnapshot> immutable=std::move(next);
        std::lock_guard<std::mutex> lock(historyMutex);history.swap(stagedHistory);std::atomic_store(&current,std::move(immutable));
    }
};
VoxelScene::VoxelScene():impl_(std::make_unique<Impl>()){}
VoxelScene::~VoxelScene()=default;
VoxelScene::ImportPreparation VoxelScene::prepareImportAsync(const json& document,unsigned timeoutMs){
    if(timeoutMs<100||timeoutMs>600000||document.dump().size()>12000)throw std::invalid_argument("invalid import job budget");
    ImportPreparation result;const auto cancellation=result.cancellation;const auto path=(fs::current_path()/"import.vscene.json").string();
    result.snapshot=std::async(std::launch::async,[document,path,timeoutMs,cancellation]{auto prepared=isolatedAsset({{"kind","scene-definition"},{"document",document},{"document_path",path}},std::string(),timeoutMs,cancellation);if(cancellation->load())throw std::runtime_error("asset import cancelled");return VoxelScene::load(prepared,path)->snapshot();});return result;
}
VoxelScene::Preparation VoxelScene::prepareFileAsync(const std::string& path,unsigned timeoutMs){
    if(timeoutMs<100||timeoutMs>600000)throw std::invalid_argument("scene preparation timeout must be 100..600000 ms");Preparation result;const auto cancellation=result.cancellation;const auto absolute=fs::absolute(path).string();
    result.document=std::async(std::launch::async,[absolute,timeoutMs,cancellation]{return isolatedAsset({{"kind","scene"},{"path",absolute}},std::string(),timeoutMs,cancellation);});return result;
}
std::unique_ptr<VoxelScene> VoxelScene::open(const std::string& path) {
    if(fs::file_size(path)>256ULL*1024*1024)throw std::invalid_argument("voxel document exceeds 256 MiB");
    std::ifstream input(path);if(!input)throw std::runtime_error("cannot open voxel scene: "+path);
    json document;input>>document;if(document.value("format",std::string())==VoxelScenePackage::format)document=VoxelScenePackage::checkpoint(path);return load(document,fs::absolute(path).string());
}
std::unique_ptr<VoxelScene> VoxelScene::load(const json& document,const std::string& path) {
    auto scene=std::unique_ptr<VoxelScene>(new VoxelScene);auto& state=*scene->impl_;
    state.directory=path.empty()?fs::current_path():fs::absolute(path).parent_path();
    const auto format=document.at("format").get<std::string>();const bool checkpoint=format=="voxel-scene-checkpoint-v1";
    if(!checkpoint&&format!="voxel-scene-v1")throw std::invalid_argument("unsupported voxel scene format");
    const auto dynamics=document.value("dynamics_model",std::string("face-bonds-bullet-v1"));
    if(dynamics!="face-bonds-bullet-v1"&&dynamics!="face-bonds-bullet-earth-v1")throw std::invalid_argument("unsupported voxel dynamics model");
    if(document.contains("bullet_version")&&integer(document.at("bullet_version"),100000,"Bullet version")!=BT_BULLET_VERSION)throw std::invalid_argument("checkpoint Bullet version differs from this build");
    if(document.value("coordinates",std::string("east-up-south-metres"))!="east-up-south-metres")throw std::invalid_argument("unsupported voxel coordinate convention");
    auto next=std::make_shared<VoxelSceneSnapshot>();next->fixedStep=number(document.value("fixed_step_seconds",.02),.001,.1,"fixed step");
    next->utcOrigin=utcOrigin(document.value("utc_origin",json::object()));
    next->weather=SceneWeather::load(document.value("weather",json()));
    next->gravity=triple(document.value("gravity_mps2",json::array({0,-9.81,0})),"gravity");
    next->nextFragment=integer(document.value("next_fragment",1),maxTick,"next fragment");if(!next->nextFragment)throw std::invalid_argument("invalid fragment sequence");
    const auto anchor=document.value("geo_anchor",json::object());next->anchor.lat_deg=number(anchor.value("lat",0.),-90,90,"anchor latitude");
    next->anchor.lon_deg=number(anchor.value("lon",0.),-180,180,"anchor longitude");next->anchor.alt_m=number(anchor.value("alt",0.),-10000,1e6,"anchor altitude");
    next->anchor.heading_deg=number(anchor.value("heading_deg",0.),-360,360,"anchor heading");

    next->tick=checkpoint?integer(document.at("tick"),maxTick,"scene tick"):0;
    next->revision=checkpoint?integer(document.at("revision"),maxTick,"scene revision"):1;
    next->contentRevision=checkpoint?integer(document.at("content_revision"),maxTick,"content revision"):1;
    if(!next->revision||!next->contentRevision||next->contentRevision>next->revision)throw std::invalid_argument("invalid voxel scene revisions");
    if(document.contains("preparation_diagnostics")){next->loadDiagnostics=document.at("preparation_diagnostics");if(!next->loadDiagnostics.is_array()||next->loadDiagnostics.size()>maxObjects+1)throw std::invalid_argument("invalid scene preparation diagnostics");}
    EarthPatches patches;
    if(document.contains("earth_patches")) {
        const auto& values=document.at("earth_patches");if(!checkpoint||!values.is_object()||values.size()>maxObjects)throw std::invalid_argument("invalid Earth checkpoint patch table");
        size_t bytes=0;
        for(const auto& entry:values.items()) {auto patch=VoxelEarthPatch::decode(entry.value());bytes+=patch->residentBytes();if(bytes>128ULL*1024*1024)throw std::invalid_argument("Earth checkpoint exceeds 128 MiB data budget");patches.emplace(identity(entry.key()),std::move(patch));}
    }
    const auto appearances=document.value("surface_appearances",json::object());if(!appearances.is_object()||appearances.size()>maxObjects)throw std::invalid_argument("invalid surface appearance table");
    const auto& objects=document.at("objects");if(!objects.is_array()||objects.size()>maxObjects)throw std::invalid_argument("invalid voxel objects");
    if(checkpoint){
        if(document.contains("ocean"))next->ocean=VoxelOcean::load(document.at("ocean"),&next->anchor,(state.directory/"scene.json").string());
        for(const auto& item:objects){auto o=object(item,state.directory,state.assets,next->tick,true,next->anchor,patches,appearances);if(!next->objects.emplace(o.id,std::move(o)).second)throw std::invalid_argument("duplicate voxel object");}
    }else{
    const auto loading=document.value("loading",json::object());if(!loading.is_object())throw std::invalid_argument("loading must be an object");
    for(const auto& entry:loading.items())if(entry.key()!="workers"&&entry.key()!="target_jobs"&&entry.key()!="isolate_prepare"&&entry.key()!="worker"&&entry.key()!="timeout_ms")throw std::invalid_argument("unknown loading field: "+entry.key());
    const auto prepareTimeout=unsigned(integer(loading.value("timeout_ms",300000),600000,"prepare timeout"));if(prepareTimeout<100)throw std::invalid_argument("prepare timeout must be 100..600000 ms");loading.value("isolate_prepare",false);loading.value("worker",std::string());
    const unsigned workers=unsigned(integer(loading.value("workers",1),8,"loading workers")),targets=unsigned(integer(loading.value("target_jobs",1),8,"target loading jobs"));SceneTasks tasks(workers,targets);
    std::future<std::shared_ptr<const VoxelOcean>> ocean;auto oceanPreparationMs=std::make_shared<double>(0);
    if(document.contains("ocean")){const auto definition=document.at("ocean");const auto directory=state.directory;const auto anchor=next->anchor;ocean=tasks.submit(SceneTaskKind::Ocean,[definition,directory,anchor,loading,prepareTimeout,oceanPreparationMs]{const auto start=std::chrono::steady_clock::now();std::shared_ptr<const VoxelOcean> water;if(loading.value("isolate_prepare",false))water=VoxelOcean::load(isolatedSceneAsset("ocean",definition,anchor,(directory/"scene.json").string(),loading.value("worker",std::string()),prepareTimeout));else water=VoxelOcean::load(definition,&anchor,(directory/"scene.json").string());*oceanPreparationMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return water;});}
    struct Prepared{VoxelSceneObject object;Cache cache;double ms=0;};
    std::map<std::string,std::shared_future<Prepared>> jobs;std::vector<std::shared_future<Prepared>> ordered;std::set<std::string> identities;
    for(const auto& item:objects){const auto id=identity(item.at("id"));if(!identities.insert(id).second)throw std::invalid_argument("duplicate voxel object: "+id);
        const auto type=checkpoint?std::string("checkpoint"):item.at("source").at("type").get<std::string>();
        const auto key=type=="mesh"||type=="box"?"asset:"+item.at("source").dump():"object:"+id;
        auto found=jobs.find(key);if(found!=jobs.end()){ordered.push_back(found->second);continue;}
        const auto directory=state.directory;const auto tick=next->tick;const auto anchor=next->anchor;const auto kind=type=="earth"?SceneTaskKind::Terrain:SceneTaskKind::Target;
        auto future=tasks.submit(kind,[item,directory,tick,checkpoint,anchor,loading,&patches,&appearances]{Prepared p;const auto start=std::chrono::steady_clock::now();p.object=object(item,directory,p.cache,tick,checkpoint,anchor,patches,appearances,loading);p.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return p;}).share();jobs.emplace(key,future);ordered.push_back(future);
    }
    for(size_t i=0;i<objects.size();++i){const auto& item=objects[i];const auto id=item.at("id").get<std::string>();try{const auto& p=ordered[i].get();for(const auto& entry:p.cache)state.assets.emplace(entry);
        auto o=id==p.object.id?p.object:object(item,state.directory,state.assets,next->tick,checkpoint,next->anchor,patches,appearances,loading);
        next->objects.emplace(o.id,std::move(o));next->loadDiagnostics.push_back({{"id",id},{"status","ready"},{"prepare_wall_ms",p.ms}});
      }catch(const std::exception& error){next->loadDiagnostics.push_back({{"id",id},{"status","failed"},{"message",error.what()}});if(checkpoint||item.value("required",true))throw std::runtime_error("required scene object "+id+": "+error.what());}}
    if(ocean.valid()){next->ocean=ocean.get();next->loadDiagnostics.push_back({{"id","@ocean"},{"status","ready"},{"prepare_wall_ms",*oceanPreparationMs}});}
    }
    if(checkpoint) {
        next->journal=document.at("journal");if(!next->journal.is_array()||next->journal.size()>maxEvents)throw std::invalid_argument("invalid voxel event journal");
        state.nextSequence=integer(document.at("next_sequence"),maxTick,"next event sequence");if(!state.nextSequence)throw std::invalid_argument("invalid event sequence");
        state.paused=document.at("paused").get<bool>();
    }
    budgets(*next);state.current=next;
    const auto events=document.value(checkpoint?"pending_events":"events",json::array());
    if(!events.is_array()||events.size()+next->journal.size()>maxEvents)throw std::invalid_argument("voxel event budget exceeded");
    std::set<uint64_t> sequences;
    uint64_t lastTick=0,lastSequence=0;
    for(const auto& record:next->journal) {
        const auto seq=integer(record.at("sequence"),maxTick,"journal sequence");
        const auto tick=integer(record.at("tick"),next->tick,"journal tick");
        if(!seq||seq>=state.nextSequence||!sequences.insert(seq).second||
           tick<lastTick||(tick==lastTick&&seq<=lastSequence))throw std::invalid_argument("invalid voxel journal ordering/sequence");
        integer(record.at("removed_voxels"),4'000'000,"removed voxel count");
        integer(record.at("settled_voxels"),4'000'000,"settled voxel count");
        const auto& entry=record.at("event");identity(entry.at("object"));
        if(integer(entry.at("tick"),maxTick,"journal event tick")!=tick)throw std::invalid_argument("journal event timestamp mismatch");
        const auto kind=entry.at("kind").get<std::string>();if(kind!="damage"&&kind!="settle"&&kind!="fracture"&&kind!="impulse"&&kind!="terrain_mode")throw std::invalid_argument("invalid journal event kind");
        lastTick=tick;lastSequence=seq;
    }
    for(const auto& item:events) {
        uint64_t seq=checkpoint?integer(item.at("sequence"),maxTick,"event sequence"):state.nextSequence++;
        if(!seq||!sequences.insert(seq).second||seq>=state.nextSequence)throw std::invalid_argument("invalid/duplicate event sequence");
        state.pending.push_back(event(item,*next,seq));
    }
    sort(state.pending);state.history.push_back(next);return scene;
}
VoxelSceneSnapshotPtr VoxelScene::snapshot() const {return std::atomic_load(&impl_->current);}
bool VoxelScene::paused() const {impl_->own();return impl_->paused;}
void VoxelScene::setPaused(bool paused){impl_->own();impl_->paused=paused;}
void VoxelScene::setPreparePublication(std::function<void(VoxelSceneSnapshotPtr)> prepare){impl_->own();impl_->prepare=std::move(prepare);}
void VoxelScene::addPublicationParticipant(const std::string& key,std::function<void(VoxelSceneSnapshotPtr)> participant) {
    impl_->own();if(key.empty()||!participant||impl_->participants.count(key))throw std::invalid_argument("invalid/duplicate voxel publication participant");
    impl_->participants.emplace(key,std::move(participant));
}
void VoxelScene::removePublicationParticipant(const std::string& key){impl_->own();impl_->participants.erase(key);}
std::vector<VoxelSceneSnapshotPtr> VoxelScene::history()const {
    std::lock_guard<std::mutex> lock(impl_->historyMutex);return {impl_->history.begin(),impl_->history.end()};
}
VoxelSceneSnapshotPtr VoxelScene::sample(double seconds)const {return sampleVoxelSceneHistory(history(),seconds);}
VoxelSceneReader readVoxelScene(VoxelScene& scene){return {[&scene]{return scene.snapshot();},[&scene](double t){return scene.sample(t);},[&scene]{return scene.history();},[&scene](const std::string& key,std::function<void(VoxelSceneSnapshotPtr)> prepare){scene.addPublicationParticipant(key,std::move(prepare));},[&scene](const std::string& key){scene.removePublicationParticipant(key);}};}
VoxelSceneSnapshotPtr sampleVoxelSceneHistory(const std::vector<VoxelSceneSnapshotPtr>& states,double seconds) {
    if(!std::isfinite(seconds)||seconds<0)throw std::invalid_argument("invalid voxel measurement time");
    if(states.empty()||seconds<states.front()->timeSeconds()-1e-10||seconds>states.back()->timeSeconds()+1e-10)throw std::out_of_range("voxel measurement time outside retained truth history");
    auto upper=std::lower_bound(states.begin(),states.end(),seconds,[](const auto& state,double t){return state->timeSeconds()<t;});
    if(upper==states.end())return states.back();
    if(std::abs((*upper)->timeSeconds()-seconds)<1e-10)return *upper;
    if(upper==states.begin())return *upper;
    const auto& left=**std::prev(upper);const auto& right=**upper;const double u=(seconds-left.timeSeconds())/(right.timeSeconds()-left.timeSeconds());
    auto interpolated=std::make_shared<VoxelSceneSnapshot>(left);interpolated->sampleTime=seconds;interpolated->contacts.clear();
    for(auto& [id,o]:interpolated->objects) {
        const auto found=right.objects.find(id);if(found==right.objects.end())throw std::logic_error("missing voxel trajectory endpoint");
        vsg::dvec3 a,b,scale;vsg::dquat qa,qb;
        vsg::decompose(o.pose(left.tick,left.fixedStep),a,qa,scale);vsg::decompose(found->second.pose(right.tick,right.fixedStep),b,qb,scale);
        o.basePose=vsg::translate(a*(1-u)+b*u)*vsg::rotate(simcore::interpolateRotation(qa,qb,u));o.motionOriginTick=left.tick;
    }
    return interpolated;
}
void VoxelScene::edit(const json& commands,uint64_t revision) {
    impl_->own();if(!impl_->paused)throw std::logic_error("pause voxel simulation before authoring");
    const auto current=snapshot();if(current->revision!=revision)throw std::logic_error("stale voxel scene revision");
    if(!commands.is_array()||commands.empty()||commands.size()>256)throw std::invalid_argument("invalid voxel edit transaction");
    auto next=std::make_shared<VoxelSceneSnapshot>(*current);auto cache=impl_->assets;bool topology=false;
    for(const auto& command:commands) {
        const auto kind=command.at("kind").get<std::string>();
        if(kind=="environment") {
            if(command.contains("utc_origin"))next->utcOrigin=utcOrigin(command.at("utc_origin"));
            if(command.contains("weather"))next->weather=SceneWeather::load(command.at("weather"));
            continue;
        }
        if(kind=="add") {
            auto o=object(command.at("object"),impl_->directory,cache,current->tick,false,next->anchor);
            if(!next->objects.emplace(o.id,o).second)throw std::invalid_argument("duplicate voxel object: "+o.id);budgets(*next);topology=true;continue;
        }
        if(kind=="ocean"){next->ocean=VoxelOcean::load(command.at("ocean"),&next->anchor,(impl_->directory/"scene.json").string());topology=true;continue;}
        const auto id=identity(command.at("object"));auto found=next->objects.find(id);
        if(found==next->objects.end())throw std::invalid_argument("unknown voxel edit object: "+id);
        if(kind=="remove"){next->objects.erase(found);topology=true;}
        else if(kind=="place") {found->second.basePose=command.contains("pose")?matrix(command.at("pose")):pose(command);found->second.motionOriginTick=current->tick;found->second.velocity=triple(command.value("velocity_mps",json::array({0,0,0})),"velocity_mps");
            found->second.angularVelocity=triple(command.value("angular_velocity_rad_s",json::array({found->second.angularVelocity.x,found->second.angularVelocity.y,found->second.angularVelocity.z})),"angular velocity");}
        else if(kind=="material"){found->second.material=material(command.at("material"));topology=true;}
        else if(kind=="sensor_materials"){const auto& value=command.at("sensor_materials");found->second.sensorMaterials=value.is_null()?nullptr:VoxelSensorMaterials::load(value,*found->second.volume,found->second.material);topology=true;}
        else if(kind=="sensor_paint") {
            auto& o=found->second;auto chart=o.sensorMaterials?o.sensorMaterials:VoxelSensorMaterials::load({{"palette",json::array({json::object()})}},*o.volume,o.material);
            o.sensorMaterials=chart->paint(command.at("region"),command.at("material"));o.provenance["sensor_properties_source"]="user-authored";topology=true;
        }
        else if(kind=="terrain_brush") {
            auto& o=found->second;if(!o.earth||o.body.motion!="static"||!o.parentId.empty())throw std::invalid_argument("terrain brush requires a static Earth root");
            auto result=o.earth->sculpt(o.volume,command.at("brush"));o.earth=std::move(result.patch);o.volume=std::move(result.volume);pruneVoxelBodyTopology(o);o.collision.reset();o.geometry.reset();o.provenance["authored_height_edits"]=true;topology=true;
        }
        else if(kind=="terrain_mode") {
            auto& o=found->second;if(!o.earth)throw std::invalid_argument("terrain_mode requires an Earth object");
            o.terrainMode=command.at("mode").get<std::string>();topology=true;
        }
        else if(kind=="physics") {
            auto& o=found->second;o.basePose=o.pose(current->tick,current->fixedStep);o.motionOriginTick=current->tick;
            auto input=command;input["anchor_cells"]=o.anchors;input["broken_bonds"]=o.brokenBonds;input["parent_id"]=o.parentId;
            const auto bonds=o.brokenBonds;o.anchors.clear();o.brokenBonds.clear();
            const bool resetSupport=command.at("physics").contains("support");validateVoxelBody(o,input,!resetSupport);
            if(resetSupport)o.brokenBonds=bonds;topology=true;
        }
        else throw std::invalid_argument("unknown voxel edit kind: "+kind);
    }
    for(const auto& queued:impl_->pending)if(!next->objects.count(queued.value.at("object").get<std::string>()))
        throw std::invalid_argument("cancel pending events before deleting their object");
    ++next->revision;if(topology)++next->contentRevision;next->contacts.clear();
    // Retain only assets used by the published scene; repeated imports/deletes
    // must not retain every historical volume for the lifetime of the editor.
    std::set<const VoxelTerrain*> used;for(const auto& [id,o]:next->objects)used.insert(o.volume.get());
    for(auto i=cache.begin();i!=cache.end();)if(!used.count(i->second.get()))i=cache.erase(i);else ++i;
    impl_->publish(next);impl_->assets.swap(cache);
}
uint64_t VoxelScene::schedule(const json& value) {
    impl_->own();if(impl_->nextSequence>=maxTick)throw std::overflow_error("voxel event sequence exhausted");if(impl_->pending.size()+snapshot()->journal.size()>=maxEvents)throw std::invalid_argument("voxel event budget exceeded");
    auto e=event(value,*snapshot(),impl_->nextSequence);impl_->pending.push_back(e);sort(impl_->pending);return impl_->nextSequence++;
}
void VoxelScene::cancel(uint64_t sequence) {
    impl_->own();auto& pending=impl_->pending;auto found=std::find_if(pending.begin(),pending.end(),[&](const auto& e){return e.sequence==sequence;});
    if(found==pending.end())throw std::invalid_argument("unknown pending voxel event");pending.erase(found);
}
VoxelScene::Advance VoxelScene::advanceTo(double seconds,uint32_t budget) {
    impl_->own();if(!std::isfinite(seconds)||seconds<0||!budget||budget>1024)throw std::invalid_argument("invalid voxel advance target/budget");
    const auto initial=snapshot();const double ticks=std::floor(seconds/initial->fixedStep+1e-8);
    if(ticks>maxTick||ticks<double(initial->tick))throw std::invalid_argument("voxel scene cannot rewind; restore a checkpoint");
    const uint64_t target=uint64_t(ticks);Advance result;
    if(impl_->paused){result.pendingSteps=target-initial->tick;return result;}
    while(result.steps<budget&&snapshot()->tick<target) {
        auto next=std::make_shared<VoxelSceneSnapshot>(*snapshot());++next->tick;++next->revision;
        impl_->dynamics.step(*snapshot(),*next);result.buoyancyMs+=impl_->dynamics.buoyancyMs;result.waterDragMs+=impl_->dynamics.waterDragMs;result.waterMs+=impl_->dynamics.waterMs;
        size_t consumed=0;bool changed=false;
        for(const auto& e:impl_->pending) {
            if(e.tick>next->tick)break;
            const auto id=e.value.at("object").get<std::string>();auto& targetObject=next->objects.at(id);
            size_t removed=0,moved=0;VoxelFractureResult fracture;
            const auto kind=e.value.at("kind").get<std::string>();
            if(kind=="damage"||kind=="settle") {
                VoxelTerrainPhysics physics(targetObject.volume);
                if(kind=="damage") {
                    auto point=triple(e.value.at("point_m"),"damage point");
                    if(e.value.value("space",std::string("scene"))=="scene")point=vsg::inverse(targetObject.pose(next->tick,next->fixedStep))*point;
                    removed=physics.damage({point,{0,0,0},e.value.value("energy_j",0.),e.value.value("radius_m",0.)});
                } else {moved=physics.collapse();if(moved&&targetObject.sensorMaterials)targetObject.sensorMaterials=targetObject.sensorMaterials->settled(*targetObject.volume);}
                if(removed||moved) {
                    targetObject.volume=physics.commit();targetObject.collision.reset();changed=true;
                    if(!targetObject.volume->occupiedCount()){targetObject.velocity=vsg::dvec3{};targetObject.angularVelocity=vsg::dvec3{};}
                    fracture=fractureVoxelObject(*next,id);
                    const double impulse=e.value.value("blast_impulse_ns",0.);
                    if(kind=="damage"&&impulse>0.) {
                        auto origin=triple(e.value.at("point_m"),"blast origin");
                        if(e.value.value("space",std::string("scene"))=="object")
                            origin=targetObject.pose(next->tick,next->fixedStep)*origin;
                        auto ids=fracture.fragments;ids.push_back(id);
                        double mass=0.;
                        for(const auto& piece:ids) {
                            const auto& body=next->objects.at(piece);
                            if(body.body.motion=="dynamic")mass+=body.massKg();
                        }
                        for(const auto& piece:ids) {
                            auto& body=next->objects.at(piece);
                            if(body.body.motion!="dynamic"||body.massKg()<=0.)continue;
                            const auto centre=body.pose(next->tick,next->fixedStep)*body.localCentreOfMass();
                            auto direction=centre-origin;
                            direction=vsg::length(direction)>.001 ? vsg::normalize(direction) : vsg::dvec3(0,1,0);
                            impl_->dynamics.impulse(body,next->tick,next->fixedStep,centre,
                                                   direction*(impulse*body.massKg()/mass));
                        }
                    }
                }
            } else if(kind=="fracture") {const auto mode=targetObject.terrainMode;fracture=fractureVoxelObject(*next,id,&e.value);changed=changed||!fracture.fragments.empty()||mode!=targetObject.terrainMode;}
            else if(kind=="terrain_mode") {
                const auto mode=e.value.at("mode").get<std::string>();
                if(mode=="hd"&&targetObject.body.motion!="static")throw std::invalid_argument("moving terrain fragments require voxel mode");
                changed=changed||targetObject.terrainMode!=mode;targetObject.terrainMode=mode;
            }
            else {
                auto point=triple(e.value.at("point_m"),"impulse point");auto impulse=triple(e.value.at("impulse_ns"),"impulse");
                if(e.value.value("space",std::string("object"))=="object") {
                    const auto placement=targetObject.pose(next->tick,next->fixedStep);point=placement*point;
                    const auto direction=placement*vsg::dvec4(impulse,0);impulse={direction.x,direction.y,direction.z};
                }
                impl_->dynamics.impulse(targetObject,next->tick,next->fixedStep,point,impulse);
            }
            next->journal.push_back({{"sequence",e.sequence},{"tick",next->tick},{"event",e.value},{"removed_voxels",removed},{"settled_voxels",moved},
                {"broken_bonds",fracture.brokenBonds},{"fracture_energy_j",fracture.spentEnergy},{"fragments",fracture.fragments}});++consumed;
        }
        if(changed){budgets(*next);impl_->dynamics.detect(*next);}
        if(changed)++next->contentRevision;
        impl_->publish(next);
        impl_->pending.erase(impl_->pending.begin(),impl_->pending.begin()+consumed);
        ++result.steps;result.events+=consumed;
    }
    result.pendingSteps=target-snapshot()->tick;return result;
}
void VoxelScene::importPrepared(VoxelSceneSnapshotPtr prepared,uint64_t revision){
    impl_->own();if(!impl_->paused)throw std::logic_error("pause before importing assets");const auto current=snapshot();
    if(!prepared||prepared->objects.empty()||current->revision!=revision)throw std::invalid_argument("empty import or stale scene revision");
    const auto& a=current->anchor;const auto& b=prepared->anchor;if(a.lat_deg!=b.lat_deg||a.lon_deg!=b.lon_deg||a.alt_m!=b.alt_m||a.heading_deg!=b.heading_deg)throw std::invalid_argument("prepared asset geodetic chart mismatch");
    auto next=std::make_shared<VoxelSceneSnapshot>(*current);for(const auto& [id,source]:prepared->objects){auto o=source;o.basePose=o.pose(prepared->tick,prepared->fixedStep);o.motionOriginTick=current->tick;if(!next->objects.emplace(id,std::move(o)).second)throw std::invalid_argument("duplicate imported object: "+id);}
    ++next->revision;++next->contentRevision;next->contacts.clear();impl_->publish(next);
}
json encodeVoxelSceneSnapshot(VoxelSceneSnapshotPtr current) {
    if(!current)throw std::invalid_argument("empty scene snapshot");const auto& a=current->anchor;auto objects=json::array(),pending=json::array();
    json patches=json::object(),appearances=json::object();std::map<const VoxelSurfaceAppearance*,std::string> appearanceRefs;std::map<const VoxelEarthPatch*,std::string> references;
    for(const auto& [id,o]:current->objects) {
        std::string ref;
        if(o.earth) {
            auto found=references.find(o.earth.get());
            if(found==references.end()) {ref="earth-patch-"+std::to_string(references.size()+1);references.emplace(o.earth.get(),ref);patches[ref]=o.earth->encode();}
            else ref=found->second;
        }
        auto encoded=encodedObject(o,ref);
        if(const auto source=o.volume->appearance()) {
            auto [entry,inserted]=appearanceRefs.emplace(source.get(),"surface-"+std::to_string(appearanceRefs.size()+1));
            if(inserted)appearances[entry->second]=std::move(encoded["volume"]["surface_appearance"]);
            encoded["volume"].erase("surface_appearance");encoded["volume"]["surface_appearance_ref"]=entry->second;
        }
        objects.push_back(std::move(encoded));
    }
    uint64_t nextSequence=1;for(const auto& e:current->journal)nextSequence=std::max(nextSequence,e.at("sequence").get<uint64_t>()+1);
    json result={{"format","voxel-scene-checkpoint-v1"},{"coordinates","east-up-south-metres"},{"fixed_step_seconds",current->fixedStep},
        {"dynamics_model",std::any_of(current->objects.begin(),current->objects.end(),[](const auto& pair){return bool(pair.second.earth);})?"face-bonds-bullet-earth-v1":"face-bonds-bullet-v1"},{"bullet_version",BT_BULLET_VERSION},
        {"gravity_mps2",{current->gravity.x,current->gravity.y,current->gravity.z}},{"next_fragment",current->nextFragment},
        {"utc_origin",current->utcOrigin},{"weather",current->weather.encode()},{"geo_anchor",{{"lat",a.lat_deg},{"lon",a.lon_deg},{"alt",a.alt_m},{"heading_deg",a.heading_deg}}},
        {"revision",current->revision},{"content_revision",current->contentRevision},{"tick",current->tick},
        {"paused",true},{"next_sequence",nextSequence},{"objects",objects},{"journal",current->journal},{"pending_events",pending}};
    if(!appearances.empty())result["surface_appearances"]=std::move(appearances);
    if(!patches.empty())result["earth_patches"]=std::move(patches);if(current->ocean)result["ocean"]=current->ocean->encode();return result;
}
json VoxelScene::checkpoint() const {
    impl_->own();auto result=encodeVoxelSceneSnapshot(snapshot());result["paused"]=impl_->paused;result["next_sequence"]=impl_->nextSequence;
    for(const auto& e:impl_->pending){auto value=e.value;value["sequence"]=e.sequence;result["pending_events"].push_back(std::move(value));}return result;
}
void VoxelScene::saveCheckpoint(const std::string& filename) const {
    const auto document=checkpoint();const auto destination=fs::absolute(filename);
    if(!fs::is_directory(destination.parent_path()))throw std::invalid_argument("checkpoint parent directory does not exist");
    static std::atomic<uint64_t> nonce{0};
    const auto temporary=destination.string()+".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(nonce++);
    try {
        // Whitespace can multiply a detailed model atlas/corner checkpoint size.
        // Compact JSON preserves every value and the existing document budget.
        const auto bytes=document.dump();if(bytes.size()+1>256ULL*1024*1024)throw std::runtime_error("voxel checkpoint exceeds 256 MiB document budget");
        {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("cannot create checkpoint temporary file");out<<bytes<<'\n';out.flush();if(!out)throw std::runtime_error("checkpoint write failed");out.close();if(!out)throw std::runtime_error("checkpoint close failed");}
        fs::rename(temporary,destination);
    } catch(...) {std::error_code error;fs::remove(temporary,error);throw;}
}
std::optional<VoxelSceneHit> VoxelScene::raycast(const vsg::dvec3& origin,const vsg::dvec3& direction,double distance)const {
    return raycastVoxelScene(*snapshot(),origin,direction,distance);
}
std::optional<VoxelSceneHit> VoxelScene::rigidRaycast(const vsg::dvec3& origin,const vsg::dvec3& direction,double distance) {
    impl_->own();raycastVoxelScene(*snapshot(),origin,direction,distance); // validate the ray contract
    return impl_->dynamics.raycast(*snapshot(),origin,vsg::normalize(direction),distance);
}
std::optional<VoxelSceneHit> raycastVoxelScene(const VoxelSceneSnapshot& current,const vsg::dvec3& origin,const vsg::dvec3& direction,double distance,bool terrainOnly) {
    const double length=vsg::length(direction);
    if(!std::isfinite(vsg::length(origin))||!std::isfinite(length)||length<1e-12||!std::isfinite(distance)||distance<0)
        throw std::invalid_argument("invalid scene ray");
    std::optional<VoxelSceneHit> result;auto ray=direction/length;
    for(const auto& [id,o]:current.objects) {
        if(terrainOnly&&o.role!="terrain")continue;
        const auto pose=o.pose(current.tick,current.fixedStep),inverse=vsg::inverse(pose);
        const auto d=inverse*vsg::dvec4(ray,0.);vsg::dvec3 point,normal;
        const bool hit=o.earth?o.surface().intersectLocal(inverse*origin,{d.x,d.y,d.z},distance,point,normal):
            o.collision->intersectLocal(inverse*origin,{d.x,d.y,d.z},distance,point,normal);
        if(!hit)continue;
        const auto world=pose*point;const double along=vsg::dot(world-origin,ray);
        if(result&&along>=result->distance)continue;
        const auto n=pose*vsg::dvec4(normal,0.);result=VoxelSceneHit{id,along,world,{n.x,n.y,n.z}};
    }
    return result;
}
VoxelSceneDisplay makeVoxelSceneDisplay(const VoxelSceneSnapshot& scene,const VoxelSceneDisplay* reuse,bool externalOceanLighting) {
    VoxelSceneDisplay result;result.node=vsg::Group::create();result.contentRevision=scene.contentRevision;
    for(const auto& [id,o]:scene.objects) {
        auto placement=vsg::MatrixTransform::create(o.pose(scene.tick,scene.fixedStep));
        if(!o.surface().indices().empty()) {
            const bool externalLighting=externalOceanLighting&&scene.ocean&&!o.earth;
            const auto found=reuse?reuse->drawings.find(id):result.drawings.end();
            if(reuse&&found!=reuse->drawings.end()&&found->second.source.get()==&o.surface()&&found->second.material==o.material&&found->second.externalLighting==externalLighting){
                placement->addChild(found->second.node);result.drawings.emplace(id,found->second);++result.reusedDrawings;
            }else{
                auto display=externalLighting?vsg::StateGroup::create():makeTerrainDisplay(o.surface());if(externalLighting)display->addChild(makeSurfaceDraw(o.surface(),true));auto draw=display->children.front().cast<vsg::VertexIndexDraw>();
                const auto& color=o.material.at("color_rgba");draw->arrays[3]->data=vsg::vec4Array::create(1,vsg::vec4(color[0].get<float>(),color[1].get<float>(),color[2].get<float>(),color[3].get<float>()));
                placement->addChild(display);result.drawings.emplace(id,VoxelSceneDisplay::Drawing{o.geometry?o.geometry:o.volume,o.material,display,externalLighting,bool(o.earth)});++result.builtDrawings;
            }
        }
        result.placements.emplace(id,placement);result.node->addChild(placement);
    }
    return result;
}
void VoxelSceneDisplay::sync(const VoxelSceneSnapshot& scene) {
    if(contentRevision!=scene.contentRevision||placements.size()!=scene.objects.size())throw std::logic_error("rebuild voxel display after topology/material changes");
    for(const auto& [id,o]:scene.objects)placements.at(id)->matrix=o.pose(scene.tick,scene.fixedStep);
}
}
