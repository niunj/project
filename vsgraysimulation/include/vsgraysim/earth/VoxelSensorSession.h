#pragma once
#include <vsgraysim/earth/VoxelSceneSensors.h>
#include <rocky_env/GpuSensorRuntime.h>
namespace vsgraysim::earth {
/// Owner-thread GPU consumer of the same published voxel trajectory as display
/// and collision. Destroy before the referenced scene. A missed history range
/// or exposure/CPI crossing a topology boundary is never replaced by latest truth.
class VoxelSensorSession {
public:
    using Aperture=std::function<rockyenv::sensor::GpuAperture(uint32_t,double)>;
    VoxelSensorSession(VoxelScene&,const std::vector<rockyenv::sensor::GpuSensorSpec>&,
                       vsg::Device* sharedDevice=nullptr,const std::string& participant="voxel-sensors",
                       std::shared_ptr<rockyenv::CloudSceneBinding> cloud={});
    VoxelSensorSession(VoxelSceneReader,const std::vector<rockyenv::sensor::GpuSensorSpec>&,
                       vsg::Device* sharedDevice=nullptr,const std::string& participant="voxel-sensors",
                       std::shared_ptr<rockyenv::CloudSceneBinding> cloud={});
    ~VoxelSensorSession();
    void advanceTo(double time,const Aperture&,const rockyenv::sensor::GpuSensorRuntime::Environment&,
                   const rockyenv::sensor::GpuSensorRuntime::Sink&,
                   const std::function<bool(uint32_t)>& enabled={});
    uint64_t rejectedSceneWindows()const;
    uint64_t contentRevision()const;
    rockyenv::sensor::GpuSensorRuntime::Statistics statistics()const;
    struct TopologyBuild {
        uint64_t contentRevision=0;
        double dataMs=0,totalMs=0;
        rockyenv::sensor::GpuSensorRuntime::SceneBuildStatistics gpu;
    };
    TopologyBuild latestTopologyBuild()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
