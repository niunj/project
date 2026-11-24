#include "vsgraysim/kernel/TimingBus.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <spdlog/spdlog.h>

namespace vsgraysim {

TimingBus::TimingBus(const TimeConfig& config)
    : config_(config),
      clock_(simcore::PeriodicTimeline::fromHz(1.0 / config.fixed_dt)),
      frame_context_(std::make_shared<FrameContext>()) {
    if (config.sub_steps == 0)
        throw std::invalid_argument("physics substep count must be positive");
    if (!config.fixed_timestep)
        throw std::logic_error("variable wall-clock stepping is unsupported by the deterministic kernel");
    setTimeScale(config.time_scale);
    frame_context_->tick = TimeTick{};
    frame_context_->tick.dt = config.fixed_dt;
    frame_context_->tick.sub_steps = config.sub_steps;
    current_tick_ = frame_context_->tick;
}

TimingBus::~TimingBus() {
    stop();
}

void TimingBus::configureFixedStep(double dt, uint32_t sub_steps) {
    const auto rate = simcore::PeriodicTimeline::fromHz(1.0 / dt);
    if (sub_steps == 0) throw std::invalid_argument("physics substep count must be positive");
    clock_.setRate(rate);
    config_.fixed_timestep = true;
    config_.fixed_dt = dt;
    config_.sub_steps = sub_steps;
    if (frame_context_) {
        frame_context_->tick.dt = dt;
        frame_context_->tick.sub_steps = sub_steps;
    }
    current_tick_.dt = dt;
    current_tick_.sub_steps = sub_steps;
}

void TimingBus::configureVariableStep(double max_dt, double target_fps) {
    throw std::logic_error("variable wall-clock stepping is unsupported; configure an explicit fixed simulation step");
}

void TimingBus::setTimeScale(double scale) {
    if (!std::isfinite(scale) || scale <= 0)
        throw std::invalid_argument("time scale must be finite and positive");
    if (scale != 1.0)
        throw std::logic_error("simulation speed belongs to host pacing; the kernel's physical step is unchanged");
    config_.time_scale = scale;
}

void TimingBus::registerSubsystem(std::shared_ptr<ISimulationSubsystem> sys, int priority) {
    if (!sys) return;
    subsystems_.push_back({sys, priority, true});
    subsystems_sorted_ = false;
    spdlog::info("Registered subsystem: {} (priority={})", sys->name(), priority);
}

void TimingBus::unregisterSubsystem(const std::string& name) {
    auto it = std::remove_if(subsystems_.begin(), subsystems_.end(),
        [&name](const SubsystemRegistration& reg) {
            return reg.subsystem && reg.subsystem->name() == name;
        });
    subsystems_.erase(it, subsystems_.end());
    subsystems_sorted_ = false;
}

std::shared_ptr<ISimulationSubsystem> TimingBus::getSubsystem(const std::string& name) const {
    for (const auto& reg : subsystems_) {
        if (reg.subsystem && reg.subsystem->name() == name) {
            return reg.subsystem;
        }
    }
    return nullptr;
}

std::vector<std::string> TimingBus::listSubsystems() const {
    std::vector<std::string> names;
    for (const auto& reg : subsystems_) {
        if (reg.subsystem) names.push_back(reg.subsystem->name());
    }
    return names;
}

void TimingBus::injectProducts(TerrainProductConstPtr terrain, SceneProductConstPtr scene) {
    frame_context_->terrain = std::move(terrain);
    frame_context_->scene = std::move(scene);
}

void TimingBus::createMutableProducts() {
    frame_context_->equipment_state = std::make_shared<EquipmentStateProduct>();
    frame_context_->display_frame = std::make_shared<DisplayFrameProduct>();
}

bool TimingBus::initialize() {
    if (initialized_) return true;

    if (!frame_context_->terrain) {
        spdlog::warn("No terrain product injected");
    }
    if (!frame_context_->scene) {
        spdlog::warn("No scene product injected");
    }
    if (!frame_context_->equipment_state) {
        createMutableProducts();
    }

    if (!initializeSubsystems()) {
        return false;
    }

    initialized_ = true;
    spdlog::info("TimingBus initialized with {} subsystems", subsystems_.size());
    return true;
}

bool TimingBus::initializeSubsystems() {
    sortSubsystems();

    for (auto& reg : subsystems_) {
        if (!reg.enabled || !reg.subsystem) continue;

        nlohmann::json cfg = {};
        if (!reg.subsystem->initialize(cfg)) {
            spdlog::error("Failed to initialize subsystem: {}", reg.subsystem->name());
            return false;
        }
        spdlog::debug("Initialized subsystem: {}", reg.subsystem->name());
    }
    return true;
}

void TimingBus::sortSubsystems() {
    if (subsystems_sorted_) return;
    std::stable_sort(subsystems_.begin(), subsystems_.end(),
        [](const SubsystemRegistration& a, const SubsystemRegistration& b) {
            return a.priority < b.priority;
        });
    subsystems_sorted_ = true;
}

void TimingBus::start() {
    if (running_) return;
    if (!initialized_) {
        if (!initialize()) return;
    }
    running_ = true;
    current_tick_.is_paused = false;
    startSubsystems();
    spdlog::info("TimingBus started");
}

void TimingBus::startSubsystems() {
    for (auto& reg : subsystems_) {
        if (reg.enabled && reg.subsystem) {
            reg.subsystem->onStart();
        }
    }
}

void TimingBus::stop() {
    if (!running_) return;
    running_ = false;
    stopSubsystems();
    spdlog::info("TimingBus stopped after {} frames", frames_run_);
}

void TimingBus::stopSubsystems() {
    for (auto& reg : subsystems_) {
        if (reg.subsystem) {
            reg.subsystem->onStop();
        }
    }
}

void TimingBus::setPaused(bool paused) {
    current_tick_.is_paused = paused;
    if (!paused) {
        // Resume: reset frame timing
        current_tick_.real_time = 0.0;
    }
}

bool TimingBus::tick() {
    if (!running_) return false;
    if (current_tick_.is_paused) return true;

    if (!doTick()) return false;

    // Check stop conditions
    if (config_.max_sim_time > 0 && current_tick_.sim_time >= config_.max_sim_time) {
        spdlog::info("Reached max simulation time: {:.2f}s", config_.max_sim_time);
        stop();
        return false;
    }
    if (config_.max_frames > 0 && frames_run_ >= config_.max_frames) {
        spdlog::info("Reached max frames: {}", config_.max_frames);
        stop();
        return false;
    }

    return true;
}

bool TimingBus::doTick() {
    // Advance time
    current_tick_.frame_id++;
    const auto previous = clock_.now();
    current_tick_.sim_time = simcore::toSeconds(clock_.advance());
    current_tick_.dt = simcore::toSeconds(clock_.now() - previous);
    current_tick_.sub_step = 0;

    frame_context_->tick = current_tick_;

    // 清空上一帧的瞬时数据（事件在 dispatch 后清空，避免先清后派）
    clearTransient();

    // Pre-tick
    for (auto& reg : subsystems_) {
        if (reg.enabled && reg.subsystem) {
            reg.subsystem->onPreTick(current_tick_);
        }
    }

    // Main tick (with sub-steps if fixed timestep)
    if (config_.fixed_timestep && config_.sub_steps > 1) {
        runSubSteps();
        for (auto& reg : subsystems_) {
            if (reg.enabled && reg.subsystem && !reg.subsystem->usesSubSteps())
                reg.subsystem->onTick(current_tick_);
        }
    } else {
        for (auto& reg : subsystems_) {
            if (reg.enabled && reg.subsystem) {
                reg.subsystem->onTick(current_tick_);
            }
        }
    }

    // Post-tick
    for (auto& reg : subsystems_) {
        if (reg.enabled && reg.subsystem) {
            reg.subsystem->onPostTick(current_tick_);
        }
    }

    // Dispatch all events published since last frame
    dispatchEvents();

    frames_run_++;
    return true;
}

void TimingBus::clearTransient() {
    // 清空产品瞬时数据但保留事件队列直到 dispatch
    if (frame_context_) {
        if (frame_context_->equipment_state) frame_context_->equipment_state->clearTransient();
        if (frame_context_->display_frame) frame_context_->display_frame->clearTransient();
        frame_context_->scratchpad.clear();
    }
}

void TimingBus::runSubSteps() {
    const TimeTick frame_tick = current_tick_;
    const double sub_dt = frame_tick.dt / config_.sub_steps;
    for (uint32_t s = 0; s < config_.sub_steps; ++s) {
        current_tick_.sub_step = s;
        current_tick_.dt = sub_dt;
        current_tick_.sim_time = frame_tick.sim_time - frame_tick.dt + (s + 1) * sub_dt;
        frame_context_->tick = current_tick_;

        for (auto& reg : subsystems_) {
            if (reg.enabled && reg.subsystem && reg.subsystem->usesSubSteps()) {
                reg.subsystem->onSubStep(current_tick_, s);
            }
        }
    }
    // Restore frame dt
    current_tick_ = frame_tick;
    frame_context_->tick = current_tick_;
}

void TimingBus::dispatchEvents() {
    std::vector<SimulationEvent> events;
    decltype(event_subscriptions_) subscriptions;
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        events.swap(frame_context_->events);
        subscriptions = event_subscriptions_;
    }
    for (const auto& event : events) {
        auto it = subscriptions.find(event.type);
        if (it != subscriptions.end()) {
            for (const auto& cb : it->second) {
                try {
                    cb(event);
                } catch (const std::exception& e) {
                    spdlog::error("Event callback exception: {}", e.what());
                }
            }
        }
    }
}

void TimingBus::run() {
    while (tick()) {
        // Continue until tick() returns false
    }
}

void TimingBus::runForFrames(uint64_t frames) {
    config_.max_frames = frames_run_ + frames;
    run();
    config_.max_frames = 0;
}

void TimingBus::subscribe(EventType type, EventCallback callback) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    event_subscriptions_[type].push_back(std::move(callback));
}

void TimingBus::unsubscribe(EventType type, EventCallback callback) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    auto it = event_subscriptions_.find(type);
    if (it != event_subscriptions_.end()) {
        // Note: std::function doesn't support equality comparison
        // In practice, use a token-based subscription system
        // For now, clear all callbacks for this type
        event_subscriptions_[type].clear();
    }
}

void TimingBus::publish(const SimulationEvent& event) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    frame_context_->publishEvent(event);
}

void TimingBus::publish(EventType type, const SimulationEvent& event) {
    SimulationEvent evt = event;
    evt.type = type;
    publish(evt);
}

} // namespace vsgraysim
