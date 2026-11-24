#pragma once
/// @file SensorScheduler.h
/// @brief Layer 4: Multi-sensor transmit timing & scheduling (section 4.2.2).
///
/// Owns the per-sensor transmit schedule for a single frame:
///   - emits rays in the configured pulse repetition order;
///   - synchronises with the RT pipeline (Layer 2);
///   - feeds the resulting RayPayloads to the DataDispatcher (Layer 4)
///     which routes normal vs. crosstalk data to the appropriate
///     CUDA signal-processing stream (Layer 3).

#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_map>
#include <optional>
#include <map>
#include <simcore/SensorRuntime.h>
#include "vsgraysim/core/SensorTypes.h"
#include "vsgraysim/core/RayPayload.h"
#include "vsgraysim/layer0_phys/CrossInductionRules.h"

namespace vsgraysim {

struct ScheduledTx {
    uint32_t  sensor_id;
    SensorType type;
    uint32_t  band_mask;
    float     tx_power;
    float     beam_width;
    float     wavelength;
    /// Simulation time (s) within this host step.
    double    fire_time;
    /// Relative priority used to break timing ties (lower = earlier).
    int       priority;
    /// Pulse index (0-based) for this sensor within the frame.  Together
    /// with fire_time it identifies one emission of a multi-pulse sensor.
    uint32_t  pulse_seq;
    double    absolute_time = 0.0;
    uint64_t  pulse_index = 0;
    double measurement_start = 0;
    bool image_measurement = false;
};

class SensorScheduler {
public:
    explicit SensorScheduler(CrossInductionRulesPtr rules);
    /// Passive imaging cadence is independent of transmit power and PRF.
    /// period==0 follows the initial host step; exposure is recorded in the event.
    void setImageCadence(uint32_t sensor_id,double period,double exposure);

    /// Build the per-frame transmit schedule for the given frame duration.
    /// Pulses are scheduled by pulse_rate (or once per frame when zero).
    void buildFrameSchedule(double frame_dt, std::optional<double> frame_end = {});

    /// Access the schedule (sorted by fire_time, then priority).
    const std::vector<ScheduledTx>& schedule() const { return schedule_; }

    /// Pop the next tx whose fire_time <= t, or nullptr if none.
    const ScheduledTx* nextDue(double t) const;

private:
    CrossInductionRulesPtr       rules_;
    std::vector<ScheduledTx>      schedule_;
    mutable std::size_t           cursor_ = 0;
    simcore::SensorRuntime pulses_,images_;
    struct ImageCadence { double period=0,exposure=0; };
    std::map<uint32_t,ImageCadence> image_cadences_;
    bool configured_=false;
    simcore::TimePoint time_ = 0;
};

using SensorSchedulerPtr = std::shared_ptr<SensorScheduler>;

} // namespace vsgraysim
