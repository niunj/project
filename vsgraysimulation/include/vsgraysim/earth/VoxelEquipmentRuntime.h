#pragma once
#include <vsgraysim/earth/VoxelSensorSession.h>
#include <vsgraysim/broker/EquipmentInfo.h>
#include <vsgraysim/model_sdk/SceneModel.h>
#include <vsgraysim/model_packages/equipment/sonar/DepthSounder.h>
#include <map>
#include <optional>
namespace vsgraysim::earth {
// A configured physical sensor, separate from vehicle/console appearance.
struct VoxelEquipmentSpec {
    EquipmentInfo equipment;
    rockyenv::sensor::GpuSensorSpec sensor;
    rockyenv::sensor::GpuAperture aperture;
    struct Visual {
        std::string model;
        double rangeMetres = 100.;
        double previewPeriodSeconds = 0.;
        double phaseDegrees = 0.;
        std::optional<double> horizontalDegrees, verticalDegrees;
    };
    std::optional<Visual> visual;
    std::string mountObject; // optional object-local rigid mount, follows fragments only by explicit id
    std::string mountId;     // optional named platform-local frame
};
struct VoxelEquipmentPlatform {
    uint32_t id=0;std::string object,name,kind;double modelHeadingDeg=0;
    std::string displayRig;
    std::string modelId;
    std::map<std::string,model_sdk::RigidMount> mounts;
};
struct VoxelNavigationSpec {
    uint32_t id, platformId;
    std::string name;
};
struct VoxelEquipmentConfig {
    std::string topicPath;
    std::vector<VoxelEquipmentSpec> equipment;
    std::vector<VoxelEquipmentPlatform> platforms;
    double visibilityM=30000;
    std::vector<model_sdk::StateConnection> connections;
    bool visualOnly=false;
    std::vector<VoxelNavigationSpec> navigation;
    std::vector<model_packages::DepthSounder> sounders;
    static VoxelEquipmentConfig open(const std::string& path);
    static VoxelEquipmentConfig parse(const nlohmann::json&,const std::string& path={});
};
// Owner-thread adapter. The viewport and all channels consume one VoxelScene.
// Actual Vulkan radiometry/IQ products are published through the existing broker.
class VoxelEquipmentRuntime {
public:
    VoxelEquipmentRuntime(VoxelScene&,VoxelEquipmentConfig,vsg::Device* sharedDevice=nullptr,bool startOnline=true);
    VoxelEquipmentRuntime(VoxelSceneReader,VoxelEquipmentConfig,vsg::Device* sharedDevice=nullptr,bool startOnline=true);
    ~VoxelEquipmentRuntime();
    void advanceMeasurements(); // caller advances scene once; no independent display/sensor worlds
    struct Statistics {uint64_t published=0,rejectedWindows=0;double sensorMs=0;rockyenv::sensor::GpuSensorRuntime::Statistics gpu;};
    Statistics statistics()const;
    nlohmann::json platformTelemetry()const;
    const VoxelEquipmentConfig& config()const{return config_;}
private:
    VoxelSceneReader scene_;VoxelEquipmentConfig config_;
    std::unique_ptr<VoxelSensorSession> sensors_;
    Statistics stats_;
};
nlohmann::json voxelEquipmentVisualTelemetry(const VoxelEquipmentConfig&,const VoxelSceneSnapshot&);
nlohmann::json voxelEquipmentStateTelemetry(const VoxelEquipmentConfig&,const VoxelSceneSnapshot&);
nlohmann::json voxelModelStateMessages(const VoxelEquipmentConfig&,const VoxelSceneSnapshot& source,
    const VoxelSceneSnapshot& current);
nlohmann::json voxelEquipmentPlatformTelemetry(const VoxelEquipmentConfig&,VoxelSceneSnapshotPtr);
}
