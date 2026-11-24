#pragma once
/// @file GroundVehicleMotion.h
/// @brief 地面目标 (坦克/装甲车) 运动模型。
///
/// 特性:
///   - 地形跟随 (轮式/履带)
///   - 转弯半径约束
///   - 加速/减速限制
///   - 爬坡能力

#include "IPlatformMotion.h"

namespace vsgraysim::platform {

/// 地面车辆运动参数
struct GroundVehicleParams {
    double max_speed_fwd       = 20.0;   // 最大前进速度 (m/s, 约 72 km/h)
    double max_speed_rev       = 8.0;    // 最大倒车速度 (m/s)
    double max_accel           = 5.0;    // 最大加速度 (m/s^2)
    double max_brake           = 8.0;    // 最大制动减速度 (m/s^2)
    double max_turn_rate       = 40.0;   // 最大转向角速度 (度/秒)
    double turn_radius_min     = 5.0;    // 最小转弯半径 (m, 履带式可原地转向)
    double max_climb_angle     = 30.0;   // 最大爬坡角度 (度)
    double max_side_slope      = 20.0;   // 最大侧倾坡度 (度)
    double suspension_travel   = 0.5;    // 悬架行程 (m, 地形跟随用)
    bool   tracked             = true;   // true=履带, false=轮式

    // 轮式特有
    double wheel_base          = 4.0;    // 轴距 (m)
    double max_steer_angle     = 30.0;   // 最大转向角 (度, 轮式)

    // 履带特有
    double track_width         = 3.0;    // 履带中心距 (m, 履带式)
};

class GroundVehicleMotion : public IPlatformMotion {
public:
    GroundVehicleMotion() = default;
    ~GroundVehicleMotion() override = default;

    PlatformCategory category() const override { return PlatformCategory::GROUND_VEHICLE; }

    bool initialize(const nlohmann::json& config,
                    const MotionState& initial) override;

    MotionState& update(double dt,
                        const EnvironmentContext& env,
                        const ControlCommand& cmd,
                        MotionState& state) override;

    nlohmann::json getParams() const override;
    void setParams(const nlohmann::json& params) override;
    void reset() override;

    const GroundVehicleParams& params() const { return params_; }
    GroundVehicleParams& mutableParams() { return params_; }

private:
    GroundVehicleParams params_;
    MotionState initial_state_;

    // 限制爬坡
    double clampSpeedOnSlope(double speed, const vsg::dvec3& terrain_normal) const;

    // 履带式转向 (差速转向)
    double trackedTurnRate(double cmd_yaw_rate, double speed) const;

    // 轮式转向 (阿克曼几何)
    double wheeledTurnRate(double cmd_yaw_rate, double speed) const;
};

} // namespace vsgraysim::platform