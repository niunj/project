#pragma once

#include <memory>
#include <random>
#include <vector>
#include "vsgraysim/core/SensorTypes.h"
#include "vsgraysim/core/RayPayload.h"
#include "vsgraysim/core/GeoUnitPhysParams.h"

namespace vsgraysim {

struct Ray {
    Vec3  origin;
    Vec3  direction;
    float weight = 1.0f;
};

class SensorProcessor {
public:
    virtual ~SensorProcessor() = default;

    virtual std::vector<Ray> emitRays(
        const Vec3& origin, const Vec3& direction,
        float beam_width, uint32_t num_rays,
        float scan_angle_az, float scan_angle_el,
        std::mt19937& rng) const = 0;

    virtual float computeSurfaceScatter(
        const Vec3& incident_dir, const Vec3& normal,
        const GeoUnitPhysParams& params, float wavelength) const = 0;

    virtual float computeReturnAmplitude(
        float tx_power, float surface_scatter,
        float atm_attenuation, float distance) const = 0;

    static std::unique_ptr<SensorProcessor> create(SensorType type);
};

} // namespace vsgraysim
