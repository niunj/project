#include "voxel_earth_fixture.h"
#include <vsgraysim/earth/VoxelScene.h>
#include <vsgraysim/earth/VoxelSceneSensors.h>
#include <vsgraysim/earth/VoxelEarthPatch.h>
#include <vsgraysim/earth/VoxelOcean.h>
#include <vsgraysim/earth/EarthDataService.h>
#include "../src/earth/VoxelDynamics.h"
#include "../src/earth/VoxelTerrain.h"
#include <vsg/core/Array.h>
#include <vsg/core/Array2D.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <iostream>
#include <cmath>
#include <unistd.h>
#include <random>
#include <algorithm>
namespace {
using namespace vsgraysim::earth;using json=nlohmann::json;namespace fs=std::filesystem;
void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
template<class F>void rejects(F f,const char* message){bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed,message);}
void compare(VoxelScene& scene,vsg::dvec3 origin,vsg::dvec3 direction,double limit=30) {
    auto query=scene.raycast(origin,direction,limit),physics=scene.rigidRaycast(origin,direction,limit);
    require(bool(query)==bool(physics),"Earth canonical ray/Bullet hit mask mismatch");
    if(query) {
        if(query->object!=physics->object||std::abs(query->distance-physics->distance)>=.002) {
            std::cerr<<"distance diagnostic canonical="<<query->object<<':'<<query->distance<<" Bullet="<<physics->object<<':'<<physics->distance<<" origin="<<origin<<" ray="<<direction<<'\n';
            throw std::runtime_error("Earth canonical ray/Bullet distance mismatch");
        }
        // Bullet's convex cast reports a support normal for moving boxes;
        // triangle terrain normals have a tighter geometric tolerance.
        const double normalTolerance=scene.snapshot()->objects.at(query->object).body.motion=="dynamic"?.01:.003;
        if(vsg::length(query->normal-physics->normal)>=normalTolerance) {
            std::cerr<<"normal diagnostic object="<<query->object<<" origin="<<origin<<" ray="<<direction<<" canonical="<<query->normal<<" Bullet="<<physics->normal<<" distance="<<query->distance<<'\n';
            throw std::runtime_error("Earth canonical ray/Bullet normal mismatch");
        }
    }
}
void consumers(VoxelScene& scene) {
    const auto snapshot=scene.snapshot();const auto display=makeVoxelSceneDisplay(*snapshot);const auto sensor=makeVoxelSceneSensorData(snapshot);
    for(const auto& instance:sensor.instances) {
        const auto& object=snapshot->objects.at(instance.id);const auto& surface=object.surface();
        auto group=display.placements.at(instance.id)->children.at(0).cast<vsg::StateGroup>();
        auto draw=group->children.at(0).cast<vsg::VertexIndexDraw>();auto measured=instance.geometry;
        require(draw&&measured,"Earth canonical display/sensor draws missing");
        auto a=draw->arrays[0]->data.cast<vsg::vec3Array>(),b=measured->arrays[0]->data.cast<vsg::vec3Array>();
        require(a->size()==surface.vertices().size()&&a->size()==b->size(),"Earth display/sensor vertex count mismatch");
        for(size_t i=0;i<a->size();++i)require((*a)[i]==(*b)[i]&&(*a)[i]==vsg::vec3(surface.vertices()[i]),"Earth display/sensor use different canonical positions");
        require(draw->indexCount==measured->indexCount&&draw->indexCount==surface.indices().size(),"Earth display/sensor topology mismatch");
    }
    for(double z:{-2.2,-.2,1.7})for(double x:{-2.7,-.4,2.3})compare(scene,{x,8,z},{0,-1,0});
    compare(scene,{-10,-.3,.3},{1,0,0});compare(scene,{.3,-10,.2},{0,1,0});compare(scene,{10,8,10},{0,-1,0});
}
void coastWaterMask(const fs::path& root){
#if VOXEL_EARTH_TEST_RASTER
    auto definition=earth_fixture::create(root);fs::copy_file(root/"dem.tif",root/"sea.tif");auto ds=GDALOpen((root/"sea.tif").c_str(),GA_Update);require(ds,"sea reference fixture missing");std::vector<double> sea(4096,100.);require(GDALRasterIO(GDALGetRasterBand(ds,1),GF_Write,0,0,64,64,sea.data(),64,64,GDT_Float64,0,0)==CE_None,"sea datum fixture write");GDALClose(ds);
    auto map=json::parse(std::ifstream(root/"earth.map.json"));map["layers"]=json::array({{{"type","GDALElevation"},{"uri","sea.tif"}}});earth_fixture::write(root/"sea.map.json",map);
    definition["objects"][0]["source"]["sea_surface_mask"]={{"map","sea.map.json"},{"epsilon_m",1.2}};definition["ocean"]={{"wave_height_m",0},{"swell_height_m",0},{"bottom_m",-8},{"reference_surface",{{"map","sea.map.json"},{"grid_size",17},{"earth",definition["objects"][0]["source"]["earth"]}}}};
    auto scene=VoxelScene::load(definition,(root/"earth.vscene.json").string());require(std::abs(scene->snapshot()->ocean->meanSurface(0,0).x)<.0001,"ocean datum loaded before the scene geo anchor");
    for(const auto& mode:{"hd","voxel"}){scene->edit(json::array({{{"kind","terrain_mode"},{"object","earth"},{"mode",mode}}}),scene->snapshot()->revision);require(!scene->raycast({-2.75,8,-3.25},{0,-1,0},20)&&!scene->rigidRaycast({-2.75,8,-3.25},{0,-1,0},20),"DSM zero-water column became a solid collider");auto land=scene->raycast({2.75,8,2.75},{0,-1,0},20);require(land&&land->object=="earth"&&land->position.y>1.5,"water mask removed real elevated land");compare(*scene,{2.75,8,2.75},{0,-1,0});}
    auto restored=VoxelScene::load(scene->checkpoint());require(restored->checkpoint()==scene->checkpoint(),"coast mask/sea datum was lost in checkpoint");require(!restored->raycast({-2.75,8,-3.25},{0,-1,0},20),"restored DSM water plane became solid");
#else
    (void)root;
#endif
}
void geometryAndDamage(const fs::path& root) {
    const auto document=earth_fixture::create(root);auto scene=VoxelScene::open((root/"earth.vscene.json").string());
    const auto before=scene->snapshot();const auto& initial=before->objects.at("earth");
    require(initial.earth&&initial.body.motion=="static"&&initial.body.fracture&&initial.role=="terrain","Earth source did not enter unified physical scene");
    require(std::abs(initial.surface().sampleLocal(-2.2,-1.3).height-(1.25-.11-.104))<.002,"HD surface lost georeferenced elevation");
    const auto initialMass=initial.massKg();consumers(*scene);
    const auto expectedNormal=vsg::normalize(vsg::vec3(-.05f,1,-.08f));
    for(size_t i=0;i<initial.surface().indices().size();i+=3) {
        const auto index=initial.surface().indices()[i];const auto a=initial.surface().vertices()[index],b=initial.surface().vertices()[initial.surface().indices()[i+1]],c=initial.surface().vertices()[initial.surface().indices()[i+2]];
        if(vsg::cross(b-a,c-a).y>1e-8)require(vsg::length(initial.surface().surface_normals.at(index)-expectedNormal)<.003,"terrain smoothing changed a planar DEM slope");
    }
    require(initial.surface().surface_normals.size()==initial.surface().vertices().size(),"DEM gradient normals missing");
    auto blockDefinition=document;blockDefinition["objects"][0]["source"]["voxel_surface"]="blocks";blockDefinition["objects"][0]["source"]["mode"]="voxel";
    auto block=VoxelScene::load(blockDefinition,(root/"earth.vscene.json").string());require(block->raycast({-2.2,8,-1.3},{0,-1,0},20)->position.y==2,"explicit block debug surface changed");consumers(*block);

    validateVoxelEarthView(*before,{0,8,0},{0,-1,0},{0,0,-1},8,9./7.,30);
    rejects([&]{validateVoxelEarthView(*before,{8,8,0},{0,-1,0},{0,0,-1},8,9./7.,30);},"Earth unknown ground outside the active set was measured as empty sky");
    auto rotated=document;rotated["geo_anchor"]["heading_deg"]=37;
    auto rotation=VoxelScene::load(rotated,(root/"earth.vscene.json").string());
    const auto& rotatedObject=rotation->snapshot()->objects.at("earth");
    const auto original=EarthDataService::loadMap((root/(VOXEL_EARTH_TEST_RASTER?"earth.map.json":"terrain.ter")).string(),rotation->snapshot()->anchor,document["objects"][0]["source"]["earth"]);
    for(const auto point:{vsg::dvec3(-1.25,0,1.75),vsg::dvec3(2.25,0,-.75)})
        require(std::abs(rotatedObject.surface().sampleLocal(point.x,point.z).height-original->sampleLocal(point.x,point.z).height)<.001,"Earth patch lost the declared geodetic heading");
    consumers(*rotation);
#if VOXEL_EARTH_TEST_RASTER
    require(initial.surface().imagery_width==32&&initial.surface().imagery_uv.size()==initial.surface().vertices().size(),"georeferenced imagery atlas missing");
    require(initial.material["temperature_k"]==314&&std::abs(initial.material["emissivity"].get<double>()-.83)<1e-6,"Earth physical material defaults lost");
    const auto& pixels=initial.surface().imagery_rgba;
    require(pixels[4*(8*32+16)]>200&&pixels[4*(24*32+16)+2]>200,"Earth imagery north/south orientation reversed");
    const auto data=makeVoxelSceneSensorData(before);const auto linear=data.textures.layers.at(0).cast<vsg::ubvec4Array2D>();
    require((*linear)(16,8).r>180&&(*linear)(16,8).g<30,"sensor atlas used sRGB values as linear reflectance");
#endif
    scene->schedule({{"kind","terrain_mode"},{"object","earth"},{"tick",1},{"mode","voxel"}});
    scene->schedule({{"kind","damage"},{"object","earth"},{"tick",2},{"space","object"},{"point_m",{0,1,0}},{"radius_m",1.6}});
    scene->schedule({{"kind","terrain_mode"},{"object","earth"},{"tick",3},{"mode","hd"}});scene->setPaused(false);
    scene->advanceTo(.02);consumers(*scene);require(std::abs(scene->raycast({-2.2,8,-1.3},{0,-1,0},20)->position.y-(1.25-.11-.104))<.002,"smooth voxel mode quantized the DEM cap");
    scene->advanceTo(.04);consumers(*scene);const auto removed=scene->snapshot()->journal[1]["removed_voxels"].get<size_t>();require(removed>0,"Earth damage did not remove cells");
    require(scene->snapshot()->objects.at("earth").massKg()<initialMass,"Earth damage did not change physical mass");
    scene->advanceTo(.06);consumers(*scene);const auto hole=scene->raycast({.25,8,.25},{0,-1,0},20);
    const auto& carved=scene->snapshot()->objects.at("earth").surface();bool hardCut=false;
    for(size_t i=0;i<carved.indices().size();i+=3){const auto a=carved.indices()[i],b=carved.indices()[i+1],c=carved.indices()[i+2];if(std::abs(carved.vertices()[a].y)<1e-9&&std::abs(carved.vertices()[b].y)<1e-9&&std::abs(carved.vertices()[c].y)<1e-9&&vsg::cross(carved.vertices()[b]-carved.vertices()[a],carved.vertices()[c]-carved.vertices()[a]).y>1e-9){require(vsg::length(carved.surface_normals.at(a)-vsg::vec3(0,1,0))<1e-6,"smoothing rounded an excavation floor");hardCut=true;}}
    require(hardCut,"terrain crater fixture had no exposed hard floor");
    require(hole&&hole->position.y<.1,"HD mode switch resurrected the undamaged DEM cap");
    require(std::abs(before->objects.at("earth").massKg()-initialMass)<1e-8&&before->objects.at("earth").terrainMode=="hd","Earth damage mutated a pinned snapshot");
    scene->schedule({{"kind","damage"},{"object","earth"},{"tick",4},{"space","object"},{"point_m",{2,-.5,0}},{"radius_m",.8}});scene->advanceTo(.08);
    const auto roof=scene->raycast({2.25,-.5,.25},{0,1,0},10);require(roof&&std::abs(roof->position.y)<.001,"HD excavation erased a surviving cave ceiling");
    compare(*scene,{2.25,-.5,.25},{0,1,0});compare(*scene,{2.25,-.5,.25},{1,0,0});consumers(*scene);
    const auto checkpoint=scene->checkpoint();
    fs::remove(root/"dem.tif");fs::remove(root/"imagery.tif");fs::remove(root/"terrain.ter");fs::remove(root/"earth.map.json");
    auto restored=VoxelScene::load(checkpoint);consumers(*restored);require(restored->checkpoint()==checkpoint,"Earth self-contained checkpoint reopens external assets or loses damage");
    scene->advanceTo(.28,64);restored->advanceTo(.28,64);require(scene->checkpoint()==restored->checkpoint(),"Earth checkpoint continuation diverged");
    restored->setPaused(true);const auto failed=restored->checkpoint();const auto history=restored->history();
    restored->addPublicationParticipant("zz-failure",[](auto){throw std::runtime_error("injected terrain consumer failure");});
    rejects([&]{restored->edit(json::array({{{"kind","terrain_mode"},{"object","earth"},{"mode","voxel"}}}),restored->snapshot()->revision);},"failed terrain publication succeeded");
    require(restored->checkpoint()==failed&&restored->history()==history,"failed terrain mode switch changed physics/history");restored->removePublicationParticipant("zz-failure");
    auto invalid=checkpoint;invalid["earth_patches"]["earth-patch-1"]["heights_m"][0]=-100;rejects([&]{VoxelScene::load(invalid);},"invalid Earth checkpoint height accepted");
    std::cout<<"PASS: georeferenced HD/voxel Earth, canonical display/Bullet/sensor meshes, craters, cave ceilings, immutable damage, atomic mode changes and source-free checkpoints\n";
}
void refinedTerrain(const fs::path& root) {
    auto document=earth_fixture::create(root);
    document["objects"][0]["source"]["hd_subdivisions"]=8;
    document["objects"][0]["source"]["mode"]="voxel";
    auto scene=VoxelScene::load(document,(root/"earth.vscene.json").string());
    const auto& surface=scene->snapshot()->objects.at("earth").surface();
    require(surface.vertices().size()*2<surface.indices().size(),"refined DEM duplicated vertices instead of sharing the cap");
    consumers(*scene);
    scene->schedule({{"kind","damage"},{"object","earth"},{"tick",1},{"space","object"},{"point_m",{.2,1,.2}},{"radius_m",1.1}});
    scene->setPaused(false);scene->advanceTo(.02);consumers(*scene);
    auto restored=VoxelScene::load(scene->checkpoint());consumers(*restored);
    require(restored->checkpoint()==scene->checkpoint(),"shared refined terrain did not round-trip after excavation");
    std::cout<<"PASS: subdivided DEM shares vertices, retains hard cuts and matches collision/checkpoint consumers\n";
}
void dynamicsAndFracture(const fs::path& root) {
    auto document=earth_fixture::create(root);
    for(const std::string mode:{"hd","voxel"}) {
        auto input=document;input["objects"][0]["source"]["mode"]=mode;
        input["objects"].push_back({{"id","probe"},{"source",{{"type","box"},{"cell_size_m",.5},{"dimensions",{1,1,1}}}},
            {"position_m",{-2.25,4,-1.25}},{"physics",{{"motion","dynamic"},{"friction",1},{"restitution",0}}}});
        auto scene=VoxelScene::load(input,(root/"earth.vscene.json").string());scene->setPaused(false);
        for(int i=1;i<=100;++i)scene->advanceTo(i*.02,1);
        const auto& body=scene->snapshot()->objects.at("probe");const auto p=body.pose(100,.02)*body.localCentreOfMass();
        const auto ground=scene->snapshot()->objects.at("earth").surface().sampleLocal(p.x,p.z);
        require(ground.status!=SurfaceStatus::NoData&&p.y>ground.height+.05&&p.y<ground.height+.65&&vsg::length(body.velocity)<.3,"dynamic target did not settle on canonical Earth collider");
        require(!scene->snapshot()->contacts.empty(),"Earth/target physical contacts missing");
        scene->schedule({{"kind","damage"},{"object","earth"},{"tick",101},{"space","object"},{"point_m",{p.x,1,p.z}},{"radius_m",1.3}});
        for(int i=101;i<=150;++i)scene->advanceTo(i*.02,1);
        const auto& after=scene->snapshot()->objects.at("probe");const auto fallen=after.pose(150,.02)*after.localCentreOfMass();
        require(fallen.y<p.y-.8,"target remained on a stale Earth collider after crater publication");compare(*scene,{fallen.x,8,fallen.z},{0,-1,0});
    }
    auto scene=VoxelScene::load(document,(root/"earth.vscene.json").string());
    scene->schedule({{"kind","fracture"},{"object","earth"},{"tick",1},{"space","object"},{"point_m",{0,0,0}},{"normal",{0,1,0}},{"radius_m",20},{"energy_j",1e9}});
    scene->setPaused(false);scene->advanceTo(.02);const auto s=scene->snapshot();require(s->objects.size()==2,"Earth face-bond cut did not detach upper material");
    const auto fragment=s->journal[0]["fragments"][0].get<std::string>();
    require(s->objects.at("earth").terrainMode=="hd"&&s->objects.at(fragment).terrainMode=="voxel"&&s->objects.at(fragment).body.motion=="dynamic"&&s->objects.at(fragment).earth,"terrain fragment did not retain imagery/physics provenance");
    const auto& rootObject=s->objects.at("earth");const auto chart=rootObject.volume->gridOrigin();const double cell=rootObject.volume->resolution_m;
    auto tiny=VoxelTerrain::fromOccupancy(chart+vsg::dvec3(2*cell,0,2*cell),cell,2,1,2,std::vector<bool>(4,true),false);auto cropped=rootObject.earth->makeSurface(tiny,"voxel",true);auto staticCrop=rootObject.earth->makeSurface(tiny,"voxel",false);require(staticCrop->imagery_rgba.size()<rootObject.surface().imagery_rgba.size(),"anchored cropped terrain kept the full imagery atlas");const auto& full=rootObject.surface();require(cropped->imagery_rgba.size()<full.imagery_rgba.size(),"small terrain fragment retained the full imagery atlas");
    auto pixel=[](const EarthSurfaceSnapshot& mesh,vsg::vec2 uv){double x=uv.x*mesh.imagery_width-.5,y=uv.y*mesh.imagery_height-.5;int ix=int(std::floor(x)),iy=int(std::floor(y));vsg::dvec3 color{};for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){const int a=std::clamp(ix+dx,0,int(mesh.imagery_width)-1),b=std::clamp(iy+dy,0,int(mesh.imagery_height)-1);const double w=(dx?x-ix:1-x+ix)*(dy?y-iy:1-y+iy);for(int c=0;c<3;++c)color[c]+=mesh.imagery_rgba[4*(size_t(b)*mesh.imagery_width+a)+c]*w;}return color;};
    const auto bounds=rootObject.earth->bounds();for(size_t i=0;i<cropped->vertices().size();++i){const auto& p=cropped->vertices()[i];const vsg::vec2 original(float((p.x-bounds.min.x)/(bounds.max.x-bounds.min.x)),float((p.z-bounds.min.z)/(bounds.max.z-bounds.min.z)));require(vsg::length(pixel(*cropped,cropped->imagery_uv[i])-pixel(full,original))<.001,"terrain fragment crop changed bilinear source imagery");}
    std::cout<<"PASS: bounded terrain-fragment imagery, original pixels and bilinear UV equivalence\n";
    consumers(*scene);const auto checkpoint=scene->checkpoint();require(checkpoint["earth_patches"].size()==1,"terrain debris duplicated source height/imagery checkpoints");auto restored=VoxelScene::load(checkpoint);
    require(restored->snapshot()->objects.at("earth").earth==restored->snapshot()->objects.at(fragment).earth,"restored terrain debris did not share immutable source data");
    restored->setPaused(true);const auto incompatible=restored->checkpoint();
    rejects([&]{restored->edit(json::array({{{"kind","terrain_mode"},{"object",fragment},{"mode","hd"}}}),restored->snapshot()->revision);},"moving terrain accepted a static triangle collider");
    require(restored->checkpoint()==incompatible,"invalid terrain fragment mode changed physical truth");restored->setPaused(false);
    for(int i=2;i<=10;++i){scene->advanceTo(i*.02,1);restored->advanceTo(i*.02,1);}require(scene->checkpoint()==restored->checkpoint(),"fractured Earth physical continuation diverged");
    std::cout<<"PASS: HD/voxel terrain rigid contact, support removal, terrain fracture, textured moving debris and deterministic continuation\n";
}
void indexedTerrainContacts(const fs::path& root) {
    auto document=earth_fixture::create(root);
    constexpr int nx=24,nz=12;constexpr double cell=.125;
    auto volume=VoxelTerrain::fromOccupancy({-1.5,0,-.75},cell,nx,1,nz,std::vector<bool>(nx*nz,true),false);
    auto skin=std::make_shared<VoxelSurfaceAppearance>();skin->origin=volume->gridOrigin();skin->dimensions={nx,1,nz};skin->cellSize=cell;
    skin->width=skin->height=10;skin->rgba.assign(400,255);
    for(int z=0;z<=nz;++z)for(int y=0;y<=1;++y)for(int x=0;x<=nx;++x)
        skin->corners[skin->cornerKey(x,y,z)]={skin->origin+vsg::dvec3(x*cell,y*cell+.012*std::sin(x*.7)*std::cos(z*.6),z*cell),{0,1,0}};
    volume->setAppearance(skin);
    document["objects"].push_back({{"id","wing"},{"position_m",{0,10,0}},
        {"source",{{"type","volume"},{"volume",volume->encodeVolume()}}},{"physics",{{"motion","dynamic"}}}});
    auto scene=VoxelScene::load(document,(root/"earth.vscene.json").string());
    // Exercise caps, excavation walls, underside and the surviving cave roof.
    scene->schedule({{"kind","damage"},{"object","earth"},{"tick",1},{"space","object"},{"point_m",{0,1,0}},{"radius_m",1.3}});
    scene->schedule({{"kind","damage"},{"object","earth"},{"tick",1},{"space","object"},{"point_m",{2,-.5,0}},{"radius_m",.8}});
    scene->setPaused(false);scene->advanceTo(.02);
    VoxelDynamics indexed,reference(false);std::mt19937 rng(4471);std::uniform_real_distribution<double> unit(0,1);
    size_t checked=0,contactCases=0;
    for(int trial=0;trial<400;++trial) {
        auto input=*scene->snapshot();input.tick=0;
        const double x=unit(rng)*9-4.5,z=unit(rng)*9-4.5,y=trial%4==0?-2.8:(trial%4==1?-.5:1.25);
        const auto frame=vsg::translate(trial%3==0?1000.:0.,trial%3==0?980.:0.,trial%3==0?-1000.:0.)*
            vsg::rotate(unit(rng)*3.,vsg::normalize(vsg::dvec3(.3,1.,.2)));
        input.objects.at("earth").basePose=frame;input.objects.at("earth").motionOriginTick=0;
        auto& wing=input.objects.at("wing");wing.basePose=frame*vsg::translate(x,y+unit(rng)*.5-.25,z)*
            vsg::rotate(unit(rng)*6.,vsg::normalize(vsg::dvec3(.3+unit(rng),.1+unit(rng),.2+unit(rng))));wing.motionOriginTick=0;
        auto actual=input,expected=input;indexed.detect(actual,1.);reference.detect(expected,1.);
        auto signatures=[](const std::vector<VoxelContact>& contacts){std::vector<std::array<double,10>> out;for(const auto& c:contacts)out.push_back({c.pointA.x,c.pointA.y,c.pointA.z,c.pointB.x,c.pointB.y,c.pointB.z,c.normalOnB.x,c.normalOnB.y,c.normalOnB.z,c.penetrationM});std::sort(out.begin(),out.end());return out;};
        const auto a=signatures(actual.contacts),b=signatures(expected.contacts);
        if(a.size()!=b.size()){std::cerr<<"contact oracle trial="<<trial<<" indexed="<<a.size()<<" reference="<<b.size()<<'\n';throw std::runtime_error("terrain spatial rejection dropped or invented a Bullet contact");}
        for(size_t i=0;i<a.size();++i)for(size_t k=0;k<a[i].size();++k)require(std::abs(a[i][k]-b[i][k])<.00005,"indexed terrain changed an exact narrow-phase contact");
        checked+=a.size();contactCases+=!a.empty();
    }
    require(contactCases>80&&checked>1000,"terrain collision oracle did not exercise enough real contacts");
    std::cout<<"PASS: indexed/reference Bullet contact equivalence at 400 poses, "<<contactCases<<" contacting poses / "<<checked<<" contacts, including rotated chart, terrain edges and excavation\n";
    auto fast=*scene->snapshot(),slow=fast;fast.tick=slow.tick=0;
    for(auto& [id,o]:fast.objects)o.motionOriginTick=0;
    auto& target=fast.objects.at("wing");target.basePose=vsg::translate(-2.,2.,-1.5);target.motionOriginTick=0;target.velocity={0,0,0};target.angularVelocity={0,0,0};slow=fast;
    size_t supported=0;
    for(int tick=1;tick<=150;++tick) {
        auto a=fast,b=slow;a.tick=b.tick=tick;indexed.step(fast,a);reference.step(slow,b);
        const auto& x=a.objects.at("wing");const auto& y=b.objects.at("wing");
        require(vsg::length(x.basePose[3]-y.basePose[3])<.0001&&vsg::length(x.velocity-y.velocity)<.001&&vsg::length(x.angularVelocity-y.angularVelocity)<.001,"terrain index changed deterministic contact dynamics against the reference path");
        supported+=!a.contacts.empty();fast=std::move(a);slow=std::move(b);
    }
    require(supported>80,"reference dynamics comparison never reached persistent support");
    std::cout<<"PASS: indexed/reference 150-step falling skin and sustained ground support with canonical solver order\n";
}
}
int main(int argc,char** argv)try {
    if(argc==3&&std::string(argv[1])=="--write-fixture"){earth_fixture::create(fs::absolute(argv[2]));return 0;}
    std::string pattern="/tmp/vsgraysim_earth_scene_XXXXXX";auto* scratch=mkdtemp(pattern.data());if(!scratch)throw std::runtime_error("scratch allocation");const fs::path root=scratch;
    try {
        coastWaterMask(root/"coast");geometryAndDamage(root/"geometry");refinedTerrain(root/"refined");dynamicsAndFracture(root/"dynamics");indexedTerrainContacts(root/"contact-index");
#if VOXEL_EARTH_TEST_RASTER
        earth_fixture::create(root/"missing",true,true);rejects([&]{VoxelScene::open((root/"missing/earth.vscene.json").string());},"DEM NoData was filled with fabricated solid ground");
#endif
    }catch(...){fs::remove_all(root);throw;}
    fs::remove_all(root);return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
