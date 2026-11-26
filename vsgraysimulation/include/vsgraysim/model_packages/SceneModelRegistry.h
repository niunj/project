#pragma once

#include <vsgraysim/model_sdk/SceneModel.h>
#include "equipment/navigation/NavigationTruth.h"
#include "platform/alvin/Alvin.h"
#include <cmath>

namespace vsgraysim::model_packages {

inline constexpr model_sdk::PlatformDescriptor platforms[] = {
    {"rigid-platform",model_sdk::PlatformFamily::Rigid,{}},
    {"ground-vehicle",model_sdk::PlatformFamily::Ground,{}},
    {"mercedes-amg-g63",model_sdk::PlatformFamily::Ground,{}},
    {"m1a2",model_sdk::PlatformFamily::Ground,{}},
    {"fixed-wing",model_sdk::PlatformFamily::FixedWing,"aircraft"},
    {"f16c",model_sdk::PlatformFamily::FixedWing,"aircraft"},
    {"a320",model_sdk::PlatformFamily::FixedWing,"airliner"},
    {"mq9",model_sdk::PlatformFamily::FixedWing,"uav"},
    {"small-fixed-wing-uav",model_sdk::PlatformFamily::FixedWing,"uav"},
    {"skywalker-x8",model_sdk::PlatformFamily::FixedWing,"uav"},
    {"rotorcraft",model_sdk::PlatformFamily::Rotorcraft,"helicopter"},
    {"ah64d",model_sdk::PlatformFamily::Rotorcraft,"helicopter"},
    {"multirotor-uav",model_sdk::PlatformFamily::Rotorcraft,"uav"},
    {"3dr-iris",model_sdk::PlatformFamily::Rotorcraft,"uav"},
    {"surface-vessel",model_sdk::PlatformFamily::Surface,{}},
    {"cg57",model_sdk::PlatformFamily::Surface,{}},
    {Alvin::id,model_sdk::PlatformFamily::Submersible,{}},
    {"cc0-diesel-submarine",model_sdk::PlatformFamily::Submersible,{}},
    {"recorded-playback",model_sdk::PlatformFamily::Playback,{}},
    {"rocket-playback",model_sdk::PlatformFamily::Playback,{}},
    {"falcon9-block5-stage1",model_sdk::PlatformFamily::Playback,{}},
    {"falcon9-block5-stage2",model_sdk::PlatformFamily::Playback,{}},
    {"swot",model_sdk::PlatformFamily::Playback,{}}
};

inline constexpr model_sdk::EquipmentDescriptor equipmentModels[] = {
    {"radar",model_sdk::EquipmentFamily::Radar,"radar"},
    {"maritime-radar-1",model_sdk::EquipmentFamily::Radar,"radar"},
    {"maritime-radar-2",model_sdk::EquipmentFamily::Radar,"radar"},
    {"sar",model_sdk::EquipmentFamily::Sar,"sar"},
    {"optical-camera",model_sdk::EquipmentFamily::Optical,"optical"},
    {"optical-camera-1",model_sdk::EquipmentFamily::Optical,"optical"},
    {"optical-camera-2",model_sdk::EquipmentFamily::Optical,"optical"},
    {"infrared-swir",model_sdk::EquipmentFamily::Infrared,"infrared","SWIR"},
    {"infrared-mwir",model_sdk::EquipmentFamily::Infrared,"infrared","MWIR"},
    {"infrared-lwir",model_sdk::EquipmentFamily::Infrared,"infrared","LWIR"},
    {"navigation-truth",model_sdk::EquipmentFamily::Navigation,{}},
    {"single-beam-depth-sounder",model_sdk::EquipmentFamily::Sonar,"sonar"}
};

inline const model_sdk::PlatformDescriptor* platformModel(std::string_view id) {
    for (const auto& entry : platforms)
        if (entry.id == id) return &entry;
    return nullptr;
}

inline const model_sdk::EquipmentDescriptor* equipmentModel(std::string_view id) {
    for (const auto& entry : equipmentModels)
        if (entry.id == id) return &entry;
    return nullptr;
}

inline std::string_view platformModelForKind(std::string_view kind) {
    if (platformModel(kind)) return kind;
    if (kind == "ship" || kind == "naval_vessel") return "surface-vessel";
    if (kind == "helicopter") return "rotorcraft";
    if (kind == "aircraft" || kind == "fighter") return "fixed-wing";
    if (kind == "airliner") return "a320";
    if (kind == "uav") return "mq9";
    if (kind == "tank") return "ground-vehicle";
    if (kind == "missile") return "recorded-playback";
    if (kind == "rocket") return "rocket-playback";
    return "rigid-platform";
}

class RigidPlatformRuntime final : public model_sdk::IPlatformRuntime {
public:
    model_sdk::PlatformSample sample(const model_sdk::PlatformInput&) const override {return {};}
};

class SurfacePlatformRuntime final : public model_sdk::IPlatformRuntime {
public:
    model_sdk::PlatformSample sample(const model_sdk::PlatformInput& in) const override {
        return {model_sdk::SurfaceMotion{std::hypot(in.velocity[0],in.velocity[2]),
            in.position[1],in.pitchDegrees,in.rollDegrees},{}};
    }
};

class AirPlatformRuntime final : public model_sdk::IPlatformRuntime {
public:
    model_sdk::PlatformSample sample(const model_sdk::PlatformInput& input) const override {
        const double horizontal=std::hypot(input.velocity[0],input.velocity[2]);
        const double vertical=input.velocity[1];
        return {{},model_sdk::AirMotion{horizontal,vertical,horizontal<0.15&&std::abs(vertical)<0.15}};
    }
};

inline const model_sdk::IPlatformRuntime& platformRuntime(model_sdk::PlatformFamily family) {
    static const RigidPlatformRuntime rigid;
    static const SurfacePlatformRuntime surface;
    static const AirPlatformRuntime air;
    if (family == model_sdk::PlatformFamily::Surface) return surface;
    if (family == model_sdk::PlatformFamily::FixedWing || family == model_sdk::PlatformFamily::Rotorcraft) return air;
    return rigid;
}

class ImagingEquipmentRuntime final : public model_sdk::IEquipmentRuntime {
public:
    model_sdk::EquipmentTiming timing(double period, double exposure) const override {
        return {period,exposure};
    }
};

class WindowedEquipmentRuntime final : public model_sdk::IEquipmentRuntime {
public:
    model_sdk::EquipmentTiming timing(double period, double) const override {
        return {period,period};
    }
};

inline const model_sdk::IEquipmentRuntime& equipmentRuntime(model_sdk::EquipmentFamily family) {
    static const ImagingEquipmentRuntime imaging;
    static const WindowedEquipmentRuntime windowed;
    if (family == model_sdk::EquipmentFamily::Navigation) return navigationTruthEquipment();
    if (family == model_sdk::EquipmentFamily::Radar || family == model_sdk::EquipmentFamily::Sar)
        return windowed;
    return imaging;
}

} // namespace vsgraysim::model_packages
