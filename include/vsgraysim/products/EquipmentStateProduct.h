#pragma once
/// @file EquipmentStateProduct.h
/// @brief 每帧装备状态产品 —— 运动学、传感器、发射器、事件。
///
/// 由 PlatformEquipmentSystem 每仿真帧产出。
/// 被 DisplaySystem、SignalSystem、DataRecorder 及其它装备消费。
///
/// 设计原则:
///   - 帧级瞬时数据（frame_id / sim_time / dt），帧间可丢弃。
///   - 装备按 equipment_id 与 SceneProduct::Entity::entity_id 对应。
///   - 传感器/发射器以向量承载，顺序稳定。
///   - 通过 `std::any` user_data / internal_state 扩展管线特定载荷。
///
/// JSON 序列化支持：通过 nlohmann::json 的 ADL 实现 to_json/from_json。
/// 注意：包含 std::any 的字段（parameters / waveform / user_data /
/// internal_state）与 SimulationEvent::payload 无法直接表示为 JSON，
/// 序列化时跳过。

#ifndef VSGRAYSIM_PRODUCTS_EQUIPMENT_STATE_PRODUCT_H
#define VSGRAYSIM_PRODUCTS_EQUIPMENT_STATE_PRODUCT_H

#include <vsg/maths/vec3.h>
#include <vsg/maths/quat.h>
#include <vsg/maths/mat4.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <any>
#include <cstdint>

#include <nlohmann/json.hpp>

#include "vsgraysim/kernel/SimulationEvent.h"

namespace vsgraysim {

/// Kinematic state (world frame)
struct KinematicState {
    vsg::dvec3 position = {0, 0, 0};       // World position (m, ECEF or local tangent)
    vsg::dvec3 velocity = {0, 0, 0};       // Linear velocity (m/s)
    vsg::dvec3 acceleration = {0, 0, 0};   // Linear acceleration (m/s²)
    vsg::dquat orientation = {0, 0, 0, 1}; // Quaternion (world from body)
    vsg::dvec3 angular_velocity = {0, 0, 0};   // Angular velocity (rad/s, body frame)
    vsg::dvec3 angular_acceleration = {0, 0, 0}; // Angular acceleration (rad/s², body frame)

    // Derived
    vsg::dmat4 world_from_body() const;
    vsg::dmat4 body_from_world() const;
};

/// Sensor state (per sensor on equipment)
struct SensorState {
    std::string sensor_id;              // Unique within equipment
    std::string sensor_type;            // "RADAR", "IR", "EO", "LIDAR", "COMM", "ESM", etc.
    std::string mode;                   // Current mode: "SEARCH", "TRACK", "STARE", "SLAM", etc.

    // Beam / FOV
    vsg::dvec3 boresight_body = {0, 0, -1};  // Body-frame boresight
    double fov_azimuth = 1.0;                // Horizontal FOV (rad)
    double fov_elevation = 1.0;              // Vertical FOV (rad)
    vsg::dvec3 beam_direction_world;         // Computed world direction

    // Parameters (mode-dependent)
    std::unordered_map<std::string, std::any> parameters;  // e.g., "frequency", "bandwidth", "prf", "gain"

    // Status
    bool active = true;
    double timestamp = 0.0;             // Last update time
    std::any user_data;                 // Sensor-specific internal state
};

/// Emitter state (per emitter on equipment)
struct EmitterState {
    std::string emitter_id;
    std::string emitter_type;           // "RADAR_TX", "JAMMER", "LASER", "COMM_TX", "IFF"

    vsg::dvec3 position_body = {0, 0, 0};
    vsg::dvec3 direction_body = {0, 0, -1};

    // Waveform / signal parameters
    std::unordered_map<std::string, std::any> waveform;  // "frequency", "pulse_width", "prf", "power", "modulation"

    // Current emission
    bool emitting = false;
    double next_pulse_time = 0.0;
    std::any user_data;
};

/// Single equipment state
struct EquipmentState {
    std::string equipment_id;           // Matches SceneProduct::Entity::entity_id
    std::string equipment_type;         // "PLATFORM", "RADAR", "IR_SENSOR", "JAMMER", "MISSILE", etc.

    KinematicState kinematics;

    std::vector<SensorState> sensors;
    std::vector<EmitterState> emitters;

    // Health / damage
    double health = 1.0;                // 0.0 = destroyed
    double fuel_fraction = 1.0;
    std::unordered_map<std::string, double> subsystem_health;  // "engine", "radar", "comm", etc.

    // Internal black-box state (only readable by same equipment plugin)
    std::any internal_state;

    // Events generated this frame
    std::vector<SimulationEvent> events;
};

/// EquipmentStateProduct — per-frame aggregate
struct EquipmentStateProduct {
    uint64_t frame_id = 0;
    double sim_time = 0.0;
    double dt = 0.0;

    std::vector<EquipmentState> equipment_states;

    // Global events (not tied to single equipment)
    std::vector<SimulationEvent> global_events;

    // ---- Query helpers ----

    EquipmentState* findEquipment(const std::string& id) {
        for (auto& eq : equipment_states) if (eq.equipment_id == id) return &eq;
        return nullptr;
    }

    const EquipmentState* findEquipment(const std::string& id) const {
        for (const auto& eq : equipment_states) if (eq.equipment_id == id) return &eq;
        return nullptr;
    }

    std::vector<EquipmentState*> findByType(const std::string& type) {
        std::vector<EquipmentState*> result;
        for (auto& eq : equipment_states) if (eq.equipment_type == type) result.push_back(&eq);
        return result;
    }

    // Clear per-frame transient data (events, sensor updates)
    void clearTransient() {
        for (auto& eq : equipment_states) eq.events.clear();
        global_events.clear();
    }
};

using EquipmentStateProductPtr = std::shared_ptr<EquipmentStateProduct>;

// nlohmann/json ADL 序列化支持
// 注意：std::any 相关字段（parameters / waveform / user_data /
// internal_state）与 SimulationEvent::payload 无法表示为 JSON，序列化时跳过。

// KinematicState 序列化
inline void to_json(nlohmann::json& j, const KinematicState& k) {
    j = nlohmann::json{
        {"position", {k.position.x, k.position.y, k.position.z}},
        {"velocity", {k.velocity.x, k.velocity.y, k.velocity.z}},
        {"acceleration", {k.acceleration.x, k.acceleration.y, k.acceleration.z}},
        {"orientation", {k.orientation.x, k.orientation.y, k.orientation.z, k.orientation.w}},
        {"angular_velocity", {k.angular_velocity.x, k.angular_velocity.y, k.angular_velocity.z}},
        {"angular_acceleration", {k.angular_acceleration.x, k.angular_acceleration.y, k.angular_acceleration.z}}
    };
}
inline void from_json(const nlohmann::json& j, KinematicState& k) {
    if (j.contains("position")) { auto& v = j["position"]; k.position = {v[0], v[1], v[2]}; }
    if (j.contains("velocity")) { auto& v = j["velocity"]; k.velocity = {v[0], v[1], v[2]}; }
    if (j.contains("acceleration")) { auto& v = j["acceleration"]; k.acceleration = {v[0], v[1], v[2]}; }
    if (j.contains("orientation")) { auto& v = j["orientation"]; k.orientation = {v[0], v[1], v[2], v[3]}; }
    if (j.contains("angular_velocity")) { auto& v = j["angular_velocity"]; k.angular_velocity = {v[0], v[1], v[2]}; }
    if (j.contains("angular_acceleration")) { auto& v = j["angular_acceleration"]; k.angular_acceleration = {v[0], v[1], v[2]}; }
}

// SensorState 序列化（parameters / user_data 含 std::any，跳过）
inline void to_json(nlohmann::json& j, const SensorState& s) {
    j = nlohmann::json{
        {"sensor_id", s.sensor_id},
        {"sensor_type", s.sensor_type},
        {"mode", s.mode},
        {"boresight_body", {s.boresight_body.x, s.boresight_body.y, s.boresight_body.z}},
        {"fov_azimuth", s.fov_azimuth},
        {"fov_elevation", s.fov_elevation},
        {"beam_direction_world", {s.beam_direction_world.x, s.beam_direction_world.y, s.beam_direction_world.z}},
        {"active", s.active},
        {"timestamp", s.timestamp}
    };
}
inline void from_json(const nlohmann::json& j, SensorState& s) {
    j.at("sensor_id").get_to(s.sensor_id);
    if (j.contains("sensor_type")) j.at("sensor_type").get_to(s.sensor_type);
    if (j.contains("mode")) j.at("mode").get_to(s.mode);
    if (j.contains("boresight_body")) { auto& v = j["boresight_body"]; s.boresight_body = {v[0], v[1], v[2]}; }
    if (j.contains("fov_azimuth")) j.at("fov_azimuth").get_to(s.fov_azimuth);
    if (j.contains("fov_elevation")) j.at("fov_elevation").get_to(s.fov_elevation);
    if (j.contains("beam_direction_world")) { auto& v = j["beam_direction_world"]; s.beam_direction_world = {v[0], v[1], v[2]}; }
    if (j.contains("active")) j.at("active").get_to(s.active);
    if (j.contains("timestamp")) j.at("timestamp").get_to(s.timestamp);
}

// EmitterState 序列化（waveform / user_data 含 std::any，跳过）
inline void to_json(nlohmann::json& j, const EmitterState& e) {
    j = nlohmann::json{
        {"emitter_id", e.emitter_id},
        {"emitter_type", e.emitter_type},
        {"position_body", {e.position_body.x, e.position_body.y, e.position_body.z}},
        {"direction_body", {e.direction_body.x, e.direction_body.y, e.direction_body.z}},
        {"emitting", e.emitting},
        {"next_pulse_time", e.next_pulse_time}
    };
}
inline void from_json(const nlohmann::json& j, EmitterState& e) {
    j.at("emitter_id").get_to(e.emitter_id);
    if (j.contains("emitter_type")) j.at("emitter_type").get_to(e.emitter_type);
    if (j.contains("position_body")) { auto& v = j["position_body"]; e.position_body = {v[0], v[1], v[2]}; }
    if (j.contains("direction_body")) { auto& v = j["direction_body"]; e.direction_body = {v[0], v[1], v[2]}; }
    if (j.contains("emitting")) j.at("emitting").get_to(e.emitting);
    if (j.contains("next_pulse_time")) j.at("next_pulse_time").get_to(e.next_pulse_time);
}

// SimulationEvent 的 JSON 投影（payload 含 std::any，跳过）
inline void to_json(nlohmann::json& j, const SimulationEvent& ev) {
    j = nlohmann::json{
        {"type", static_cast<int>(ev.type)},
        {"source_subsystem", ev.source_subsystem},
        {"source_entity_id", ev.source_entity_id},
        {"target_entity_id", ev.target_entity_id},
        {"frame_id", ev.frame_id},
        {"timestamp", ev.timestamp},
        {"description", ev.description}
    };
}
inline void from_json(const nlohmann::json& j, SimulationEvent& ev) {
    if (j.contains("type")) ev.type = static_cast<EventType>(j.at("type").get<int>());
    if (j.contains("source_subsystem")) j.at("source_subsystem").get_to(ev.source_subsystem);
    if (j.contains("source_entity_id")) j.at("source_entity_id").get_to(ev.source_entity_id);
    if (j.contains("target_entity_id")) j.at("target_entity_id").get_to(ev.target_entity_id);
    if (j.contains("frame_id")) j.at("frame_id").get_to(ev.frame_id);
    if (j.contains("timestamp")) j.at("timestamp").get_to(ev.timestamp);
    if (j.contains("description")) j.at("description").get_to(ev.description);
}

// EquipmentState 序列化（subsystem_health 可序列化；internal_state 含 std::any 跳过）
inline void to_json(nlohmann::json& j, const EquipmentState& e) {
    j = nlohmann::json{
        {"equipment_id", e.equipment_id},
        {"equipment_type", e.equipment_type},
        {"kinematics", e.kinematics},
        {"sensors", e.sensors},
        {"emitters", e.emitters},
        {"health", e.health},
        {"fuel_fraction", e.fuel_fraction},
        {"subsystem_health", e.subsystem_health},
        {"events", e.events}
    };
}
inline void from_json(const nlohmann::json& j, EquipmentState& e) {
    j.at("equipment_id").get_to(e.equipment_id);
    if (j.contains("equipment_type")) j.at("equipment_type").get_to(e.equipment_type);
    if (j.contains("kinematics")) j.at("kinematics").get_to(e.kinematics);
    if (j.contains("sensors")) j.at("sensors").get_to(e.sensors);
    if (j.contains("emitters")) j.at("emitters").get_to(e.emitters);
    if (j.contains("health")) j.at("health").get_to(e.health);
    if (j.contains("fuel_fraction")) j.at("fuel_fraction").get_to(e.fuel_fraction);
    if (j.contains("subsystem_health")) j.at("subsystem_health").get_to(e.subsystem_health);
    if (j.contains("events")) j.at("events").get_to(e.events);
}

// EquipmentStateProduct 序列化
inline void to_json(nlohmann::json& j, const EquipmentStateProduct& p) {
    j = nlohmann::json{
        {"frame_id", p.frame_id},
        {"sim_time", p.sim_time},
        {"dt", p.dt},
        {"equipment_states", p.equipment_states},
        {"global_events", p.global_events}
    };
}
inline void from_json(const nlohmann::json& j, EquipmentStateProduct& p) {
    j.at("frame_id").get_to(p.frame_id);
    j.at("sim_time").get_to(p.sim_time);
    j.at("dt").get_to(p.dt);
    if (j.contains("equipment_states")) j.at("equipment_states").get_to(p.equipment_states);
    if (j.contains("global_events")) j.at("global_events").get_to(p.global_events);
}

} // namespace vsgraysim

#endif // VSGRAYSIM_PRODUCTS_EQUIPMENT_STATE_PRODUCT_H