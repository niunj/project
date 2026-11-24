/// @file GroundVehicleMotion.cpp
/// @brief 地面目标 (坦克/装甲车) 运动模型实现。

#include "GroundVehicleMotion.h"
#include <simcore/Orientation.h>
#include <algorithm>
#include <cmath>

namespace vsgraysim::platform {

// =============================================================================
// 工具函数
// =============================================================================
static double deg2rad(double deg) { return deg * M_PI / 180.0; }
static double rad2deg(double rad) { return rad * 180.0 / M_PI; }
static double clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
static vsg::dvec3 normalizeOrZero(const vsg::dvec3& v) {
    double len = vsg::length(v);
    return len > 1e-10 ? v / len : vsg::dvec3{0, 0, 0};
}

// =============================================================================
bool GroundVehicleMotion::initialize(const nlohmann::json& config,
                                      const MotionState& initial)
{
    initial_state_ = initial;

    if (config.contains("max_speed_fwd"))   params_.max_speed_fwd   = config["max_speed_fwd"];
    if (config.contains("max_speed_rev"))   params_.max_speed_rev   = config["max_speed_rev"];
    if (config.contains("max_accel"))       params_.max_accel       = config["max_accel"];
    if (config.contains("max_brake"))       params_.max_brake       = config["max_brake"];
    if (config.contains("max_turn_rate"))   params_.max_turn_rate   = config["max_turn_rate"];
    if (config.contains("turn_radius_min")) params_.turn_radius_min = config["turn_radius_min"];
    if (config.contains("max_climb_angle")) params_.max_climb_angle = config["max_climb_angle"];
    if (config.contains("max_side_slope"))  params_.max_side_slope  = config["max_side_slope"];
    if (config.contains("tracked"))         params_.tracked         = config["tracked"];
    if (config.contains("wheel_base"))      params_.wheel_base      = config["wheel_base"];
    if (config.contains("max_steer_angle")) params_.max_steer_angle = config["max_steer_angle"];
    if (config.contains("track_width"))     params_.track_width     = config["track_width"];
    return true;
}

// =============================================================================
MotionState& GroundVehicleMotion::update(double dt,
                                          const EnvironmentContext& env,
                                          const ControlCommand& cmd,
                                          MotionState& state)
{
    // 1. 速度控制 (加速/制动)
    double target_speed = cmd.desired_speed;
    target_speed = clamp(target_speed, -params_.max_speed_rev, params_.max_speed_fwd);

    double current_speed = vsg::length(state.velocity);
    double speed_sign = 1.0;
    if (current_speed > 0.01) {
        speed_sign = (vsg::dot(state.velocity, state.rotation * vsg::dvec3{0, 0, -1}) >= 0) ? 1.0 : -1.0;
    }
    double signed_speed = current_speed * speed_sign;

    double speed_diff = target_speed - signed_speed;
    double max_change = (speed_diff >= 0 ? params_.max_accel : params_.max_brake) * dt;
    double new_speed = signed_speed + clamp(speed_diff, -max_change, max_change);
    new_speed = clamp(new_speed, -params_.max_speed_rev, params_.max_speed_fwd);

    // 爬坡速度限制
    double slope_angle = std::acos(clamp(vsg::dot(env.terrain_normal, vsg::dvec3{0, 1, 0}), -1.0, 1.0));
    slope_angle = rad2deg(slope_angle);
    if (new_speed > 0 && slope_angle > params_.max_climb_angle) {
        new_speed = 0; // 爬不上去
    } else if (new_speed > 0 && slope_angle > params_.max_climb_angle * 0.7) {
        // 接近极限坡度时减速
        double factor = 1.0 - (slope_angle - params_.max_climb_angle * 0.7) / (params_.max_climb_angle * 0.3);
        new_speed *= clamp(factor, 0.0, 1.0);
    }

    // 2. 转向控制
    double yaw_rate = 0.0;
    if (std::abs(new_speed) > 0.1) {
        if (params_.tracked) {
            yaw_rate = trackedTurnRate(cmd.desired_yaw_rate, new_speed);
        } else {
            yaw_rate = wheeledTurnRate(cmd.desired_yaw_rate, new_speed);
        }
    }

    // 3. 更新航向
    state.yaw += yaw_rate * dt;
    state.yaw = fmod(state.yaw, 360.0);
    if (state.yaw < 0) state.yaw += 360.0;

    // 4. 地形跟随: 计算前进方向的速度向量
    double yaw_rad = deg2rad(state.yaw);
    vsg::dvec3 forward = {std::sin(yaw_rad), 0, -std::cos(yaw_rad)};

    // 在地形切平面上投影
    vsg::dvec3 proj_forward = forward - vsg::dot(forward, env.terrain_normal) * env.terrain_normal;
    proj_forward = normalizeOrZero(proj_forward);

    state.velocity = proj_forward * new_speed;

    // 5. 更新位置
    state.position += state.velocity * dt;

    // 地形跟随: 保持在地形表面上
    state.position.y = env.terrain_height + params_.suspension_travel;

    // 6. 更新姿态 (俯仰/滚转跟随地形)
    state.pitch = -rad2deg(std::asin(clamp(vsg::dot(forward, env.terrain_normal), -1.0, 1.0)));
    state.roll = rad2deg(std::asin(clamp(vsg::dot(proj_forward, vsg::dvec3{0, 1, 0}), -1.0, 1.0)));

    // 限制侧倾角
    state.roll = clamp(state.roll, -params_.max_side_slope, params_.max_side_slope);

    // 更新四元数
    state.rotation = simcore::headingRotation(state.yaw,state.pitch,state.roll);

    state.accel = (state.velocity - initial_state_.velocity) / std::max(dt, 1e-6);
    initial_state_.velocity = state.velocity;

    return state;
}

// =============================================================================
nlohmann::json GroundVehicleMotion::getParams() const {
    return {
        {"max_speed_fwd",   params_.max_speed_fwd},
        {"max_speed_rev",   params_.max_speed_rev},
        {"max_accel",       params_.max_accel},
        {"max_brake",       params_.max_brake},
        {"max_turn_rate",   params_.max_turn_rate},
        {"turn_radius_min", params_.turn_radius_min},
        {"max_climb_angle", params_.max_climb_angle},
        {"max_side_slope",  params_.max_side_slope},
        {"tracked",         params_.tracked},
        {"wheel_base",      params_.wheel_base},
        {"max_steer_angle", params_.max_steer_angle},
        {"track_width",     params_.track_width}
    };
}

void GroundVehicleMotion::setParams(const nlohmann::json& params) {
    if (params.contains("max_speed_fwd"))   params_.max_speed_fwd   = params["max_speed_fwd"];
    if (params.contains("max_speed_rev"))   params_.max_speed_rev   = params["max_speed_rev"];
    if (params.contains("max_accel"))       params_.max_accel       = params["max_accel"];
    if (params.contains("max_turn_rate"))   params_.max_turn_rate   = params["max_turn_rate"];
    if (params.contains("turn_radius_min")) params_.turn_radius_min = params["turn_radius_min"];
    if (params.contains("max_climb_angle")) params_.max_climb_angle = params["max_climb_angle"];
    if (params.contains("tracked"))         params_.tracked         = params["tracked"];
}

void GroundVehicleMotion::reset() {
    // 重置到初始状态
}

// =============================================================================
// 履带式转向
double GroundVehicleMotion::trackedTurnRate(double cmd_yaw_rate, double speed) const {
    // 履带差速转向: 速度越快转向越受限
    double max_turn = params_.max_turn_rate;
    if (speed > params_.max_speed_fwd * 0.5) {
        double factor = 1.0 - (speed - params_.max_speed_fwd * 0.5) / (params_.max_speed_fwd * 0.5);
        max_turn *= clamp(factor, 0.3, 1.0);
    }
    return clamp(cmd_yaw_rate, -max_turn, max_turn);
}

// =============================================================================
// 轮式转向 (阿克曼几何)
double GroundVehicleMotion::wheeledTurnRate(double cmd_yaw_rate, double speed) const {
    // 轮式: 转弯半径 = wheel_base / tan(steer_angle)
    double steer_angle_rad = deg2rad(params_.max_steer_angle);
    double min_radius = params_.wheel_base / std::tan(steer_angle_rad);
    double max_yaw_rate = rad2deg(speed / min_radius);
    max_yaw_rate = clamp(max_yaw_rate, 0.0, params_.max_turn_rate);
    return clamp(cmd_yaw_rate, -max_yaw_rate, max_yaw_rate);
}

} // namespace vsgraysim::platform