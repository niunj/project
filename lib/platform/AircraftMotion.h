#pragma once
/// @file AircraftMotion.h
/// @brief 空中目标 (飞机/无人机) 运动模型。
///
/// 特性:
///   - 6自由度运动 (简化质点+姿态)
///   - 飞行包线约束 (速度/高度范围)
///   - 机动过载限制
///   - 高度保持 / 地形跟随

#include "IPlatformMotion.h"

namespace vsgraysim::platform {

/// 飞行器运动参数
struct AircraftParams {
    double max_speed           = 300.0;  // 最大速度 (m/s, ~M0.88)
    double min_speed           = 50.0;   // 最小速度 (m/s, 失速速度)
    double cruise_speed        = 200.0;  // 巡航速度 (m/s)
    double max_accel           = 10.0;   // 最大加速度 (m/s^2)
    double max_decel           = 15.0;   // 最大减速 (m/s^2)
    double max_load_factor     = 9.0;    // 最大过载 (G)
    double max_yaw_rate        = 30.0;   // 最大偏航角速度 (度/秒)
    double max_pitch_rate      = 20.0;   // 最大俯仰角速度 (度/秒)
    double max_roll_rate       = 60.0;   // 最大滚转角速度 (度/秒)
    double max_roll_angle      = 60.0;   // 最大滚转角 (度)

    double max_altitude        = 15000.0; // 最大飞行高度 (m)
    double min_altitude        = 10.0;    // 最低飞行高度 (m, 地形跟随)
    double climb_rate_max      = 50.0;    // 最大爬升率 (m/s)
    double sink_rate_max       = 30.0;    // 最大下降率 (m/s)

    bool   terrain_following   = false;   // 是否启用地形跟随
    double terrain_clearance   = 50.0;    // 地形跟随离地高度 (m)
    bool   altitude_hold       = false;   // 高度保持模式
    double hold_altitude       = 1000.0;  // 保持高度 (m)
};

class AircraftMotion : public IPlatformMotion {
public:
    AircraftMotion() = default;
    ~AircraftMotion() override = default;

    PlatformCategory category() const override { return PlatformCategory::AIRCRAFT; }

    bool initialize(const nlohmann::json& config,
                    const MotionState& initial) override;

    MotionState& update(double dt,
                        const EnvironmentContext& env,
                        const ControlCommand& cmd,
                        MotionState& state) override;

    nlohmann::json getParams() const override;
    void setParams(const nlohmann::json& params) override;
    void reset() override;

    const AircraftParams& params() const { return params_; }
    AircraftParams& mutableParams() { return params_; }
    nlohmann::json controlInputs() const;

private:
    MotionState& updateFlight(double dt, const ControlCommand&, MotionState&);
    // 限制过载
    void applyLoadFactorLimit(MotionState& state, const ControlCommand& cmd) const;

    // 飞行包线检查
    void enforceFlightEnvelope(MotionState& state) const;

    // 高度保持/地形跟随
    void applyAltitudeControl(double dt, const EnvironmentContext& env,
                              ControlCommand& cmd, const MotionState& state) const;

    AircraftParams params_;
    MotionState initial_state_;
    double throttle_=.5, elevator_=0, aileron_=0, rudder_=0;
    bool control_inputs_valid_=false;
};

} // namespace vsgraysim::platform
