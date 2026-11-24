/// @file AircraftMotion.cpp
/// @brief 空中目标 (飞机/无人机) 运动模型实现。

#include "AircraftMotion.h"
#include <simcore/Orientation.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vsgraysim::platform {

static double deg2rad(double deg) { return deg * M_PI / 180.0; }
static double rad2deg(double rad) { return rad * 180.0 / M_PI; }
static double clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
static double normalizeAngle(double a) {
    a = fmod(a, 360.0);
    return a < 0 ? a + 360.0 : a;
}

// =============================================================================
bool AircraftMotion::initialize(const nlohmann::json& config,
                                 const MotionState& initial)
{
    initial_state_ = initial;
    params_ = AircraftParams{};
    for (const auto* key : {"max_speed","min_speed","cruise_speed","max_accel","max_decel","max_load_factor","max_yaw_rate","max_pitch_rate","max_roll_rate","max_roll_angle","max_altitude","min_altitude","climb_rate_max","sink_rate_max","terrain_clearance","hold_altitude"})
        if (config.contains(key) && (!config[key].is_number() || !std::isfinite(config[key].get<double>())))
            throw std::invalid_argument(std::string("invalid aircraft parameter: ")+key);
    for (const auto* key : {"terrain_following","altitude_hold","flight_control_enabled"})
        if(config.contains(key) && !config[key].is_boolean()) throw std::invalid_argument(std::string("invalid aircraft flag: ")+key);

    if (config.contains("max_speed"))         params_.max_speed         = config["max_speed"];
    if (config.contains("min_speed"))         params_.min_speed         = config["min_speed"];
    if (config.contains("cruise_speed"))      params_.cruise_speed      = config["cruise_speed"];
    if (config.contains("max_accel"))         params_.max_accel         = config["max_accel"];
    if (config.contains("max_decel"))         params_.max_decel         = config["max_decel"];
    if (config.contains("max_load_factor"))   params_.max_load_factor   = config["max_load_factor"];
    if (config.contains("max_yaw_rate"))      params_.max_yaw_rate      = config["max_yaw_rate"];
    if (config.contains("max_pitch_rate"))    params_.max_pitch_rate    = config["max_pitch_rate"];
    if (config.contains("max_roll_rate"))     params_.max_roll_rate     = config["max_roll_rate"];
    if (config.contains("max_roll_angle"))    params_.max_roll_angle    = config["max_roll_angle"];
    if (config.contains("max_altitude"))      params_.max_altitude      = config["max_altitude"];
    if (config.contains("min_altitude"))      params_.min_altitude      = config["min_altitude"];
    if (config.contains("climb_rate_max"))    params_.climb_rate_max    = config["climb_rate_max"];
    if (config.contains("sink_rate_max"))     params_.sink_rate_max     = config["sink_rate_max"];
    if (config.contains("terrain_following")) params_.terrain_following = config["terrain_following"];
    if (config.contains("terrain_clearance")) params_.terrain_clearance = config["terrain_clearance"];
    if (config.contains("altitude_hold"))     params_.altitude_hold     = config["altitude_hold"];
    if (config.contains("hold_altitude"))     params_.hold_altitude     = config["hold_altitude"];
    if (!(params_.min_speed>0 && params_.max_speed<=2000 && params_.min_speed<=params_.cruise_speed && params_.cruise_speed<=params_.max_speed) ||
        !(params_.min_altitude>=-100000 && params_.max_altitude<=1e6 && params_.min_altitude<params_.max_altitude) ||
        !(params_.max_accel>0 && params_.max_decel>0 && params_.max_load_factor>=1 && params_.max_roll_angle>0 && params_.max_roll_angle<=89) ||
        !(params_.max_pitch_rate>0 && params_.max_pitch_rate<=360 && params_.max_roll_rate>0 && params_.max_roll_rate<=360 && params_.max_yaw_rate>0 && params_.max_yaw_rate<=360) ||
        !(params_.climb_rate_max>0 && params_.sink_rate_max>0 && params_.terrain_clearance>=0))
        throw std::invalid_argument("inconsistent aircraft flight envelope");
    throttle_=clamp((vsg::length(initial.velocity)-params_.min_speed)/std::max(.001,params_.max_speed-params_.min_speed),0.,1.);
    elevator_=aileron_=rudder_=0;control_inputs_valid_=false;
    return true;
}

// =============================================================================
MotionState& AircraftMotion::update(double dt,
                                     const EnvironmentContext& env,
                                     const ControlCommand& cmd,
                                     MotionState& state)
{
    if(!std::isfinite(dt) || dt<=0) return state;
    if(!cmd.flight_mode.empty()) return updateFlight(dt,cmd,state);
    control_inputs_valid_=false;
    // 1. 高度保持 / 地形跟随控制 (修正控制指令)
    ControlCommand local_cmd = cmd;
    applyAltitudeControl(dt, env, local_cmd, state);

    // 2. 速度控制
    double target_speed = local_cmd.auto_control ? params_.cruise_speed : local_cmd.desired_speed;
    target_speed = clamp(target_speed, params_.min_speed, params_.max_speed);

    double current_speed = vsg::length(state.velocity);
    double speed_diff = target_speed - current_speed;
    double max_speed_change = (speed_diff >= 0 ? params_.max_accel : params_.max_decel) * dt;
    double new_speed = current_speed + clamp(speed_diff, -max_speed_change, max_speed_change);

    // 3. 姿态控制
    double yaw_rate = clamp(local_cmd.desired_yaw_rate, -params_.max_yaw_rate, params_.max_yaw_rate);
    double pitch_rate = clamp(local_cmd.desired_pitch_rate, -params_.max_pitch_rate, params_.max_pitch_rate);
    double target_roll = clamp(local_cmd.desired_roll, -params_.max_roll_angle, params_.max_roll_angle);

    // 滚转与偏航联动 (协调转弯)
    if (std::abs(yaw_rate) > 0.1) {
        // 协调转弯: roll = atan(yaw_rate * speed / g)
        double coordinated_roll = rad2deg(std::atan2(deg2rad(yaw_rate) * new_speed, 9.81));
        target_roll = clamp(coordinated_roll, -params_.max_roll_angle, params_.max_roll_angle);
    }

    // 平滑滚转
    double roll_diff = target_roll - state.roll;
    double max_roll_change = params_.max_roll_rate * dt;
    state.roll += clamp(roll_diff, -max_roll_change, max_roll_change);

    // 4. 更新姿态角
    state.yaw += yaw_rate * dt;
    state.yaw = normalizeAngle(state.yaw);
    state.pitch += pitch_rate * dt;
    state.pitch = clamp(state.pitch, -60.0, 60.0); // 俯仰角限制

    // 5. 计算速度方向
    double yaw_rad = deg2rad(state.yaw);
    double pitch_rad = deg2rad(state.pitch);

    // 速度向量: 先从机体坐标系转出
    vsg::dvec3 forward = {
        std::sin(yaw_rad) * std::cos(pitch_rad),
        std::sin(pitch_rad),
        -std::cos(yaw_rad) * std::cos(pitch_rad)
    };

    state.velocity = forward * new_speed;

    // 6. 更新位置
    state.position += state.velocity * dt;

    // 7. 过载限制
    applyLoadFactorLimit(state, local_cmd);

    // 8. 飞行包线检查
    enforceFlightEnvelope(state);

    // 更新四元数
    state.rotation = simcore::headingRotation(state.yaw,state.pitch,state.roll);

    state.accel = (state.velocity - initial_state_.velocity) / std::max(dt, 1e-6);
    initial_state_.velocity = state.velocity;

    return state;
}


// A normalized, rate-limited point-mass control model. These inputs are not
// calibrated airframe deflections or a six-DOF aerodynamic force model.
nlohmann::json AircraftMotion::controlInputs() const {
    if(!control_inputs_valid_)return nlohmann::json::object();
    return {{"throttle",throttle_},{"elevator",elevator_},{"aileron",aileron_},{"rudder",rudder_}};
}
MotionState& AircraftMotion::updateFlight(double dt,const ControlCommand& cmd,MotionState& state) {
    control_inputs_valid_=true;
    const auto previous=state.velocity;
    const double speed=vsg::length(previous);
    double throttle=cmd.throttle,elevator=cmd.elevator,aileron=cmd.aileron,rudder=cmd.rudder;
    if(cmd.flight_mode!="manual") {
        const double target=clamp(cmd.desired_speed,params_.min_speed,params_.max_speed);
        throttle=clamp((target-params_.min_speed)/std::max(.001,params_.max_speed-params_.min_speed),0.,1.);
        const double climb=clamp((cmd.desired_altitude-state.position.y)*.3,-params_.sink_rate_max,params_.climb_rate_max);
        const double pitch=rad2deg(std::atan2(climb,std::max(speed,params_.min_speed)));
        elevator=clamp((pitch-state.pitch)*2/params_.max_pitch_rate,-1.,1.);
        double heading_error=std::remainder(cmd.target_heading-state.yaw,360.);
        const double turn=clamp(heading_error*.4,-params_.max_yaw_rate,params_.max_yaw_rate);
        const double bank=clamp(rad2deg(std::atan2(deg2rad(turn)*std::max(speed,params_.min_speed),9.81)),-params_.max_roll_angle,params_.max_roll_angle);
        aileron=clamp((bank-state.roll)*2/params_.max_roll_rate,-1.,1.); rudder=0;
    }
    auto approach=[&](double& actual,double demand,double rate){actual+=clamp(demand-actual,-rate*dt,rate*dt);};
    approach(throttle_,clamp(throttle,0.,1.),.7);
    approach(elevator_,clamp(elevator,-1.,1.),2.);
    approach(aileron_,clamp(aileron,-1.,1.),2.);
    approach(rudder_,clamp(rudder,-1.,1.),2.);
    const double target_speed=params_.min_speed+throttle_*(params_.max_speed-params_.min_speed);
    const double acceleration=clamp((target_speed-speed)/1.5,-params_.max_decel,params_.max_accel);
    const double new_speed=clamp(speed+acceleration*dt,params_.min_speed,params_.max_speed);
    const double load_bank=rad2deg(std::acos(1./params_.max_load_factor));
    const double max_bank=std::min(params_.max_roll_angle,load_bank);
    const double previous_roll=state.roll, previous_pitch=state.pitch;
    state.roll=clamp(state.roll+aileron_*params_.max_roll_rate*dt,-max_bank,max_bank);
    state.pitch=clamp(state.pitch+elevator_*params_.max_pitch_rate*dt,-60.,60.);
    // Climb/sink limits constrain the flight-path angle without teleporting.
    state.pitch=clamp(state.pitch,-rad2deg(std::asin(std::min(1.,params_.sink_rate_max/new_speed))),rad2deg(std::asin(std::min(1.,params_.climb_rate_max/new_speed))));
    const double turn=rad2deg(9.81*std::tan(deg2rad(state.roll))/std::max(new_speed,1.));
    state.yaw_rate=clamp(turn+rudder_*params_.max_yaw_rate*.35,-params_.max_yaw_rate,params_.max_yaw_rate);
    state.yaw=normalizeAngle(state.yaw+state.yaw_rate*dt);
    state.pitch_rate=(state.pitch-previous_pitch)/dt;state.roll_rate=(state.roll-previous_roll)/dt;
    const double yaw=deg2rad(state.yaw),pitch=deg2rad(state.pitch);
    state.velocity=vsg::dvec3{std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch)}*new_speed;
    if((state.position.y<=params_.min_altitude && state.velocity.y<0)||(state.position.y>=params_.max_altitude && state.velocity.y>0))state.velocity.y=0;
    state.position+=state.velocity*dt;
    state.rotation=simcore::headingRotation(state.yaw,state.pitch,state.roll);
    state.accel=(state.velocity-previous)/dt;
    return state;
}

// =============================================================================
void AircraftMotion::applyLoadFactorLimit(MotionState& state,
                                           const ControlCommand& cmd) const
{
    // 计算当前过载 (加速度 / g)
    double speed = vsg::length(state.velocity);
    if (speed < 1.0) return;

    double lateral_accel = vsg::length(state.accel);
    double load_factor = lateral_accel / 9.81;

    if (load_factor > params_.max_load_factor) {
        // 限制过载: 减速
        double scale = params_.max_load_factor / load_factor;
        state.velocity *= scale;
        state.accel *= scale;
    }
}

// =============================================================================
void AircraftMotion::enforceFlightEnvelope(MotionState& state) const
{
    // 高度限制
    state.position.y = clamp(state.position.y, params_.min_altitude, params_.max_altitude);

    // 速度限制
    double speed = vsg::length(state.velocity);
    speed = clamp(speed, params_.min_speed, params_.max_speed);
    double dir_len = vsg::length(state.velocity);
    if (dir_len > 1e-10) {
        state.velocity = (state.velocity / dir_len) * speed;
    }
}

// =============================================================================
void AircraftMotion::applyAltitudeControl(double dt, const EnvironmentContext& env,
                                           ControlCommand& cmd,
                                           const MotionState& state) const
{
    double target_alt = 0.0;

    if (params_.terrain_following) {
        // 地形跟随
        target_alt = env.terrain_height + params_.terrain_clearance;
    } else if (params_.altitude_hold) {
        // 高度保持
        target_alt = params_.hold_altitude;
    } else {
        return; // 不介入
    }

    double alt_error = target_alt - state.position.y;
    double pitch_cmd = clamp(alt_error * 0.05, -20.0, 20.0);
    cmd.desired_pitch_rate = (pitch_cmd - state.pitch) / std::max(dt, 0.01);
    cmd.desired_pitch_rate = clamp(cmd.desired_pitch_rate, -params_.max_pitch_rate, params_.max_pitch_rate);
}

// =============================================================================
nlohmann::json AircraftMotion::getParams() const {
    return {
        {"max_speed",         params_.max_speed},
        {"min_speed",         params_.min_speed},
        {"cruise_speed",      params_.cruise_speed},
        {"max_accel",         params_.max_accel},
        {"max_load_factor",   params_.max_load_factor},
        {"max_yaw_rate",      params_.max_yaw_rate},
        {"max_pitch_rate",    params_.max_pitch_rate},
        {"max_roll_rate",     params_.max_roll_rate},
        {"max_roll_angle",    params_.max_roll_angle},
        {"max_altitude",      params_.max_altitude},
        {"min_altitude",      params_.min_altitude},
        {"climb_rate_max",    params_.climb_rate_max},
        {"terrain_following", params_.terrain_following},
        {"terrain_clearance", params_.terrain_clearance},
        {"altitude_hold",     params_.altitude_hold},
        {"hold_altitude",     params_.hold_altitude}
    };
}

void AircraftMotion::setParams(const nlohmann::json& params) {
    if (params.contains("max_speed"))         params_.max_speed         = params["max_speed"];
    if (params.contains("min_speed"))         params_.min_speed         = params["min_speed"];
    if (params.contains("cruise_speed"))      params_.cruise_speed      = params["cruise_speed"];
    if (params.contains("max_load_factor"))   params_.max_load_factor   = params["max_load_factor"];
    if (params.contains("max_yaw_rate"))      params_.max_yaw_rate      = params["max_yaw_rate"];
    if (params.contains("terrain_following")) params_.terrain_following = params["terrain_following"];
    if (params.contains("altitude_hold"))     params_.altitude_hold     = params["altitude_hold"];
}

void AircraftMotion::reset() {
    // 重置到初始状态
}

} // namespace vsgraysim::platform