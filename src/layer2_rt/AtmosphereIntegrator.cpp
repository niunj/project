#include "vsgraysim/layer2_rt/AtmosphereIntegrator.h"

#include <cmath>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace vsgraysim {

namespace {
float altitude(const Vec3& p) { return std::sqrt(p.x*p.x + p.z*p.z + p.y*p.y); }
// Note: for a flat-earth simulation `p.y` would suffice; we use spherical
// altitude here to match the standard-atmosphere model used by SAR/long-range
// radar scenarios. Replace with `return p.y;` for a flat-earth setup.
} // namespace

AtmosphereIntegrator::AtmosphereIntegrator(AtmosphereModelPtr atm)
    : atm_(std::move(atm)) {
    if (!atm_) {
        spdlog::warn("AtmosphereIntegrator: null atmosphere model; using empty model");
        atm_ = std::make_shared<AtmosphereModel>();
    }
}

AtmIntegrationResult
AtmosphereIntegrator::integrate(const Vec3& origin,
                                const Vec3& dir,
                                float distance,
                                float tx_power,
                                BandChannel ch,
                                float step_min,
                                float step_max) const {
    AtmIntegrationResult r{1.0f, 0.0f, 0.0f, -1};

    float travelled = 0.0f;
    Vec3  p = origin;

    // Adaptive step: denser air -> smaller steps (capped to [step_min, step_max]).
    auto step_for = [&](float density) {
        // Reference density 1.225 kg/m^3 at sea level.
        float factor = std::clamp(1.225f / std::max(1e-3f, density), 0.1f, 50.0f);
        return std::clamp(step_min * factor, step_min, step_max);
    };

    while (travelled < distance) {
        const auto layer = atm_->sampleAtAltitude(altitude(p));
        const float dl = std::min(step_for(layer.density), distance - travelled);

        // 1. Attenuation: L *= exp(-sigma_e * dl)
        float sigma_e = layer.ext_coeff[ch];
        r.total_attenuation *= std::exp(-sigma_e * dl);

        // 2. Scatter: P_scatter += P_tx * sigma_s * dl
        float sigma_s = layer.scatter_coeff[ch];
        // Use the remaining power after attenuation up to this step:
        float power_here = tx_power * r.total_attenuation;
        r.scatter_power += power_here * sigma_s * dl;

        // 3. Refraction: integrate d(n)/dh across the vertical gradient.
        //   The angular deflection per step is approximately:
        //     dtheta = -tan(theta_z) * (1/n) * (dn/dh) * dl
        //   where theta_z is the local zenith angle of the ray direction.
        //   We use a simplified flat-earth approximation here:
        float cos_zenith = std::clamp(std::abs(dir.y), 0.0f, 1.0f);
        float sin_zenith = std::sqrt(1.0f - cos_zenith * cos_zenith);
        // Approximate dn/dh from the slab transition: assume n ~ 1 + small*kappa
        const float kappa = 2.8e-4f; // typical sea-level refractivity
        float n = layer.refract_index;
        float dndh = -kappa / std::max(1.0f, altitude(p));
        r.refraction_offset += -sin_zenith * (1.0f / std::max(1.0f, n)) * dndh * dl;

        r.last_layer_id = static_cast<int>(atm_->layerCount() > 0 ? 0 : -1);
        // Advance position
        p.x += dir.x * dl; p.y += dir.y * dl; p.z += dir.z * dl;
        travelled += dl;
    }
    return r;
}

} // namespace vsgraysim
