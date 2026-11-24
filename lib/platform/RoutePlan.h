#pragma once
/// @file RoutePlan.h
/// @brief 航迹规划数据结构 — 带时间参数的平台运动调度计划。
///
/// 航迹由场景设计软件提前规划 (每个航点带时间戳), 保存到场景 JSON,
/// 仿真软件加载后进行基于仿真时间的调度, 驱动平台沿规划航迹运动。
///
/// 每种平台类别对应不同的航迹类型:
///   - 地面目标 (坦克/装甲车): PATROL 巡逻 / CONVOY 护送 / WAYPOINT 过点
///   - 空中目标 (飞机/无人机): CRUISE 巡航 / PATROL 巡逻 / INTERCEPT 拦截
///   - 海面目标 (舰船):        PATROL 巡逻 / WAYPOINT 过点
///   - 导弹:                   INTERCEPT 攻击 / LOITER 巡航后俯冲
///
/// 调度方式: RoutePlan::sample() 根据相对时刻 elapsed 在航点间线性插值,
/// 生成目标位置与速度, 供 PlatformManager 转换为运动控制指令。

#include "PlatformTypes.h"

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <vsg/maths/vec3.h>
#include <vsg/maths/quat.h>

namespace vsgraysim::platform {

// =============================================================================
// 航迹类型
// =============================================================================
enum class RouteType : uint8_t {
    WAYPOINT  = 0,  // 过点 (依次经过各航点)
    PATROL    = 1,  // 巡逻 (到达终点后折返/循环)
    CRUISE    = 2,  // 巡航 (空中平台保持高度飞行)
    INTERCEPT = 3,  // 拦截/攻击 (有明确目标)
    CONVOY    = 4,  // 护送 (跟随编队)
    HOLD      = 5   // 待命 (原地/指定区域保持)
};

inline const char* routeTypeName(RouteType t) {
    switch (t) {
        case RouteType::WAYPOINT:  return "Waypoint";
        case RouteType::PATROL:    return "Patrol";
        case RouteType::CRUISE:    return "Cruise";
        case RouteType::INTERCEPT: return "Intercept";
        case RouteType::CONVOY:    return "Convoy";
        case RouteType::HOLD:      return "Hold";
        default:                   return "Waypoint";
    }
}

/// 按平台类别返回适用的默认航迹类型
inline RouteType defaultRouteType(PlatformCategory cat) {
    switch (cat) {
        case PlatformCategory::GROUND_VEHICLE: return RouteType::PATROL;
        case PlatformCategory::AIRCRAFT:       return RouteType::CRUISE;
        case PlatformCategory::NAVAL_VESSEL:   return RouteType::PATROL;
        case PlatformCategory::MISSILE:        return RouteType::INTERCEPT;
        default:                               return RouteType::WAYPOINT;
    }
}

// =============================================================================
// 航点 (带时间参数)
// =============================================================================
struct RouteWaypoint {
    double      time_s = 0.0;     // 相对航迹起始时刻 (秒), 单调递增
    vsg::dvec3  position = {0, 0, 0}; // 目标位置 (m)
    double      speed = 0.0;      // 到达/途经速度 (m/s); 0=沿用上一段
    bool        has_attitude = false; // 是否指定姿态
    double      yaw_deg   = 0.0;  // 目标偏航角 (度)
    double      pitch_deg = 0.0;  // 目标俯仰角 (度)
};

// =============================================================================
// 完整航迹计划
// =============================================================================
class RoutePlan {
public:
    // ---- 元数据 ----
    std::string platform_name;        // 关联平台名称
    PlatformCategory category = PlatformCategory::GROUND_VEHICLE;
    RouteType   type = RouteType::WAYPOINT;
    bool        loop = false;          // 到达终点后是否循环 (巡逻/巡航用)
    double      start_delay_s = 0.0;   // 仿真开始后延迟启动 (秒)

    // ---- 航点序列 (按 time_s 升序) ----
    std::vector<RouteWaypoint> waypoints;

    // ---- 时间调度 ----

    /// 航迹总时长 (最后一个航点时刻 + 特性)
    double totalDuration() const;

    /// 给定相对时刻 elapsed (相对启动时刻), 采样目标位置/速度。
    /// @param elapsed  相对航迹启动的时刻 (秒), 可为负 (尚未启动)
    /// @param pos      [输出] 插值后的目标位置
    /// @param speed    [输出] 插值后的目标速度 (m/s)
    /// @param hold_last 若为 true, 超过总时长后保持最后航点 (不循环时)
    /// @return 是否处于有效航迹区间内 (true = 平台应沿航迹运动)
    bool sample(double elapsed, vsg::dvec3& pos, double& speed,
                bool hold_last = true) const;

    /// 给定相对时刻, 采样目标位置 (便捷重载)
    bool samplePosition(double elapsed, vsg::dvec3& pos) const;

    /// 提取时刻 t 所在航点段的下标 (用于断点/插值)
    int segmentFor(double elapsed) const;

    // ---- JSON ----

    nlohmann::json toJSON() const;
    static RoutePlan fromJSON(const nlohmann::json& j);

    /// 从场景实体 JSON 读取航迹 (字段 route_plan 或 legacy waypoints)
    static RoutePlan fromEntityJSON(const nlohmann::json& entity_json);
};

} // namespace vsgraysim::platform