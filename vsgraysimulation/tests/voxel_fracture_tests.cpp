#include <vsgraysim/earth/VoxelScene.h>
#include <vsgraysim/earth/VoxelSceneSensors.h>
#include "../src/earth/VoxelTerrain.h"
#include "../src/earth/VoxelSensorMaterials.h"
#include <vsg/all.h>
#include <iostream>
#include <stdexcept>
using namespace vsgraysim::earth;using json=nlohmann::json;
namespace {
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class F>void rejects(F f,const char* message){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected,message);}
json body(const std::string& id,json dimensions,json position,const std::string& motion){return {{"id",id},{"position_m",position},
    {"source",{{"type","box"},{"dimensions",dimensions},{"cell_size_m",1}}},
    {"material",{{"density_kg_m3",1000},{"temperature_k",325}}},
    {"physics",{{"motion",motion},{"linear_damping",0},{"angular_damping",0},{"restitution",0},{"friction",.8}}}};}
json columnScene(){auto column=body("column",{2,6,2},{0,0,0},"static");column["physics"]["fracture"]=true;column["physics"]["support"]="base";
    return {{"format","voxel-scene-v1"},{"objects",{body("floor",{12,1,12},{-5,-1,-5},"static"),column}},
        {"events",json::array({{{"tick",1},{"kind","damage"},{"object","column"},{"point_m",{1,3,1}},{"space","object"},{"radius_m",1.1}}})}};}
size_t voxels(const VoxelSceneSnapshot& s){size_t result=0;for(const auto& [id,o]:s.objects)result+=o.volume->occupiedCount();return result;}
void rays(VoxelScene& scene,const vsg::dvec3& p,const vsg::dvec3& d){const auto a=scene.raycast(p,d,100),b=scene.rigidRaycast(p,d,100);
    require(bool(a)==bool(b),"occupied-volume DDA and Bullet disagree on occlusion");
    if(a){require(a->object==b->object,"DDA and Bullet hit different bodies");require(vsg::length(a->position-b->position)<.0002,"Bullet collider expanded into air or used an AABB");}}
std::pair<vsg::dvec3,vsg::dvec3> momentum(const VoxelSceneSnapshot& state) {
    vsg::dvec3 linear{},angular{};
    for(const auto& [id,o]:state.objects){const auto n=o.volume->dimensions();const auto pose=o.pose(state.tick,state.fixedStep);const double cell=o.volume->resolution_m,m=cell*cell*cell*o.material.at("density_kg_m3").get<double>();
        for(uint32_t i=0;i<o.volume->cellCount();++i)if(o.volume->occupancy()[i]) {
            const auto p=o.volume->gridOrigin()+vsg::dvec3(i%n[0]+.5,i/n[0]%n[1]+.5,i/(n[0]*n[1])+.5)*cell;
            const auto r=pose*vsg::dvec4(p,0);const auto offset=vsg::dvec3(r.x,r.y,r.z);const auto world=pose*p;
            const auto v=o.velocity+vsg::cross(o.angularVelocity,offset);linear+=v*m;angular+=vsg::cross(world,v*m)+o.angularVelocity*(m*cell*cell/6.);
        }}return {linear,angular};
}
void damageDetachAndLand(){
    auto scene=VoxelScene::load(columnScene());const auto intact=scene->snapshot();const auto before=voxels(*intact);scene->setPaused(false);
    scene->advanceTo(.02);const auto split=scene->snapshot();require(split->objects.size()==3,"damage did not detach an unsupported island");
    const auto removed=split->journal[0].at("removed_voxels").get<size_t>();require(voxels(*split)+removed==before,"fracture duplicated or lost surviving voxels");
    const auto id=split->journal[0].at("fragments").at(0).get<std::string>();require(split->objects.at(id).body.motion=="dynamic"&&split->objects.at("column").body.motion=="static","anchored and detached pieces have wrong motion modes");
    require(intact->objects.at("column").volume->occupiedCount()==24,"fracture mutated retained geometry");
    rays(*scene,{1,20,1},{0,-1,0});rays(*scene,{1,3,-5},{0,0,1});require(!scene->raycast({1,3,-5},{0,0,1},20),"destroyed support layer retained ghost collision");
    auto display=makeVoxelSceneDisplay(*split);auto sensors=makeVoxelSceneSensorData(split);
    for(const auto& instance:sensors.instances)require(instance.transform==display.placements.at(instance.id)->matrix,"display and sensor fragment placements differ");
    scene->advanceTo(.5,32);require(scene->snapshot()->objects.at(id).pose(scene->snapshot()->tick,.02)[3][1]<-.5,"detached fragment did not fall");
    display.sync(*scene->snapshot());rays(*scene,{1,20,1},{0,-1,0});
    auto resumed=VoxelScene::load(scene->checkpoint());resumed->setPaused(false);scene->advanceTo(2,128);resumed->advanceTo(2,128);
    require(scene->checkpoint()==resumed->checkpoint(),"Bullet checkpoint continuation depends on unsaved contact caches");
    const auto landed=scene->snapshot();const auto& o=landed->objects.at(id);double bottom=1e30;const auto box=o.localBounds();const auto pose=o.pose(landed->tick,.02);
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x)bottom=std::min(bottom,(pose*vsg::dvec3(x?box.max.x:box.min.x,y?box.max.y:box.min.y,z?box.max.z:box.min.z)).y);
    require(bottom>1.94&&bottom<2.1,"fragment penetrated the surviving target or did not settle onto it");require(!landed->contacts.empty(),"rigid contacts were not published");
    require(vsg::length(o.velocity)<.1,"resting fragment retained unbounded contact energy");
    const auto sample=scene->sample(1.99);require(std::abs(sample->timeSeconds()-1.99)<1e-12,"measurement history did not sample requested time");
    rejects([&]{scene->sample(3);},"measurement extrapolated beyond published truth");
    std::cout<<"PASS: unsupported-island fracture, mass accounting, exact concave collision, gravity, landing and checkpoint continuation\n";
}
void bondsAndImpulse(){
    auto target=body("block",{2,2,2},{0,10,0},"dynamic");target["physics"]["fracture"]=true;target["velocity_mps"]={2,0,0};target["angular_velocity_rad_s"]={0,0,1};
    json definition={{"format","voxel-scene-v1"},{"gravity_mps2",{0,0,0}},{"objects",{target}}};
    auto scene=VoxelScene::load(definition);scene->schedule({{"kind","fracture"},{"object","block"},{"tick",1},{"point_m",{1,1,1}},{"normal",{1,0,0}},{"radius_m",10},{"energy_j",999}});
    scene->setPaused(false);scene->advanceTo(.02);require(scene->snapshot()->objects.size()==1&&scene->snapshot()->journal[0]["broken_bonds"]==0,"insufficient fracture energy broke a face bond");
    auto reference=VoxelScene::load(scene->checkpoint());reference->advanceTo(.04);
    scene->schedule({{"kind","fracture"},{"object","block"},{"tick",2},{"point_m",{1,1,1}},{"normal",{1,0,0}},{"radius_m",10},{"energy_j",4000}});scene->advanceTo(.04);
    const auto split=scene->snapshot();require(split->objects.size()==2&&voxels(*split)==8,"bond fracture removed material or failed to partition the target");
    require(split->journal[1]["broken_bonds"]==4&&split->journal[1]["fracture_energy_j"]==4000,"face-bond energy accounting is wrong");
    const auto expected=momentum(*reference->snapshot()),actual=momentum(*split);
    require(vsg::length(expected.first-actual.first)<.01&&vsg::length(expected.second-actual.second)<.1,"fracture did not inherit linear/angular momentum");
    scene->schedule({{"kind","impulse"},{"object","block"},{"tick",3},{"space","scene"},{"point_m",{0,15,0}},{"impulse_ns",{100,0,0}}});scene->advanceTo(.06);
    const auto kicked=momentum(*scene->snapshot());require(std::abs(kicked.first.x-actual.first.x-100)<.05,"off-centre impulse has wrong linear momentum");
    require(vsg::length(scene->snapshot()->objects.at("block").angularVelocity-split->objects.at("block").angularVelocity)>.001,"off-centre impulse did not produce angular motion");
    std::cout<<"PASS: face-bond energy threshold, conserved fragment mass/momentum and off-centre torque\n";
}
void concaveAndFastContact(){
    std::vector<bool> cells(25,false);for(int y=0;y<5;++y)for(int x=0;x<5;++x)if(x==0||x==4||y==4)cells[y*5+x]=true;
    auto arch=body("arch",{1,1,1},{5,0,0},"static");arch["rotation_deg"]={0,37,0};
    arch["source"]={{"type","volume"},{"volume",VoxelTerrain::fromOccupancy({0,0,0},1,5,5,1,cells)->encodeVolume()}};
    const auto frame=vsg::translate(5.,0.,0.)*vsg::rotate(vsg::radians(-37.),vsg::dvec3(0,1,0));
    auto probe=[&](const char* id,double x){auto object=body(id,{1,1,1},{0,0,0},"dynamic");object["source"]["cell_size_m"]=.5;
        const auto p=frame*vsg::dvec3(x,2.25,-4);const auto v=frame*vsg::dvec4(0,0,80,0);object["position_m"]={p.x,p.y,p.z};object["velocity_mps"]={v.x,v.y,v.z};object["rotation_deg"]={0,37,0};return object;};
    auto scene=VoxelScene::load({{"format","voxel-scene-v1"},{"gravity_mps2",{0,0,0}},{"objects",{arch,probe("through-hole",2.25),probe("into-leg",.25)}}});
    const auto d=frame*vsg::dvec4(0,0,1,0);rays(*scene,frame*vsg::dvec3(2.5,2.5,-2),{d.x,d.y,d.z});
    require(!scene->rigidRaycast(frame*vsg::dvec3(2.5,2.5,-2),{d.x,d.y,d.z},3),"concave collider filled the arch opening");
    scene->setPaused(false);scene->advanceTo(.1,8);const auto state=scene->snapshot();const auto inverse=vsg::inverse(frame);
    const auto open=inverse*state->objects.at("through-hole").pose(state->tick,state->fixedStep)[3];
    const auto blocked=inverse*state->objects.at("into-leg").pose(state->tick,state->fixedStep)[3];
    std::cout<<"fast probe open="<<open.x<<","<<open.y<<","<<open.z<<" blocked="<<blocked.x<<","<<blocked.y<<","<<blocked.z<<" speed="<<vsg::length(state->objects.at("into-leg").velocity)<<" contacts="<<state->contacts.size()<<'\n';
    require(open.z>3.5,"moving body collided with a convex/AABB proxy across empty space");
    require(blocked.z<0&&vsg::length(state->objects.at("into-leg").velocity)<1,"fast moving body tunneled through an occupied voxel wall");
    std::cout<<"PASS: rotated concave occupied-volume contacts and adaptive substeps at 80 m/s\n";
}
void rollbackAndHole(){
    auto scene=VoxelScene::load(columnScene()),reference=VoxelScene::load(columnScene());scene->setPaused(false);reference->setPaused(false);const auto before=scene->checkpoint();
    scene->addPublicationParticipant("zz-fault",[](auto){throw std::runtime_error("injected consumer allocation failure");});
    rejects([&]{scene->advanceTo(.02);},"fracture publication failure was ignored");require(scene->checkpoint()==before&&scene->history().size()==1,"failed fracture leaked bodies, IDs, events or history");
    scene->removePublicationParticipant("zz-fault");scene->advanceTo(.02);reference->advanceTo(.02);require(scene->checkpoint()==reference->checkpoint(),"failed publication changed subsequent Bullet dynamics");
    auto archived=scene->snapshot();const auto id=archived->journal[0]["fragments"][0].get<std::string>();
    scene->schedule({{"kind","damage"},{"object",id},{"tick",2},{"point_m",{1,5,1}},{"space","object"},{"radius_m",100}});scene->advanceTo(.04);
    require(scene->snapshot()->objects.at(id).massKg()==0,"destroyed fragment retained material");rays(*scene,{1,20,1},{0,-1,0});
    require(archived->objects.at(id).massKg()>0,"repeated damage mutated an archived fragment");
    std::cout<<"PASS: multi-consumer publication rollback and repeated damage without ghost fragment colliders\n";
}
void fractionalCellRays() {
    for(int i=0;i<40;++i) {
        const double cell=.1044718724489212,base=.07+i*.001;
        std::vector<bool> cells(4*4*4,false);cells[(1*4+2)*4+1]=true;
        auto volume=VoxelTerrain::fromOccupancy({-.02,base,-.02},cell,4,4,4,cells,false);
        auto definition=body("thin",{1,1,1},{0,0,0},"static");definition["source"]={{"type","volume"},{"volume",volume->encodeVolume()}};
        auto scene=VoxelScene::load({{"format","voxel-scene-v1"},{"objects",{definition}}});
        const vsg::dvec3 origin{-.02+1.5*cell,50,-.02+1.5*cell};
        auto hit=scene->raycast(origin,{0,-1,0},100);require(hit&&std::abs(hit->distance-(50-base-3*cell))<1e-8,"fractional-cell DDA skipped an empty boundary layer");
        rays(*scene,origin,{0,-1,0});
    }
    std::cout<<"PASS: fractional-cell boundary traversal and exact Bullet cuboid rays\n";
}
void reconstructedLanding() {
    const int n=16;const double cell=.25;
    auto volume=VoxelTerrain::fromOccupancy({0,0,0},cell,n,1,n,std::vector<bool>(n*n,true),false);
    auto appearance=std::make_shared<VoxelSurfaceAppearance>();appearance->origin={0,0,0};appearance->dimensions={n,1,n};appearance->cellSize=cell;appearance->width=appearance->height=10;appearance->rgba.assign(400,255);
    for(int z=0;z<=n;++z)for(int y=0;y<=1;++y)for(int x=0;x<=n;++x) {
        const vsg::dvec3 point(x*cell,y*cell+.002*std::sin(x*.5)*std::cos(z*.5),z*cell);
        appearance->corners[appearance->cornerKey(x,y,z)]={point,{0,y?1.f:-1.f,0}};
    }
    volume->setAppearance(appearance);auto target=body("skin",{1,1,1},{0,2,0},"dynamic");target["source"]={{"type","volume"},{"volume",volume->encodeVolume()}};
    auto scene=VoxelScene::load({{"format","voxel-scene-v1"},{"objects",{body("floor",{20,1,20},{-8,-1,-8},"static"),target}}});
    scene->setPaused(false);scene->advanceTo(2,128);const auto landed=scene->snapshot();
    require(!landed->contacts.empty()&&landed->contacts.size()<=32,"dense skin support contacts were not bounded");
    require(vsg::length(landed->objects.at("skin").velocity)<.15,"reconstructed skin did not settle stably");
    auto resumed=VoxelScene::load(scene->checkpoint());scene->advanceTo(2.04,4);resumed->setPaused(false);resumed->advanceTo(2.04,4);
    require(scene->checkpoint()==resumed->checkpoint(),"reconstructed skin continuation lost deterministic contact state");
    std::cout<<"PASS: curved skin landing, bounded support contacts and deterministic continuation\n";
}
void damageBlastImpulse() {
    auto definition=columnScene();
    definition["events"][0]["blast_impulse_ns"]=2400.;
    auto scene=VoxelScene::load(definition);
    scene->setPaused(false);scene->advanceTo(.02);
    const auto state=scene->snapshot();
    const auto id=state->journal[0]["fragments"][0].get<std::string>();
    const auto& piece=state->objects.at(id);
    require(std::abs(vsg::length(piece.velocity)*piece.massKg()-2400.)<.01,
            "damage blast failed to apply the declared impulse to detached material");
    require(vsg::length(state->objects.at("column").velocity)==0.,"blast moved an anchored target");
    auto resumed=VoxelScene::load(scene->checkpoint());
    require(resumed->checkpoint()==scene->checkpoint(),"blast state did not survive checkpoint restore");
    definition["events"][0]["blast_impulse_ns"]=-1.;
    rejects([&]{VoxelScene::load(definition);},"negative damage blast impulse was accepted");
    std::cout<<"PASS: damage blast impulse, anchored support and restored fragment state\n";
}
void reconstructedMassCentre() {
    const int nx=16,ny=2,nz=8;const double cell=.125;const vsg::dvec3 origin(17.3,-2.7,11.1);
    auto volume=VoxelTerrain::fromOccupancy(origin,cell,nx,ny,nz,std::vector<bool>(nx*ny*nz,true),false);
    auto appearance=std::make_shared<VoxelSurfaceAppearance>();appearance->origin=origin;appearance->dimensions={nx,ny,nz};appearance->cellSize=cell;appearance->width=appearance->height=10;appearance->rgba.assign(400,255);
    for(int z=0;z<=nz;++z)for(int y=0;y<=ny;++y)for(int x=0;x<=nx;++x)
        appearance->corners[appearance->cornerKey(x,y,z)]={origin+vsg::dvec3(x*cell,y*cell+.003*std::sin(x*.5)*std::cos(z*.5),z*cell),{0,1,0}};
    volume->setAppearance(appearance);auto target=body("skin",{1,1,1},{3,11,-7},"dynamic");target["rotation_deg"]={10,37,5};target["source"]={{"type","volume"},{"volume",volume->encodeVolume()}};
    auto scene=VoxelScene::load({{"format","voxel-scene-v1"},{"gravity_mps2",{0,0,0}},{"objects",{target}}});
    const auto initial=scene->snapshot();const auto& o=initial->objects.at("skin");const auto centre=o.pose(0,.02)*o.localCentreOfMass();const vsg::dvec3 delta(3,-2,1),impulse=delta*o.massKg();
    scene->schedule({{"kind","impulse"},{"object","skin"},{"tick",1},{"space","scene"},{"point_m",{centre.x,centre.y,centre.z}},{"impulse_ns",{impulse.x,impulse.y,impulse.z}}});
    scene->setPaused(false);scene->advanceTo(.02);const auto& kicked=scene->snapshot()->objects.at("skin");
    require(vsg::length(kicked.angularVelocity)<.001,"a centre impulse spun the reconstructed hull: compound COM differs from occupied mass");
    require(vsg::length(kicked.velocity-delta)<.002,"reconstructed centre impulse has wrong translational momentum");
    auto restored=VoxelScene::load(scene->checkpoint());restored->setPaused(false);scene->advanceTo(.22,16);restored->advanceTo(.22,16);
    require(scene->checkpoint()==restored->checkpoint(),"reconstructed COM fix depends on unsaved Bullet caches");
    const auto& moved=scene->snapshot()->objects.at("skin");const auto actual=moved.pose(scene->snapshot()->tick,.02)*moved.localCentreOfMass();
    require(vsg::length(actual-(centre+delta*.2))<.002,"free reconstructed body drifted away from its mass-centre trajectory");
    const auto before=momentum(*scene->snapshot());const auto point=actual+vsg::dvec3(0,.7,.5);const vsg::dvec3 kick(20,-10,30);
    scene->schedule({{"kind","impulse"},{"object","skin"},{"tick",12},{"space","scene"},{"point_m",{point.x,point.y,point.z}},{"impulse_ns",{kick.x,kick.y,kick.z}}});
    scene->advanceTo(.24,1);const auto after=momentum(*scene->snapshot());
    require(vsg::length(after.first-before.first-kick)<.02,"reconstructed off-centre impulse changed linear momentum incorrectly");
    require(vsg::length(after.second-before.second-vsg::cross(point,kick))<.15,"contact-skin bounds replaced the intrinsic inertia of occupied voxel mass");
    std::cout<<"PASS: rotated/translated reconstructed mass centre, torque-free centre impulse and deterministic free flight\n";
}
void impactCrush() {
    auto target=body("cab",{12,6,12},{-1.5,1.2,-1.5},"dynamic");
    target["source"]["cell_size_m"]=.25;
    target["velocity_mps"]={0,-8,0};
    target["physics"]["crush"]={{"radius_m",4.},{"yield_speed_mps",3.},{"max_strain",.75}};
    target["sensor_materials"]={{"palette",json::array({json::object(),{{"temperature_k",390.}}})},
        {"regions",json::array({{{"min_m",{0.,0.,0.}},{"max_m",{3.,1.5,1.5}},{"material",1}}})}};
    const json definition={{"format","voxel-scene-v1"},{"objects",json::array({
        body("floor",{16,1,16},{-8,-1,-8},"static"),target})}};
    auto scene=VoxelScene::load(definition);
    const auto before=scene->snapshot();
    scene->setPaused(false);
    unsigned crushedBodies=0;
    for(int k=1;k<=40&&!scene->snapshot()->objects.at("cab").crushedAtTick;++k)
        crushedBodies+=scene->advanceTo(k*.02,1).crushedBodies;
    const auto after=scene->snapshot();
    const auto& intact=before->objects.at("cab");
    const auto& wreck=after->objects.at("cab");
    const auto countWarm=[](const auto& o){
        const auto n=o.volume->dimensions();
        size_t warm=0;
        for(size_t i=0;i<o.volume->cellCount();++i)if(o.volume->occupancy()[i]){
            const auto p=o.volume->gridOrigin()+vsg::dvec3(i%n[0]+.5,i/n[0]%n[1]+.5,i/(size_t(n[0])*n[1])+.5)*o.volume->resolution_m;
            if(o.sensorMaterials->index(p)==1)++warm;
        }
        return warm;
    };
    require(crushedBodies==1&&wreck.crushedAtTick==after->tick&&after->objects.size()==2&&
        wreck.volume!=intact.volume&&wreck.massKg()==intact.massKg(),"hard contact did not conserve and deform the intact body");
    require(wreck.volume->centreOfMass().y<intact.volume->centreOfMass().y-.04,
        "the occupied mass did not compact toward the contact");
    require(countWarm(wreck)==countWarm(intact),"impact lost part of the sensor material partition");
    const auto display=makeVoxelSceneDisplay(*after);
    const auto sensors=makeVoxelSceneSensorData(after);
    require(display.placements.at("cab")->matrix==wreck.pose(after->tick,.02)&&
        sensors.snapshot->contentRevision==after->contentRevision,"deformed surface was not published to both consumers");
    for(int x=-4;x<=4;++x)for(int z=-4;z<=4;++z)
        rays(*scene,{x*.31+.017,5.,z*.31+.063},{0,-1,0});
    auto restored=VoxelScene::load(scene->checkpoint());
    restored->advanceTo(.9,80);scene->advanceTo(.9,80);
    require(restored->checkpoint()==scene->checkpoint(),"impact deformation failed checkpoint continuation");
    auto gentle=definition;gentle["objects"][1]["position_m"]={-1.5,.02,-1.5};
    gentle["objects"][1]["velocity_mps"]={0,-.25,0};
    auto light=VoxelScene::load(gentle);light->setPaused(false);light->advanceTo(.75,80);
    require(!light->snapshot()->objects.at("cab").crushedAtTick,"ordinary gentle contact caused permanent deformation");
    std::cout<<"PASS: impact yield, flattened occupancy, mass and sensor palette conservation, exact rays and replay\n";
}
}
int main()try{damageDetachAndLand();bondsAndImpulse();concaveAndFastContact();rollbackAndHole();damageBlastImpulse();fractionalCellRays();reconstructedMassCentre();reconstructedLanding();impactCrush();return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
