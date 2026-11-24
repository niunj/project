#pragma once
/// @file AtmosphereIntegrator.h
/// @brief Layer 2: CPU-side atmospheric propagation integrator.
///
/// Mirrors the GPU ray-tracing closest-hit shader's atmospheric sampling
/// (section 3.1.2).  Used both:
///   * By Layer 2 to validate the GPU implementation against a reference.
///   * By Layer 3 (CUDA) to pre-compute per-ray atmospheric attenuation when
///     running in the CPU-validation path.
///
/// The integrator performs ray-marching along the propagation direction,
/// sampling the Layer-0 AtmosphereModel at each step and accumulating:
///   - total attenuation  L_atm = exp(-int sigma_e dh)
///   - scatter power       P_scatter = P_tx * sigma_s * dl
///   - refraction offset  (ray bending by integrating n(h) gradient)

#include <memory>
#include "vsgraysim/core/RayPayload.h"
#include "vsgraysim/layer0_phys/AtmosphereModel.h"

namespace vsgraysim {

struct AtmIntegrationResult {
    float total_attenuation;   ///< Multiplicative [0,1]
    float scatter_power;      ///< Power scattered out (W)
    float refraction_offset;  ///< Angular deviation (rad)
    int   last_layer_id;      ///< Final slab index reached
};

class AtmosphereIntegrator {
public:
    explicit AtmosphereIntegrator(AtmosphereModelPtr atm);

    /// March a single ray segment from `origin` along `dir` for `distance`
    /// metres, with emitted `tx_power` at band channel `ch`.
    /// `step_min`/`step_max` are adaptive step bounds in metres.
    AtmIntegrationResult integrate(const Vec3& origin,
                                   const Vec3& dir,
                                   float distance,
                                   float tx_power,
                                   BandChannel ch,
                                   float step_min = 10.0f,
                                   float step_max = 1000.0f) const;

private:
    AtmosphereModelPtr atm_;
};

using AtmosphereIntegratorPtr = std::shared_ptr<AtmosphereIntegrator>;

} // namespace vsgraysim
