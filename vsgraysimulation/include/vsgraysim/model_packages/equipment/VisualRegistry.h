#pragma once

#include <utility>

#include "maritime_radar/Visual.h"
#include "optical_camera/Visual.h"

namespace vsgraysim::model_packages {

inline const model_sdk::EquipmentVisual* equipmentVisual(std::string_view id) {
    static const MaritimeRadarVisual radar1({43, 228, 155}, 12.);
    static const MaritimeRadarVisual radar2({255, 187, 77}, 20.);
    static const OpticalCameraVisual camera1({73, 175, 255});
    static const OpticalCameraVisual camera2({225, 138, 255});
    static const OpticalCameraVisual swir({183, 228, 116});
    static const OpticalCameraVisual mwir({255, 125, 96});
    static const OpticalCameraVisual lwir({245, 92, 181});
    const std::pair<std::string_view,const model_sdk::EquipmentVisual*> models[]{
        {"maritime-radar-1",&radar1}, {"maritime-radar-2",&radar2},
        {"optical-camera-1",&camera1}, {"optical-camera-2",&camera2},
        {"infrared-swir",&swir}, {"infrared-mwir",&mwir}, {"infrared-lwir",&lwir}
    };
    for(const auto& [key,model]:models)
        if(key==id)return model;
    return nullptr;
}

} // namespace vsgraysim::model_packages
