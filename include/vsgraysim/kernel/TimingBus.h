#pragma once
/// @file TimingBus.h
/// @brief Central simulation kernel — time stepping, subsystem scheduling, event bus.

#ifndef VSGRAYSIM_KERNEL_TIMINGBUS_H
#define VSGRAYSIM_KERNEL_TIMINGBUS_H

#include "vsgraysim/kernel/TimeTick.h"
#include "vsgraysim/kernel/FrameContext.h"
#include "vsgraysim/kernel/SimulationEvent.h"
#include "vsgraysim/subsystems/ISubsystem.h"
#include <simcore/Time.h>

#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <mutex>

namespace vsgraysim {

/// Subsystem registration with priority
struct SubsystemRegistration {
    std::shared_ptr<ISimulationSubsystem> subsystem;
    int priority = 0;  // Lower = earlier tick
    bool enabled = true;
};

/// TimingBus — the simulation kernel
class TimingBus : public IEventBus {
public:
    explicit TimingBus(const TimeConfig& config = {});
    ~TimingBus() override;

    // Non-copyable, movable
    TimingBus(const TimingBus&) = delete;
    TimingBus& operator=(const TimingBus&) = delete;
    TimingBus(TimingBus&&) = default;
    TimingBus& operator=(TimingBus&&) = default;

    // ---- Configuration ----

    /// Configure fixed time step mode
    void configureFixedStep(double dt, uint32_t sub_steps = 1);

    /// Unsupported: throws until variable steps can be supplied explicitly.
    void configureVariableStep(double max_dt, double target_fps);

    /// Only 1 is supported. Host wall-clock pacing controls playback speed.
    void setTimeScale(double scale);

    /// Set max simulation time (0 = unlimited)
    void setMaxSimTime(double time) { config_.max_sim_time = time; }

    /// Set max frames (0 = unlimited)
    void setMaxFrames(uint64_t frames) { config_.max_frames = frames; }

    // ---- Subsystem management ----

    /// Register a simulation subsystem with priority
    void registerSubsystem(std::shared_ptr<ISimulationSubsystem> sys, int priority = 0);

    /// Unregister subsystem
    void unregisterSubsystem(const std::string& name);

    /// Get registered subsystem by name
    std::shared_ptr<ISimulationSubsystem> getSubsystem(const std::string& name) const;

    /// List all registered subsystems
    std::vector<std::string> listSubsystems() const;

    // ---- Product injection (called once at startup) ----

    /// Inject immutable products (terrain, scene)
    void injectProducts(TerrainProductConstPtr terrain, SceneProductConstPtr scene);

    /// Create mutable products (equipment_state, display_frame)
    void createMutableProducts();

    // ---- Simulation control ----

    /// Initialize all subsystems (call after registration + product injection)
    bool initialize();

    /// Start simulation
    void start();

    /// Stop simulation
    void stop();

    /// Pause/resume
    void setPaused(bool paused);

    /// Single frame tick (returns false if simulation should stop)
    bool tick();

    /// Run until stop condition (blocking)
    void run();

    /// Run for N frames (blocking)
    void runForFrames(uint64_t frames);

    // ---- Status ----

    bool isRunning() const { return running_; }
    bool isPaused() const { return current_tick_.is_paused; }
    const TimeTick& currentTick() const { return current_tick_; }
    const FrameContext& context() const { return *frame_context_; }
    FrameContext& mutableContext() { return *frame_context_; }
    const TimeConfig& config() const { return config_; }

    // ---- IEventBus implementation ----

    void subscribe(EventType type, EventCallback callback) override;
    void unsubscribe(EventType type, EventCallback callback) override;
    void publish(const SimulationEvent& event) override;
    void publish(EventType type, const SimulationEvent& event) override;

private:
    // Internal tick implementation
    bool doTick();

    // Run sub-steps for fixed time step mode
    void runSubSteps();

    // Sort subsystems by priority
    void sortSubsystems();

    // Call subsystem lifecycle
    bool initializeSubsystems();
    void startSubsystems();
    void stopSubsystems();

    // Event dispatch
    void dispatchEvents();

    // Clear per-frame transient data (events kept until dispatch)
    void clearTransient();

    TimeConfig config_;
    simcore::SimulationClock clock_;
    FrameContextPtr frame_context_;

    std::vector<SubsystemRegistration> subsystems_;
    bool subsystems_sorted_ = false;

    TimeTick current_tick_;
    bool running_ = false;
    bool initialized_ = false;
    uint64_t frames_run_ = 0;

    // Event bus
    std::unordered_map<EventType, std::vector<EventCallback>> event_subscriptions_;
    mutable std::mutex event_mutex_;
};

} // namespace vsgraysim

#endif // VSGRAYSIM_KERNEL_TIMINGBUS_H
