#pragma once
/// @file AtmosphereModel.h
/// @brief Layer 0: Stratified standard-atmosphere model (section 3.1.1).
///
/// Loads an atmosphere configuration JSON file describing altitude slabs
/// and per-band propagation coefficients, exposes a CPU-side lookup
/// (`sampleAtAltitude`) used by the RT shader closest-hit stage (mirrored
/// GPU-side in `g_atm_params`), and supports runtime dynamic updates
/// (turbulence, aerosol, cloud density).

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include "vsgraysim/core/AtmLayerParams.h"

namespace vsgraysim {

class AtmosphereModel {
public:
    /// Load the layered-atmosphere JSON config.
    void loadConfig(const std::string& json_path);

    /// Sample atmospheric parameters at the given altitude `h` (m above MSL).
    /// Interpolates linearly across the slab that contains `h`.  The first
    /// slab below and first slab above are used if `h` is outside the table.
    AtmLayerParams sampleAtAltitude(float h) const;

    /// Update dynamic fields (turbulence, aerosol, cloud_density) for a slab.
    void updateDynamic(uint32_t layer_id,
                       float turbulence,
                       float aerosol,
                       float cloud_density);

    /// Recompute the per-band coefficients for a weather scenario
    /// (e.g. "clear", "cloudy", "rain", "fog").  Pure-CPU; the GPU buffer
    /// must be re-uploaded by Layer 2.
    void applyWeatherScenario(const std::string& scenario, float severity = 1.0f);

    const std::vector<AtmLayerParams>& layers() const { return layers_; }
    std::size_t layerCount() const { return layers_.size(); }

private:
    std::vector<AtmLayerParams> layers_;
    float                      min_height_ = 0.0f;
    float                      max_height_ = 100000.0f;
};

using AtmosphereModelPtr = std::shared_ptr<AtmosphereModel>;

} // namespace vsgraysim
