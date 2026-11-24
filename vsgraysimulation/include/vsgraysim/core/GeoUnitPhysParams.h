#pragma once
/// @file GeoUnitPhysParams.h
/// @brief GPU-side multi-spectral physical parameter cache structure (Layer 0).
///
/// Mirrors the `GeoUnitPhysParams` struct defined in section 2.1.2 of the
/// design document. One entry per geometry unit (face/voxel), indexed by
/// `geo_unit_id` in a Vulkan SSBO.

#include <cstdint>
#include "vsgraysim/core/BandTypes.h"

namespace vsgraysim {

/// BRDF model selector stored in `vis_brdf_type`.
enum BRDFType : int32_t {
    BRDF_DIFFUSE     = 0,
    BRDF_ROUGH_METAL = 1,
    BRDF_MIRROR      = 2,
    BRDF_RUBBER      = 3,
};

/// GPU-cached physical parameters for a single geometry unit
/// (face / voxel).  Keep this struct 16-byte aligned for SSBO packing.
struct GeoUnitPhysParams {
    uint32_t  object_id;          ///< Owning scene element ID
    uint32_t  geo_unit_id;        ///< Face / voxel ID
    uint32_t  band_mask;          ///< Supported waveband mask (BandMask)

    // --- Visible / laser -------------------------------------------------
    float     vis_albedo;
    int32_t   vis_brdf_type;      ///< BRDFType
    float     laser_scatter;      ///< Laser scattering coefficient
    float     laser_absorb;       ///< Laser pulse absorption coefficient

    // --- Infrared --------------------------------------------------------
    float     ir_emissivity[2];   ///< [0]=LWIR, [1]=SWIR
    float     ir_base_temp;       ///< Base temperature (K)
    float     ir_thermal_conduct; ///< Thermal conductivity (dynamic targets)
    uint32_t  ir_temp_tex_id;    ///< Sampler id of temperature distribution texture

    // --- Microwave radar -------------------------------------------------
    uint32_t  radar_rcs_lut_id;   ///< Sampler id of angular RCS LUT
    float     radar_epsilon_real;
    float     radar_epsilon_imag;
    float     radar_surface_rough;

    // --- SAR -------------------------------------------------------------
    float     sar_sigma0_db;
    float     sar_polar_couple;
    float     sar_doppler_corr;

    // --- Dynamic correction (runtime mutable) -----------------------------
    float     rcs_modify_factor; ///< RCS modification factor (stealth/damage)
    float     temp_modify_offset; ///< Temperature offset (K)
    uint32_t  padding[3];          ///< Pad to 96 bytes (multiple of 16)
};

static_assert(sizeof(GeoUnitPhysParams) % 16 == 0,
              "GeoUnitPhysParams must be 16-byte aligned for SSBO usage");

/// Band-parameter mapping entry: maps (geo_unit_id, band) -> array index
/// for fast lookup from ray-tracing shaders.
struct BandParamMap {
    uint32_t geo_unit_id;
    uint32_t band_mask;
    uint32_t param_offset;        ///< Offset into the GeoUnitPhysParams array
    uint32_t reserved;
};

} // namespace vsgraysim
