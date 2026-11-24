#pragma once
/// @file NavalVesselMotion.h
/// @brief 海面目标 (舰船) 运动模型。
///
/// 特性:
///   - 水平面运动 (2D + 波浪起伏)
///   - 海浪影响 (横摇/纵摇/升沉)
///   - 航向保持 / 转向速率限制

#include "IPlatformMotion.h"

namespace vsgraysim::platform {

/// 舰船运动参数
struct NavalVesselParams {
    double max_speed          = 15.0;   // 最大航速 (m/s, ~30 节)
    double cruise_speed       = 10.0;   // 巡航速度 (m/s)
    double max_accel          = 2.0;    // 最大加速度 (m/s^2)
    double max_decel          = 3.0;    // 最大减速度 (m/s^2)
    double max_turn_rate      = 5.0;    // 最大转向角速度 (度/秒)
    double turn_radius_min    = 200.0;  // 最小转弯半径 (m)
    double heading_hold_gain  = 0.5;    // 航向保持增益

    // 船体参数 (影响海浪响应)
    double hull_length        = 100.0;  // 船长 (m)
    double hull_beam          = 15.0;   // 船宽 (m)
    double hull_draft         = 5.0;    // 吃水 (m)
    double metacentric_height = 1.5;    // 稳心高度 (m)

    // 海浪响应
    bool   enable_wave_response = true; // 是否响应海浪
    double roll_damping       = 0.1;    // 横摇阻尼比
    double pitch_damping      = 0.15;   // 纵摇阻尼比
    double heave_damping      = 0.2;    // 升沉阻尼比
};

class NavalVesselMotion : public IPlatformMotion {
public:
    NavalVesselMotion() = default;
    ~NavalVesselMotion() override = default;

    PlatformCategory category() const override { return PlatformCategory::NAVAL_VESSEL; }

    bool initialize(const nlohmann::json& config,
                    const MotionState& initial) override;

    MotionState& update(double dt,
                        const EnvironmentContext& env,
                        const ControlCommand& cmd,
                        MotionState& state) override;

    nlohmann::json getParams() const override;
    void setParams(const nlohmann::json& params) override;
    void reset() override;

    const NavalVesselParams& params() const { return params_; }
    NavalVesselParams& mutableParams() { return params_; }

private:
    // 航向保持
    double applyHeadingHold(double current_yaw, double desired_yaw_rate,
                            double speed) const;

    // 海浪响应 (横摇/纵摇/升沉)
    void applyWaveResponse(double dt, const EnvironmentContext& env,
                           MotionState& state) const;

    // 安全速度限制 (高速转弯时减速)
    double clampSpeedOnTurn(double speed, double yaw_rate) const;

    NavalVesselParams params_;
    MotionState initial_state_;

    // 海浪响应状态 (用于平滑)
    mutable double wave_roll_ = 0.0;
    mutable double wave_pitch_ = 0.0;
    mutable double wave_heave_ = 0.0;
};

} // namespace vsgraysim::platform