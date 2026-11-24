/// @file NavalVesselMotion.cpp
/// @brief 海面目标 (舰船) 运动模型实现。

#include "NavalVesselMotion.h"
#include <simcore/Orientation.h>
#include <algorithm>
#include <cmath>

namespace vsgraysim::platform {

static double deg2rad(double deg) { return deg * M_PI / 180.0; }
static double rad2deg(double rad) { return rad * 180.0 / M_PI; }
static double clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
static double normalizeAngle(double a) {
    a = fmod(a, 360.0);
    return a < 0 ? a + 360.0 : a;
}

// =============================================================================
bool NavalVesselMotion::initialize(const nlohmann::json& config,
                                    const MotionState& initial)
{
    initial_state_ = initial;

    if (config.contains("max_speed"))             params_.max_speed          = config["max_speed"];
    if (config.contains("cruise_speed"))          params_.cruise_speed       = config["cruise_speed"];
    if (config.contains("max_accel"))             params_.max_accel          = config["max_accel"];
    if (config.contains("max_decel"))             params_.max_decel          = config["max_decel"];
    if (config.contains("max_turn_rate"))         params_.max_turn_rate      = config["max_turn_rate"];
    if (config.contains("turn_radius_min"))       params_.turn_radius_min    = config["turn_radius_min"];
    if (config.contains("heading_hold_gain"))     params_.heading_hold_gain  = config["heading_hold_gain"];
    if (config.contains("hull_length"))           params_.hull_length        = config["hull_length"];
    if (config.contains("hull_beam"))             params_.hull_beam          = config["hull_beam"];
    if (config.contains("enable_wave_response"))  params_.enable_wave_response = config["enable_wave_response"];
    return true;
}

// =============================================================================
MotionState& NavalVesselMotion::update(double dt,
                                        const EnvironmentContext& env,
                                        const ControlCommand& cmd,
                                        MotionState& state)
{
    // 1. 速度控制
    double target_speed = cmd.auto_control ? params_.cruise_speed : cmd.desired_speed;
    target_speed = clamp(target_speed, 0.0, params_.max_speed);

    double current_speed = vsg::length(state.velocity);
    double speed_diff = target_speed - current_speed;
    double max_change = (speed_diff >= 0 ? params_.max_accel : params_.max_decel) * dt;
    double new_speed = current_speed + clamp(speed_diff, -max_change, max_change);
    new_speed = clamp(new_speed, 0.0, params_.max_speed);

    // 2. 转向控制
    double yaw_rate = applyHeadingHold(state.yaw, cmd.desired_yaw_rate, current_speed);

    // 高速转弯时减速
    new_speed = clampSpeedOnTurn(new_speed, yaw_rate);

    // 3. 更新航向
    state.yaw += yaw_rate * dt;
    state.yaw = normalizeAngle(state.yaw);

    // 4. 计算速度向量 (水平面)
    double yaw_rad = deg2rad(state.yaw);
    vsg::dvec3 forward = {std::sin(yaw_rad), 0, -std::cos(yaw_rad)};
    state.velocity = forward * new_speed;

    // 5. 更新位置 (水平面)
    state.position.x += state.velocity.x * dt;
    state.position.z += state.velocity.z * dt;

    // 6. 海面跟随
    state.position.y = env.sea_level + env.wave_height;

    // 7. 海浪响应
    if (params_.enable_wave_response) {
        applyWaveResponse(dt, env, state);
    }

    // 更新四元数
    state.rotation = simcore::headingRotation(state.yaw,state.pitch,state.roll);

    state.accel = (state.velocity - initial_state_.velocity) / std::max(dt, 1e-6);
    initial_state_.velocity = state.velocity;

    return state;
}

// =============================================================================
double NavalVesselMotion::applyHeadingHold(double current_yaw,
                                            double desired_yaw_rate,
                                            double speed) const
{
    if (speed < 0.5) return 0.0; // 低速时无法转向

    double max_turn = params_.max_turn_rate;

    // 低速时转向能力下降
    if (speed < params_.cruise_speed * 0.3) {
        max_turn *= speed / (params_.cruise_speed * 0.3);
    }

    return clamp(desired_yaw_rate, -max_turn, max_turn);
}

// =============================================================================
void NavalVesselMotion::applyWaveResponse(double dt, const EnvironmentContext& env,
                                           MotionState& state) const
{
    if (env.wave_height < 0.01) {
        wave_roll_ = 0.0;
        wave_pitch_ = 0.0;
        wave_heave_ = 0.0;
        return;
    }

    // 简化的海浪响应模型
    // 横摇: 共振频率近似于船宽
    double roll_freq = 2.0 * M_PI * std::sqrt(9.81 / params_.hull_beam);
    double roll_excitation = env.wave_height * 0.1; // 波浪激励幅度
    wave_roll_ += (roll_excitation * std::sin(roll_freq * 0.5) - params_.roll_damping * wave_roll_) * dt;
    state.roll = wave_roll_;

    // 纵摇: 船长
    double pitch_freq = 2.0 * M_PI * std::sqrt(9.81 / params_.hull_length);
    double pitch_excitation = env.wave_height * 0.05;
    wave_pitch_ += (pitch_excitation * std::sin(pitch_freq * 0.3) - params_.pitch_damping * wave_pitch_) * dt;
    state.pitch = wave_pitch_;

    // 升沉
    double heave_freq = 2.0 * M_PI * std::sqrt(9.81 / params_.hull_draft);
    double heave_excitation = env.wave_height * 0.3;
    wave_heave_ += (heave_excitation * std::sin(heave_freq * 0.4) - params_.heave_damping * wave_heave_) * dt;
    state.position.y += wave_heave_;
}

// =============================================================================
double NavalVesselMotion::clampSpeedOnTurn(double speed, double yaw_rate) const
{
    if (std::abs(yaw_rate) < 0.1) return speed;

    // 高速转弯时自动减速
    double turn_radius = speed / std::max(deg2rad(std::abs(yaw_rate)), 1e-6);
    if (turn_radius < params_.turn_radius_min) {
        speed *= turn_radius / params_.turn_radius_min;
    }
    return speed;
}

// =============================================================================
nlohmann::json NavalVesselMotion::getParams() const {
    return {
        {"max_speed",             params_.max_speed},
        {"cruise_speed",          params_.cruise_speed},
        {"max_accel",             params_.max_accel},
        {"max_turn_rate",         params_.max_turn_rate},
        {"turn_radius_min",       params_.turn_radius_min},
        {"heading_hold_gain",     params_.heading_hold_gain},
        {"hull_length",           params_.hull_length},
        {"hull_beam",             params_.hull_beam},
        {"enable_wave_response",  params_.enable_wave_response}
    };
}

void NavalVesselMotion::setParams(const nlohmann::json& params) {
    if (params.contains("max_speed"))     params_.max_speed     = params["max_speed"];
    if (params.contains("cruise_speed"))  params_.cruise_speed  = params["cruise_speed"];
    if (params.contains("max_turn_rate")) params_.max_turn_rate = params["max_turn_rate"];
    if (params.contains("turn_radius_min")) params_.turn_radius_min = params["turn_radius_min"];
}

void NavalVesselMotion::reset() {
    wave_roll_ = 0.0;
    wave_pitch_ = 0.0;
    wave_heave_ = 0.0;
}

} // namespace vsgraysim::platform