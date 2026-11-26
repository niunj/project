#pragma once

#include <vsgraysim/model_sdk/SceneModel.h>

namespace vsgraysim::model_packages {

struct NavigationSample {
    model_sdk::SceneTime time;
    double latitudeDegrees, longitudeDegrees, ellipsoidHeightMetres;
    model_sdk::Vec3 velocitySceneMetresPerSecond;
};

class NavigationTruthEquipment final : public model_sdk::IEquipmentRuntime {
public:
    model_sdk::EquipmentTiming timing(double sceneStepSeconds, double) const override {
        return {sceneStepSeconds,0.};
    }

    NavigationSample sample(const model_sdk::SceneTime& time,
        const model_sdk::Vec3& geodetic, const model_sdk::Vec3& velocity) const {
        return {time,geodetic[0],geodetic[1],geodetic[2],velocity};
    }
};

inline const NavigationTruthEquipment& navigationTruthEquipment() {
    static const NavigationTruthEquipment equipment;
    return equipment;
}

} // namespace vsgraysim::model_packages
