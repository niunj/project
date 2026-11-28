#include <VoxelDeploymentAdapter.h>
#include "../src/earth/VoxelSensorMaterials.h"
#include "../src/earth/VoxelTerrain.h"
#include "voxel_earth_fixture.h"
#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include <vsgraysim/earth/VoxelScenePackage.h>
#include <vsgraysim/earth/VoxelSceneSensors.h>
#include <vsgraysim/broker/SimDataBroker.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace fs=std::filesystem;
using namespace vsgraysim;
using namespace vsgraysim::earth;
using deployment::Adapter;
using deployment::Json;

void require(bool ok,const char* what) { if(!ok) throw std::runtime_error(what); }
template<class F> void rejects(F operation) {
    try { operation(); } catch(const std::exception&) { return; }
    throw std::runtime_error("accepted invalid deployment");
}

int main(int argc,char** argv) try {
    // By default tests are independent of local DEM availability. The acceptance
    // mode takes the deployed Taipei topic and optionally keeps the published package.
    const bool real=argc>=2;
    std::string directory=(fs::temp_directory_path()/"vsgraysim-deployment-XXXXXX").string();
    require(mkdtemp(directory.data())!=nullptr,"cannot create temporary directory");
    struct Cleanup { std::string path; ~Cleanup(){std::error_code e;fs::remove_all(path,e);} } cleanup{directory};
    const fs::path temporary=directory;
    auto topicPath=(temporary/"survey.vtopic.json").string();
    if(real) topicPath=fs::absolute(argv[1]).string();
    else {
        auto fixture=earth_fixture::create(temporary);
        fixture["objects"][0]["source"]["earth"]["strict"]=false;
        earth_fixture::write(temporary/"earth.vscene.json",fixture);
        earth_fixture::write(topicPath,{{"format","earth-scene-topic-v1"},{"name","Deployment test"},
            {"scene","earth.vscene.json"},{"earth_map","earth.map.json"},{"paused",true}});
    }
    auto project=VoxelSceneProject::open(topicPath);
    auto runtime=project.createScene();
    runtime->setPaused(true);
    const auto before=runtime->snapshot();
    auto topic=project.document;
    Adapter adapter("config/deployment/catalog.json");
    auto plan=Json::parse(std::ifstream("config/deployment/taipei-survey.json"));
    auto& actions=plan.at("actions");
    const auto terrain=real?"taipei-terrain":"earth";
    const auto ground=raycastVoxelScene(*before,{0,35000,0},{0,-1,0},70000,true,terrain);
    require(bool(ground),"expected terrain at test site");
    const auto& groundObject=before->objects.at(terrain);
    const auto resolution=groundObject.volume->resolution_m;
    const auto& grid=groundObject.volume;
    const auto origin=grid->gridOrigin();
    const double top=groundObject.localBounds().max.y+resolution;
    const Json center={{"relative_m",{0,.5*(top+origin.y)-ground->position.y,0}}};
    const Json region={{"center",center},{"size_m",{2.8*resolution,top-origin.y,2.8*resolution}}};
    actions[0]["position"]={{"relative_m",{0,0,0}},{"agl_m",250}};
    actions[1]["points"][0]=actions[0].at("position");
    if(!real) {
        actions[1]["points"][1]={{"local_m",{1,0,1}},{"agl_m",255}};
        actions[1]["points"][2]={{"wgs84",{0,0,365}}};
    } else {
        actions[1]["points"][1]={{"local_m",{55,0,75}},{"agl_m",255}};
        actions[1]["points"][2]={{"local_m",{120,0,125}},{"agl_m",260}};
    }
    actions.push_back({{"op","material"},{"object",terrain},{"region",region},
        {"parameters",{{"rho_tv",.11},{"eps_lwir",.96},{"radar",{{"rcs_scale",.7}}}}}});
    const auto context=adapter.context(*before,terrain,ground->position);
    require(context.at("selected_object")==terrain&&context.at("models").contains("skywalker-x8"),
        "model context omitted the selected terrain or asset");
    const auto request=scene_deployment::modelRequest("vsim-local","部署测绘无人机",context);
    const auto& messages=request.at("messages");
    require(messages.size()==2&&messages.at(1).at("content").get<std::string>().find("部署测绘无人机")!=std::string::npos,
        "model context lost natural-language input");
    require(scene_deployment::parse(plan.dump())==plan,"portable plan roundtrip changed values");
    const auto completion=Json{{"choices",Json::array({{{"finish_reason","stop"},{"message",{{"content",plan.dump()}}}}})}};
    require(scene_deployment::modelPlan(completion.dump())==plan,"model envelope changed plan");
    auto invalid=plan;
    invalid["actions"][0]["path"]="/tmp/untrusted.obj";
    rejects([&]{scene_deployment::validate(invalid);});
    rejects([&]{adapter.stage(plan,before,topic,topicPath);});  // no point selected
    auto edit=adapter.stage(plan,before,topic,topicPath,ground->position);
    require(runtime->snapshot()==before&&topic==project.document,"preflight edited scene");
    auto preparation=VoxelScene::prepareImportAsync(edit.imports);
    const auto prepared=preparation.snapshot.get();
    auto outside=edit;
    outside.commands.back()["region"]={{"min_m",{200000,0,200000}},{"max_m",{300000,20,300000}}};
    rejects([&]{Adapter::commit(*runtime,topic,outside,prepared);});
    require(runtime->snapshot()==before&&topic==project.document,"failed paint partly imported target/equipment");
    runtime->addPublicationParticipant("validation",[](VoxelSceneSnapshotPtr){throw std::runtime_error("reject publication");});
    rejects([&]{Adapter::commit(*runtime,topic,edit,prepared);});
    runtime->removePublicationParticipant("validation");
    require(runtime->snapshot()==before&&topic==project.document,"failed publication partly applied transaction");

    const auto undo=Adapter::commit(*runtime,topic,edit,prepared);
    const auto deployed=runtime->snapshot();
    const auto& object=deployed->objects.at("survey-x8");
    require(deployed->revision==before->revision+1&&object.poseTrack&&object.poseTrack->size()==3,
        "model and route did not publish in one revision");
    require(std::abs(object.poseTrack->front().position.y-ground->position.y-250.)<1e-6,"AGL used a different terrain surface");
    require(object.material.at("radar").at("rcs_scale")==1.1&&object.material.at("eps_lwir")==.88,
        "whole target material dropped radar or IR attributes");
    const auto field=deployed->objects.at(terrain).sensorMaterials;
    const auto patched=field->at({0,origin.y+.5*resolution,0});
    const auto unpatched=field->at({2.5*resolution,origin.y+.5*resolution,0});
    require(patched.at("radar").at("rcs_scale")==.7&&patched.at("rho_tv")==.11&&
        !unpatched.contains("radar")&&std::abs(patched.at("radar").at("dielectric_imag").get<double>()-.1)<1e-6,
        "terrain patch leaked outside its region or replaced the inherited radar properties");
    require(deployed->weather.enabled,"weather was not applied");
    const auto sensorScene=makeVoxelSceneSensorData(deployed);
    require(sensorScene.snapshot==deployed&&sensorScene.instances.size()==deployed->objects.size(),"measurement scene missed a deployed object");
    const auto& intended=topic.at("equipment").at("equipment");
    const auto destination=argc>=3?fs::absolute(argv[2]):temporary/"survey.vsimscene";
    const Json checkpoint=runtime->checkpoint();
    auto publishedTopic=topic;
    if(real){
        const auto p=object.poseTrack->front().position;
        publishedTopic["earth_map"]=fs::absolute("config/earth/taipei-region.map.json").string();
        publishedTopic["camera"]={{"eye_m",{p.x+2.3,p.y+1.25,p.z+4.1}},{"target_m",{p.x,p.y,p.z}}};
    }
    VoxelScenePackage::save(destination.string(),publishedTopic,checkpoint);
    auto reopened=VoxelSceneProject::open(destination.string());
    auto restored=reopened.createScene();
    auto config=reopened.equipmentConfig();
    require(restored->checkpoint()==checkpoint,"published package lost authored terrain/model/route");
    size_t mounted=0;
    for(const auto& device:config.equipment)
        if(device.mountObject=="survey-x8"&&device.mountId=="deployment-sensor") ++mounted;
    require(mounted==3&&config.equipment.size()==intended.size(),"published equipment mount was not restored");

    if(argc>=4&&std::string(argv[3])=="--gpu"){
        // Keep only the new devices active for this acquisition pass. The
        // package itself retains all devices and their original settings.
        config.equipment.erase(std::remove_if(config.equipment.begin(),config.equipment.end(),
            [](const auto& device){return device.mountObject!="survey-x8";}),config.equipment.end());
        VoxelEquipmentRuntime measured(*restored,config);
        auto& broker=SimDataBroker::instance();
        for(const auto& device:config.equipment) require(broker.subscribe(device.sensor.id),"subscribe failed");
        restored->setPaused(false);
        for(int i=1;i<=12;++i){
            restored->advanceTo(i*.02,1);
            measured.advanceMeasurements();
        }
        for(const auto& device:config.equipment){
            const auto id=device.sensor.id;
            if(device.equipment.type==SensorType::VIS){VisFrame frame;require(broker.latestVis(id,&frame),"optical not published");}
            else if(device.equipment.type==SensorType::IR){IrFrame frame;require(broker.latestIr(id,&frame),"IR not published");}
            else {RadarFrame frame;require(broker.latestRadar(id,&frame)&&frame.stage==0,"raw radar IQ not published");}
            broker.unsubscribe(id);
        }
        std::cout<<"Vulkan: optical, LWIR and radar IQ published\n";
    }
    Adapter::undo(*runtime,topic,undo);
    require(topic==project.document&&runtime->snapshot()->revision==deployed->revision+1&&
        runtime->snapshot()->objects.size()==before->objects.size()&&
        runtime->snapshot()->objects.at(terrain).sensorMaterials==before->objects.at(terrain).sensorMaterials,
        "undo did not restore scene and equipment together");
    rejects([&]{Adapter::commit(*runtime,topic,edit,prepared);});
    std::cout<<"PASS: plan, AGL, model import, transactional route, materials, equipment, package reopen and undo\n";
    return 0;
} catch(const std::exception& e) {
    std::cerr<<"FAIL: "<<e.what()<<'\n';
    return 1;
}
