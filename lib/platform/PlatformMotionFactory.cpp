/// @file PlatformMotionFactory.cpp
/// @brief 平台运动工厂实现。

#include "PlatformMotionFactory.h"
#include "RoutePlan.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <simcore/Orientation.h>

namespace vsgraysim::platform {

// =============================================================================
size_t PlatformMotionFactory::createFromScene(
    const SceneProduct& scene,
    PlatformManager& manager,
    uint32_t& next_equip_id)
{
    size_t count = 0;
    for (const auto& entity : scene.entities) {
        if (!isPlatformEntity(entity)) continue;

        if (createFromEntity(entity, manager, next_equip_id) > 0) {
            count++;
        }
    }
    return count;
}

// =============================================================================
uint32_t PlatformMotionFactory::createFromEntity(
    const Entity& entity,
    PlatformManager& manager,
    uint32_t& next_equip_id)
{
    PlatformCategory cat = detectCategory(entity);
    if (cat == PlatformCategory::STATIC) return 0;

    // 构建初始状态
    MotionState initial = buildInitialState(entity);

    // 构建运动参数
    nlohmann::json motion_config = buildMotionConfig(entity, cat);

    // 创建平台
    uint32_t platform_id = manager.createPlatform(
        entity.name.empty() ? entity.entity_id : entity.name,
        cat,
        motion_config,
        initial);

    if (platform_id == 0) return 0;

    // 检测并创建装备
    detectAndCreateEquipment(platform_id, entity, manager, next_equip_id);

    return platform_id;
}

// =============================================================================
// 为平台分配航迹计划 (从实体配置)
// =============================================================================
// =============================================================================
// 从 JSON 读取阵营并应用到平台 (blue=1 蓝方进攻, red=2 红方防御)
// =============================================================================
static void applyFaction(uint32_t platform_id,
                         const nlohmann::json& entity_json,
                         PlatformManager& manager) {
    int faction = 0;
    if (entity_json.contains("faction")) {
        const auto& f = entity_json["faction"];
        if (f.is_string()) {
            std::string s = f.get<std::string>();
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            if (s == "blue" || s == "蓝方" || s == "进攻方") faction = 1;
            else if (s == "red" || s == "红方" || s == "防御方") faction = 2;
        } else if (f.is_number_integer()) {
            faction = f.get<int>();
        }
    }
    // 兜底: 从 tags 里识别 faction/blue/red
    if (faction == 0 && entity_json.contains("tags") && entity_json["tags"].is_array()) {
        for (const auto& tag : entity_json["tags"]) {
            std::string t = tag.get<std::string>();
            std::transform(t.begin(), t.end(), t.begin(), ::tolower);
            if (t == "blue" || t == "蓝方") { faction = 1; break; }
            if (t == "red" || t == "红方") { faction = 2; break; }
        }
    }
    if (faction != 0)
        manager.setPlatformFaction(platform_id, faction);
}

static void assignRouteIfPresent(uint32_t platform_id,
                                 const nlohmann::json& entity_json,
                                 PlatformManager& manager) {
    RoutePlan route = RoutePlan::fromEntityJSON(entity_json);
    if (!route.waypoints.empty()) {
        manager.assignRoute(platform_id, route);
    }
}

// =============================================================================
bool PlatformMotionFactory::isPlatformEntity(const Entity& entity) {
    // 检查标签: 可移动平台一定有特定标签
    for (const auto& tag : entity.tags) {
        std::string lower = tag;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower == "platform" || lower == "ground_vehicle" ||
            lower == "aircraft" || lower == "naval_vessel" ||
            lower == "missile" || lower == "vehicle" ||
            lower == "tank" || lower == "ship" || lower == "plane" ||
            lower == "uav" || lower == "drone") {
            return true;
        }
    }

    // 检查 PhysicsDesc 的 is_vehicle 标记
    if (entity.components.find("platform") != entity.components.end()) {
        return true;
    }

    return false;
}

// =============================================================================
PlatformCategory PlatformMotionFactory::detectCategory(const Entity& entity) {
    for (const auto& tag : entity.tags) {
        std::string lower = tag;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower == "missile" || lower == "guided_missile" ||
            lower == "rocket") {
            return PlatformCategory::MISSILE;
        }
        if (lower == "aircraft" || lower == "plane" || lower == "uav" ||
            lower == "drone" || lower == "fighter" || lower == "helicopter") {
            return PlatformCategory::AIRCRAFT;
        }
        if (lower == "naval_vessel" || lower == "ship" || lower == "boat" ||
            lower == "submarine" || lower == "destroyer" || lower == "carrier") {
            return PlatformCategory::NAVAL_VESSEL;
        }
        if (lower == "ground_vehicle" || lower == "tank" || lower == "vehicle" ||
            lower == "armored_vehicle" || lower == "apc" || lower == "truck" ||
            lower == "howitzer") {
            return PlatformCategory::GROUND_VEHICLE;
        }
    }

    // 检查 components 中的 platform_type
    auto it = entity.components.find("platform_type");
    if (it != entity.components.end()) {
        // std::any conversion - fallback to tag-based detection
    }

    return PlatformCategory::GROUND_VEHICLE; // 默认地面车辆
}

// =============================================================================
nlohmann::json PlatformMotionFactory::buildMotionConfig(
    const Entity& entity, PlatformCategory cat)
{
    nlohmann::json config;

    // 尝试从 components 中读取运动参数
    auto it = entity.components.find("motion_params");
    if (it != entity.components.end()) {
        // 如果有预设参数, 可以在未来扩展
    }

    // 根据类别设置默认参数
    switch (cat) {
        case PlatformCategory::GROUND_VEHICLE:
            config["max_speed_fwd"] = 20.0;
            config["max_accel"] = 5.0;
            config["max_turn_rate"] = 40.0;
            config["max_climb_angle"] = 30.0;
            config["tracked"] = true;
            break;

        case PlatformCategory::AIRCRAFT:
            config["max_speed"] = 300.0;
            config["cruise_speed"] = 200.0;
            config["max_load_factor"] = 9.0;
            config["max_altitude"] = 15000.0;
            config["terrain_following"] = false;
            config["altitude_hold"] = true;
            config["hold_altitude"] = 1000.0;
            break;

        case PlatformCategory::NAVAL_VESSEL:
            config["max_speed"] = 15.0;
            config["cruise_speed"] = 10.0;
            config["max_turn_rate"] = 5.0;
            config["turn_radius_min"] = 200.0;
            config["enable_wave_response"] = true;
            config["hull_length"] = 100.0;
            config["hull_beam"] = 15.0;
            break;

        case PlatformCategory::MISSILE:
            config["max_speed"] = 800.0;
            config["boost_accel"] = 50.0;
            config["max_lateral_accel"] = 200.0;
            config["guidance_law"] = 1; // PROPORTIONAL
            config["nav_constant"] = 4.0;
            config["seeker_range"] = 20000.0;
            config["boost_duration"] = 3.0;
            config["sustain_duration"] = 20.0;
            config["max_flight_time"] = 60.0;
            config["enable_terminal_maneuver"] = true;
            break;

        default:
            break;
    }

    return config;
}

// =============================================================================
MotionState PlatformMotionFactory::buildInitialState(const Entity& entity) {
    MotionState state;

    // 从 Transform 提取位置
    state.position = entity.world_transform.translation;
    state.rotation = entity.world_transform.rotation;

    // 从四元数提取欧拉角
    vsg::dquat& q = state.rotation;
    // 偏航角 (绕 Y 轴)
    double siny = 2.0 * (q.w * q.y + q.x * q.z);
    double cosy = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
    state.yaw = std::atan2(siny, cosy) * 180.0 / M_PI;

    // 俯仰角 (绕 X 轴)
    double sinp = 2.0 * (q.w * q.x - q.y * q.z);
    state.pitch = std::asin(std::clamp(sinp, -1.0, 1.0)) * 180.0 / M_PI;

    // 滚转角 (绕 Z 轴)
    double sinr = 2.0 * (q.w * q.z + q.x * q.y);
    double cosr = 1.0 - 2.0 * (q.z * q.z + q.x * q.x);
    state.roll = std::atan2(sinr, cosr) * 180.0 / M_PI;

    // 初始速度为零
    state.velocity = {0, 0, 0};
    state.accel = {0, 0, 0};

    return state;
}

// =============================================================================
void PlatformMotionFactory::detectAndCreateEquipment(
    uint32_t platform_id,
    const Entity& entity,
    PlatformManager& manager,
    uint32_t& next_equip_id)
{
    // 检查实体标签, 为每个平台创建默认装备
    bool has_radar = false;
    bool has_sar = false;
    bool has_ir = false;
    bool has_weapon = false;
    bool has_comm = false;
    bool has_sonar = false;

    for (const auto& tag : entity.tags) {
        std::string lower = tag;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower == "radar" || lower == "has_radar") has_radar = true;
        if (lower == "sar" || lower == "has_sar") has_sar = true;
        if (lower == "ir" || lower == "infrared" || lower == "has_ir") has_ir = true;
        if (lower == "weapon" || lower == "has_weapon" || lower == "missile_launcher") has_weapon = true;
        if (lower == "comm" || lower == "has_comm" || lower == "communication") has_comm = true;
        if (lower == "sonar" || lower == "has_sonar") has_sonar = true;
    }

    // 根据平台类别推断默认装备
    PlatformCategory cat = detectCategory(entity);
    switch (cat) {
        case PlatformCategory::GROUND_VEHICLE:
            has_radar = has_radar || true;    // 坦克通常有雷达
            has_weapon = has_weapon || true;  // 坦克有主炮
            has_comm = has_comm || true;      // 有通信
            has_ir = has_ir || true;          // 有红外
            break;

        case PlatformCategory::AIRCRAFT:
            has_radar = has_radar || true;    // 飞机有火控雷达
            has_weapon = has_weapon || true;  // 有武器
            has_comm = has_comm || true;      // 有通信
            has_ir = has_ir || true;          // 有红外/光电
            break;

        case PlatformCategory::NAVAL_VESSEL:
            has_radar = has_radar || true;    // 舰船有搜索雷达
            has_sonar = has_sonar || true;    // 有声纳
            has_weapon = has_weapon || true;  // 有武器
            has_comm = has_comm || true;      // 有通信
            has_ir = has_ir || true;          // 有红外
            break;

        case PlatformCategory::MISSILE:
            has_radar = has_radar || true;    // 导弹有导引头
            break;

        default:
            break;
    }

    // 创建装备
    if (has_radar) {
        manager.addEquipment(platform_id, next_equip_id++,
                             EquipmentType::RADAR, "雷达系统");
    }
    if (has_sar) {
        manager.addEquipment(platform_id, next_equip_id++,
                             EquipmentType::SAR, "SAR成像系统");
    }
    if (has_ir) {
        manager.addEquipment(platform_id, next_equip_id++,
                             EquipmentType::INFRARED, "红外探测系统");
    }
    if (has_weapon) {
        manager.addEquipment(platform_id, next_equip_id++,
                             EquipmentType::WEAPON, "武器系统");
    }
    if (has_comm) {
        manager.addEquipment(platform_id, next_equip_id++,
                             EquipmentType::COMMUNICATION, "通信系统");
    }
    if (has_sonar) {
        manager.addEquipment(platform_id, next_equip_id++,
                             EquipmentType::SONAR, "声纳系统");
    }
}

// =============================================================================
// 从 JSON 实体创建平台 (场景文件 JSON 中的 entities 字段)
// =============================================================================
uint32_t PlatformMotionFactory::createFromJSON(
    const nlohmann::json& entity_json,
    PlatformManager& manager,
    uint32_t& next_equip_id)
{
    // 判断是否为可移动平台
    bool is_platform = false;
    PlatformCategory cat = PlatformCategory::GROUND_VEHICLE;

    if (entity_json.contains("tags")) {
        for (const auto& tag : entity_json["tags"]) {
            std::string t = tag.get<std::string>();
            std::transform(t.begin(), t.end(), t.begin(), ::tolower);
            if (t == "platform" || t == "ground_vehicle" || t == "tank" ||
                t == "aircraft" || t == "plane" || t == "uav" ||
                t == "naval_vessel" || t == "ship" ||
                t == "missile" || t == "vehicle") {
                is_platform = true;
            }
            if (t == "missile") cat = PlatformCategory::MISSILE;
            else if (t == "aircraft" || t == "plane" || t == "uav") cat = PlatformCategory::AIRCRAFT;
            else if (t == "naval_vessel" || t == "ship") cat = PlatformCategory::NAVAL_VESSEL;
            else if (t == "ground_vehicle" || t == "tank" || t == "vehicle") cat = PlatformCategory::GROUND_VEHICLE;
        }
    }

    if (!is_platform) return 0;

    // 提取名称
    std::string name = entity_json.value("name", entity_json.value("entity_id", "platform"));

    // 构建初始运动状态
    MotionState initial;
    if (entity_json.contains("world_transform") || entity_json.contains("transform")) {
        auto& trans = entity_json.contains("world_transform") ? entity_json["world_transform"] : entity_json["transform"];
        if (trans.contains("translation")) {
            auto& t = trans["translation"];
            initial.position = {t[0], t[1], t[2]};
        }
        if (trans.contains("rotation") && trans["rotation"].is_array()) {
            auto& r = trans["rotation"];
            if (r.size() >= 4) {
                initial.rotation = vsg::dquat(r[0], r[1], r[2], r[3]);
            }
        }
    } else if (entity_json.contains("position")) {
        auto& p = entity_json["position"];
        initial.position = {p[0], p[1], p[2]};
    }

    // 构建运动配置
    nlohmann::json motion_config = buildMotionConfigJSON(entity_json, cat);

    // 创建平台
    uint32_t platform_id = manager.createPlatform(name, cat, motion_config, initial);
    if (platform_id == 0) return 0;

    // 阵营 (从场景 JSON faction/tags 读取: blue=1 红方=2)
    applyFaction(platform_id, entity_json, manager);

    // 创建默认装备
    createDefaultEquipment(platform_id, entity_json, cat, manager, next_equip_id);

    // 分配航迹计划 (若实体配置了 route_plan / waypoints)
    assignRouteIfPresent(platform_id, entity_json, manager);

    return platform_id;
}

// =============================================================================
// 从 JSON 对象创建平台 (兼容旧格式场景文件, objects 字段)
// =============================================================================
uint32_t PlatformMotionFactory::createFromObjectJSON(
    const nlohmann::json& obj_json,
    PlatformManager& manager,
    uint32_t& next_equip_id)
{
    std::string obj_id = obj_json.value("object_id", obj_json.value("id", obj_json.value("name", "object")));
    std::string name = obj_id;

    // 从对象类型推断平台类别
    PlatformCategory cat = PlatformCategory::GROUND_VEHICLE;
    std::string type = obj_json.value("type", obj_json.value("class", ""));
    std::string lower_type = type;
    std::transform(lower_type.begin(), lower_type.end(), lower_type.begin(), ::tolower);

    if (lower_type.find("missile") != std::string::npos) cat = PlatformCategory::MISSILE;
    else if (lower_type.find("aircraft") != std::string::npos || lower_type.find("plane") != std::string::npos || lower_type.find("uav") != std::string::npos)
        cat = PlatformCategory::AIRCRAFT;
    else if (lower_type.find("ship") != std::string::npos || lower_type.find("vessel") != std::string::npos || lower_type.find("boat") != std::string::npos)
        cat = PlatformCategory::NAVAL_VESSEL;
    else if (lower_type.find("tank") != std::string::npos || lower_type.find("vehicle") != std::string::npos || lower_type.find("apc") != std::string::npos)
        cat = PlatformCategory::GROUND_VEHICLE;
    else
        return 0; // 非平台类型

    // 位置
    MotionState initial;
    const auto read_position = [](const nlohmann::json& j, vsg::dvec3& pos) {
        if (j.is_array() && j.size() >= 3) pos = {j[0].get<double>(), j[1].get<double>(), j[2].get<double>()};
    };
    if (obj_json.contains("position")) {
        read_position(obj_json["position"], initial.position);
    } else if (obj_json.contains("physics") && obj_json["physics"].contains("initial_position")) {
        read_position(obj_json["physics"]["initial_position"], initial.position);
    }
    // 初始航向 (yaw, 度) [可选]
    if (obj_json.contains("physics") && obj_json["physics"].contains("initial_rotation_deg")) {
        auto& r = obj_json["physics"]["initial_rotation_deg"];
        if (r.is_array() && r.size() >= 3) {
            initial.pitch=r[0].get<double>(); initial.yaw=r[1].get<double>(); initial.roll=r[2].get<double>();
        }
    }
    initial.rotation=simcore::headingRotation(initial.yaw,initial.pitch,initial.roll);
    if (obj_json.contains("physics") && obj_json["physics"].contains("initial_velocity"))
        read_position(obj_json["physics"]["initial_velocity"],initial.velocity);

    // 运动配置
    nlohmann::json motion_config = buildMotionConfigJSON(obj_json, cat);

    uint32_t platform_id = manager.createPlatform(name, cat, motion_config, initial);
    if (platform_id == 0) return 0;

    // 阵营 (从场景 JSON faction/tags 读取: blue=1 红方=2)
    applyFaction(platform_id, obj_json, manager);

    // 默认装备
    createDefaultEquipment(platform_id, obj_json, cat, manager, next_equip_id);

    // 分配航迹计划 (若实体配置了 route_plan / waypoints)
    assignRouteIfPresent(platform_id, obj_json, manager);

    return platform_id;
}

// =============================================================================
// 内部辅助: 从 JSON 构建运动配置
// =============================================================================
nlohmann::json PlatformMotionFactory::buildMotionConfigJSON(
    const nlohmann::json& json,
    PlatformCategory cat)
{
    nlohmann::json config;

    // 优先从 JSON 的 motion_params 字段读取
    if (json.contains("motion_params")) {
        config = json["motion_params"];
        return config;
    }

    // 根据类别设置默认参数
    switch (cat) {
        case PlatformCategory::GROUND_VEHICLE:
            config["max_speed_fwd"] = json.value("max_speed", 20.0);
            config["max_accel"] = json.value("max_accel", 5.0);
            config["max_turn_rate"] = json.value("max_turn_rate", 40.0);
            config["max_climb_angle"] = json.value("max_climb_angle", 30.0);
            config["tracked"] = json.value("tracked", true);
            break;

        case PlatformCategory::AIRCRAFT:
            config["max_speed"] = json.value("max_speed", 300.0);
            config["cruise_speed"] = json.value("cruise_speed", 200.0);
            config["max_load_factor"] = json.value("max_load_factor", 9.0);
            config["max_altitude"] = json.value("max_altitude", 15000.0);
            config["altitude_hold"] = json.value("altitude_hold", true);
            config["hold_altitude"] = json.value("hold_altitude", 1000.0);
            break;

        case PlatformCategory::NAVAL_VESSEL:
            config["max_speed"] = json.value("max_speed", 15.0);
            config["cruise_speed"] = json.value("cruise_speed", 10.0);
            config["max_turn_rate"] = json.value("max_turn_rate", 5.0);
            config["enable_wave_response"] = json.value("enable_wave_response", true);
            break;

        case PlatformCategory::MISSILE:
            config["max_speed"] = json.value("max_speed", 800.0);
            config["boost_accel"] = json.value("boost_accel", 50.0);
            config["max_lateral_accel"] = json.value("max_lateral_accel", 200.0);
            config["guidance_law"] = json.value("guidance_law", 1);
            config["nav_constant"] = json.value("nav_constant", 4.0);
            config["seeker_range"] = json.value("seeker_range", 20000.0);
            config["boost_duration"] = json.value("boost_duration", 3.0);
            config["max_flight_time"] = json.value("max_flight_time", 60.0);
            config["enable_terminal_maneuver"] = json.value("enable_terminal_maneuver", true);
            break;

        default:
            break;
    }

    return config;
}

// =============================================================================
// 内部辅助: 为平台创建默认装备
// =============================================================================
void PlatformMotionFactory::createDefaultEquipment(
    uint32_t platform_id,
    const nlohmann::json& json,
    PlatformCategory cat,
    PlatformManager& manager,
    uint32_t& next_equip_id)
{
    // 检查 JSON 中是否有 equipment 字段
    if (json.contains("equipment") && json["equipment"].is_array()) {
        // 从 JSON 读取装备配置
        for (const auto& eq_json : json["equipment"]) {
            EquipmentType eq_type = EquipmentType::RADAR;
            int type_int = eq_json.value("type", 0);
            if (type_int >= 0 && type_int <= 10) {
                eq_type = static_cast<EquipmentType>(type_int);
            }
            std::string eq_name = eq_json.value("name", "Equipment");
            manager.addEquipment(platform_id, next_equip_id++, eq_type, eq_name);
        }
        return;
    }

    // 根据平台类别创建默认装备
    switch (cat) {
        case PlatformCategory::GROUND_VEHICLE:
            manager.addEquipment(platform_id, next_equip_id++, EquipmentType::RADAR, "火控雷达");
            manager.addEquipment(platform_id, next_equip_id++, EquipmentType::INFRARED, "红外探测");
            manager.addEquipment(platform_id, next_equip_id++, EquipmentType::WEAPON, "主武器系统");
            manager.addEquipment(platform_id, next_equip_id++, EquipmentType::COMMUNICATION, "通信系统");
            break;

        case PlatformCategory::AIRCRAFT:
            manager.addEquipment(platform_id, next_equip_id++, EquipmentType::RADAR, "火控雷达");
            manager.addEquipment(platform_id, next_equip_id++, EquipmentType::INFRARED, "光电探测");
            manager.addEquipment(platform_id, next_equip_id++, EquipmentType::WEAPON, "武器系统");
            manager.addEquipment(platform_id, next_equip_id++, EquipmentType::COMMUNICATION, "通信系统");
            break;

        case PlatformCategory::NAVAL_VESSEL:
            manager.addEquipment(platform_id, next_equip_id++, EquipmentType::RADAR, "搜索雷达");
            manager.addEquipment(platform_id, next_equip_id++, EquipmentType::SONAR, "声纳系统");
            manager.addEquipment(platform_id, next_equip_id++, EquipmentType::WEAPON, "舰载武器");
            manager.addEquipment(platform_id, next_equip_id++, EquipmentType::COMMUNICATION, "通信系统");
            break;

        case PlatformCategory::MISSILE:
            manager.addEquipment(platform_id, next_equip_id++, EquipmentType::RADAR, "导引头");
            break;

        default:
            break;
    }
}

} // namespace vsgraysim::platform
