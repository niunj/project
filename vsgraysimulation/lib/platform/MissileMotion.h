#pragma once
/// @file MissileMotion.h
/// @brief 导弹运动模型。
///
/// 特性:
///   - 比例导引法 / 追踪法
///   - 过载限制
///   - 引信逻辑 (触发/近炸)
///   - 末端机动 (突防)

#include "IPlatformMotion.h"

namespace vsgraysim::platform {

/// 导弹制导方式
enum class GuidanceLaw : uint8_t {
    PURSUIT         = 0,  // 纯追踪法 (速度指向目标)
    PROPORTIONAL   = 1,   // 比例导引法 (经典比例导引)
    AUGMENTED_PN   = 2,   // 增广比例导引 (考虑目标加速度)
    COMMAND_LOS    = 3,   // 指令视线法
    LEAD_ANGLE     = 4,   // 前置角法
    TERMINAL_MANEUVER = 5 // 末端机动模式
};

/// 导弹运动参数
struct MissileParams {
    double max_speed           = 800.0;  // 最大速度 (m/s, ~M2.5)
    double min_speed           = 100.0;  // 最小速度 (m/s, 失速)
    double boost_accel         = 50.0;   // 助推段加速度 (m/s^2)
    double sustain_accel       = 10.0;   // 续航段加速度 (m/s^2)
    double max_lateral_accel   = 200.0;  // 最大横向过载 (m/s^2, ~20G)
    double max_yaw_rate        = 180.0;  // 最大偏航角速度 (度/秒)
    double max_pitch_rate      = 180.0;  // 最大俯仰角速度 (度/秒)
    double max_roll_rate       = 360.0;  // 最大滚转角速度 (度/秒)

    // 制导参数
    GuidanceLaw guidance_law   = GuidanceLaw::PROPORTIONAL;
    double nav_constant        = 4.0;    // 比例导引导航常数 N
    double seeker_range        = 20000.0;// 导引头探测距离 (m)
    double seeker_fov          = 60.0;   // 导引头视场 (度)
    double max_los_rate        = 90.0;   // 最大视线角速度 (度/秒)

    // 飞行阶段
    double boost_duration      = 3.0;    // 助推段时间 (s)
    double sustain_duration    = 20.0;   // 续航段时间 (s)
    double max_flight_time     = 60.0;   // 最大飞行时间 (s)

    // 末端机动
    bool   enable_terminal_maneuver = false;  // 是否启用末端机动
    double terminal_maneuver_distance = 5000.0; // 距目标多远开始机动 (m)
    double maneuver_amplitude  = 50.0;   // 机动幅度 (m/s^2)
    double maneuver_frequency  = 0.5;    // 机动频率 (Hz)

    // 引信参数
    double fuze_arming_delay   = 0.5;    // 引信解除保险延迟 (s)
    double proximity_fuze_range = 20.0;  // 近炸引信作用距离 (m)
    bool   impact_fuze         = true;   // 触发引信
    bool   proximity_fuze      = true;   // 近炸引信
};

class MissileMotion : public IPlatformMotion {
public:
    MissileMotion() = default;
    ~MissileMotion() override = default;

    PlatformCategory category() const override { return PlatformCategory::MISSILE; }

    bool initialize(const nlohmann::json& config,
                    const MotionState& initial) override;

    MotionState& update(double dt,
                        const EnvironmentContext& env,
                        const ControlCommand& cmd,
                        MotionState& state) override;

    nlohmann::json getParams() const override;
    void setParams(const nlohmann::json& params) override;
    void reset() override;

    const MissileParams& params() const { return params_; }
    MissileParams& mutableParams() { return params_; }

    // 查询结果
    bool hasHitTarget() const { return hit_target_; }
    bool hasExpired() const { return flight_time_ >= params_.max_flight_time; }
    double flightTime() const { return flight_time_; }
    double distanceToTarget() const { return distance_to_target_; }

private:
    // 比例导引律
    vsg::dvec3 proportionalNavigation(const vsg::dvec3& pos,
                                       const vsg::dvec3& vel,
                                       const vsg::dvec3& target_pos,
                                       const vsg::dvec3& target_vel) const;

    // 增广比例导引
    vsg::dvec3 augmentedPN(const vsg::dvec3& pos,
                            const vsg::dvec3& vel,
                            const vsg::dvec3& target_pos,
                            const vsg::dvec3& target_vel,
                            const vsg::dvec3& target_accel) const;

    // 纯追踪法
    vsg::dvec3 pursuitGuidance(const vsg::dvec3& pos,
                                const vsg::dvec3& target_pos) const;

    // 末端机动
    vsg::dvec3 terminalManeuver(double dt, double distance_to_target) const;

    // 引信检测
    bool checkFuze(const vsg::dvec3& pos, const vsg::dvec3& target_pos) const;

    // 视场检查 (目标是否在导引头视场内)
    bool targetInSeekerFOV(const vsg::dvec3& pos, const vsg::dvec3& vel,
                           const vsg::dvec3& target_pos) const;

    MissileParams params_;
    MotionState initial_state_;
    double flight_time_ = 0.0;
    double distance_to_target_ = 0.0;
    bool hit_target_ = false;
    bool armed_ = false;  // 引信已解除保险
};

} // namespace vsgraysim::platform