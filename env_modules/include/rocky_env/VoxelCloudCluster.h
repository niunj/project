#pragma once

#include <rocky_env/SparseCloudVolume.h>
#include <cstdint>
#include <memory>

namespace rockyenv {

// One physical cloud object.  It owns its voxel state; the renderer may pack
// many objects into one GPU atlas, but atlas residency never defines cloud
// identity or lifetime.
struct VoxelCloudClusterState {
    uint64_t id=0;
    vsg::vec3 centre{};             // local ENU metres
    float baseMetres=500.f;
    float depthMetres=1800.f;
    float horizontalRadius=700.f;
    float vapour=.016f;           // kg/kg water-vapour mixing ratio
    float temperatureExcess=3.5f;  // kelvin
    float updraft=4.f;              // m/s
    vsg::vec3 wind{};
};

class VoxelCloudCluster {
public:
    VoxelCloudCluster(VoxelCloudClusterState,SparseCloudConfig);

    uint64_t id() const { return state_.id; }
    const VoxelCloudClusterState& state() const { return state_; }
    SparseCloudVolume& volume() { return *volume_; }
    const SparseCloudVolume& volume() const { return *volume_; }
    bool resident(const vsg::vec3& localMetres) const;
    float density(const vsg::vec3& localMetres) const;
    void step(float dt,const CloudDynamics&);

private:
    VoxelCloudClusterState state_;
    std::unique_ptr<SparseCloudVolume> volume_;
};

} // namespace rockyenv
