#pragma once

#include <cstdint>
#include <complex>
#include <vector>
#include "vsgraysim/core/SensorTypes.h"

namespace vsgraysim {

struct SignalFrame {
    uint32_t width = 0;
    uint32_t height = 0;
    float fs = 0.0f;
    float prf = 0.0f;
    double carrier_hz = 0.0;
    SensorType sensor_type = SensorType::VIS;
    uint32_t sensor_id = 0;

    /// Simulation time at the frame this signal was formed from (s).
    double sim_time = 0.0;
    /// Frame number this signal belongs to.
    uint64_t frame_id = 0;
    /// Number of pulses coherently accumulated in this RD map (CPI size).
    uint32_t coherent_pulses = 0;

    std::vector<std::complex<float>> samples;

    std::vector<float> range_axis;
    std::vector<float> doppler_axis;

    float range_min = 0.0f;
    float range_max = 0.0f;
    uint8_t product_stage = 4; ///< P0 radiance/raw echo, P2 RD, P4 electronic image

    uint64_t surface_revision = 0;

    bool empty() const { return width == 0 || height == 0; }
    void clear() { width = height = 0; samples.clear(); }

    void resize(uint32_t w, uint32_t h) {
        width = w; height = h;
        samples.resize(size_t(w) * h);
    }
};

} // namespace vsgraysim
