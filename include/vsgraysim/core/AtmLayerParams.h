#pragma once
/// @file AtmLayerParams.h
/// @brief GPU-side atmospheric layering model (Layer 0).
///
/// Mirrors `AtmLayerParams` defined in section 3.3 of the design document.
/// Each entry describes a single altitude slab of the standard atmosphere
/// and stores per-band propagation coefficients.

#include <cstdint>
#include "vsgraysim/core/BandTypes.h"

namespace vsgraysim {

/// One slab of the stratified standard atmosphere model.
struct AtmLayerParams {
    float height_min;        ///< Slab floor (m above MSL)
    float height_max;        ///< Slab ceiling (m above MSL)
    float temp;              ///< Temperature (K)
    float pressure;         ///< Pressure (Pa)
    float humidity;         ///< Relative humidity [0..1]
    float density;          ///< Air density (kg/m^3)
    float aerosol;           ///< Aerosol concentration (1/m^3)

    /// Per-band extinction coefficient (1/m): [vis, ir, radar, sar].
    float ext_coeff[BAND_CHANNEL_COUNT];
    /// Per-band scattering coefficient (1/m).
    float scatter_coeff[BAND_CHANNEL_COUNT];
    /// Per-band absorption coefficient (1/m).
    float absorb_coeff[BAND_CHANNEL_COUNT];

    float refract_index;     ///< Refractive index (real part)
    float turbulence;        ///< Cn^2 turbulence strength (m^-2/3)
    float cloud_density;     ///< Cloud water content (g/m^3) in this slab
    float reserved[2];        ///< Pad to 96 bytes (multiple of 16)
};

static_assert(sizeof(AtmLayerParams) % 16 == 0,
              "AtmLayerParams must be 16-byte aligned for SSBO usage");

} // namespace vsgraysim
