#include "terrain_stream_benchmark.h"
#include "../src/earth/TiledVoxelTerrainPhysics.h"
#include <vsgraysim/earth/EarthDataService.h>
#include <vsgraysim/earth/SurfaceQuality.h>
#include <vsgraysim/Simulation.h>
#include <vsgraysim/broker/EquipmentDataSource.h>
#include <vsgraysim/broker/SimDataBroker.h>
#include <rocky_env/GpuContext.h>
#include <gdal.h>
#include <ogr_spatialref.h>
#include <cpl_conv.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <thread>
#include <future>
#include <random>
#include <atomic>

namespace {
using namespace vsgraysim;
using namespace vsgraysim::earth;
using json=nlohmann::json;
namespace fs=std::filesystem;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F&& f,const char* message){bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed,message);}
struct Scratch {
    fs::path root;
    Scratch(){std::string path="/tmp/vsgraysim_earth_XXXXXX";auto* made=mkdtemp(path.data());require(made,"Earth scratch directory");root=made;}
    ~Scratch(){SimDataBroker::instance().serverOnline(false);std::error_code error;fs::remove_all(root,error);}
};
void writeJson(const fs::path& path,const json& value){fs::create_directories(path.parent_path());std::ofstream file(path);file<<value.dump(2);require(bool(file),"Earth fixture document write");}
void raster(const fs::path& path,double height=100,bool hole=false,double left=-.0032,double top=.0032,double pixel=.0001,bool slope=true,bool georeferenced=true){
    GDALAllRegister();fs::create_directories(path.parent_path());auto driver=GDALGetDriverByName("GTiff");require(driver,"GTiff unavailable");
    auto dataset=GDALCreate(driver,path.c_str(),64,64,1,GDT_Float64,nullptr);require(dataset,"Earth GeoTIFF create");
    if(georeferenced){double transform[6]={left,pixel,0,top,0,-pixel};GDALSetGeoTransform(dataset,transform);
    OGRSpatialReference crs;crs.SetWellKnownGeogCS("WGS84");crs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);char* wkt=nullptr;crs.exportToWkt(&wkt);GDALSetProjection(dataset,wkt);CPLFree(wkt);}
    std::vector<double> data(64*64);
    for(int y=0;y<64;++y)for(int x=0;x<64;++x){const double lon=left+(x+.5)*pixel,lat=top-(y+.5)*pixel;data[y*64+x]=height+(slope?.1*lon*111319.+.2*lat*110574.:0);if(hole&&x>=30&&x<=33&&y>=30&&y<=33)data[y*64+x]=-9999;}
    auto band=GDALGetRasterBand(dataset,1);GDALSetRasterNoDataValue(band,-9999);
    const auto result=GDALRasterIO(band,GF_Write,0,0,64,64,data.data(),64,64,GDT_Float64,0,0);GDALClose(dataset);require(result==CE_None,"Earth GeoTIFF write");
}
double tmsHeight(double lon,double lat){return 100+2*lon+3*lat;}
void tmsGrid(const fs::path& path,double west,double north,double span){
    // Raw TMS elevation grid, matching Rocky's endpoint/node convention rather
    // than GDAL's usual area-pixel convention. Row zero is the northern edge.
    fs::create_directories(path.parent_path());auto dataset=GDALCreate(GDALGetDriverByName("GTiff"),path.c_str(),64,64,1,GDT_Float64,nullptr);require(dataset,"raw TMS grid create");
    std::vector<double> data(64*64);for(int y=0;y<64;++y)for(int x=0;x<64;++x)data[y*64+x]=tmsHeight(west+x*span/63,north-y*span/63);
    const auto result=GDALRasterIO(GDALGetRasterBand(dataset,1),GF_Write,0,0,64,64,data.data(),64,64,GDT_Float64,0,0);GDALClose(dataset);require(result==CE_None,"raw TMS grid write");
}
json earthConfig(){return {{"map","../data/earth.map.json"},{"grid_size",17},{"strict",true},{"vertical_datum","ellipsoidal"},{"coverage",{{"min_x",-100},{"max_x",100},{"min_z",-100},{"max_z",100}}}};}
json scene(const json& earth){return {{"scenario_name","earth_fixture"},{"geo_anchor",{{"lat",0},{"lon",0},{"alt",0},{"heading_deg",0}}},
    {"earth",earth},{"objects",json::array()},{"sensors",{{"sensor_list",json::array()}}},{"simulation",{{"sensor_backend","cpu"},{"use_gpu_rt",false},{"frame_dt",.01},{"physics_step",.01}}}};}
void qualityStatistics(){
    struct Mesh:EarthSurfaceSnapshot {Mesh(){
        // An upward slope, a downward geology bottom, and an unused invalid
        // position: only the referenced valid top contributes to relief.
        vertices_={{0,10,0},{0,12,2},{2,10,0},{0,-100,0},{2,-100,0},{0,-100,2},
            {0,std::numeric_limits<double>::quiet_NaN(),0}};
        indices_={0,1,2,3,4,5,0,0,0,0,1,6,999,1,2};
        revision=42;dem_spacing_m=30;imagery_spacing_m=100;resolution_m=30;
        imagery_width=2;imagery_height=1;imagery_rgba={10,20,30,255,110,120,130,255};
    } void clearMesh(){indices_.clear();}} mesh;
    const auto q=surfaceQuality(mesh);require(q.revision==42&&q.validTriangles==2&&q.upwardTriangles==1,"quality included invalid mesh triangles");
    require(q.heightMin==10&&q.heightMax==12&&std::abs(q.slopeMaxDegrees-45)<1e-9,"quality included buried geometry or missed actual slope");
    require(q.edgeMinMetres==2&&q.edgeMeanMetres>2&&q.edgeMaxMetres>2,"quality did not measure mesh geometry");
    require(q.imageryValid&&q.imagePixels==2&&q.rgbMin[0]==10&&q.rgbMax[2]==130&&q.imageLumaStdDev>.19&&q.sourceCoarse,"quality missed image variation or coarse source spacing");
    const auto report=q.json();require(report["surface_revision"]==mesh.revision&&report["top_height_range_m"]==2,"quality JSON lost co-versioned data");
    mesh.imagery_rgba={10,20,30,255,10,20,30,255};require(surfaceQuality(mesh).imageLumaStdDev==0,"constant imagery reported false texture detail");
    mesh.imagery_rgba={255,255,255,0,10,20,30,255};const auto opaque=surfaceQuality(mesh);require(opaque.imagePixels==1&&opaque.rgbMax[0]==10&&opaque.imageLumaStdDev==0,"transparent imagery inflated contrast");
    mesh.imagery_rgba.pop_back();require(!surfaceQuality(mesh).imageryValid,"malformed imagery passed quality diagnostics");
    mesh.provenance_assumed=true;mesh.dem_spacing_m=mesh.imagery_spacing_m=1;require(!surfaceQuality(mesh).sourceCoarse&&surfaceQuality(mesh).provenanceAssumed,"unknown provenance was confused with coarse resolution");
    mesh.clearMesh();const auto empty=surfaceQuality(mesh).json();require(empty["top_height_min_m"].is_null()&&empty["triangle_edge_mean_m"].is_null(),"absent geometry was advertised as zero-height terrain");
    std::cout<<"PASS: read-only surface quality distinguishes actual relief/image variation from coarse sources, invalid geometry and missing imagery\n";
}
void geometryAndConsumers(const fs::path& root){
    const auto map=root/"data/earth.map.json",path=root/"scenes/mission.json",dem=root/"rasters/surface.tif";
    raster(dem);writeJson(map,{{"layers",{{{"type","GDALElevation"},{"uri","../rasters/surface.tif"}}}}});
    auto document=scene(earthConfig());writeJson(path,document);
    auto surface=EarthDataService::loadScenario(document,path.string());require(surface&&surface->revision&&surface->grid_size==17,"pinned Earth snapshot missing");
    const auto quality=surfaceQuality(*surface);require(quality.validTriangles==surface->indices().size()/3&&quality.upwardTriangles>0&&quality.heightMax-quality.heightMin>50&&quality.slopeMaxDegrees>10,"loaded DEM lost actual relief in quality diagnostics");
    require(quality.revision==surface->revision&&quality.demSpacingMetres>0,"quality lost loaded source identity or native resolution");
    const auto center=surface->sampleLocal(0,0);require(center.status==SurfaceStatus::Ready&&std::abs(center.height-100)<1e-4,"ellipsoidal raster did not reach local height");
    require(surface->sampleLocal(101,0).status==SurfaceStatus::NoData,"outside coverage became false ground");
    const auto sample=surface->sampleLocal(23,-18);vsg::dvec3 hit,normal;
    require(surface->intersectLocal({23,500,-18},{0,-1,0},1000,hit,normal)&&std::abs(hit.y-sample.height)<1e-7,"query interpolation disagrees with canonical triangles");
    require(vsg::length(normal-sample.normal)<1e-8&&normal.y>.9,"canonical terrain normal mismatch");
    Simulation simulation;require(simulation.initFromScenario(path.string()),"production Simulation did not load Earth dataset");
    require(simulation.earthSurface()->revision==surface->revision,"display/query and simulation pinned different surface revisions");
    auto physics=simulation.physics();require(physics->groundPlanes().empty()&&physics->bodyExists("__earth_surface"),"Earth scene retained infinite default plane");
    require(physics->bodyLocalTriangles(0).size()==surface->indices().size(),"physics did not receive canonical Earth triangles");
    double height=0;vsg::dvec3 physicalNormal;
    require(physics->groundAt(23,-18,height,physicalNormal)&&std::abs(height-sample.height)<.005,"Bullet collision ground disagrees with Earth query");
    require(vsg::length(physicalNormal-sample.normal)<.001,"Bullet terrain normal disagrees with query");
    require(!physics->raycast({1000,500,0},{1000,-100,0},hit),"outside ROI hit a fabricated ground plane");
    RayTracer tracer(std::make_shared<AtmosphereModel>(),{},std::make_shared<CrossInductionRules>());tracer.setPhysicsEngine(physics.get());tracer.setAtmosphereEnabled(false);
    RayTraceConfig request;request.sensor_id=44;request.sensor_type=SensorType::VIS;request.origin={23,500,-18};request.direction={0,-1,0};request.beam_width=0;request.tx_power=1;request.wavelength=550e-9f;request.band_mask=BAND_VIS;request.num_rays=1;request.max_distance=1000;request.measurement_time=0.;
    const auto measured=tracer.traceSensor(request);require(measured.size()==1&&std::abs(measured[0].position.y-sample.height)<.005,"CPU measurement BVH did not use Earth collision triangles");
    Simulation::SensorPose aperture;aperture.position={0,500,0};aperture.direction={0,-1,0};aperture.up={0,0,1};
    simulation.validateEarthMeasurement(aperture,1,1,1000);
    rejects([&]{simulation.validateEarthMeasurement(aperture,90,90,1000);},"strict frustum crossing ROI silently measured empty space");
    aperture.position.x=200;rejects([&]{simulation.validateEarthMeasurement(aperture,1,1,1000);},"strict aperture outside ROI was accepted");
    simcore::RigidState escaped;escaped.position={500,300,0};require(physics->createBoxBody("outside_roi",{1,1,1},escaped,0),"coverage fixture body");
    EquipmentDataSource source(&simulation);require(source.initialize(json::object()),"Earth data source init");TimeTick tick;tick.dt=.01;tick.sim_time=.01;source.onTick(tick);
    require(!source.lastTickOk(),"physics platform outside Earth coverage did not fail tick");simulation.stop();
    raster(dem,110);auto changed=EarthDataService::loadScenario(document,path.string());
    require(changed->revision!=surface->revision&&std::abs(changed->sampleLocal(0,0).height-110)<1e-4&&std::abs(surface->sampleLocal(0,0).height-100)<1e-4,"new Earth version mutated a pinned measurement snapshot");
    auto unbound=document;unbound.erase("earth");const auto oldScene=root/"scenes/unbound.json";writeJson(oldScene,unbound);
    Simulation overridden;overridden.setEarthSurfaceOverride(surface);require(overridden.initFromScenario(oldScene.string())&&overridden.earthSurface()==surface&&overridden.physics()->groundPlanes().empty(),"host validated Earth binding did not reach legacy production scene");
    rejects([&]{overridden.setEarthSurfaceOverride(changed);},"active simulation accepted Earth coordinate replacement");overridden.stop();
    require(overridden.initFromScenario(oldScene.string())&&!overridden.earthSurface()&&!overridden.physics()->groundPlanes().empty(),"host Earth binding leaked into later scene");overridden.stop();
    overridden.setEarthSurfaceOverride(surface);require(overridden.initFromScenario(path.string())&&overridden.earthSurface()->revision==changed->revision,"host override replaced explicit scene Earth contract");overridden.stop();
    unbound["geo_anchor"]["lat"]=1;writeJson(oldScene,unbound);overridden.setEarthSurfaceOverride(surface);require(!overridden.initFromScenario(oldScene.string()),"mismatched host Earth anchor was accepted");
    raster(dem,100,true);rejects([&]{EarthDataService::loadScenario(document,path.string());},"strict Earth preload ignored NoData holes");
    document["earth"]["strict"]=false;auto partial=EarthDataService::loadScenario(document,path.string());require(partial->sampleLocal(0,0).status==SurfaceStatus::NoData&&!partial->intersectLocal({0,500,0},{0,-1,0},1000,hit,normal),"partial coverage filled NoData with zero ground");
    document["earth"]["strict"]=true;document["earth"]["vertical_datum"]="unknown";rejects([&]{EarthDataService::loadScenario(document,path.string());},"strict unknown vertical reference accepted");
    document["earth"]["vertical_datum"]="ellipsoidal";document["earth"]["grid_size"]=17.5;rejects([&]{EarthDataService::loadScenario(document,path.string());},"fractional Earth grid was coerced");
    std::cout<<"PASS: same Earth snapshot -> local query, Bullet collision and CPU sensor BVH; strict coverage, NoData and immutable revisions\n";
}
void tmsAndProvenance(const fs::path& root){
    const auto path=root/"tms/map.json";
    for(int x=0;x<2;++x)raster(root/("tiles/0/"+std::to_string(x)+"/0.tif"),155,false,-180+x*180,90,180./64,false,false);
    json map={{"layers",{{{"type","TMSElevation"},{"profile","global-geodetic"},{"uri","../tiles/{z}/{x}/{-y}.tif"},{"maxLevel",1}}}},
        {"earth",{{"vertical_datum","ellipsoidal"},{"coverage",{{"min_x",-100},{"max_x",100},{"min_z",-100},{"max_z",100}}},{"grid_size",9}}}};
    writeJson(path,map);simcore::geo::GeoAnchor anchor;anchor.lat_deg=anchor.lon_deg=anchor.alt_m=anchor.heading_deg=0;
    auto coarse=EarthDataService::loadMap(path.string(),anchor);require(coarse->sampleLocal(0,0).status==SurfaceStatus::Coarse&&std::abs(coarse->sampleLocal(0,0).height-155)<1e-5,"TMS lower LOD fallback not marked coarse");
    for(int x:{1,2})for(int y=0;y<2;++y)raster(root/("tiles/1/"+std::to_string(x)+"/"+std::to_string(y)+".tif"),170,false,-180+x*90,-90+(y+1)*90,90./64,false,false);
    auto fine=EarthDataService::loadMap(path.string(),anchor);require(fine->sampleLocal(0,0).status==SurfaceStatus::Ready&&std::abs(fine->sampleLocal(0,0).height-170)<1e-5&&fine->revision!=coarse->revision,"TMS addressing or LOD version invalidation failed");
    for(int x:{1,2})for(int y=0;y<2;++y)tmsGrid(root/("tiles/1/"+std::to_string(x)+"/"+std::to_string(y)+".tif"),-180+x*90,-90+(y+1)*90,90);
    for(const auto& origin:std::vector<std::pair<double,double>>{{0,0},{12,15}}){
        anchor.lat_deg=origin.first;anchor.lon_deg=origin.second;auto grid=EarthDataService::loadMap(path.string(),anchor);
        for(const auto& vertex:grid->vertices()){const auto ecef=simcore::geo::localToECEF(anchor,vertex);const auto llh=simcore::geo::ecefToGeodetic(ecef.x,ecef.y,ecef.z);require(std::abs(llh.z-tmsHeight(llh.y,llh.x))<1e-5,"TMS sampling differs from Rocky grid endpoints, north row orientation or shared seam");}
    }
    anchor.lat_deg=anchor.lon_deg=0;
    auto undeclared=map;undeclared["layers"][0].erase("profile");writeJson(path,undeclared);rejects([&]{EarthDataService::loadMap(path.string(),anchor);},"raw TMS silently guessed an undeclared tile profile");
    map["earth"].erase("vertical_datum");writeJson(path,map);rejects([&]{EarthDataService::loadMap(path.string(),anchor);},"strict Earth silently accepted a missing vertical datum");
    map["earth"]["provenance_assumed"]=true;writeJson(path,map);auto assumed=EarthDataService::loadMap(path.string(),anchor);require(assumed->provenance_assumed&&assumed->sampleLocal(0,0).status==SurfaceStatus::Coarse,"explicit reference package advertised calibrated data");
    auto unknown=map;unknown["earth"]["vertical_datum"]="unknown";writeJson(path,unknown);rejects([&]{EarthDataService::loadMap(path.string(),anchor);},"strict unknown datum accepted despite assumed reference flag");writeJson(path,map);
    setenv("VSGRAYSIM_EARTH_DATA_ROOT",root.c_str(),1);require(EarthDataService::resolveUri("${VSGRAYSIM_EARTH_DATA_ROOT}/tiles",path.string())==(root/"tiles").string(),"explicit Earth data root did not resolve deterministically");
    unsetenv("VSGRAYSIM_EARTH_DATA_ROOT");rejects([&]{EarthDataService::resolveUri("${VSGRAYSIM_EARTH_DATA_ROOT}/tiles",path.string());},"missing deployment data root silently fell back");
    auto unreferenced=map;unreferenced["layers"]={{{"type","GDALElevation"},{"uri","../tiles/0/0/0.tif"}}};writeJson(path,unreferenced);
    rejects([&]{EarthDataService::loadMap(path.string(),anchor);},"bare GDAL raster silently acquired TMS georeferencing");
    std::cout<<"PASS: raw offline TMS {-y}, Rocky grid endpoints/seams, declared profile, fallback precision and strict vertical provenance\n";
}
void legacy(const fs::path& root){
    const auto ter=root/"legacy/height.ter",project=root/"legacy/project.vsgterrain";fs::create_directories(ter.parent_path());
    {std::ofstream out(ter,std::ios::binary);out.write("VSGTER1\n",8);uint32_t n=9,zero=0;double extent=200,scale=10,seed=0,water=0;out.write(reinterpret_cast<char*>(&n),4);for(double* value:{&extent,&scale,&seed,&water})out.write(reinterpret_cast<char*>(value),8);out.write(reinterpret_cast<char*>(&zero),4);out.write(reinterpret_cast<char*>(&zero),4);std::vector<double> values(81,7);out.write(reinterpret_cast<char*>(values.data()),values.size()*sizeof(double));}
    writeJson(project,{{"format","vsgterrain-project"},{"heightmap","height.ter"}});json document={{"terrain_ref","../legacy/project.vsgterrain"},{"geo_anchor",{{"lat",0},{"lon",0}}}};
    auto surface=EarthDataService::loadScenario(document,(root/"scenes/legacy.json").string());require(surface&&surface->grid_size==9&&surface->sampleLocal(0,0).height==7,"legacy terrain_ref did not enter common Earth snapshot");
    require(!EarthDataService::loadScenario(json::object(),(root/"none.json").string()),"legacy local fixture invented Earth coverage");
    std::cout<<"PASS: legacy local heightmap/project adapter and explicit no-Earth compatibility\n";
}
void canonicalIdentity(const fs::path& root) {
    const auto directory=root/"canonical",path=directory/"mission.json",mapPath=directory/"earth.map.json",dem=directory/"surface.tif";
    raster(dem,100,false,-.0032,.0032,.0001,false);
    auto defaults=earthConfig();defaults.erase("map");defaults["lwir_params"]={{"base_temp",300},{"emissivity",.8}};
    json map={{"name","original"},{"earth",defaults},{"layers",{{{"type","GDALElevation"},{"name","original elevation"},{"uri","surface.tif"}}}}};
    writeJson(mapPath,map);auto raw=scene({{"map","earth.map.json"}});auto baseline=EarthDataService::loadScenario(raw,path.string());
    auto merged=raw;merged["earth"]=defaults;merged["earth"]["map"]=mapPath.string();merged["earth"]["editor_source_hash"]="presentation_only";
    merged["earth"]["lwir_params"]["base_temp"]=300.;merged["earth"]["lwir_params"]["emissivity_swir"]=.8;
    merged["earth"]["vis_params"]={{"brdf_type","diffuse"},{"albedo",.2},{"laser_absorb",0},{"laser_scatter",0},{"editor_label","irrelevant"}};
    auto rehydrated=EarthDataService::loadScenario(merged,path.string());
    require(baseline->revision==rehydrated->revision&&baseline->material_config==rehydrated->material_config,"GUI-expanded/default/absolute map syntax changed physical Earth identity");
    require(baseline->material_config["lwir_params"]["base_temp"].get<float>()==300.f,"map material defaults did not reach snapshot");
    map["name"]="renamed";map["layers"][0]["name"]="renamed elevation";map["layers"][0]["uri"]=dem.string();writeJson(mapPath,map);
    require(EarthDataService::loadScenario(raw,path.string())->revision==baseline->revision,"source URI normalization or map display name changed surface identity");
    auto changed=merged;changed["earth"]["lwir_params"]["base_temp"]=320;require(EarthDataService::loadScenario(changed,path.string())->revision!=baseline->revision,"physical material change did not version Earth snapshot");
    changed=merged;changed["earth"]["coverage"]["min_x"]=-90;require(EarthDataService::loadScenario(changed,path.string())->revision!=baseline->revision,"effective coverage change did not version Earth snapshot");
    raster(dem,110,false,-.0032,.0032,.0001,false);require(EarthDataService::loadScenario(raw,path.string())->revision!=baseline->revision,"physical geometry change did not version Earth snapshot");
    std::cout<<"PASS: canonical Earth identity across raw scene, GUI rehydration, numeric defaults and URI forms; effective geometry/material/coverage invalidation\n";
}
void voxelGeometry(const fs::path& root) {
    const auto map = root/"data/blocks.map.json", path = root/"scenes/blocks.json";
    auto document = scene({{"backend","voxel"},{"map","../data/blocks.map.json"}});
    json package = {{"format","voxel-terrain-v1"},{"origin_m",{0,0,0}},{"dimensions",{2,3,1}},
                    {"cell_size_m",10},{"occupied",{{0,0,0},{1,0,0},{1,2,0}}}};
    writeJson(map,package);
    writeJson(path,document);
    auto surface = EarthDataService::loadScenario(document,path.string());
    require(surface->backend()==EarthBackend::Voxel && surface->indices().size()==16*6,
            "voxel adjacency or backend dispatch failed");
    require(surface->sampleLocal(3,5).height==10 && surface->sampleLocal(17,5).height==30,
            "voxel height query lost a floating cell");
    vsg::dvec3 point,normal;
    require(surface->waterBedLocal(17,5,15).height==10 && surface->waterBedLocal(17,5,25).height==30,
            "local voxel ocean queried the roof instead of the bed");
    require(surface->intersectLocal({17,15,5},{0,1,0},30,point,normal) && point.y==20 && normal.y==-1,
            "underside of a floating voxel missing or reversed");
    require(!surface->intersectLocal({-1,15,5},{1,0,0},30,point,normal),"empty space became solid");
    Simulation sim;
    sim.setEarthSurfaceOverride(surface);
    require(sim.initFromScenario(path.string()) && sim.earthSurface()==surface,"view snapshot was not pinned");
    auto physical=sim.physics();
    require(physical->groundPlanes().empty() && physical->bodyLocalTriangles(0).size()==surface->indices().size(),
            "Bullet did not receive the exposed voxel triangles");
    double height=0;
    require(physical->groundAt(17,5,height,normal) && std::abs(height-30)<.001,"voxel collision height mismatch");
    sim.stop();
    std::reverse(package["occupied"].begin(), package["occupied"].end());
    writeJson(map,package);
    require(EarthDataService::loadScenario(document,path.string())->revision==surface->revision,
            "occupied list order changed terrain identity");
    package["occupied"].erase(package["occupied"].begin());
    writeJson(map,package);
    require(EarthDataService::loadScenario(document,path.string())->revision!=surface->revision &&
            surface->sampleLocal(17,5).height==30,"reload failed to isolate published voxel versions");
    package["occupied"].push_back({1,0,0}); writeJson(map,package);
    rejects([&]{EarthDataService::loadScenario(document,path.string());},"duplicate voxel accepted");
    package["occupied"]={{0,0,0}}; writeJson(map,package);
    rejects([&]{EarthDataService::loadScenario(document,path.string());},"strict empty column accepted");
    document["earth"]["strict"]=false;
    require(EarthDataService::loadScenario(document,path.string())->sampleLocal(17,5).status==SurfaceStatus::NoData,
            "empty column turned into zero ground");
    document["earth"]["backend"]="rocky";
    rejects([&]{EarthDataService::loadScenario(document,path.string());},"mismatched backend silently accepted");
    std::cout<<"PASS: voxel terraces, air gap, underside, collision and immutable revision\n";
}
void rayIndexRegression() {
    struct Mesh:EarthSurfaceSnapshot {
        Mesh(){const int n=33;for(int z=0;z<n;++z)for(int x=0;x<n;++x)vertices_.push_back({-10+x*.7,.4*std::sin(x*.3)*std::cos(z*.2),-10+z*.7});
            for(int z=0;z<n-1;++z)for(int x=0;x<n-1;++x){const uint32_t a=z*n+x;indices_.insert(indices_.end(),{a,a+n,a+1,a+1,a+n,a+n+1});}
            const auto at=uint32_t(vertices_.size());vertices_.insert(vertices_.end(),{{-10,-3.5,-10},{-10,-3.5,12.4},{12.4,-3.5,-10},{12.4,-3.5,12.4}});indices_.insert(indices_.end(),{at,at+1,at+2,at+2,at+1,at+3});revision=100;
        }
        void lift(){for(auto& p:vertices_)p.y+=.125;invalidateRayIndex();}
    } mesh;
    auto brute=[](const EarthSurfaceSnapshot& s,const vsg::dvec3& o,vsg::dvec3 ray,vsg::dvec3& hit,vsg::dvec3& normal){
        ray=vsg::normalize(ray);double nearest=100;bool found=false;
        const auto& v=s.vertices();const auto& ix=s.indices();
        for(size_t i=0;i<ix.size();i+=3){const auto a=v[ix[i]],e=v[ix[i+1]]-a,f=v[ix[i+2]]-a,p=vsg::cross(ray,f);const double d=vsg::dot(e,p);if(std::abs(d)<1e-12)continue;const double inverse=1/d;const auto t=o-a;const double u=vsg::dot(t,p)*inverse;if(u<0||u>1)continue;const auto q=vsg::cross(t,e);const double w=vsg::dot(ray,q)*inverse;if(w<0||u+w>1)continue;const double distance=vsg::dot(f,q)*inverse;if(distance<0||distance>nearest)continue;nearest=distance;hit=o+ray*distance;normal=vsg::normalize(vsg::cross(e,f));found=true;}
        return found;
    };
    auto compare=[&](const Mesh& s,const vsg::dvec3& o,const vsg::dvec3& ray){vsg::dvec3 a,b,an,bn;const bool x=s.intersectLocal(o,ray,100,a,an),y=brute(s,o,ray,b,bn);require(x==y,"ray index changed hit mask");if(x&&(vsg::length(a-b)>=1e-8||vsg::length(an-bn)>=1e-8)){std::cerr<<"ray-index diagnostic origin="<<o<<" ray="<<ray<<" indexed="<<a<<" normal="<<an<<" brute="<<b<<" normal="<<bn<<'\n';throw std::runtime_error("ray index changed the closest triangle, edge tie or normal");}};
    std::mt19937_64 random(17082026);std::uniform_real_distribution<double> location(-20,20),direction(-1,1);
    for(int phase=0;phase<2;++phase){for(int i=0;i<1500;++i)compare(mesh,{location(random),location(random),location(random)},{direction(random),direction(random),direction(random)});
        for(int z=1;z<32;z+=5)for(int x=1;x<32;x+=5){compare(mesh,{-10+x*.7,10,-10+z*.7},{0,-1,0});compare(mesh,{-10+x*.7,-10,-10+z*.7},{0,1,0});}mesh.lift();}
    Mesh shared;std::atomic<bool> failed{false};std::vector<std::thread> readers;
    for(int i=0;i<4;++i)readers.emplace_back([&,i]{try{for(int x=0;x<30;++x)compare(shared,{-9+x*.7,8,-8+i*2.7},{0,-2,0});}catch(...){failed=true;}});
    for(auto& reader:readers)reader.join();require(!failed,"concurrent immutable ray-index publication changed results");
    std::cout<<"PASS: indexed triangle rays match brute force, shared edges, underside, geometry revisions and concurrent readers\n";
}
void imageryCacheRegression(const fs::path& root) {
    const auto dem=root/"cache-dem.tif",image=root/"cache-rgba.tif",gray=root/"cache-gray.tif",map=root/"cache.map.json";
    raster(dem,100,false,-.0032,.0032,.0001,false);
    OGRSpatialReference crs;crs.SetWellKnownGeogCS("WGS84");char* wkt=nullptr;crs.exportToWkt(&wkt);
    auto channel=[](int c,int x,int y)->uint8_t {
        if(c==0)return 30+x%160;if(c==1)return 40+y%160;if(c==2)return 80+(x+y)%100;
        return x>=126&&x<=130&&y>=126&&y<=130?0:255;
    };
    for(bool fallback:{true,false}) {
        auto ds=GDALCreate(GDALGetDriverByName("GTiff"),(fallback?gray:image).c_str(),260,260,fallback?1:4,GDT_Byte,nullptr);require(ds,"cache image create");
        double gt[6]={-.00325,.000025,0,.00325,0,-.000025};GDALSetGeoTransform(ds,gt);GDALSetProjection(ds,wkt);
        for(int c=0;c<(fallback?1:4);++c) {
            std::vector<uint8_t> pixels(260*260);
            for(int y=0;y<260;++y)for(int x=0;x<260;++x)pixels[y*260+x]=fallback?17:channel(c,x,y);
            auto band=GDALGetRasterBand(ds,c+1);if(!fallback)GDALSetRasterColorInterpretation(band,c==3?GCI_AlphaBand:GDALColorInterp(GCI_RedBand+c));
            require(GDALRasterIO(band,GF_Write,0,0,260,260,pixels.data(),260,260,GDT_Byte,0,0)==CE_None,"cache image write");
        }
        GDALClose(ds);
    }
    CPLFree(wkt);
    writeJson(map,{{"earth",{{"vertical_datum","ellipsoidal"},{"coverage",{{"min_x",-300},{"max_x",300},{"min_z",-300},{"max_z",300}}},{"grid_size",65},{"imagery_size",1056}}},
                  {"layers",{{{"type","GDALElevation"},{"uri",dem.string()}},{{"type","GDALImage"},{"uri",gray.string()}},{{"type","GDALImage"},{"uri",image.string()}}}}});
    simcore::geo::GeoAnchor anchor;anchor.lat_deg=0;anchor.lon_deg=0;anchor.alt_m=0;auto surface=EarthDataService::loadMap(map.string(),anchor);
    require(surface->imagery_width==1056,"high-resolution imagery was silently capped at 1024");
    auto linear=[](uint8_t value){double n=value/255.;return n<=.04045?n/12.92:std::pow((n+.055)/1.055,2.4);};
    bool sawFallback=false,sawBoundary=false;
    for(const auto& pixel:std::array<std::array<int,2>,8>{{{0,0},{1055,1055},{527,527},{519,540},{70,999},{300,127},{518,518},{517,540}}}) {
        const int x=pixel[0],y=pixel[1];const double ex=-300+(x+.5)*600/1056,ez=-300+(y+.5)*600/1056;
        const auto ecef=simcore::geo::localToECEF(anchor,{ex,surface->sampleLocal(ex,ez).height,ez});const auto llh=simcore::geo::ecefToGeodetic(ecef.x,ecef.y,ecef.z);
        const double fx=(llh.y+.00325)/.000025-.5,fy=(.00325-llh.x)/.000025-.5;
        const int ix=int(std::floor(fx)),iy=int(std::floor(fy));const double u=fx-ix,v=fy-iy;
        bool valid=true;std::array<double,3> color{};
        for(int j=0;j<2;++j)for(int i=0;i<2;++i) {
            const double weight=(i?u:1-u)*(j?v:1-v);if(weight<=1e-12)continue;
            if(!channel(3,ix+i,iy+j))valid=false;
            for(int c=0;c<3;++c)color[c]+=weight*linear(channel(c,ix+i,iy+j));
        }
        sawFallback|=!valid;sawBoundary|=ix==127||iy==127;
        for(int c=0;c<3;++c) {
            const auto encoded=valid?std::lround(255*(color[c]<=.0031308?12.92*color[c]:1.055*std::pow(color[c],1/2.4)-.055)):17;
            require(std::abs(int(surface->imagery_rgba[4*(size_t(y)*1056+x)+c])-encoded)<=1,"imagery cache changed linear interpolation or alpha fallback at block boundaries");
        }
    }
    require(sawFallback&&sawBoundary,"cache regression did not cross the alpha hole and 128-pixel block edge");
    setenv("VSGRAYSIM_TAIPEI_DATA_ROOT",root.c_str(),1);require(EarthDataService::resolveUri("${VSGRAYSIM_TAIPEI_DATA_ROOT}/cache",map.string())==(root/"cache").string(),"Taipei data root resolution");
    unsetenv("VSGRAYSIM_TAIPEI_DATA_ROOT");rejects([&]{EarthDataService::resolveUri("${VSGRAYSIM_TAIPEI_DATA_ROOT}/cache",map.string());},"missing Taipei data root silently fell back");
    std::cout<<"PASS: high-resolution RGB/gray, cached block edges, alpha holes and linear interpolation\n";
}
json tiledFixture(const fs::path& root) {
    GDALAllRegister();
    const auto data=root/"global";
    for(int x=0;x<2;++x) {
        auto dem=data/"dem/0"/std::to_string(x)/"0.tif";
        raster(dem,100,false,0,0,1,false,false);
        auto image=data/"rgb/0"/std::to_string(x)/"0.tif";
        fs::create_directories(image.parent_path());
        auto ds=GDALCreate(GDALGetDriverByName("GTiff"),image.c_str(),32,32,3,GDT_Byte,nullptr);
        require(ds,"create RGB tile");
        for(int b=1;b<=3;++b) require(GDALFillRaster(GDALGetRasterBand(ds,b),b*25+40*x,0)==CE_None,"RGB fill");
        GDALClose(ds);
    }
    json map={{"format","voxel-geodetic-v1"},{"profile","global-geodetic"},{"cells_per_tile",4},
        {"texels_per_tile",8},{"vertical_step_m",5},{"dem",{{"uri","dem/{z}/{x}/{-y}.tif"},{"max_level",0}}},
        {"imagery",{{"uri","rgb/{z}/{x}/{y}.tif"},{"max_level",0}}},
        {"earth",{{"min_lod",7},{"max_lod",14},{"initial_lod",10},{"vertical_datum","ellipsoidal"}}}};
    writeJson(data/"terrain.map.json",map);
    auto document=scene({{"map",(data/"terrain.map.json").string()},{"backend","voxel"}});
    document["geo_anchor"]={{"lat",.12},{"lon",179.96},{"alt",0},{"heading_deg",17}};
    return document;
}
void nativeVolumeRegression(const fs::path& root) {
    const auto map=fs::absolute("config/earth/native-volume.map.json");
    const double span=180./512;
    simcore::geo::GeoAnchor anchor;
    anchor.lat_deg=90.-(184+.4)*span;anchor.lon_deg=-180.+(858+.4)*span;
    auto surface=EarthDataService::loadMap(map.string(),anchor);
    require(surface->orbital && surface->orbital->dataset,"published surface lacks a pinned globe");
    for(auto coordinate:{vsg::dvec2(46.,8.),vsg::dvec2(-33.,-70.),vsg::dvec2(89.99,-179.999)}) {
        auto remote=EarthDataService::observerTiles(surface,coordinate.x,coordinate.y,9);
        require(remote->orbital==surface->orbital && remote->revision!=surface->revision &&
                remote->occupiedGeodetic(coordinate.x,coordinate.y,0.)==true,"remote observer datum, frame or coverage mismatch");
    }
    const auto copy=root/"catalog-test";
    fs::create_directories(copy);
    fs::copy_file(map,copy/map.filename());
    fs::copy(map.parent_path()/"native-volume",copy/"native-volume",fs::copy_options::recursive);
    const auto detail=copy/"native-volume/9/858/184.json";
    auto pinned=EarthDataService::loadMap((copy/map.filename()).string(),anchor);
    json edited;std::ifstream(detail)>>edited;edited["profiles"][0]["rgb"][0]=1;writeJson(detail,edited);
    rejects([&]{EarthDataService::loadMap((copy/map.filename()).string(),anchor);},"changed catalogued payload accepted");
    fs::remove(detail);
    rejects([&]{EarthDataService::loadMap((copy/map.filename()).string(),anchor);},"missing catalogued child silently downgraded");
    require(pinned->columnTopGeodetic(anchor.lat_deg,anchor.lon_deg)==180. &&
            pinned->orbital->top(-30.,-170.)==20.,"source mutation invalidated an installed generation");
    auto occupied=[&](double h){return surface->occupiedGeodetic(anchor.lat_deg,anchor.lon_deg,h);};
    require(occupied(99)==true && occupied(100)==false && occupied(139)==false &&
            occupied(140)==true && occupied(180)==false,"native radial voxel intervals");
    require(!surface->occupiedGeodetic(0,0,50).has_value(),"unloaded volume was treated as empty");
    auto tiled=std::dynamic_pointer_cast<const TiledVoxelTerrain>(surface);require(bool(tiled),"native tiled volume type");
    auto window=tiled->extractDamageWindow(anchor.lat_deg,anchor.lon_deg,500,-200);require(bool(window),"damage window extraction");
    auto changed=std::make_shared<TiledVoxelTerrain>(*tiled);
    auto runs=window->originalRuns;size_t selected=runs.size();
    for(size_t i=0;i<runs.size();++i)if(!runs[i].empty()){selected=i;runs[i].clear();break;}
    require(selected<runs.size(),"damage window has no occupied source column");
    const auto signature=tiled->occupancySignature(),revision=tiled->revision;
    require(changed->applyDamage(*window,runs)>0 && changed->occupancySignature()!=signature &&
            changed->revision>revision && tiled->occupancySignature()==signature && tiled->damageOverrides().empty(),
            "damage mutated an immutable source snapshot or failed to version occupancy");
    require(changed->vertices().size()!=tiled->vertices().size() || changed->indices()!=tiled->indices(),"damage failed to rebuild displayed geometry");
    require(changed->applyDamage(*window,runs)==0,"repeated damage changed an unchanged snapshot");
    TiledVoxelTerrainPhysics mutableTerrain(tiled);
    require(mutableTerrain.focus(anchor.lat_deg,anchor.lon_deg,500,-200)&&mutableTerrain.commit()==0,
            "no-op damage focus/commit changed original geology");
    vsg::dvec3 hit,normal,physicsHit;
    require(surface->intersectLocal({0,120,0},{0,1,0},30,hit,normal) && normal.y<-.99 && std::abs(hit.y-140)<3,
            "missing native cavity roof or wrong winding");
    require(std::abs(surface->waterBedLocal(0,0,120).height-100)<3 &&
            std::abs(surface->waterBedLocal(0,0,160).height-180)<3 &&
            std::abs(surface->sampleLocal(0,0).height-180)<3,"overhang hid the water bed or admitted water inside solid");
    auto doc=scene({{"backend","voxel"},{"map",map.string()}});
    doc["geo_anchor"]={{"lat",anchor.lat_deg},{"lon",anchor.lon_deg},{"alt",0}};
    const auto path=root/"cavity.scene.json";writeJson(path,doc);
    Simulation sim;require(sim.initFromScenario(path.string()) && sim.step(.02),"native volume physics init");
    require(sim.physics()->raycast({0,120,0},{0,150,0},physicsHit) && std::abs(physicsHit.y-hit.y)<.02,
            "Bullet cavity ceiling differs from native geometry");
    require(sim.earthSurface()->revision==surface->revision && sim.physics()->snapshot()->surface_revision==surface->revision,
            "native volume revision handoff");
    auto raised=EarthDataService::selectTiles(surface,anchor.lat_deg,anchor.lon_deg,10);
    require(raised!=surface && raised->occupiedGeodetic(anchor.lat_deg,anchor.lon_deg,120)==false &&
        EarthDataService::loadMap(map.string(),anchor,raised->declaration)->revision==raised->revision,"native LOD publication");
    sim.stop();

    const auto input=root/"import";
    const auto rasterScene=tiledFixture(input);
    const auto inputMap=rasterScene.at("earth").at("map").get<std::string>();
    const auto out=root/"native-package";
    EarthDataService::bakeVolume(inputMap,out.string(),.12,179.96,9,10,2,-100);
    fs::remove_all(input); // Reopen an exported scene with no source rasters present.
    auto baked=EarthDataService::loadMap((out/"earth.map.json").string(),{.12,179.96,0,0});
    require(baked->resident_tiles==9 && baked->occupiedGeodetic(.12,179.96,50)==true &&
            baked->occupiedGeodetic(.12,179.96,110)==false && !baked->imagery_rgba.empty() &&
            baked->dem_spacing_m>1000,"standalone voxel bake lost native data or source spacing");
    require(!baked->orbital,"regional package advertised global coverage");
    auto next=EarthDataService::selectTiles(baked,.12,-179.94,9);
    require(next->revision!=baked->revision && next->occupiedGeodetic(.12,-179.94,50)==true,"baked date-line handoff");
    const auto key=EarthDataService::tileKey(.12,179.96,10);
    const auto block=out/"tiles"/"10"/std::to_string(key[1])/(std::to_string(key[2])+".json");
    json corrupt;std::ifstream(block)>>corrupt;
    corrupt["profiles"][0]["runs"]={{0,2},{1,5}};writeJson(block,corrupt);
    rejects([&]{EarthDataService::loadMap((out/"earth.map.json").string(),baked->anchor,baked->declaration);},"bad native run accepted");
    require(baked->occupiedGeodetic(.12,179.96,50)==true,"corrupt reload mutated pinned voxels");
    std::cout<<"PASS: native cavities, ECEF occupancy, collision, LOD, independent DEM import and malformed tile rollback\n";
}

void tiledRegression(const fs::path& root) {
    auto document=tiledFixture(root);
    const auto path=root/"global/test.scene.json";
    writeJson(path,document);
    auto current=EarthDataService::loadScenario(document,path.string());
    require(current && current->resident_tiles==9 && current->imagery_width==24 &&
        current->imagery_rgba[0]!=current->imagery_rgba[(current->imagery_width-1)*4],"antimeridian RGB tile addressing");
    require(EarthDataService::tileKey(0,180,10)==EarthDataService::tileKey(0,-180,10),"date line wrap");
    require(EarthDataService::selectTiles(current,.12,179.96,10)==current,"unchanged selection rebuilt geometry");
    // Constant height isolates topology: every internal edge, including each
    // fine/coarse interface, has exactly two incident triangles.
    auto manifest=root/"global/terrain.map.json";
    json mixed; std::ifstream(manifest)>>mixed; mixed["peripheral_lod_delta"]=1;
    auto mixedPath=root/"global/mixed.map.json"; writeJson(mixedPath,mixed);
    auto adaptive=EarthDataService::loadMap(mixedPath.string(),current->anchor);
    require(adaptive->indices().size()<current->indices().size(),"outer LOD did not reduce geometry");
    std::map<std::pair<int,int>,int> edges;
    auto node=[&](uint32_t i) {
        auto uv=adaptive->imagery_uv[i];
        return int(std::lround(uv.y*12))*13+int(std::lround(uv.x*12));
    };
    const auto& indices=adaptive->indices();
    for(size_t t=0;t<indices.size();t+=3) for(int k=0;k<3;++k) {
        int a=node(indices[t+k]),b=node(indices[t+(k+1)%3]);
        ++edges[std::minmax(a,b)];
    }
    for(const auto& [edge,count]:edges) {
        auto [a,b]=edge;
        bool outer=(a%13==0 && b%13==0)||(a%13==12 && b%13==12)||
                   (a/13==0 && b/13==0)||(a/13==12 && b/13==12);
        require(count==(outer?1:2),"crack or duplicate face at LOD boundary");
    }
    const auto moved=EarthDataService::selectTiles(current,.12,-179.94,10);
    require(moved->reused_tiles>=6 && moved->revision!=current->revision && current->resident_tiles==9,"adjacent working-set reuse");
    const auto reopened=EarthDataService::loadMap(current->source,current->anchor,moved->declaration);
    require(reopened->revision==moved->revision && reopened->imagery_rgba==moved->imagery_rgba,"display rehydration");
    const auto lower=EarthDataService::selectTiles(moved,.12,-179.94,9);
    require(lower->resolution_m>moved->resolution_m && lower->resident_tiles==9,"LOD transition");
    auto polar=document; polar["geo_anchor"]={{"lat",89.99},{"lon",10}};
    auto cap=EarthDataService::loadScenario(polar,path.string());
    require(cap->resident_tiles==6 && cap->sampleLocal(0,0).status!=SurfaceStatus::NoData,"polar cap coverage");
    Simulation sim; sim.setEarthSurfaceOverride(current);
    require(sim.initFromScenario(path.string()) && sim.step(.01),"tiled physics initialization");
    int commits=0;
    sim.setEarthCommitHandler([&](auto s){require(s==sim.earthSurface(),"published different ground");++commits;});
    sim.selectEarthRegion(.12,-179.94,10);
    const auto surface=sim.earthSurface();
    auto sample=surface->sampleLocal(20,30);
    double h;vsg::dvec3 n;
    require(sample.status!=SurfaceStatus::NoData && sim.physics()->groundAt(20,30,h,n) &&
        std::abs(h-sample.height)<.02 && sim.physics()->snapshot()->surface_revision==surface->revision && sim.physics()->bodyCount()==1 && commits==1,"switched collision differs from view geometry");
    require(sim.earthValidFrom()==.01 && sim.step(.01),"terrain transition lost physics time");
    // Hold a prepared display candidate across several ticks, then reverse focus.
    std::promise<void> captured,release;
    auto started=captured.get_future();
    auto gate=release.get_future().share();
    int viewPublications=0;
    sim.setEarthViewPreparation([&](auto) {
        captured.set_value();
        if(gate.wait_for(std::chrono::seconds(5))!=std::future_status::ready)
            throw std::runtime_error("owner blocked while display preparation was pending");
        return [&]{++viewPublications;};
    });
    sim.requestEarthRegion(.12,179.96,10);
    require(sim.step(.01) && started.wait_for(std::chrono::seconds(3))==std::future_status::ready,"background candidate never started");
    for(int i=0;i<3;++i)
        require(sim.step(.01) && sim.earthSurface()==surface && sim.physics()->snapshot()->surface_revision==surface->revision,
                "unready candidate escaped step boundary");
    sim.requestEarthRegion(.12,-179.94,10);
    release.set_value();
    require(sim.step(.01) && sim.earthSurface()==surface && viewPublications==0,"superseded display candidate published");
    sim.setEarthViewPreparation({});
    // A failed load must leave both current world and collider intact.
    fs::remove(root/"global/rgb/0/1/0.tif");
    sim.requestEarthRegion(.12,179.96,9);
    const auto failedBy=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    bool failed=false;
    while(!failed && std::chrono::steady_clock::now()<failedBy) {
        failed=!sim.step(.01);
        require(sim.earthSurface()==surface && sim.physics()->snapshot()->surface_revision==surface->revision,
                "failed candidate replaced the active generation");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(failed,"background missing imagery silently admitted");
    rejects([&]{sim.selectEarthRegion(.12,179.96,9);},"missing imagery silently admitted");
    require(commits==1 && sim.earthSurface()==surface && sim.physics()->groundAt(20,30,h,n),"partial tile set committed");
    sim.stop();
    document=tiledFixture(root);
    document["earth"]["follow_body"]="survey";
    writeJson(path,document);
    require(sim.initFromScenario(path.string()),"survey initialization");
    simcore::RigidState survey; survey.position={6000,600,1000};
    require(sim.physics()->createBoxBody("survey",{1,1,1},survey,1),"survey observer");
    auto initial=sim.earthSurface()->revision;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(sim.earthSurface()->revision==initial && std::chrono::steady_clock::now()<deadline) {
        require(sim.step(.01) && sim.physics()->snapshot()->surface_revision==sim.earthSurface()->revision,"in-flight physics revision");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(sim.earthSurface()->revision!=initial &&
        sim.earthSurface()->containsLocal(6000,1000),"automatic tile/LOD follow");
    sim.stop();
    std::cout<<"PASS: date line, poles, bounded tile reuse, LOD, transactional handoff and collision\n";
}
void tiledGpu(const fs::path& root) {
    auto document=tiledFixture(root);
    document["simulation"]["sensor_backend"]="gpu";
    document["sensors"]["sensor_list"]={{{"sensor_id",51},{"type","ir"},{"band","MWIR"},
        {"tx_params",{{"power",0},{"wavelength",4e-6}}},
        {"signal_params",{{"image_width",9},{"image_height",7},{"frame_interval_s",.02},{"integration_time",.008}}},
        {"aperture",{{"position_offset",{20,500,30}},{"direction",{0,-1,0}},{"fov_horizontal",10}}}}};
    const auto path=root/"global/camera.json"; writeJson(path,document);
    Simulation sim;
    require(sim.initFromScenario(path.string()) && sim.gpuSensorBackendActive(),"tiled GPU initialization");
    auto first=sim.earthSurface();
    require(sim.step(.039) && sim.completedFrames().size()==1 && sim.completedFrames()[0].surface_revision==first->revision,
            "first tiled GPU terrain measurement");
    auto before=sim.completedFrames()[0].samples;
    for(int x=0;x<2;++x) raster(root/"global/dem/0"/std::to_string(x)/"0.tif",650,false,0,0,1,false,false);
    sim.selectEarthRegion(.12,-179.94,10);
    auto next=sim.earthSurface();
    require(next->revision!=first->revision && next->indices().size()==first->indices().size(),"equal-topology terrain replacement fixture");
    require(sim.step(.001) && sim.completedFrames().empty(),"exposure spanning two terrain generations was published");
    require(sim.step(.02) && sim.completedFrames().size()==1,"next complete exposure missing");
    const auto& frame=sim.completedFrames().front();
    require(frame.surface_revision==next->revision && frame.samples!=before && std::abs(frame.sim_time-.06)<1e-8,
            "equal-sized GPU terrain change reused old geometry or reset the sensor clock");
    sim.stop();
    std::cout<<"PASS: GPU scene replacement, unchanged triangle count, cross-version rejection and measurement stamp\n";
}
void taipeiDefaultSources() {
    const auto regional=fs::absolute("config/earth/taipei-hires.map.json"),global=fs::absolute("config/earth/global-voxel.map.json");
    simcore::geo::GeoAnchor anchor;
    const json small={{"coverage",{{"min_x",-256},{"max_x",256},{"min_z",-256},{"max_z",256}}},{"grid_size",65},{"imagery_size",128}};
    auto current=EarthDataService::loadMap(fs::absolute("config/earth/default.map.json").string(),anchor,small);
    require(current->dem_spacing_m<40&&current->imagery_spacing_m<=10.01,"default Earth silently used the old Taipei sources");
    auto high=EarthDataService::loadMap(regional.string(),anchor,small);
    require(std::abs(current->sampleLocal(0,0).height-high->sampleLocal(0,0).height)<1e-5&&current->imagery_rgba==high->imagery_rgba,
            "scene-design Earth and regional Earth have different source geometry/imagery");
    double maxError=0;
    for(int level:{12,14}) {
        auto globe=EarthDataService::loadMap(global.string(),anchor,{{"initial_lod",level}});
        require(globe->resident_tiles==9&&globe->dem_spacing_m<50&&globe->imagery_spacing_m<20,"global Taipei residency did not use the new regional TMS");
        const auto key=EarthDataService::tileKey(anchor.lat_deg,anchor.lon_deg,level);const double span=180./(1<<level);
        const int n=32;const double west=-180.+key[1]*span,north=90.-key[2]*span;
        const int col=int((anchor.lon_deg-west)/span*n),row=int((north-anchor.lat_deg)/span*n);
        auto focus=anchor;focus.lon_deg=west+(col+.5)*span/n;focus.lat_deg=north-(row+.5)*span/n;focus.alt_m=0;
        auto reference=EarthDataService::loadMap(regional.string(),focus,{{"coverage",{{"min_x",-1},{"max_x",1},{"min_z",-1},{"max_z",1}}},{"grid_size",3},{"imagery_size",2}});
        const double expected=reference->sampleLocal(0,0).height;
        const auto top=simcore::geo::ecefToLocal(anchor,simcore::geo::geodeticToECEF(focus.lat_deg,focus.lon_deg,10000));
        const auto bottom=simcore::geo::ecefToLocal(anchor,simcore::geo::geodeticToECEF(focus.lat_deg,focus.lon_deg,-1000));
        vsg::dvec3 hit,normal;require(globe->intersectLocal(top,vsg::normalize(bottom-top),20000,hit,normal),"global Taipei surface missing");
        const auto ecef=simcore::geo::localToECEF(anchor,hit);const auto actual=simcore::geo::ecefToGeodetic(ecef.x,ecef.y,ecef.z).z;
        maxError=std::max(maxError,std::abs(actual-expected));
        require(std::abs(actual-expected)<5,"global and regional Taipei heights differ by more than the existing 5 m quantum");
    }
    auto europe=anchor;europe.lat_deg=46.;europe.lon_deg=8.;
    auto outside=EarthDataService::loadMap(global.string(),europe,{{"initial_lod",9}});
    require(outside->valid_samples&&outside->dem_spacing_m>1000&&outside->imagery_spacing_m>5000,"regional upgrade lost the existing global fallback outside Taipei");
    json scene;{std::ifstream input("config/earth/taipei-80km.vscene.json");input>>scene;}
    const auto& source=scene.at("objects").at(0).at("source");
    require(source.at("earth").at("grid_size")==1025&&source.at("earth").at("imagery_size")==3072&&source.at("map")=="taipei-region.map.json","canonical 80 km scene still points at the coarse baseline");
    std::cout<<"PASS: default/regional identical DSM/RGB, global Taipei LOD 12/14 use new sources, height error="<<maxError<<"m within existing 5m quantum; global fallback retained\n";
}
void tileBenchmark() {
    using Clock=std::chrono::steady_clock;
    auto ms=[](Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();};
    const fs::path path=fs::absolute("config/earth/global-voxel.scene.json");
    json document; std::ifstream(path)>>document;
    auto begin=Clock::now(); auto first=EarthDataService::loadScenario(document,path.string());
    double cold=ms(begin);
    Simulation sim;
    sim.setEarthSurfaceOverride(first); require(sim.initFromScenario(path.string()),"benchmark simulation init");
    const auto a=first->anchor;
    std::vector<double> switchTimes;
    size_t hits=0,tiles=0;
    for(int i=0;i<60;++i) {
        double lon=a.lon_deg+(i%8)*180./512;
        int level=i%9==0?8:9;
        auto previous=sim.earthSurface();
        begin=Clock::now();sim.selectEarthRegion(a.lat_deg,lon,level);
        double elapsed=ms(begin);
        auto current=sim.earthSurface();
        if(current!=previous) {
            switchTimes.push_back(elapsed); hits+=current->reused_tiles; tiles+=current->resident_tiles;
        }
        require(sim.physics()->bodyCount()==1 && current->resident_tiles<=9,"residency grew with path length");
    }
    auto current=sim.earthSurface();
    auto key=current->declaration.at("tile").get<std::array<int,3>>();
    double span=180./(1<<key[0]);
    begin=Clock::now();
    for(int i=0;i<10000;++i) require(EarthDataService::selectTiles(current,90.-(key[2]+.5)*span,-180.+(key[1]+.5)*span,key[0])==current,"stationary benchmark");
    double steady=ms(begin);
    std::sort(switchTimes.begin(),switchTimes.end());
    std::cout<<"BENCH real global DEM+RGB cold_mesh_ms="<<cold<<" switches="<<switchTimes.size()
        <<" switch_mesh_and_bullet_p50_ms="<<switchTimes[switchTimes.size()/2]
        <<" p95_ms="<<switchTimes[size_t((switchTimes.size()-1)*.95)]<<" max_ms="<<switchTimes.back()
        <<" reused_tiles="<<hits<<"/"<<tiles<<" stationary_lookup_us="<<steady*.1
        <<" src_dem_spacing_m="<<current->dem_spacing_m<<" src_rgb_spacing_m="<<current->imagery_spacing_m<<" final_triangles="<<current->indices().size()/3<<" atlas_bytes="<<current->imagery_rgba.size()<<"\n";
}
void gpuConsumer(const fs::path& root){
    const auto dem=root/"rasters/gpu.tif",map=root/"data/earth.map.json",path=root/"scenes/gpu.json";
    raster(dem,100,false,-.0032,.0032,.0001,false);writeJson(map,{{"layers",{{{"type","GDALElevation"},{"uri","../rasters/gpu.tif"}}}}});
    auto document=scene(earthConfig());document["simulation"]["sensor_backend"]="gpu";
    document["atmosphere"]={{"cloud_cover",0},{"visibility",1000000},{"rain",0}};
    document["sensors"]["sensor_list"]={{{"sensor_id",51},{"type","ir"},{"band","MWIR"},
        {"tx_params",{{"power",0},{"wavelength",4e-6}}},
        {"signal_params",{{"image_width",9},{"image_height",7},{"frame_interval_s",.02},{"integration_time",.005}}},
        {"aperture",{{"position_offset",{0,500,0}},{"direction",{0,-1,0}},{"fov_horizontal",10}}}}};
    rockyenv::sensor::GpuContext owner;owner.initialize();
    auto radiance=[&](double temperature,bool inherit=false){
        if(inherit)document["earth"].erase("lwir_params");else document["earth"]["lwir_params"]={{"emissivity",1},{"base_temp",temperature}};writeJson(path,document);
        Simulation simulation;simulation.setSensorDevice(owner.device());require(simulation.initFromScenario(path.string())&&simulation.gpuSensorBackendActive(),"strict GPU Earth initialization did not use the production measurement backend");
        require(simulation.physics()->groundPlanes().empty()&&simulation.physics()->bodyLocalTriangles(0).size()==simulation.earthSurface()->indices().size(),"GPU Earth scene substituted a plane");
        require(simulation.step(.02),"production GPU Earth measurement failed");require(simulation.completedFrames().size()==1,"GPU Earth measurement cadence");
        const auto& frame=simulation.completedFrames().front();require(frame.product_stage==0&&frame.width==9&&frame.height==7,"Earth GPU radiance product contract");
        const double value=frame.samples.at(3*9+4).real();require(std::isfinite(value)&&value>0,"GPU Earth thermal surface was not hit");simulation.stop();return value;
    };
    const double cold=radiance(260),warm=radiance(320);require(warm>cold*1.5,"GPU radiance did not consume Earth surface material");
    writeJson(map,{{"earth",{{"lwir_params",{{"emissivity",1},{"base_temp",320}}}}},{"layers",{{{"type","GDALElevation"},{"uri","../rasters/gpu.tif"}}}}});
    require(std::abs(radiance(0,true)-warm)<1e-5,"raw scene did not consume inherited canonical Earth material in production GPU measurements");
    document["earth"]={{"map","../data/earth.map.json"},{"backend","voxel"}};
    writeJson(map,{{"format","voxel-terrain-v1"},{"dimensions",{2,1,2}},{"origin_m",{-100,0,-100}},
                   {"cell_size_m",100},{"occupied",{{0,0,0},{1,0,0},{0,0,1},{1,0,1}}}});
    const double voxelWarm=radiance(320);
    require(std::abs(voxelWarm-warm)<.001,"GPU voxel top did not match the co-located DEM surface");
    std::cout<<"PASS: production offscreen GPU uses pinned Earth triangles and responds to physical Earth temperature, cold="<<cold<<" warm="<<warm<<'\n';
}
}
int main(int argc,char** argv) {
    try {
        Scratch scratch;
        const std::string mode=argc==2 ? argv[1] : "";
        if(mode=="--surface-quality")qualityStatistics();
        else if(mode=="--taipei-defaults")taipeiDefaultSources();
        else if(mode=="--gpu") {
            gpuConsumer(scratch.root);
            tiledGpu(scratch.root);
        } else if(mode=="--tile-gpu-benchmark") terrain_benchmark::run(scratch.root);
        else if(mode=="--tile-benchmark") tileBenchmark();
        else if(argc==1) {
            qualityStatistics();
            rayIndexRegression();
            geometryAndConsumers(scratch.root);
            imageryCacheRegression(scratch.root);
            tmsAndProvenance(scratch.root);
            legacy(scratch.root);
            canonicalIdentity(scratch.root);
            voxelGeometry(scratch.root);
            tiledRegression(scratch.root);
            nativeVolumeRegression(scratch.root);
        } else throw std::invalid_argument("unknown earth test mode");
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';
        return 1;
    }
}
