#pragma once
/// @file SensorTypes.h
/// @brief Sensor enumeration & sensor parameter structures (Layer 0).

#include <cstdint>
#include <string>
#include <vector>
#include "vsgraysim/core/BandTypes.h"

namespace vsgraysim {

/// Sensor type enumeration.
enum class SensorType : int32_t {
    VIS   = 0,
    LASER = 1,
    IR    = 2,
    RADAR = 3,
    SAR   = 4,
    SONAR = 5,
};

inline const char* sensorTypeName(SensorType t) {
    switch (t) {
        case SensorType::VIS:   return "vis";
        case SensorType::LASER: return "laser";
        case SensorType::IR:    return "ir";
        case SensorType::RADAR: return "radar";
        case SensorType::SAR:   return "sar";
        case SensorType::SONAR: return "sonar";
    }
    return "unknown";
}

/// Sensor transmit parameters.
struct TxParams {
    float power;        ///< Transmit power (W); 0 = passive sensor
    float beam_width;   ///< Beam width (rad)
    float wavelength;   ///< Centre wavelength (m)
    float pulse_rate;   ///< Pulse repetition rate (Hz)
    float duty_cycle;   ///< Duty cycle [0..1]
};

/// One cross-induction receive rule: a (rx sensor) entry inside another
/// sensor's `rx_cross_sensors` list.
struct RxCrossSensor {
    uint32_t    rx_sensor_id;
    SensorType  rx_type;
    std::string band_label;
    float       receive_range;     ///< Receive FoV half-angle (rad)
    float       coupling_factor;   ///< Crosstalk coupling coefficient
    float       band_match;        ///< Band-overlap factor [0..1]
    bool        is_process;        ///< Whether to process the crosstalk
};

/// Per-sensor cross-induction configuration.
struct SensorCrossEntry {
    uint32_t    sensor_id;
    SensorType  type;
    std::string band_label;
    TxParams    tx_params;
    std::vector<RxCrossSensor> rx_cross_sensors;
};

} // namespace vsgraysim
