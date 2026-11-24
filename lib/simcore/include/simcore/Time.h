#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace simcore {
using TimePoint = int64_t; // elapsed SI nanoseconds; calendar conversion is a separate concern
inline constexpr TimePoint nanosecondsPerSecond = 1000000000;

inline TimePoint fromSeconds(double seconds) {
    const long double ns = static_cast<long double>(seconds) * nanosecondsPerSecond;
    if (!std::isfinite(seconds) || ns < 0 || ns > std::numeric_limits<TimePoint>::max() - 0.5L)
        throw std::invalid_argument("simulation time must be finite, nonnegative and representable");
    return static_cast<TimePoint>(std::round(ns));
}
inline double toSeconds(TimePoint t) { return double(t) / nanosecondsPerSecond; }

/// Indexed rational event clock. Compute every timestamp from the origin;
/// never repeatedly add a rounded period (e.g. 1/60 second).
class PeriodicTimeline {
public:
    PeriodicTimeline(uint64_t hzNumerator = 60, uint64_t hzDenominator = 1)
        : numerator_(hzNumerator), denominator_(hzDenominator) {
        if (!numerator_ || !denominator_) throw std::invalid_argument("zero event rate");
        const auto divisor = std::gcd(numerator_, denominator_);
        numerator_ /= divisor; denominator_ /= divisor;
        if (static_cast<long double>(numerator_) / denominator_ > nanosecondsPerSecond)
            throw std::invalid_argument("event period is below one nanosecond");
    }
    static PeriodicTimeline fromHz(double hz) {
        if (!std::isfinite(hz) || hz < 1e-6 || hz > nanosecondsPerSecond)
            throw std::invalid_argument("event rate is outside 1e-6..1e9 Hz");
        // Configuration adapter: exact decimal rates to microhertz resolution.
        return {static_cast<uint64_t>(std::llround(hz * 1000000.0)), 1000000};
    }
    PeriodicTimeline dividedBy(uint64_t divisor) const {
        if (!divisor || denominator_ > std::numeric_limits<uint64_t>::max() / divisor)
            throw std::overflow_error("invalid event-rate divisor");
        return {numerator_, denominator_ * divisor};
    }
    TimePoint at(uint64_t ordinal) const {
        // GCC/Clang integer intermediate prevents overflow before division.
        using Wide = __uint128_t;
        constexpr Wide maximum = ~Wide(0);
        Wide value = Wide(ordinal) * denominator_;
        if (value > (maximum - numerator_ / 2) / nanosecondsPerSecond)
            throw std::overflow_error("event timestamp overflow");
        value = (value * nanosecondsPerSecond + numerator_ / 2) / numerator_;
        if (value > Wide(std::numeric_limits<TimePoint>::max()))
            throw std::overflow_error("event timestamp overflow");
        return static_cast<TimePoint>(value);
    }
    double periodSeconds() const { return double(denominator_) / double(numerator_); }
private:
    uint64_t numerator_, denominator_;
};

class SimulationClock {
public:
    explicit SimulationClock(PeriodicTimeline timeline = {}) : timeline_(timeline) {}
    TimePoint now() const { return now_; }
    TimePoint advance() {
        const auto delta = timeline_.at(index_ + 1);
        if (delta > std::numeric_limits<TimePoint>::max() - origin_)
            throw std::overflow_error("simulation timestamp overflow");
        ++index_; now_ = origin_ + delta; return now_;
    }
    void setRate(PeriodicTimeline timeline) {
        timeline_ = timeline; origin_ = now_; index_ = 0;
    }
    void reset(TimePoint origin=0) {
        if (origin<0) throw std::invalid_argument("negative simulation origin");
        origin_=now_=origin; index_=0;
    }
private:
    PeriodicTimeline timeline_;
    TimePoint origin_ = 0, now_ = 0;
    uint64_t index_ = 0;
};

/// Merge an indexed physics heartbeat with measurement deadlines. Landing on
/// an exposure/CPI boundary must not shift the following physics heartbeat.
class EventClock {
public:
    explicit EventClock(PeriodicTimeline physics = {}) : physics_(physics) {}
    TimePoint now() const { return now_; }
    TimePoint advance(TimePoint measurementDeadline) {
        if (measurementDeadline <= now_)
            throw std::invalid_argument("measurement deadline must follow the current time");
        const auto nextPhysics = physics_.at(next_physics_);
        now_ = nextPhysics < measurementDeadline ? nextPhysics : measurementDeadline;
        if (now_ == nextPhysics) ++next_physics_;
        return now_;
    }
private:
    PeriodicTimeline physics_;
    TimePoint now_ = 0;
    uint64_t next_physics_ = 1;
};
} // namespace simcore
