/// @file MissileMotion.cpp
/// @brief 导弹运动模型实现 (比例导引/追踪法/末端机动)。

#include "MissileMotion.h"
#include <simcore/Orientation.h>
#include <algorithm>
#include <cmath>

namespace vsgraysim::platform {

static double deg2rad(double deg) { return deg * M_PI / 180.0; }
static double rad2deg(double rad) { return rad * 180.0 / M_PI; }
static double clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
static vsg::dvec3 normalizeOrZero(const vsg::dvec3& v) {
    double len = vsg::length(v);
    return len > 1e-10 ? v / len : vsg::dvec3{0, 0, 0};
}

static double normalizeAngle(double a) {
    a = fmod(a, 360.0);
    return a < 0 ? a + 360.0 : a;
}

// =============================================================================
bool MissileMotion::initialize(const nlohmann::json& config,
                                const MotionState& initial)
{
    initial_state_ = initial;
    flight_time_ = 0.0;
    hit_target_ = false;
    armed_ = false;

    if (config.contains("max_speed"))           params_.max_speed           = config["max_speed"];
    if (config.contains("min_speed"))           params_.min_speed           = config["min_speed"];
    if (config.contains("boost_accel"))         params_.boost_accel         = config["boost_accel"];
    if (config.contains("sustain_accel"))       params_.sustain_accel       = config["sustain_accel"];
    if (config.contains("max_lateral_accel"))   params_.max_lateral_accel   = config["max_lateral_accel"];
    if (config.contains("max_yaw_rate"))        params_.max_yaw_rate        = config["max_yaw_rate"];
    if (config.contains("max_pitch_rate"))      params_.max_pitch_rate      = config["max_pitch_rate"];
    if (config.contains("guidance_law"))        params_.guidance_law        = static_cast<GuidanceLaw>(config["guidance_law"].get<int>());
    if (config.contains("nav_constant"))        params_.nav_constant        = config["nav_constant"];
    if (config.contains("seeker_range"))        params_.seeker_range        = config["seeker_range"];
    if (config.contains("boost_duration"))      params_.boost_duration      = config["boost_duration"];
    if (config.contains("sustain_duration"))    params_.sustain_duration    = config["sustain_duration"];
    if (config.contains("max_flight_time"))     params_.max_flight_time     = config["max_flight_time"];
    if (config.contains("enable_terminal_maneuver")) params_.enable_terminal_maneuver = config["enable_terminal_maneuver"];
    if (config.contains("proximity_fuze_range")) params_.proximity_fuze_range = config["proximity_fuze_range"];
    return true;
}

// =============================================================================
MotionState& MissileMotion::update(double dt,
                                    const EnvironmentContext& env,
                                    const ControlCommand& cmd,
                                    MotionState& state)
{
    flight_time_ += dt;

    // 1. 引信解除保险
    if (flight_time_ >= params_.fuze_arming_delay) {
        armed_ = true;
    }

    // 2. 速度控制 (助推/续航)
    double target_speed = params_.min_speed;
    if (flight_time_ <= params_.boost_duration) {
        // 助推段
        target_speed = params_.min_speed + params_.boost_accel * flight_time_;
    } else if (flight_time_ <= params_.boost_duration + params_.sustain_duration) {
        // 续航段
        double sustain_time = flight_time_ - params_.boost_duration;
        double boost_end_speed = params_.min_speed + params_.boost_accel * params_.boost_duration;
        target_speed = boost_end_speed + params_.sustain_accel * sustain_time;
    } else {
        // 滑翔段 (减速)
        target_speed = params_.max_speed * 0.7;
    }
    target_speed = clamp(target_speed, params_.min_speed, params_.max_speed);

    double current_speed = vsg::length(state.velocity);
    double speed_diff = target_speed - current_speed;
    double max_accel_this_frame = params_.boost_accel * dt;
    double new_speed = current_speed + clamp(speed_diff, -max_accel_this_frame, max_accel_this_frame);
    new_speed = clamp(new_speed, params_.min_speed, params_.max_speed);

    // 3. 制导计算 (如果有目标)
    vsg::dvec3 guidance_accel = {0, 0, 0};
    distance_to_target_ = 0.0;

    if (cmd.has_target) {
        vsg::dvec3 target_pos = cmd.target_position;
        vsg::dvec3 target_vel = cmd.auto_control ? vsg::dvec3{0, 0, 0} : vsg::dvec3{0, 0, 0};

        distance_to_target_ = vsg::length(target_pos - state.position);

        // 检查目标是否在导引头视场内
        bool in_fov = targetInSeekerFOV(state.position, state.velocity, target_pos);

        if (in_fov && distance_to_target_ <= params_.seeker_range) {
            switch (params_.guidance_law) {
                case GuidanceLaw::PURSUIT:
                    guidance_accel = pursuitGuidance(state.position, target_pos);
                    break;
                case GuidanceLaw::PROPORTIONAL:
                    guidance_accel = proportionalNavigation(state.position, state.velocity,
                                                            target_pos, target_vel);
                    break;
                case GuidanceLaw::AUGMENTED_PN:
                    guidance_accel = augmentedPN(state.position, state.velocity,
                                                  target_pos, target_vel, {0, 0, 0});
                    break;
                case GuidanceLaw::TERMINAL_MANEUVER:
                    guidance_accel = terminalManeuver(dt, distance_to_target_);
                    break;
                default:
                    guidance_accel = proportionalNavigation(state.position, state.velocity,
                                                            target_pos, target_vel);
                    break;
            }
        }

        // 末端机动 (叠加在制导指令上)
        if (params_.enable_terminal_maneuver &&
            distance_to_target_ <= params_.terminal_maneuver_distance) {
            guidance_accel += terminalManeuver(dt, distance_to_target_);
        }

        // 限制横向过载
        double lateral_mag = vsg::length(guidance_accel);
        if (lateral_mag > params_.max_lateral_accel) {
            guidance_accel = (guidance_accel / lateral_mag) * params_.max_lateral_accel;
        }

        // 引信检测
        if (armed_ && checkFuze(state.position, target_pos)) {
            hit_target_ = true;
        }
    }

    // 4. 完整性检查
    if (flight_time_ >= params_.max_flight_time) {
        // 自毁
        state.velocity = {0, 0, 0};
        return state;
    }

    if (hit_target_) {
        state.velocity = {0, 0, 0};
        return state;
    }

    // 5. 更新速度方向 (制导加速度 + 当前速度)
    vsg::dvec3 velocity_dir = normalizeOrZero(state.velocity);
    if (vsg::length(guidance_accel) > 0.01) {
        vsg::dvec3 new_dir = normalizeOrZero(state.velocity + guidance_accel * dt);
        if (vsg::length(new_dir) > 0.5) {
            velocity_dir = new_dir;
        }
    }

    state.velocity = velocity_dir * new_speed;

    // 6. 更新位置
    state.position += state.velocity * dt;

    // 7. 更新姿态角 (从速度方向推导)
    double yaw_rad = std::atan2(state.velocity.x, -state.velocity.z);
    double pitch_rad = std::asin(clamp(state.velocity.y / std::max(new_speed, 1.0), -1.0, 1.0));

    state.yaw = normalizeAngle(rad2deg(yaw_rad));
    state.pitch = rad2deg(pitch_rad);
    state.roll = 0.0; // 导弹通常不关心滚转

    // 更新四元数
    state.rotation = simcore::headingRotation(state.yaw,state.pitch,state.roll);

    state.accel = (state.velocity - initial_state_.velocity) / std::max(dt, 1e-6);
    initial_state_.velocity = state.velocity;

    return state;
}

// =============================================================================
// 比例导引律
vsg::dvec3 MissileMotion::proportionalNavigation(
    const vsg::dvec3& pos, const vsg::dvec3& vel,
    const vsg::dvec3& target_pos, const vsg::dvec3& target_vel) const
{
    vsg::dvec3 los = target_pos - pos;
    double R = vsg::length(los);
    if (R < 1.0) return {0, 0, 0};

    vsg::dvec3 los_unit = los / R;

    // 视线角速度 = (r × v) / r^2
    vsg::dvec3 rel_vel = vel - target_vel;
    vsg::dvec3 los_rate = vsg::cross(los, rel_vel) / (R * R);

    // 比例导引: a_cmd = N * Vc * (los_unit × los_rate)
    double Vc = -vsg::dot(rel_vel, los_unit); // 接近速度
    Vc = std::max(Vc, 1.0);

    vsg::dvec3 a_cmd = params_.nav_constant * Vc * vsg::cross(los_unit, los_rate);

    // 限制
    double a_mag = vsg::length(a_cmd);
    if (a_mag > params_.max_lateral_accel) {
        a_cmd = (a_cmd / a_mag) * params_.max_lateral_accel;
    }

    return a_cmd;
}

// =============================================================================
// 增广比例导引
vsg::dvec3 MissileMotion::augmentedPN(
    const vsg::dvec3& pos, const vsg::dvec3& vel,
    const vsg::dvec3& target_pos, const vsg::dvec3& target_vel,
    const vsg::dvec3& target_accel) const
{
    vsg::dvec3 base = proportionalNavigation(pos, vel, target_pos, target_vel);

    // 增广项: (N/2) * a_T
    vsg::dvec3 aug = params_.nav_constant * 0.5 * target_accel;

    vsg::dvec3 result = base + aug;
    double a_mag = vsg::length(result);
    if (a_mag > params_.max_lateral_accel) {
        result = (result / a_mag) * params_.max_lateral_accel;
    }
    return result;
}

// =============================================================================
// 纯追踪法
vsg::dvec3 MissileMotion::pursuitGuidance(
    const vsg::dvec3& pos, const vsg::dvec3& target_pos) const
{
    vsg::dvec3 to_target = target_pos - pos;
    double dist = vsg::length(to_target);
    if (dist < 1.0) return {0, 0, 0};

    // 速度指向目标
    vsg::dvec3 desired_dir = to_target / dist;
    vsg::dvec3 current_dir = normalizeOrZero(initial_state_.velocity);

    // 加速度 = 比例 * (期望方向 - 当前方向) * 速度
    double speed = vsg::length(initial_state_.velocity);
    vsg::dvec3 a_cmd = (desired_dir - current_dir) * speed * 2.0;

    double a_mag = vsg::length(a_cmd);
    if (a_mag > params_.max_lateral_accel) {
        a_cmd = (a_cmd / a_mag) * params_.max_lateral_accel;
    }
    return a_cmd;
}

// =============================================================================
// 末端机动
vsg::dvec3 MissileMotion::terminalManeuver(double dt, double distance_to_target) const
{
    // 蛇形机动: 垂直于视线方向的周期性加速度
    double phase = 2.0 * M_PI * params_.maneuver_frequency * flight_time_;
    double amplitude = params_.maneuver_amplitude;

    // 机动方向: 侧向 (简化为在水平面内左右摆动)
    vsg::dvec3 maneuver = {
        amplitude * std::cos(phase),
        amplitude * 0.3 * std::sin(phase * 0.7),
        amplitude * std::sin(phase)
    };

    return maneuver;
}

// =============================================================================
// 引信检测
bool MissileMotion::checkFuze(const vsg::dvec3& pos,
                               const vsg::dvec3& target_pos) const
{
    double dist = vsg::length(target_pos - pos);

    // 触发引信: 距离 < 1m 视为碰撞
    if (params_.impact_fuze && dist < 1.0) {
        return true;
    }

    // 近炸引信: 距离 < 近炸半径
    if (params_.proximity_fuze && dist < params_.proximity_fuze_range) {
        return true;
    }

    return false;
}

// =============================================================================
// 导引头视场检查
bool MissileMotion::targetInSeekerFOV(const vsg::dvec3& pos,
                                       const vsg::dvec3& vel,
                                       const vsg::dvec3& target_pos) const
{
    vsg::dvec3 to_target = target_pos - pos;
    double dist = vsg::length(to_target);
    if (dist < 1.0) return true;

    vsg::dvec3 los_dir = to_target / dist;
    vsg::dvec3 body_dir = normalizeOrZero(vel);

    double cos_angle = vsg::dot(los_dir, body_dir);
    double half_fov = deg2rad(params_.seeker_fov * 0.5);

    return cos_angle >= std::cos(half_fov);
}

// =============================================================================
nlohmann::json MissileMotion::getParams() const {
    return {
        {"max_speed",           params_.max_speed},
        {"boost_accel",         params_.boost_accel},
        {"sustain_accel",       params_.sustain_accel},
        {"max_lateral_accel",   params_.max_lateral_accel},
        {"guidance_law",        static_cast<int>(params_.guidance_law)},
        {"nav_constant",        params_.nav_constant},
        {"seeker_range",        params_.seeker_range},
        {"boost_duration",      params_.boost_duration},
        {"sustain_duration",    params_.sustain_duration},
        {"max_flight_time",     params_.max_flight_time},
        {"enable_terminal_maneuver", params_.enable_terminal_maneuver},
        {"proximity_fuze_range", params_.proximity_fuze_range}
    };
}

void MissileMotion::setParams(const nlohmann::json& params) {
    if (params.contains("max_speed"))           params_.max_speed         = params["max_speed"];
    if (params.contains("boost_accel"))         params_.boost_accel       = params["boost_accel"];
    if (params.contains("max_lateral_accel"))   params_.max_lateral_accel = params["max_lateral_accel"];
    if (params.contains("guidance_law"))        params_.guidance_law      = static_cast<GuidanceLaw>(params["guidance_law"].get<int>());
    if (params.contains("nav_constant"))        params_.nav_constant      = params["nav_constant"];
    if (params.contains("enable_terminal_maneuver")) params_.enable_terminal_maneuver = params["enable_terminal_maneuver"];
}

void MissileMotion::reset() {
    flight_time_ = 0.0;
    hit_target_ = false;
    armed_ = false;
    distance_to_target_ = 0.0;
}

} // namespace vsgraysim::platform