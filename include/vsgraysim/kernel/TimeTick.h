#pragma once
/// @file TimeTick.h
/// @brief Time representation for simulation ticks.

#ifndef VSGRAYSIM_KERNEL_TIMETICK_H
#define VSGRAYSIM_KERNEL_TIMETICK_H

#include <cstdint>
#include <chrono>

namespace vsgraysim {

/// High-resolution time tick
struct TimeTick {
    uint64_t frame_id = 0;              // Monotonically increasing frame counter
    double sim_time = 0.0;              // Simulation time (seconds)
    double dt = 0.0;                    // Delta time for this tick (seconds)
    double real_time = 0.0;             // Wall-clock time (seconds)
    double time_scale = 1.0;            // Simulation speed multiplier
    bool is_paused = false;             // True if simulation is paused
    uint32_t sub_step = 0;              // Current sub-step index (0-based)
    uint32_t sub_steps = 1;             // Total sub-steps per frame

    // Convenience
    std::chrono::duration<double> simDuration() const { return std::chrono::duration<double>(sim_time); }
    std::chrono::duration<double> deltaDuration() const { return std::chrono::duration<double>(dt); }

    // Advance to next frame
    TimeTick next(double next_dt = 0.0) const {
        TimeTick next = *this;
        next.frame_id++;
        next.sim_time += dt;
        next.dt = next_dt > 0 ? next_dt : dt;
        next.sub_step = 0;
        return next;
    }

    // Advance to next sub-step
    TimeTick nextSubStep() const {
        TimeTick next = *this;
        next.sub_step++;
        next.dt = dt / sub_steps;
        return next;
    }
};

/// Time configuration
struct TimeConfig {
    double fixed_dt = 1.0 / 60.0;       // Fixed time step (seconds)
    double max_dt = 1.0 / 30.0;         // Max variable time step
    double target_fps = 60.0;           // Target FPS for variable step
    uint32_t sub_steps = 1;             // Physics sub-steps per frame
    bool fixed_timestep = true;         // Use fixed vs variable step
    double time_scale = 1.0;            // Global time scale
    double max_sim_time = 0.0;          // 0 = unlimited
    uint64_t max_frames = 0;            // 0 = unlimited
};

} // namespace vsgraysim

#endif // VSGRAYSIM_KERNEL_TIMETICK_H