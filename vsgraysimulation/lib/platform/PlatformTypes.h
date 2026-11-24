#pragma once
/// @file PlatformTypes.h
/// @brief 平台运动模型共享类型定义。

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

#include <nlohmann/json.hpp>

#include <vsg/maths/vec3.h>
#include <vsg/maths/quat.h>
#include <vsg/maths/mat4.h>

namespace vsgraysim::platform {

// =============================================================================
// 平台类型枚举
// =============================================================================
enum class PlatformCategory : uint8_t {
    GROUND_VEHICLE = 0,  // 地面目标 (坦克/装甲车)
    AIRCRAFT       = 1,  // 空中目标 (飞机/无人机)
    NAVAL_VESSEL   = 2,  // 海面目标 (舰船)
    MISSILE        = 3,  // 导弹
    STATIC         = 4,  // 静止目标 (建筑/设施)
    OTHER          = 5
};

inline const char* platformCategoryName(PlatformCategory cat) {
    switch (cat) {
        case PlatformCategory::GROUND_VEHICLE: return "GroundVehicle";
        case PlatformCategory::AIRCRAFT:       return "Aircraft";
        case PlatformCategory::NAVAL_VESSEL:   return "NavalVessel";
        case PlatformCategory::MISSILE:        return "Missile";
        case PlatformCategory::STATIC:         return "Static";
        default:                               return "Other";
    }
}

// =============================================================================
// 平台运动状态
// =============================================================================
struct MotionState {
    // 位置与姿态 (世界坐标系)
    vsg::dvec3 position  = {0, 0, 0};     // 质心位置 (m)
    vsg::dquat rotation  = {0, 0, 0, 1};  // 姿态四元数 (ENU 坐标系)
    vsg::dvec3 velocity  = {0, 0, 0};     // 速度向量 (m/s, 世界坐标系)
    vsg::dvec3 accel     = {0, 0, 0};     // 加速度 (m/s^2)

    // 姿态角 (欧拉, 度)
    double yaw   = 0.0;   // 偏航角 (度, 北偏东为正)
    double pitch = 0.0;   // 俯仰角 (度, 抬头为正)
    double roll  = 0.0;   // 滚转角 (度, 右倾为正)

    // 角速度
    double yaw_rate   = 0.0;  // 偏航角速度 (度/秒)
    double pitch_rate = 0.0;  // 俯仰角速度
    double roll_rate  = 0.0;  // 滚转角速度

    // 导出变换矩阵
    vsg::dmat4 matrix() const;
};

// =============================================================================
// 地形/环境上下文 (由 PlatformManager 每帧更新)
// =============================================================================
struct EnvironmentContext {
    double terrain_height = 0.0;     // 地形高度 (m)
    vsg::dvec3 terrain_normal = {0, 1, 0};  // 地形法线
    double sea_level = 0.0;          // 海平面高度 (m)
    double wave_height = 0.0;        // 当前浪高 (m)
    double wind_speed = 0.0;         // 风速 (m/s)
    vsg::dvec3 wind_direction = {0, 0, 0};  // 风向
    double air_density = 1.225;      // 空气密度 (kg/m^3)
};

// =============================================================================
// 装备类型枚举 (平台装备联动)
// =============================================================================
enum class EquipmentType : uint8_t {
    RADAR       = 0,   // 雷达 (搜索/跟踪)
    SAR         = 1,   // SAR 成像
    INFRARED    = 2,   // 红外 (FLIR/IRST)
    COMMUNICATION = 3, // 通信
    WEAPON      = 4,   // 武器 (导弹/火炮)
    EWAR        = 5,   // 电子战
    LASER       = 6,   // 激光
    SONAR       = 7,   // 声纳 (舰船)
    IFF         = 8,   // 敌我识别
    ECM         = 9,   // 电子干扰
    OTHER_EQ    = 10
};

// =============================================================================
// 装备实例 (属于某个平台)
// =============================================================================
struct EquipmentInstance {
    uint32_t        equipment_id = 0;   // 全局唯一 ID (对应 EquipmentInfo)
    EquipmentType   type = EquipmentType::RADAR;
    std::string     name;
    bool            powered = true;     // 电源开关
    bool            active  = false;    // 工作状态
    double          current_power = 0.0; // 当前功率 (0~1)
    double          current_range = 0.0; // 当前探测距离 (m)

    // 装备性能受平台运动影响
    // e.g. 转弯时雷达扫描范围受限, 高机动时红外精度下降
    struct MotionEffect {
        double max_yaw_rate   = 45.0;   // 超过此偏航角速度, 性能下降 (度/秒)
        double max_pitch_rate = 30.0;
        double max_roll_angle = 30.0;   // 超过此滚转角, 性能下降 (度)
        double perf_degradation = 0.0;  // 当前性能衰减 (0~1, 0=正常)
    };
    MotionEffect motion_effect;
};

// =============================================================================
// 平台完整描述
// =============================================================================
struct PlatformInstance {
    uint32_t           platform_id = 0;    // 全局唯一 ID
    std::string        name;               // 平台名称
    PlatformCategory   category = PlatformCategory::STATIC;
    MotionState        state;              // 当前运动状态
    bool               kinematic = false; // prescribed trajectory vs Bullet-integrated state
    bool               flight_control_enabled = true;
    int                faction = 0;        // 阵营: 0=中立 1=蓝方(进攻) 2=红方(防御)

    // 运动参数 (由具体运动模型填充)
    void*              motion_model = nullptr;  // 运动模型实例指针 (IPlatformMotion*)

    // 装备列表
    std::vector<EquipmentInstance> equipment;

    // 平台关联: 所属编队 / 上级平台
    uint32_t           formation_id = 0;
    uint32_t           parent_platform_id = 0;

    // 健康状态
    double             health = 1.0;       // 0~1, 0=摧毁
    bool               alive = true;

    // 决策分步步长 (调度参数, 秒): 帧内运动决策 (导引律/引信/姿态)
    // 按此步长细分执行; 0 = 不分步。快平台 (导弹/飞机) 借此保证决策粒度,
    // 慢平台 (地面/水面) 帧步长已足够。可由 motion_params.max_integration_step 覆写。
    double             max_integration_step = 0.0;
};

// =============================================================================
// 控制指令 (由轨迹规划/用户输入/自动驾驶产生)
// =============================================================================
struct ControlCommand {
    // Explicit aircraft actuator/hold control. Empty preserves legacy route behavior.
    std::string flight_mode;
    double throttle = .5, elevator = 0, aileron = 0, rudder = 0;
    double target_heading = 0;
    double desired_speed = 0.0;       // 期望速度 (m/s)
    double desired_yaw_rate = 0.0;     // 期望偏航角速度 (度/秒)
    double desired_pitch_rate = 0.0;   // 期望俯仰角速度 (度/秒)
    double desired_roll = 0.0;         // 期望滚转角 (度)
    double desired_altitude = 0.0;     // 期望高度 (m)
    vsg::dvec3 desired_accel = {0, 0, 0}; // 期望加速度

    // 目标制导 (导弹/飞行器)
    vsg::dvec3 target_position = {0, 0, 0};
    uint32_t target_platform_id = 0;   // 锁定目标平台 ID

    // 是否自动驾驶 / 手动控制
    bool auto_control = true;
    bool has_target = false;

    // 轨迹点索引 (跟随预设轨迹时)
    size_t waypoint_index = 0;
    std::vector<vsg::dvec3> waypoints;
};

} // namespace vsgraysim::platform
