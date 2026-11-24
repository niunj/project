/// @file RoutePlan.cpp
/// @brief 航迹规划数据结构实现。

#include "RoutePlan.h"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace vsgraysim::platform {

static double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }

// =============================================================================
double RoutePlan::totalDuration() const {
    if (waypoints.empty()) return 0.0;
    return waypoints.back().time_s;
}

// =============================================================================
int RoutePlan::segmentFor(double elapsed) const {
    if (waypoints.empty()) return -1;
    // 二分查找第一个 time_s > elapsed 的航点
    auto it = std::upper_bound(waypoints.begin(), waypoints.end(), elapsed,
        [](double t, const RouteWaypoint& w) { return t < w.time_s; });
    if (it == waypoints.begin()) return 0;             // 在第一个航点之前
    if (it == waypoints.end()) return int(waypoints.size()) - 2; // 在最后航点之后
    return int(it - waypoints.begin()) - 1;            // 所在段的前端
}

// =============================================================================
bool RoutePlan::sample(double elapsed, vsg::dvec3& pos, double& speed,
                       bool hold_last) const {
    if (waypoints.empty()) return false;

    // 循环取模 (巡逻/巡航)
    const double total = totalDuration();
    if (loop && total > 0.0) {
        // 模后还要保持相位, 处理负值
        elapsed = std::fmod(elapsed, total);
        if (elapsed < 0.0) elapsed += total;
    }

    // 尚未启动 (在首点之前)
    if (elapsed <= waypoints.front().time_s) {
        pos = waypoints.front().position;
        speed = waypoints.front().speed > 0.0 ? waypoints.front().speed : (waypoints.size() > 1 ? 0.0 : 0.0);
        return true;
    }

    // 超过总时长
    if (elapsed >= waypoints.back().time_s) {
        if (loop) {
            // 已取模, 不会走到这
        }
        if (hold_last) {
            pos = waypoints.back().position;
            speed = waypoints.back().speed;
            return true;
        }
        return false; // 不保持, 航迹结束
    }

    // 在航点之间线性插值
    const int seg = segmentFor(elapsed);
    if (seg < 0 || size_t(seg + 1) >= waypoints.size()) {
        pos = waypoints.back().position;
        speed = waypoints.back().speed;
        return true;
    }

    const RouteWaypoint& a = waypoints[size_t(seg)];
    const RouteWaypoint& b = waypoints[size_t(seg + 1)];
    const double span = b.time_s - a.time_s;
    double t = span > 1e-9 ? (elapsed - a.time_s) / span : 1.0;
    t = clamp01(t);

    pos = a.position + (b.position - a.position) * t;
    speed = b.speed > 0.0 ? b.speed : a.speed;
    return true;
}

// =============================================================================
bool RoutePlan::samplePosition(double elapsed, vsg::dvec3& pos) const {
    double speed = 0.0;
    return sample(elapsed, pos, speed, true);
}

// =============================================================================
nlohmann::json RoutePlan::toJSON() const {
    nlohmann::json j;
    j["platform"] = platform_name;
    j["category"] = static_cast<int>(category);
    j["type"]     = static_cast<int>(type);
    j["type_name"]= routeTypeName(type);
    j["loop"]     = loop;
    j["start_delay_s"] = start_delay_s;

    nlohmann::json wps = nlohmann::json::array();
    for (const auto& w : waypoints) {
        nlohmann::json jw;
        jw["time"] = w.time_s;
        jw["pos"] = { w.position.x, w.position.y, w.position.z };
        jw["speed"] = w.speed;
        if (w.has_attitude) {
            jw["yaw_deg"] = w.yaw_deg;
            jw["pitch_deg"] = w.pitch_deg;
        }
        wps.push_back(jw);
    }
    j["waypoints"] = wps;
    return j;
}

// =============================================================================
RoutePlan RoutePlan::fromJSON(const nlohmann::json& j) {
    RoutePlan r;
    if (j.contains("platform")) r.platform_name = j["platform"].get<std::string>();
    if (j.contains("category")) r.category = static_cast<PlatformCategory>(j["category"].get<int>());
    if (j.contains("type"))     r.type     = static_cast<RouteType>(j["type"].get<int>());
    if (j.contains("loop"))     r.loop     = j["loop"].get<bool>();
    if (j.contains("start_delay_s")) r.start_delay_s = j["start_delay_s"].get<double>();

    if (j.contains("waypoints") && j["waypoints"].is_array()) {
        for (const auto& jw : j["waypoints"]) {
            RouteWaypoint w;
            w.time_s = jw.value("time", 0.0);
            if (jw.contains("pos") && jw["pos"].is_array() && jw["pos"].size() >= 3) {
                w.position = { jw["pos"][0].get<double>(),
                               jw["pos"][1].get<double>(),
                               jw["pos"][2].get<double>() };
            }
            w.speed = jw.value("speed", 0.0);
            if (jw.contains("yaw_deg")) {
                w.has_attitude = true;
                w.yaw_deg   = jw["yaw_deg"].get<double>();
                w.pitch_deg = jw.value("pitch_deg", 0.0);
            }
            r.waypoints.push_back(std::move(w));
        }
        // 确保按时间排序
        std::sort(r.waypoints.begin(), r.waypoints.end(),
                  [](const RouteWaypoint& a, const RouteWaypoint& b) {
                      return a.time_s < b.time_s;
                  });
    }
    return r;
}

// =============================================================================
// 从场景实体 JSON 读取航迹
// 1. 新版: entity["route_plan"] (RoutePlan 完整格式)
// 2. 兼容: entity["waypoints"] ({time, pos:[x,y,z]})
// =============================================================================
RoutePlan RoutePlan::fromEntityJSON(const nlohmann::json& entity_json) {
    // 新版 route_plan
    if (entity_json.contains("route_plan") && entity_json["route_plan"].is_object()) {
        RoutePlan r = fromJSON(entity_json["route_plan"]);
        if (r.platform_name.empty() && entity_json.contains("name")) {
            r.platform_name = entity_json["name"].get<std::string>();
        }
        return r;
    }

    // 兼容旧 waypoints
    if (entity_json.contains("waypoints") && entity_json["waypoints"].is_array()) {
        RoutePlan r;
        if (entity_json.contains("name")) r.platform_name = entity_json["name"].get<std::string>();
        if (entity_json.contains("tags")) {
            for (const auto& tag : entity_json["tags"]) {
                std::string t = tag.get<std::string>();
                std::transform(t.begin(), t.end(), t.begin(), ::tolower);
                if (t == "missile") { r.category = PlatformCategory::MISSILE; break; }
                if (t == "aircraft" || t == "plane" || t == "uav") { r.category = PlatformCategory::AIRCRAFT; break; }
                if (t == "ship" || t == "vessel") { r.category = PlatformCategory::NAVAL_VESSEL; break; }
                if (t == "tank" || t == "vehicle") { r.category = PlatformCategory::GROUND_VEHICLE; break; }
            }
        }
        r.type = defaultRouteType(r.category);
        r.loop = (r.type == RouteType::PATROL || r.type == RouteType::CRUISE);
        for (const auto& jw : entity_json["waypoints"]) {
            RouteWaypoint w;
            w.time_s = jw.value("time", 0.0);
            if (jw.contains("pos") && jw["pos"].is_array() && jw["pos"].size() >= 3) {
                w.position = { jw["pos"][0].get<double>(),
                               jw["pos"][1].get<double>(),
                               jw["pos"][2].get<double>() };
            }
            r.waypoints.push_back(std::move(w));
        }
        std::sort(r.waypoints.begin(), r.waypoints.end(),
                  [](const RouteWaypoint& a, const RouteWaypoint& b) {
                      return a.time_s < b.time_s;
                  });
        return r;
    }

    return RoutePlan{};
}

} // namespace vsgraysim::platform