#pragma once

#include <vsgraysim/model_sdk/SceneModel.h>

namespace vsgraysim::model_packages {

struct SoundProfile {
    std::string_view cue;
    double referenceDistance;
    double gain;
    double pitch=1.;
};

inline SoundProfile platformSound(const model_sdk::PlatformDescriptor& model) {
    using F=model_sdk::PlatformFamily;
    if(model.id=="skywalker-x8"||model.id=="small-fixed-wing-uav")return {"propeller",14.,.45,1.12};
    if(model.id=="3dr-iris"||model.id=="multirotor-uav")return {"small_rotor",10.,.50,1.};
    if(model.id=="a320")return {"jet",85.,.58,.78};
    if(model.id=="mq9")return {"propeller",28.,.52,.87};
    switch(model.family){
        case F::FixedWing:return {"jet",72.,.65,1.};
        case F::Rotorcraft:return {"helicopter",42.,.58,.92};
        case F::Ground:return {"land_engine",22.,.51,model.id=="m1a2"?.80:1.};
        case F::Surface:return {"ship_engine",36.,.49,.85};
        case F::Submersible:return {"submersible",13.,.31,.93};
        default:return {};
    }
}

inline SoundProfile equipmentSound(const model_sdk::EquipmentDescriptor& model) {
    using F=model_sdk::EquipmentFamily;
    switch(model.family){
        case F::Radar:return {"equipment_fan",5.,.24,model.id=="maritime-radar-2"?.80:1.};
        case F::Sar:return {"equipment_fan",5.,.22,.91};
        case F::Infrared:return {"equipment_fan",3.,.14,model.band=="LWIR"?.88:1.2};
        case F::Optical:return {"optical",3.,.12,model.id=="optical-camera-2"?1.14:1.};
        case F::Navigation:return {"navigation",2.,.10,1.};
        case F::Sonar:return {"depth_ping",14.,.28,1.};
    }
    return {};
}

}
