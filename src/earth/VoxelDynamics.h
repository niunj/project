#pragma once
#include <vsgraysim/earth/VoxelScene.h>
#include <memory>
namespace vsgraysim::earth {
// A staged Bullet world is reconstructed from a published value each tick.
// Warm-start/manifold caches never become uncheckpointed simulation state.
class VoxelDynamics {
public:
    // The reference path is retained for narrow-phase equivalence regressions.
    explicit VoxelDynamics(bool indexedTerrain=true);
    ~VoxelDynamics();
    double buoyancyMs=0,waterDragMs=0,waterMs=0; // transient owner-thread diagnostics, never physical state
    void step(const VoxelSceneSnapshot& previous,VoxelSceneSnapshot& candidate);
    void detect(VoxelSceneSnapshot& candidate,double maximumContactDistance=.001);
    void impulse(VoxelSceneObject&,uint64_t tick,double step,const vsg::dvec3& point,const vsg::dvec3& impulse);
    std::optional<VoxelSceneHit> raycast(const VoxelSceneSnapshot&,const vsg::dvec3&,const vsg::dvec3&,double);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
struct VoxelFractureResult {
    size_t brokenBonds=0;
    double spentEnergy=0;
    std::vector<std::string> fragments;
};
void pruneVoxelBodyTopology(VoxelSceneObject&);
VoxelFractureResult fractureVoxelObject(VoxelSceneSnapshot&,const std::string& id,
                                       const nlohmann::json* planeEvent=nullptr);
void validateVoxelBody(VoxelSceneObject&,const nlohmann::json& object,bool checkpoint);
nlohmann::json encodeVoxelBody(const VoxelSceneObject&);
}
