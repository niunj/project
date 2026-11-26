#pragma once

#include <vsgraysim/model_sdk/SoundMount.h>

namespace vsgraysim::model_packages {

inline model_sdk::SoundMount equipmentFanMount(const model_sdk::EquipmentDescriptor& model) {
    using model_sdk::EquipmentFamily;
    switch(model.family){
        case EquipmentFamily::Optical:return {"fan",{.08,.025,-.16}};
        case EquipmentFamily::Infrared:return {"fan",{.12,.04,-.21}};
        case EquipmentFamily::Radar:
        case EquipmentFamily::Sar:return {"fan",{.22,-.18,-.30}};
        default:return {};
    }
}

}
