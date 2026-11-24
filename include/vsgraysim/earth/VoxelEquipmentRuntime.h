#pragma once
#include <vsgraysim/earth/VoxelSensorSession.h>
#include <vsgraysim/broker/EquipmentInfo.h>
namespace vsgraysim::earth {
// A configured physical sensor, separate from vehicle/console appearance.
struct VoxelEquipmentSpec {
    EquipmentInfo equipment;
    rockyenv::sensor::GpuSensorSpec sensor;
    rockyenv::sensor::GpuAperture aperture;
    std::string mountObject; // optional object-local rigid mount, follows fragments only by explicit id
};
struct VoxelEquipmentPlatform {
    uint32_t id=0;std::string object,name,kind;double modelHeadingDeg=0;
};
struct VoxelEquipmentConfig {
    std::string topicPath;
    std::vector<VoxelEquipmentSpec> equipment;
    std::vector<VoxelEquipmentPlatform> platforms;
    double visibilityM=30000;
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
nlohmann::json voxelEquipmentPlatformTelemetry(const VoxelEquipmentConfig&,VoxelSceneSnapshotPtr);
}
