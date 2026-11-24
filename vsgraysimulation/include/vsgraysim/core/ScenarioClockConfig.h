#pragma once
#include <simcore/Time.h>
#include <simcore/UtcEpoch.h>
#include <nlohmann/json.hpp>

namespace vsgraysim {
/// One disk contract shared by editor, GUI simulation and headless engine.
struct ScenarioClockConfig {
    double frame_dt=1.0/60.0, physics_step=1.0/120.0, history_seconds=10;
    std::string epoch_utc="2026-09-10T04:00:00Z";
    nlohmann::json extra=nlohmann::json::object();
    void validate() const {
        if (!std::isfinite(frame_dt) || !std::isfinite(physics_step) || frame_dt<=0 || frame_dt>1 ||
            physics_step<=0 || physics_step>1 || !std::isfinite(history_seconds) || history_seconds<frame_dt)
            throw std::invalid_argument("invalid simulation timestep or truth-history window");
        simcore::PeriodicTimeline::fromHz(1/frame_dt);
        simcore::PeriodicTimeline::fromHz(1/physics_step);
        simcore::fromSeconds(history_seconds);
        simcore::parseUtcEpoch(epoch_utc);
    }
    static ScenarioClockConfig fromJson(const nlohmann::json& j) {
        if (!j.is_object()) throw std::invalid_argument("simulation configuration must be an object");
        ScenarioClockConfig c;
        c.extra=j; c.frame_dt=j.value("frame_dt",c.frame_dt); c.physics_step=j.value("physics_step",c.physics_step);
        c.history_seconds=j.value("history_seconds",c.history_seconds); c.epoch_utc=j.value("epoch_utc",c.epoch_utc);
        c.validate(); return c;
    }
    nlohmann::json toJson() const {
        validate(); auto j=extra;
        j["frame_dt"]=frame_dt; j["physics_step"]=physics_step; j["history_seconds"]=history_seconds; j["epoch_utc"]=epoch_utc;
        return j;
    }
};
} // namespace vsgraysim
