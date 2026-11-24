#pragma once
/// @file DynamicMultiSpectralNode.h
/// @brief Layer 1: Dynamic variant of MultiSpectralGeometryNode.
///
/// Adds a runtime-mutable parameter update interface (section 2.2.5) for
/// moving targets and dynamic media (clouds, smoke, hot engine blocks).
/// Inherits from MultiSpectralGeometryNode so the binding API is shared.

#include <functional>
#include <mutex>
#include "vsgraysim/layer1_scene/MultiSpectralGeometryNode.h"

namespace vsgraysim {

/// Pending update descriptor applied at the next frame.
struct PendingDynUpdate {
    uint32_t object_id;
    float    rcs_modify_factor = 1.0f;
    float    temp_offset_K     = 0.0f;
    float    cloud_density     = -1.0f;  ///< -1 = unchanged
    float    turbulence        = -1.0f;  ///< -1 = unchanged
};

class DynamicMultiSpectralNode : public vsg::Inherit<MultiSpectralGeometryNode, DynamicMultiSpectralNode> {
public:
    DynamicMultiSpectralNode() = default;

    /// Thread-safe enqueue of a dynamic update applied at the next frame.
    void enqueueUpdate(const PendingDynUpdate& u);

    /// Drain the pending queue (called once per frame by SceneBuilder).
    /// Returns the drained updates; caller is responsible for forwarding
    /// them to Layer 0 (PhysParamManager) and Layer 2 (GPU cache).
    std::vector<PendingDynUpdate> drainPending();

    /// Optional per-frame callback hook (e.g. integration step for
    /// dynamic temperature fields).
    using PerFrameHook = std::function<void(DynamicMultiSpectralNode&, float /*dt*/)>;
    void setPerFrameHook(PerFrameHook h) { hook_ = std::move(h); }
    void runPerFrameHook(float dt) { if (hook_) hook_(*this, dt); }

private:
    std::mutex                mutex_;
    std::vector<PendingDynUpdate> pending_;
    PerFrameHook              hook_;
};

} // namespace vsgraysim
