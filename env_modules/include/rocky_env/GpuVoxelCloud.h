#pragma once

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>

#include <cstdint>
#include <vector>

namespace vsg {
class Commands;
class Device;
class ImageView;
class Sampler;
}

namespace rockyenv {

// A bounded high-resolution cloud laboratory.  It deliberately models one
// cloud first: the same GPU state is used by rendering, collision wakes and
// later sensor sampling.  World coordinates are local ENU metres.
struct GpuVoxelCloudConfig {
    uint32_t width = 96;
    uint32_t height = 64;
    uint32_t depth = 96;
    float voxelSizeMetres = 24.f;
    vsg::vec3 originMetres{-1152.f, 700.f, -1152.f};
    uint32_t pressureIterations = 20;
    uint32_t maxColliders = 32;
    /// Deterministic weather-pattern seed for the reset pass. Fixed per
    /// configuration so rebasing the laboratory never redraws the weather;
    /// it must not be derived from the simulation clock.
    uint32_t weatherSeed = 0x5EED2026u;
};

struct GpuCloudCollider {
    uint64_t id = 0;
    vsg::vec3 centreMetres{};
    float radiusMetres = 12.f;
    vsg::vec3 velocityMetresPerSecond{};
    float coupling = 1.f;
};

/// One destructive/forcing event injected into the cloud microphysics:
/// an explosion, engine plume or weapon blast. Unlike a collider wake it
/// carries energy: a hot expanding core that evaporates liquid water and
/// rain, plus a radial velocity impulse that physically displaces the
/// surrounding voxel air. Units are local ENU metres / Kelvin / m/s.
struct GpuCloudImpulse {
    vsg::vec3 centreMetres{};
    float radiusMetres = 60.f;
    float temperatureKickK = 40.f;          ///< core temperature perturbation
    float radialVelocityMetresPerSecond = 55.f; ///< expanding shock velocity
    float dissipation = 1.f;                ///< 0..1 fraction of liquid removed in core
    float durationSeconds = 1.5f;           ///< how long the impulse keeps acting
};

struct GpuVoxelCloudFrame {
    double simulationSeconds = 0.0;
    float stepSeconds = 1.f / 30.f;
    vsg::vec3 ambientWind{};
    float ambientVapourMixingRatio = .0095f;
    float ambientTemperatureK = 285.15f;
    float ambientPressurePa = 90000.f;
    float lapseRateKPerMetre = .0065f;
    float gravity = 9.80665f;
    float condensationRate = 2.2f;
    float evaporationRate = .45f;
    float buoyancy = 1.f;
    float waterLoading = 1.f;
    float vorticityConfinement = .18f;
    float velocityDamping = .018f;
    float solarHeatingKPerSecond = .0015f;
    float wakeSeconds = 8.f;
    float displacement = .95f;
    float cloudCover = .5f;
    std::vector<GpuCloudCollider> colliders;
    /// Destructive/forcing impulses for this step (explosions, plumes).
    /// Applied inside the microphysics pass with energy, not just displacement.
    std::vector<GpuCloudImpulse> impulses;
};
class GpuVoxelCloud {
public:
    struct Result { bool ok = false; const char* error = ""; };

    GpuVoxelCloud();
    ~GpuVoxelCloud();
    GpuVoxelCloud(const GpuVoxelCloud&) = delete;
    GpuVoxelCloud& operator=(const GpuVoxelCloud&) = delete;

    Result build(vsg::Device&, const GpuVoxelCloudConfig& = {});
    /// Submit up to 64 identical fixed substeps. Reset applies only to the
    /// first substep; pressure history survives all subsequent substeps.
    void update(const GpuVoxelCloudFrame&,uint32_t substeps=1);
    // Repositions the bounded ENU laboratory without reallocating images.
    // The next update reseeds the physical moist thermal in the new domain
    // using the fixed configured weather seed, so repeated rebases draw the
    // same deterministic weather pattern.
    void setOriginMetres(const vsg::vec3&);
    void reset();

    // Execute initializationCommands exactly once after compilation, then
    // execute stepCommands at the cloud simulation cadence.  This split keeps
    // Vulkan image-layout transitions out of the recurring frame graph.
    vsg::Commands* initializationCommands() const;
    vsg::Commands* stepCommands() const;

    // R = vapour mixing ratio (kg/kg), G = liquid water (kg/m^3),
    // B = temperature perturbation (K), A = rain water (kg/m^3).
    vsg::ImageView* thermodynamicMap() const;
    // XYZ = air velocity in local ENU metres/second.
    vsg::ImageView* velocityMap() const;
    // R = pressure perturbation consumed by the projection pass; the last
    // Jacobi sweep leaves it in this image. Exposed for solver diagnostics
    // and warm-start regression tests.
    vsg::ImageView* pressureMap() const;
    vsg::Sampler* sampler() const;

    const GpuVoxelCloudConfig& config() const { return config_; }
    uint64_t revision() const { return revision_; }
    bool ready() const { return ready_; }

    /// Number of destructive impulse slots carried in the parameter UBO.
    static constexpr uint32_t maxImpulses() { return 16; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    GpuVoxelCloudConfig config_{};
    uint64_t revision_ = 0;
    bool ready_ = false;
    bool resetPending_ = true;
};

} // namespace rockyenv
