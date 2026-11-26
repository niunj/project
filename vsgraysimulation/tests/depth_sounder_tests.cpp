#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include <vsgraysim/broker/SimDataBroker.h>
#include <vsgraysim/earth/ScenePresentation.h>
#include <vsgraysim/model_packages/platform/alvin/Alvin.h>
#include <iostream>
#include <stdexcept>

using namespace vsgraysim;
using json=nlohmann::json;

namespace {
void require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}

json box(std::string id,std::string role,double y) {
    return {{"id",id},{"role",role},{"physics",{{"motion","static"}}},
        {"source",{{"type","box"},{"dimensions",{20,1,20}},{"origin_m",{-10,y,-10}},{"cell_size_m",1.}}}};
}
}

int main()try {
    const auto project=VoxelSceneProject::open("config/topics/taipei-underwater-survey.vtopic.json");
    const auto config=project.equipmentConfig();
    require(config.equipment.empty()&&config.sounders.size()==1,"sonar-only model configuration was not preserved");
    const auto& sounder=config.sounders.front();
    auto scene=earth::VoxelScene::load({{"format","voxel-scene-v1"},{"geo_anchor",{{"lat",25.22},{"lon",121.8}}},
        {"ocean",{{"wave_height_m",0},{"swell_height_m",0},{"bottom_m",-40}}},
        {"objects",json::array({box("assumed-bed","terrain",-16),box("alvin","target",-5),box("survey-vessel","target",1)})}});
    auto state=scene->snapshot();
    const auto origin=sounder.origin(*state);
    require(origin.has_value(),"ship mount did not resolve");
    const auto reading=sounder.sample(*state,*origin);
    require(reading.valid()&&reading.bottom==BottomReturn::Valid&&std::abs(reading.water_depth_m-15.)<1e-7,
        "expected designated seabed at 15 m despite a voxel object in the beam");
    require(std::abs(reading.clearance_m-13.7)<1e-7&&std::abs(reading.two_way_ms-2.*13.7/1.5)<1e-7,
        "clearance and water depth use different origins");
    auto reduced=sounder;reduced.rangeMetres=2.;
    require(reduced.sample(*state,*origin).bottom==BottomReturn::NoBottom,"short range reported a return");
    require(sounder.sample(*state,{0,5,0}).bottom==BottomReturn::DryTransducer,"dry transducer reported a return");
    auto bad=reading;bad.two_way_ms=0.;require(!bad.valid(),"bad frame timing accepted");
    auto nearEcho=sounder;nearEcho.waterColumnReturns=true;
    auto hit=nearEcho.sample(*state,*origin);
    require(hit.valid()&&hit.bottom==BottomReturn::NoBottom&&std::abs(hit.echo_range_m-2.7)<1e-7&&
        std::abs(hit.echoDepthM()-4.)<1e-7&&std::abs(hit.echoTwoWayMs()-3.6)<1e-7,
        "water-column return and transducer-relative geometry disagree");
    const auto clear=nearEcho.sample(*state,{11.,origin->y,origin->z});
    require(clear.valid()&&clear.echo_range_m==0.,"off-beam object generated an echo");
    const auto air=nearEcho.sample(*state,{origin->x,5.,origin->z});
    require(air.valid()&&air.bottom==BottomReturn::DryTransducer&&air.echo_range_m==0.,
        "dry transducer generated a water-column echo");
    hit.water_column_enabled=false;
    require(!hit.valid(),"legacy sonar frame admitted a water-column echo");
    using model_packages::Alvin;
    require(Alvin::state(0.,0.,0.)=="surfaced"&&Alvin::state(6.,-.3,.2)=="diving"&&
        Alvin::state(6.,0.,0.)=="hovering"&&Alvin::state(6.,0.,.3)=="submerged_cruise"&&
        Alvin::state(6.,.4,0.)=="ascending","submersible state contract");

    auto& registry=EquipmentRegistry::instance();
    auto& broker=SimDataBroker::instance();
    registry.registerEquipment(sounder.equipment);
    broker.serverOnline(true);
    require(broker.subscribe(reading.sensor_id)&&broker.publishSonar(reading)&&!broker.publishSonar(reading),
        "valid first frame or duplicate rejection failed");
    registry.setActive(reading.sensor_id,true);
    auto presentation=earth::scenePresentation(config,*state,earth::voxelEquipmentStateTelemetry(config,*state));
    require(presentation.at("equipment").at(0).at("state")=="working"&&
        presentation.at("events").at(0).at("cue")=="depth_ping","measurement state and ping are not linked");
    registry.setPowered(reading.sensor_id,false);
    auto next=reading;++next.frame_id;
    require(!broker.publishSonar(next),"unpowered sonar accepted data");
    broker.serverOnline(false);
    broker.resetEquipmentChannel(reading.sensor_id);
    registry.unregisterEquipment(reading.sensor_id);
    std::cout<<"PASS: submarine states, physical mount, seabed-only frame, dry/no-bottom, power and audio\n";
    return 0;
}catch(const std::exception& error){
    std::cerr<<"FAIL: "<<error.what()<<'\n';
    return 1;
}
