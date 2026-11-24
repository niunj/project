#include <vsgraysim/earth/VoxelScene.h>
#include <vsgraysim/earth/VoxelOcean.h>
#include "../src/earth/VoxelTerrain.h"
#include "../src/earth/VoxelModelVoxelizer.h"
#include <fstream>
#include <vsgraysim/earth/VoxelEarthPatch.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <signal.h>
#include "../src/earth/VoxelBuoyancy.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstdlib>
#include <unordered_set>
#include <array>
#include <algorithm>
#include <cmath>
#include <btBulletDynamicsCommon.h>
#include <chrono>
#include <vsg/maths/transform.h>
int main(int argc,char** argv)try {
    using vsgraysim::earth::VoxelScene;
    if(argc<3){std::cerr<<"Usage: voxel_scene validate SCENE | run SCENE TICKS CHECKPOINT | inspect CHECKPOINT | probe SCENE\n";return 2;}
    const std::string mode=argv[1];if(mode!="validate"&&mode!="inspect"&&mode!="run"&&mode!="probe"&&mode!="compile-model"&&mode!="hydro-profile")throw std::invalid_argument("unknown voxel_scene operation");
    if(mode=="compile-model"){
        if(prctl(PR_SET_PDEATHSIG,SIGKILL)!=0)throw std::runtime_error("cannot bind worker lifetime to its parent");if(getppid()==1)return 4;
        if(argc!=4||std::filesystem::file_size(argv[2])>16384)throw std::invalid_argument("compile-model requires a bounded JOB and CBOR output");nlohmann::json j;std::ifstream(argv[2])>>j;nlohmann::json prepared;
        const auto kind=j.value("kind",std::string("model"));
        if(kind=="scene"){auto scene=VoxelScene::open(j.at("path"));prepared=scene->checkpoint();prepared["preparation_diagnostics"]=scene->snapshot()->loadDiagnostics;}
        else if(kind=="scene-definition"){auto definition=j.at("document");definition["loading"]["isolate_prepare"]=false;prepared=VoxelScene::load(definition,j.at("document_path"))->checkpoint();}
        else if(kind=="terrain"||kind=="ocean"){const auto& a=j.at("anchor");simcore::geo::GeoAnchor anchor;anchor.lat_deg=a.at("lat");anchor.lon_deg=a.at("lon");anchor.alt_m=a.at("alt");anchor.heading_deg=a.at("heading_deg");if(kind=="terrain")prepared=vsgraysim::earth::VoxelEarthPatch::load(j.at("source"),anchor,j.at("document_path"))->encode();else prepared=vsgraysim::earth::VoxelOcean::load(j.at("source"),&anchor,j.at("document_path"))->encode();}
        else if(kind=="model"){vsgraysim::earth::VoxelModelConfig c;c.maxDimension=j.at("max_dimension");c.voxelSize=j.at("voxel_size_m");c.margin=j.at("margin_m");c.unitsToMetres=j.at("units_to_metres");c.upAxis=j.at("up_axis");c.fillInterior=j.at("fill_interior");c.sourceSurface=j.at("source_surface");c.workers=j.at("workers");prepared=vsgraysim::earth::voxelizeFile(j.at("path"),c).terrain->encodeVolume();}else throw std::invalid_argument("unknown isolated asset kind");
        const auto bytes=nlohmann::json::to_cbor(prepared);if(bytes.size()>256ULL*1024*1024)throw std::runtime_error("compiled model exceeds 256 MiB worker budget");std::ofstream out(argv[3],std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(!out)throw std::runtime_error("cannot save compiled model");return 0;
    }
    auto scene=VoxelScene::open(argv[2]);
    if(mode=="hydro-profile"){
        const auto state=scene->snapshot();nlohmann::json report;for(const auto& [id,o]:state->objects)if(o.body.motion=="dynamic"&&state->ocean){
            const auto boxes=vsgraysim::earth::makeHydroBoxes(*o.volume,2.);const auto tree=vsgraysim::earth::makeHydroTree(*boxes);const auto columns=vsgraysim::earth::makeHydroBoxes(*o.volume,2.,true);const auto columnTree=vsgraysim::earth::makeHydroTree(*columns);bool columnMode=false;const auto pose=o.pose(state->tick,state->fixedStep);const auto cg=pose*o.localCentreOfMass();btBoxShape dummy({1,1,1});btRigidBody body(btRigidBody::btRigidBodyConstructionInfo(btScalar(o.massKg()),nullptr,&dummy,{1,1,1}));btTransform transform;transform.setIdentity();transform.setOrigin({btScalar(cg.x),btScalar(cg.y),btScalar(cg.z)});body.setWorldTransform(transform);body.updateInertiaTensor();
            auto evaluate=[&](const vsg::dmat4& p,const vsgraysim::earth::VoxelOcean& water){body.clearForces();return vsgraysim::earth::applyVoxelWater(body,*boxes,p,{},water,state->timeSeconds(),0,state->gravity,tree.get(),columnMode?columns.get():nullptr,columnMode?columnTree.get():nullptr);};
            auto flat=*state->ocean;flat.harmonics.clear();flat.wakes.clear();flat.refreshBounds();const auto rest=evaluate(pose,flat);const double displaced=body.getTotalForce().y()/9.81/flat.density;double torques[2];int n=0;for(double deg:{-1.,1.}){auto p=pose*vsg::translate(o.localCentreOfMass())*vsg::rotate(vsg::radians(deg),vsg::dvec3(0,0,1))*vsg::translate(-o.localCentreOfMass());evaluate(p,flat);const auto t=body.getTotalTorque();const auto axis=pose*vsg::dvec4(0,0,1,0);torques[n++]=t.x()*axis.x+t.y()*axis.y+t.z()*axis.z;}
            const double gm=-(torques[1]-torques[0])/(2*vsg::radians(1.)*9.81*flat.density*displaced);
            nlohmann::json stats={{"mass_kg",o.massKg()},{"ballast_mass_kg",o.body.ballastMassKg},{"centre_of_mass_m",{o.localCentreOfMass().x,o.localCentreOfMass().y,o.localCentreOfMass().z}},{"flat_displacement_m3",displaced},{"estimated_GM_m",gm},{"hydro_boxes",boxes->size()},{"pressure_columns",columns->size()}};
            btVector3 referenceForce,referenceTorque;for(bool cache:{false,true}){columnMode=cache;if(cache){unsetenv("VSGRAYSIM_OCEAN_NO_SPATIAL_PHASE_CACHE");unsetenv("VSGRAYSIM_OCEAN_NO_EXPONENTIAL_CACHE");}else{setenv("VSGRAYSIM_OCEAN_NO_SPATIAL_PHASE_CACHE","1",1);setenv("VSGRAYSIM_OCEAN_NO_EXPONENTIAL_CACHE","1",1);}std::vector<double> pressure,all;for(int i=0;i<25;++i){const auto timing=evaluate(pose,*state->ocean);if(i>=5){pressure.push_back(timing.pressureMs);all.push_back(timing.totalMs);}}std::sort(pressure.begin(),pressure.end());std::sort(all.begin(),all.end());stats[cache?"cached":"uncached"]={{"pressure_median_ms",pressure[pressure.size()/2]},{"water_median_ms",all[all.size()/2]}};if(!cache){referenceForce=body.getTotalForce();referenceTorque=body.getTotalTorque();}else{stats["force_difference_n"]=(body.getTotalForce()-referenceForce).length();stats["torque_difference_nm"]=(body.getTotalTorque()-referenceTorque).length();stats["force_relative_difference"]=(body.getTotalForce()-referenceForce).length()/std::max(1.,double(referenceForce.length()));stats["buoyancy_lever_difference_m"]=(body.getTotalTorque()-referenceTorque).length()/std::max(1.,double(referenceForce.length()));}}unsetenv("VSGRAYSIM_OCEAN_NO_SPATIAL_PHASE_CACHE");unsetenv("VSGRAYSIM_OCEAN_NO_EXPONENTIAL_CACHE");report[id]=stats;
        }std::cout<<report.dump(2)<<'\n';return 0;
    }
    if(mode=="probe"){scene->setPaused(false);scene->advanceTo(scene->snapshot()->timeSeconds()+scene->snapshot()->fixedStep,1);scene->setPaused(true);}
    if(mode=="run") {
        if(argc!=5)throw std::invalid_argument("run requires TICKS and CHECKPOINT");
        size_t used=0;const std::string text=argv[3];if(text.empty()||text[0]=='-')throw std::invalid_argument("invalid tick count");
        const uint64_t count=std::stoull(text,&used);if(used!=text.size()||count>1'000'000)throw std::invalid_argument("tick count exceeds run budget");
        const auto start=scene->snapshot();const double target=(start->tick+count)*start->fixedStep;
        scene->setPaused(false);while(scene->advanceTo(target,128).pendingSteps){}
        scene->setPaused(true);scene->saveCheckpoint(argv[4]);
    } else if(argc!=3)throw std::invalid_argument("unexpected voxel_scene arguments");
    const auto s=scene->snapshot();nlohmann::json objects=nlohmann::json::array();
    for(const auto& [id,o]:s->objects){nlohmann::json stats={{"id",id},{"role",o.role},{"mass_kg",o.massKg()},{"voxel_size_m",o.volume->resolution_m},{"grid_dimensions",o.volume->dimensions()},{"occupied_voxels",o.volume->occupiedCount()},{"surface_vertices",o.surface().vertices().size()},{"surface_triangles",o.surface().indices().size()/3}};
        if(s->ocean&&o.body.motion=="dynamic"){auto boxes=vsgraysim::earth::makeHydroBoxes(*o.volume,2.);double wet=0,total=0;const auto pose=o.pose(s->tick,s->fixedStep);for(const auto& box:*boxes){const double volume=box.size.x*box.size.y*box.size.z;const auto p=pose*(box.low+box.size*.5);auto matrix=pose;const auto low=pose*box.low;for(int a=0;a<3;++a)matrix[3][a]=low[a];const auto mean=s->ocean->meanSurface(p.x,p.z);wet+=volume*vsgraysim::earth::clipBuoyancyBox(box.size,matrix,{0,1,0},mean.x).fraction;total+=volume;}stats["mean_sea_displacement_m3"]=wet;stats["neutral_bulk_density_kg_m3_at_pose"]=total>0?s->ocean->density*wet/total:0;stats["hydro_boxes"]=boxes->size();}
        if(std::getenv("VSGRAYSIM_SURFACE_DIAGNOSTICS")){using Point=std::array<int64_t,3>;using Triangle=std::array<Point,3>;struct Hash{size_t operator()(const Triangle& t)const{size_t h=0;for(const auto& p:t)for(auto v:p)h^=std::hash<int64_t>{}(v)+0x9e3779b9+(h<<6)+(h>>2);return h;}};std::unordered_set<Triangle,Hash> seen;size_t duplicates=0,collapsed=0;const auto& points=o.surface().vertices();const auto& indices=o.surface().indices();for(size_t i=0;i+2<indices.size();i+=3){Triangle t;for(int c=0;c<3;++c)for(int a=0;a<3;++a)t[c][a]=std::llround(points[indices[i+c]][a]*1e8);std::sort(t.begin(),t.end());if(!seen.insert(t).second)++duplicates;const auto a=points[indices[i]],b=points[indices[i+1]],c=points[indices[i+2]];if(vsg::length(vsg::cross(b-a,c-a))<1e-12)++collapsed;}stats["surface_triangles"]=indices.size()/3;stats["coincident_triangles"]=duplicates;stats["collapsed_triangles"]=collapsed;}
        objects.push_back(std::move(stats));}
    if(std::getenv("VSGRAYSIM_RANGE_DIAGNOSTICS")){nlohmann::json rays=nlohmann::json::array();double maximumError=0;for(double z:{-40.,0.,40.}){const auto a=scene->raycast({0,100,z},{0,-1,0},130),b=scene->rigidRaycast({0,100,z},{0,-1,0},130);nlohmann::json ray={{"z",z}};if(a)ray["cpu"]={{"object",a->object},{"distance",a->distance}};if(b)ray["bullet"]={{"object",b->object},{"distance",b->distance}};if(a&&b){ray["error_m"]=std::abs(a->distance-b->distance);maximumError=std::max(maximumError,std::abs(a->distance-b->distance));}rays.push_back(ray);}std::cerr<<nlohmann::json({{"rays",rays},{"maximum_error_m",maximumError}}).dump(2)<<'\n';}
    std::cout<<nlohmann::json({{"revision",s->revision},{"content_revision",s->contentRevision},{"tick",s->tick},
        {"time_seconds",s->timeSeconds()},{"objects",objects},{"executed_events",s->journal.size()}}).dump(2)<<'\n';
    return 0;
}catch(const std::exception& error){std::cerr<<"voxel_scene: "<<error.what()<<'\n';return 1;}
