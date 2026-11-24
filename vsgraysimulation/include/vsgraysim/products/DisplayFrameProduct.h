#pragma once
/// @file DisplayFrameProduct.h
/// @brief 每帧显示/渲染产品 —— 视图数据、传感器视图、叠加层。
///
/// 由 DisplaySystem 每帧产出。
/// 被 Qt GUI（RadarDisplay, Viewport）、Headless 渲染器、视频录制器消费。
///
/// 设计原则:
///   - 帧级瞬时数据，可丢弃。
///   - 主视图 + 传感器视图（按 sensor_id 键控，顺序稳定）。
///   - 场景图对象（vsg::ref_ptr）仅存在于内存视图，不参与序列化。
///   - 通过 `std::any` user_data 扩展管线特定载荷。
///
/// JSON 序列化支持：通过 nlohmann::json 的 ADL 实现 to_json/from_json。
/// 注意：vsg 场景图对象（scene_root / render_target / overlay_geometry /
/// shadow_map / geometry）为运行时指针，序列化时跳过。

#ifndef VSGRAYSIM_PRODUCTS_DISPLAY_FRAME_PRODUCT_H
#define VSGRAYSIM_PRODUCTS_DISPLAY_FRAME_PRODUCT_H

#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include <vsg/maths/mat4.h>
#include <vsg/nodes/Node.h>
#include <vsg/state/Image.h>
#include <vsg/app/ViewMatrix.h>
#include <vsg/app/ProjectionMatrix.h>
#include <vsg/core/ref_ptr.h>

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <any>
#include <cstdint>

#include <nlohmann/json.hpp>

namespace vsgraysim {

/// Light data for rendering
struct LightData {
    enum class Type { Directional, Point, Spot, Ambient } type = Type::Directional;
    vsg::vec3 color = {1, 1, 1};
    float intensity = 1.0f;
    vsg::dvec3 direction = {0, 0, -1};  // For directional/spot
    vsg::dvec3 position = {0, 0, 0};    // For point/spot
    float range = 1000.0f;
    float inner_angle = 0.0f;  // Spot
    float outer_angle = 45.0f; // Spot
    bool cast_shadow = false;
    vsg::ref_ptr<vsg::Node> shadow_map; // Optional precomputed shadow map
};

/// Environment uniforms for shaders
struct EnvironmentUniforms {
    vsg::vec3 sun_direction = {0.3f, -0.5f, -0.8f};
    vsg::vec3 sun_color = {1.0f, 0.95f, 0.8f};
    float sun_intensity = 100000.0f;
    vsg::vec3 sky_color = {0.5f, 0.7f, 1.0f};
    vsg::vec3 ground_color = {0.3f, 0.3f, 0.3f};
    float fog_density = 0.0001f;
    vsg::vec3 fog_color = {0.7f, 0.75f, 0.8f};
    float time_of_day = 12.0f;
    // Atmosphere
    float rayleigh_scale = 1.0f;
    float mie_scale = 0.001f;
    vsg::vec3 absorption = {0, 0, 0};
    // Ocean
    bool enable_ocean = false;
    float ocean_level = 0.0f;
    vsg::vec3 water_color = {0.05f, 0.15f, 0.25f};
    float wave_amplitude = 0.5f;
    float wave_wavelength = 50.0f;
    float wave_speed = 5.0f;
};

/// Main camera view data
struct ViewData {
    vsg::ref_ptr<vsg::ViewMatrix> view;
    vsg::ref_ptr<vsg::ProjectionMatrix> projection;
    vsg::ref_ptr<vsg::Node> scene_root;      // Complete scene graph (terrain + entities + effects)
    std::vector<LightData> lights;
    EnvironmentUniforms environment;
    vsg::dvec3 camera_position;              // World position for LOD/culling
    double near_far[2] = {0.1, 100000.0};
};

/// Sensor-specific view (radar PPI, IR scope, SAR image plane, etc.)
struct SensorViewData {
    uint32_t sensor_id = 0;
    std::string sensor_type;                 // "RADAR", "IR", "EO", "SAR", "LIDAR"
    std::string view_mode;                   // "PPI", "B-SCOPE", "RANGE_DOPPLER", "WATERFALL", "IMAGE"

    // Render target (VSG texture/image)
    vsg::ref_ptr<vsg::Image> render_target;  // Ready for display
    vsg::ref_ptr<vsg::Node> overlay_geometry; // Optional geometry overlay (beam, tracks, grids)

    // Metadata for UI
    struct ViewInfo {
        double range_max = 100000.0;
        double azimuth_center = 0.0;
        double azimuth_span = 360.0;
        double elevation_center = 0.0;
        double elevation_span = 30.0;
        std::unordered_map<std::string, std::any> parameters;
    } info;
};

/// Debug / diagnostic overlay
struct DebugOverlay {
    enum class Type { Wireframe, BoundingBox, Normals, Velocity, Grid, Text, Custom } type;
    vsg::ref_ptr<vsg::Node> geometry;
    vsg::vec4 color = {1, 1, 0, 1};
    bool depth_test = true;
    std::string label;
    std::any user_data;
};

/// DisplayFrameProduct — per-frame rendering package
struct DisplayFrameProduct {
    uint64_t frame_id = 0;
    double sim_time = 0.0;
    double dt = 0.0;

    // Primary view
    ViewData main_view;

    // Sensor views (keyed by sensor_id)
    std::map<uint32_t, SensorViewData> sensor_views;

    // Debug overlays
    std::vector<DebugOverlay> overlays;

    // Performance stats
    struct Stats {
        double frame_time_ms = 0.0;
        double gpu_time_ms = 0.0;
        uint32_t draw_calls = 0;
        uint32_t triangles = 0;
        uint64_t texture_memory_bytes = 0;
    } stats;

    // Pipeline extensions
    std::unordered_map<std::string, std::any> user_data;

    // ---- Helpers ----

    void clearTransient() {
        sensor_views.clear();
        overlays.clear();
    }
};

using DisplayFrameProductPtr = std::shared_ptr<DisplayFrameProduct>;

// nlohmann/json ADL 序列化支持
// 注意：vsg 场景图指针（scene_root / render_target / overlay_geometry /
// shadow_map / geometry）与 std::any（user_data / parameters）是运行时对象，
// 序列化时跳过。

// LightData 序列化（shadow_map 为 vsg 指针，跳过）
inline void to_json(nlohmann::json& j, const LightData& l) {
    j = nlohmann::json{
        {"type", static_cast<int>(l.type)},
        {"color", {l.color.r, l.color.g, l.color.b}},
        {"intensity", l.intensity},
        {"direction", {l.direction.x, l.direction.y, l.direction.z}},
        {"position", {l.position.x, l.position.y, l.position.z}},
        {"range", l.range},
        {"inner_angle", l.inner_angle},
        {"outer_angle", l.outer_angle},
        {"cast_shadow", l.cast_shadow}
    };
}
inline void from_json(const nlohmann::json& j, LightData& l) {
    if (j.contains("type")) l.type = static_cast<LightData::Type>(j.at("type").get<int>());
    if (j.contains("color")) { auto& v = j["color"]; l.color = {v[0], v[1], v[2]}; }
    if (j.contains("intensity")) j.at("intensity").get_to(l.intensity);
    if (j.contains("direction")) { auto& v = j["direction"]; l.direction = {v[0], v[1], v[2]}; }
    if (j.contains("position")) { auto& v = j["position"]; l.position = {v[0], v[1], v[2]}; }
    if (j.contains("range")) j.at("range").get_to(l.range);
    if (j.contains("inner_angle")) j.at("inner_angle").get_to(l.inner_angle);
    if (j.contains("outer_angle")) j.at("outer_angle").get_to(l.outer_angle);
    if (j.contains("cast_shadow")) j.at("cast_shadow").get_to(l.cast_shadow);
}

// EnvironmentUniforms 序列化
inline void to_json(nlohmann::json& j, const EnvironmentUniforms& e) {
    j = nlohmann::json{
        {"sun_direction", {e.sun_direction.x, e.sun_direction.y, e.sun_direction.z}},
        {"sun_color", {e.sun_color.r, e.sun_color.g, e.sun_color.b}},
        {"sun_intensity", e.sun_intensity},
        {"sky_color", {e.sky_color.r, e.sky_color.g, e.sky_color.b}},
        {"ground_color", {e.ground_color.r, e.ground_color.g, e.ground_color.b}},
        {"fog_density", e.fog_density},
        {"fog_color", {e.fog_color.r, e.fog_color.g, e.fog_color.b}},
        {"time_of_day", e.time_of_day},
        {"rayleigh_scale", e.rayleigh_scale},
        {"mie_scale", e.mie_scale},
        {"absorption", {e.absorption.r, e.absorption.g, e.absorption.b}},
        {"enable_ocean", e.enable_ocean},
        {"ocean_level", e.ocean_level},
        {"water_color", {e.water_color.r, e.water_color.g, e.water_color.b}},
        {"wave_amplitude", e.wave_amplitude},
        {"wave_wavelength", e.wave_wavelength},
        {"wave_speed", e.wave_speed}
    };
}
inline void from_json(const nlohmann::json& j, EnvironmentUniforms& e) {
    if (j.contains("sun_direction")) { auto& v = j["sun_direction"]; e.sun_direction = {v[0], v[1], v[2]}; }
    if (j.contains("sun_color")) { auto& v = j["sun_color"]; e.sun_color = {v[0], v[1], v[2]}; }
    if (j.contains("sun_intensity")) j.at("sun_intensity").get_to(e.sun_intensity);
    if (j.contains("sky_color")) { auto& v = j["sky_color"]; e.sky_color = {v[0], v[1], v[2]}; }
    if (j.contains("ground_color")) { auto& v = j["ground_color"]; e.ground_color = {v[0], v[1], v[2]}; }
    if (j.contains("fog_density")) j.at("fog_density").get_to(e.fog_density);
    if (j.contains("fog_color")) { auto& v = j["fog_color"]; e.fog_color = {v[0], v[1], v[2]}; }
    if (j.contains("time_of_day")) j.at("time_of_day").get_to(e.time_of_day);
    if (j.contains("rayleigh_scale")) j.at("rayleigh_scale").get_to(e.rayleigh_scale);
    if (j.contains("mie_scale")) j.at("mie_scale").get_to(e.mie_scale);
    if (j.contains("absorption")) { auto& v = j["absorption"]; e.absorption = {v[0], v[1], v[2]}; }
    if (j.contains("enable_ocean")) j.at("enable_ocean").get_to(e.enable_ocean);
    if (j.contains("ocean_level")) j.at("ocean_level").get_to(e.ocean_level);
    if (j.contains("water_color")) { auto& v = j["water_color"]; e.water_color = {v[0], v[1], v[2]}; }
    if (j.contains("wave_amplitude")) j.at("wave_amplitude").get_to(e.wave_amplitude);
    if (j.contains("wave_wavelength")) j.at("wave_wavelength").get_to(e.wave_wavelength);
    if (j.contains("wave_speed")) j.at("wave_speed").get_to(e.wave_speed);
}

// ViewData 序列化（scene_root / view / projection 为 vsg 指针，跳过）
inline void to_json(nlohmann::json& j, const ViewData& v) {
    j = nlohmann::json{
        {"lights", v.lights},
        {"environment", v.environment},
        {"camera_position", {v.camera_position.x, v.camera_position.y, v.camera_position.z}},
        {"near", v.near_far[0]},
        {"far", v.near_far[1]}
    };
}
inline void from_json(const nlohmann::json& j, ViewData& v) {
    if (j.contains("lights")) j.at("lights").get_to(v.lights);
    if (j.contains("environment")) j.at("environment").get_to(v.environment);
    if (j.contains("camera_position")) { auto& p = j["camera_position"]; v.camera_position = {p[0], p[1], p[2]}; }
    if (j.contains("near")) v.near_far[0] = j["near"].get<double>();
    if (j.contains("far")) v.near_far[1] = j["far"].get<double>();
}

// SensorViewData 序列化（render_target / overlay_geometry 为 vsg 指针，跳过）
inline void to_json(nlohmann::json& j, const SensorViewData& s) {
    j = nlohmann::json{
        {"sensor_id", s.sensor_id},
        {"sensor_type", s.sensor_type},
        {"view_mode", s.view_mode},
        {"range_max", s.info.range_max},
        {"azimuth_center", s.info.azimuth_center},
        {"azimuth_span", s.info.azimuth_span},
        {"elevation_center", s.info.elevation_center},
        {"elevation_span", s.info.elevation_span}
    };
}
inline void from_json(const nlohmann::json& j, SensorViewData& s) {
    if (j.contains("sensor_id")) j.at("sensor_id").get_to(s.sensor_id);
    if (j.contains("sensor_type")) j.at("sensor_type").get_to(s.sensor_type);
    if (j.contains("view_mode")) j.at("view_mode").get_to(s.view_mode);
    if (j.contains("range_max")) j.at("range_max").get_to(s.info.range_max);
    if (j.contains("azimuth_center")) j.at("azimuth_center").get_to(s.info.azimuth_center);
    if (j.contains("azimuth_span")) j.at("azimuth_span").get_to(s.info.azimuth_span);
    if (j.contains("elevation_center")) j.at("elevation_center").get_to(s.info.elevation_center);
    if (j.contains("elevation_span")) j.at("elevation_span").get_to(s.info.elevation_span);
}

// DebugOverlay 序列化（geometry 为 vsg 指针 / user_data 含 std::any，跳过）
inline void to_json(nlohmann::json& j, const DebugOverlay& d) {
    j = nlohmann::json{
        {"type", static_cast<int>(d.type)},
        {"color", {d.color.r, d.color.g, d.color.b, d.color.a}},
        {"depth_test", d.depth_test},
        {"label", d.label}
    };
}
inline void from_json(const nlohmann::json& j, DebugOverlay& d) {
    if (j.contains("type")) d.type = static_cast<DebugOverlay::Type>(j.at("type").get<int>());
    if (j.contains("color")) { auto& v = j["color"]; d.color = vsg::vec4(v[0], v[1], v[2], v[3]); }
    if (j.contains("depth_test")) j.at("depth_test").get_to(d.depth_test);
    if (j.contains("label")) j.at("label").get_to(d.label);
}

// DisplayFrameProduct 序列化（主视图场景图与传感器渲染目标跳过）
inline void to_json(nlohmann::json& j, const DisplayFrameProduct& p) {
    nlohmann::json sensor_views = nlohmann::json::object();
    for (const auto& [id, sv] : p.sensor_views) {
        sensor_views[std::to_string(id)] = sv;
    }
    j = nlohmann::json{
        {"frame_id", p.frame_id},
        {"sim_time", p.sim_time},
        {"dt", p.dt},
        {"main_view", p.main_view},
        {"sensor_views", sensor_views},
        {"overlays", p.overlays},
        {"frame_time_ms", p.stats.frame_time_ms},
        {"gpu_time_ms", p.stats.gpu_time_ms},
        {"draw_calls", p.stats.draw_calls},
        {"triangles", p.stats.triangles},
        {"texture_memory_bytes", p.stats.texture_memory_bytes}
    };
}
inline void from_json(const nlohmann::json& j, DisplayFrameProduct& p) {
    j.at("frame_id").get_to(p.frame_id);
    j.at("sim_time").get_to(p.sim_time);
    j.at("dt").get_to(p.dt);
    if (j.contains("main_view")) j.at("main_view").get_to(p.main_view);
    if (j.contains("sensor_views") && j["sensor_views"].is_object()) {
        for (const auto& [key, sv] : j["sensor_views"].items()) {
            SensorViewData data;
            sv.get_to(data);
            p.sensor_views[static_cast<uint32_t>(std::stoul(key))] = data;
        }
    }
    if (j.contains("overlays")) j.at("overlays").get_to(p.overlays);
    if (j.contains("frame_time_ms")) j.at("frame_time_ms").get_to(p.stats.frame_time_ms);
    if (j.contains("gpu_time_ms")) j.at("gpu_time_ms").get_to(p.stats.gpu_time_ms);
    if (j.contains("draw_calls")) j.at("draw_calls").get_to(p.stats.draw_calls);
    if (j.contains("triangles")) j.at("triangles").get_to(p.stats.triangles);
    if (j.contains("texture_memory_bytes")) j.at("texture_memory_bytes").get_to(p.stats.texture_memory_bytes);
}

} // namespace vsgraysim

#endif // VSGRAYSIM_PRODUCTS_DISPLAY_FRAME_PRODUCT_H