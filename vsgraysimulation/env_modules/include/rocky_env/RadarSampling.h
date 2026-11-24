#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace rockyenv::sensor {

/// Physical fast-time sampling. FFT padding never changes this clock.
struct RadarSampling {
    static constexpr double lightSpeed = 299792458.0;
    double rate_hz;

    static RadarSampling make(double bandwidth_hz, double explicit_rate_hz = 0.0) {
        const double rate = explicit_rate_hz == 0.0 ? bandwidth_hz : explicit_rate_hz;
        if (!std::isfinite(bandwidth_hz) || bandwidth_hz <= 0.0 ||
            !std::isfinite(rate) || rate < bandwidth_hz)
            throw std::invalid_argument("radar sampling rate must be finite and >= bandwidth > 0");
        return {rate};
    }

    double period() const { return 1.0 / rate_hz; }
    double rangeSpacing() const { return lightSpeed / (2.0 * rate_hz); }
    double rangeAt(uint32_t bin, double range_min_m = 0.0) const {
        return range_min_m + double(bin) * rangeSpacing();
    }
};

} // namespace rockyenv::sensor
