#pragma once
/// @file BandTypes.h
/// @brief Waveband mask / enumeration shared across all layers.
///
/// Defines the canonical 4-band mask scheme used throughout the
/// multi-spectral simulation (visible/laser, IR, radar, SAR) as well as
/// cross-induction type codes.

#include <cstdint>

namespace vsgraysim {

/// Waveband bit mask (matches the spec's `band_mask` definition).
enum BandMask : uint32_t {
    BAND_VIS     = 0b0001,  ///< Visible / laser
    BAND_IR      = 0b0010,  ///< Infrared (LWIR/SWIR)
    BAND_RADAR   = 0b0100,  ///< Microwave radar (X/C/Ku...)
    BAND_SAR     = 0b1000,  ///< SAR
};

/// Convenience: channel index used for per-band arrays in GPU structs.
enum BandChannel : uint32_t {
    CH_VIS   = 0,
    CH_IR    = 1,
    CH_RADAR = 2,
    CH_SAR   = 3,
    BAND_CHANNEL_COUNT = 4,
};

/// Cross-induction type (laser->IR, radar->SAR, ...).
enum class CrossType : int32_t {
    NONE        = 0,
    LASER_TO_IR = 1,
    RADAR_TO_SAR = 2,
    LASER_TO_VIS = 3,
    IR_TO_RADAR = 4,
    CUSTOM      = 99,
};

inline const char* crossTypeName(CrossType t) {
    switch (t) {
        case CrossType::LASER_TO_IR:  return "laser->ir";
        case CrossType::RADAR_TO_SAR: return "radar->sar";
        case CrossType::LASER_TO_VIS: return "laser->vis";
        case CrossType::IR_TO_RADAR:  return "ir->radar";
        case CrossType::NONE:         return "none";
        default:                      return "custom";
    }
}

} // namespace vsgraysim
