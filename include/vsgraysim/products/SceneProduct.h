#pragma once
/// @file SceneProduct.h
/// @brief 场景数据产品 —— 实体层级、预制件、导航、环境。
///
/// 这是 SceneSystem 的**唯一输出契约**。被 PlatformEquipmentSystem、
/// DisplaySystem、导出器引用。使用 AssetRef<TerrainProduct> 表达跨产品依赖。
///
/// 设计原则:
///   - 构造后不可变（编辑时写时复制）。
///   - 装配与实例化分离：Prefab 是模板，Entity 是对模板的实例化。
///   - 分层：图层(Layer)以标签方式归属实体，可批量可见/可选中。
///   - 通过 `std::any` user_data 扩展管线特定载荷。
///
/// JSON 序列化支持：通过 nlohmann::json 的 ADL 实现 to_json/from_json。
/// 注意：包含 std::any 的字段（user_data / components / vehicle_params）
/// 无法直接表示为 JSON，序列化时跳过。

#ifndef VSGRAYSIM_PRODUCTS_SCENE_PRODUCT_H
#define VSGRAYSIM_PRODUCTS_SCENE_PRODUCT_H

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/quat.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/transform.h>
#include <vsg/core/ref_ptr.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <any>
#include <optional>

#include <nlohmann/json.hpp>

#include "vsgraysim/products/TerrainProduct.h"

namespace vsgraysim {

/// Forward declaration for asset reference
template<typename T>
class AssetRef;

/// Transform component (world space)
struct Transform {
    vsg::dvec3 translation = {0, 0, 0};
    vsg::dquat rotation = {0, 0, 0, 1};
    vsg::dvec3 scale = {1, 1, 1};

    vsg::dmat4 matrix() const;
    static Transform fromMatrix(const vsg::dmat4& m);
};

/// Physics description for an entity/prefab
struct PhysicsDesc {
    enum class Type { Static, Kinematic, Dynamic, Character } type = Type::Static;
    double mass = 1.0;
    vsg::dvec3 inertia = {1, 1, 1};
    vsg::dvec3 com_offset = {0, 0, 0};  // Center of mass offset

    struct Collider {
        enum class Shape { Box, Sphere, Capsule, Cylinder, ConvexHull, TriangleMesh, Heightfield } shape;
        vsg::dvec3 half_extents = {1, 1, 1};  // Box
        double radius = 1.0;                    // Sphere/Capsule/Cylinder
        double height = 2.0;                    // Capsule/Cylinder
        std::string mesh_path;                  // ConvexHull/TriangleMesh
        Transform local_transform;
    };
    std::vector<Collider> colliders;

    // Vehicle/character specific
    bool is_vehicle = false;
    bool is_character = false;
    std::unordered_map<std::string, std::any> vehicle_params;  // Engine, suspension, etc.
};

/// Component data (type-erased, stored as JSON-compatible map)
using ComponentData = std::unordered_map<std::string, std::any>;

/// Entity definition
struct Entity {
    std::string entity_id;            // UUID
    std::string name;
    Transform world_transform;
    std::string prefab_ref;           // References PrefabLibrary::prefabs[prefab_ref]
    std::vector<std::string> tags;    // "radar_target", "friendly", "building", etc.
    std::vector<std::string> layers;  // "visual", "physics", "navigation", "radar"
    ComponentData components;         // Arbitrary key-value (scripts, custom properties)
    std::any user_data;               // Pipeline-specific
};

/// Prefab definition (reusable entity template)
struct Prefab {
    std::string prefab_id;            // UUID
    std::string name;

    // Visual
    std::string gltf_path;            // glTF 2.0 file (meshes, materials, animations)
    std::unordered_map<std::string, std::string> material_overrides;  // material_name -> override_path

    // Physics
    PhysicsDesc physics;

    // Default components (merged into entity on instantiate)
    ComponentData default_components;

    // LOD configuration
    struct LODLevel {
        double screen_size_threshold = 0.0;  // 0 = base LOD
        std::string gltf_path;
    };
    std::vector<LODLevel> lods;

    std::any user_data;
};

/// Layer system (logical grouping)
struct LayerSystem {
    struct Layer {
        std::string name;
        bool visible = true;
        bool selectable = true;
        std::vector<std::string> entity_tags;  // Entities with any of these tags belong here
    };
    std::vector<Layer> layers;
    std::string default_layer = "default";
};

/// Navigation mesh (recast/detour compatible)
struct NavMesh {
    std::vector<float> vertices;      // 3 floats per vertex
    std::vector<uint32_t> indices;    // 3 indices per triangle (CCW)
    std::vector<uint8_t> areas;       // Area type per polygon
    double cell_size = 0.3;
    double cell_height = 0.2;
    double agent_radius = 0.5;
    double agent_height = 1.8;
    double agent_max_climb = 0.9;
    double agent_max_slope = 45.0;
    std::any user_data;               // Detour navmesh pointer, etc.
};

/// Environment settings (sky, lighting, atmosphere, ocean)
struct EnvironmentSettings {
    // Sky
    std::string sky_type = "physical";  // "physical", "hdri", "gradient", "simple"
    std::string hdri_path;
    vsg::vec3 ground_color = {0.3f, 0.3f, 0.3f};
    vsg::vec3 zenith_color = {0.5f, 0.7f, 1.0f};

    // Sun
    vsg::dvec3 sun_direction = {0.3, -0.5, -0.8};  // Normalized, pointing TO sun
    vsg::vec3 sun_color = {1.0f, 0.95f, 0.8f};
    double sun_intensity = 100000.0;  // lux
    double sun_angular_diameter = 0.53;  // degrees

    // Atmosphere
    bool enable_atmosphere = true;
    double rayleigh_scale = 1.0;
    double mie_scale = 0.001;
    vsg::vec3 absorption = {0.0, 0.0, 0.0};

    // Fog
    bool enable_fog = true;
    vsg::vec3 fog_color = {0.7f, 0.75f, 0.8f};
    double fog_density = 0.0001;
    double fog_start = 0.0;
    double fog_end = 10000.0;

    // Ocean / water
    bool enable_ocean = false;
    double ocean_level = 0.0;
    vsg::vec3 water_color = {0.05f, 0.15f, 0.25f};
    double wave_amplitude = 0.5;
    double wave_wavelength = 50.0;
    double wave_speed = 5.0;

    // Time of day (for physical sky)
    double time_of_day = 12.0;  // 0-24 hours
    double latitude = 39.9;
    double longitude = 116.4;
    int day_of_year = 172;

    std::any user_data;
};

/// Asset reference (weak, resolved at runtime)
template<typename T>
class AssetRef {
public:
    AssetRef() = default;
    explicit AssetRef(const std::string& product_id) : product_id_(product_id) {}

    const std::string& productId() const { return product_id_; }
    bool isValid() const { return !product_id_.empty(); }

    /// Resolve to actual product (requires ProjectManager/AssetIndex)
    std::shared_ptr<const T> resolve() const;

    bool operator==(const AssetRef& other) const { return product_id_ == other.product_id_; }
    bool operator<(const AssetRef& other) const { return product_id_ < other.product_id_; }

private:
    std::string product_id_;
};

/// SceneProduct — complete scene description
struct SceneProduct {
    std::string product_id;           // UUID
    std::string name;
    uint32_t version_major = 1;
    uint32_t version_minor = 0;

    // Referenced terrain (optional)
    AssetRef<TerrainProduct> terrain_ref;

    // Entities
    std::vector<Entity> entities;

    // Prefab library
    std::unordered_map<std::string, Prefab> prefab_library;

    // Layer system
    LayerSystem layers;

    // Navigation
    std::shared_ptr<NavMesh> navmesh;

    // Environment
    EnvironmentSettings environment;

    // Pipeline extensions
    std::unordered_map<std::string, std::any> user_data;

    // ---- Query helpers ----

    /// Find entity by ID
    const Entity* findEntity(const std::string& entity_id) const;

    /// Find entities by tag
    std::vector<const Entity*> findEntitiesByTag(const std::string& tag) const;

    /// Find entities by layer
    std::vector<const Entity*> findEntitiesByLayer(const std::string& layer) const;

    /// Get prefab
    const Prefab* getPrefab(const std::string& prefab_ref) const;

    // ---- Export ----

    /// Export to USD (Universal Scene Description)
    bool exportUSD(const std::string& path) const;

    /// Export to glTF Scene
    bool exportGLTF(const std::string& path) const;

    // ---- Factory ----

    static std::shared_ptr<SceneProduct> create(const std::string& name = "NewScene");
    static std::shared_ptr<SceneProduct> load(const std::string& path);  // .vsgscene
    bool save(const std::string& path) const;
};

using SceneProductPtr = std::shared_ptr<SceneProduct>;
using SceneProductConstPtr = std::shared_ptr<const SceneProduct>;

// nlohmann/json ADL 序列化支持
// 注意：std::any 相关字段（user_data / components / vehicle_params）
// 无法表示为 JSON，序列化时跳过。

// Transform 序列化
inline void to_json(nlohmann::json& j, const Transform& t) {
    j = nlohmann::json{
        {"translation", {t.translation.x, t.translation.y, t.translation.z}},
        {"rotation", {t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w}},
        {"scale", {t.scale.x, t.scale.y, t.scale.z}}
    };
}
inline void from_json(const nlohmann::json& j, Transform& t) {
    if (j.contains("translation")) {
        auto& v = j["translation"];
        t.translation = {v[0], v[1], v[2]};
    }
    if (j.contains("rotation")) {
        auto& v = j["rotation"];
        t.rotation = {v[0], v[1], v[2], v[3]};
    }
    if (j.contains("scale")) {
        auto& v = j["scale"];
        t.scale = {v[0], v[1], v[2]};
    }
}

// PhysicsDesc::Collider 序列化
inline void to_json(nlohmann::json& j, const PhysicsDesc::Collider& c) {
    j = nlohmann::json{
        {"shape", static_cast<int>(c.shape)},
        {"half_extents", {c.half_extents.x, c.half_extents.y, c.half_extents.z}},
        {"radius", c.radius},
        {"height", c.height},
        {"mesh_path", c.mesh_path},
        {"local_transform", c.local_transform}
    };
}
inline void from_json(const nlohmann::json& j, PhysicsDesc::Collider& c) {
    if (j.contains("shape")) c.shape = static_cast<PhysicsDesc::Collider::Shape>(j.at("shape").get<int>());
    if (j.contains("half_extents")) {
        auto& v = j["half_extents"];
        c.half_extents = {v[0], v[1], v[2]};
    }
    if (j.contains("radius")) j.at("radius").get_to(c.radius);
    if (j.contains("height")) j.at("height").get_to(c.height);
    if (j.contains("mesh_path")) j.at("mesh_path").get_to(c.mesh_path);
    if (j.contains("local_transform")) j.at("local_transform").get_to(c.local_transform);
}

// PhysicsDesc 序列化（vehicle_params 含 std::any，跳过）
inline void to_json(nlohmann::json& j, const PhysicsDesc& p) {
    j = nlohmann::json{
        {"type", static_cast<int>(p.type)},
        {"mass", p.mass},
        {"inertia", {p.inertia.x, p.inertia.y, p.inertia.z}},
        {"com_offset", {p.com_offset.x, p.com_offset.y, p.com_offset.z}},
        {"colliders", p.colliders},
        {"is_vehicle", p.is_vehicle},
        {"is_character", p.is_character}
    };
}
inline void from_json(const nlohmann::json& j, PhysicsDesc& p) {
    if (j.contains("type")) p.type = static_cast<PhysicsDesc::Type>(j.at("type").get<int>());
    if (j.contains("mass")) j.at("mass").get_to(p.mass);
    if (j.contains("inertia")) {
        auto& v = j["inertia"];
        p.inertia = {v[0], v[1], v[2]};
    }
    if (j.contains("com_offset")) {
        auto& v = j["com_offset"];
        p.com_offset = {v[0], v[1], v[2]};
    }
    if (j.contains("colliders")) j.at("colliders").get_to(p.colliders);
    if (j.contains("is_vehicle")) j.at("is_vehicle").get_to(p.is_vehicle);
    if (j.contains("is_character")) j.at("is_character").get_to(p.is_character);
}

// Entity 序列化（components / user_data 含 std::any，跳过）
inline void to_json(nlohmann::json& j, const Entity& e) {
    j = nlohmann::json{
        {"entity_id", e.entity_id},
        {"name", e.name},
        {"world_transform", e.world_transform},
        {"prefab_ref", e.prefab_ref},
        {"tags", e.tags},
        {"layers", e.layers}
    };
}
inline void from_json(const nlohmann::json& j, Entity& e) {
    j.at("entity_id").get_to(e.entity_id);
    if (j.contains("name")) j.at("name").get_to(e.name);
    if (j.contains("world_transform")) j.at("world_transform").get_to(e.world_transform);
    if (j.contains("prefab_ref")) j.at("prefab_ref").get_to(e.prefab_ref);
    if (j.contains("tags")) j.at("tags").get_to(e.tags);
    if (j.contains("layers")) j.at("layers").get_to(e.layers);
}

// Prefab::LODLevel 序列化
inline void to_json(nlohmann::json& j, const Prefab::LODLevel& l) {
    j = nlohmann::json{
        {"screen_size_threshold", l.screen_size_threshold},
        {"gltf_path", l.gltf_path}
    };
}
inline void from_json(const nlohmann::json& j, Prefab::LODLevel& l) {
    if (j.contains("screen_size_threshold")) j.at("screen_size_threshold").get_to(l.screen_size_threshold);
    if (j.contains("gltf_path")) j.at("gltf_path").get_to(l.gltf_path);
}

// Prefab 序列化（default_components / user_data 含 std::any，跳过）
inline void to_json(nlohmann::json& j, const Prefab& p) {
    j = nlohmann::json{
        {"prefab_id", p.prefab_id},
        {"name", p.name},
        {"gltf_path", p.gltf_path},
        {"material_overrides", p.material_overrides},
        {"physics", p.physics},
        {"lods", p.lods}
    };
}
inline void from_json(const nlohmann::json& j, Prefab& p) {
    j.at("prefab_id").get_to(p.prefab_id);
    if (j.contains("name")) j.at("name").get_to(p.name);
    if (j.contains("gltf_path")) j.at("gltf_path").get_to(p.gltf_path);
    if (j.contains("material_overrides")) j.at("material_overrides").get_to(p.material_overrides);
    if (j.contains("physics")) j.at("physics").get_to(p.physics);
    if (j.contains("lods")) j.at("lods").get_to(p.lods);
}

// LayerSystem::Layer 序列化
inline void to_json(nlohmann::json& j, const LayerSystem::Layer& l) {
    j = nlohmann::json{
        {"name", l.name},
        {"visible", l.visible},
        {"selectable", l.selectable},
        {"entity_tags", l.entity_tags}
    };
}
inline void from_json(const nlohmann::json& j, LayerSystem::Layer& l) {
    j.at("name").get_to(l.name);
    if (j.contains("visible")) j.at("visible").get_to(l.visible);
    if (j.contains("selectable")) j.at("selectable").get_to(l.selectable);
    if (j.contains("entity_tags")) j.at("entity_tags").get_to(l.entity_tags);
}

// LayerSystem 序列化
inline void to_json(nlohmann::json& j, const LayerSystem& l) {
    j = nlohmann::json{
        {"layers", l.layers},
        {"default_layer", l.default_layer}
    };
}
inline void from_json(const nlohmann::json& j, LayerSystem& l) {
    if (j.contains("layers")) j.at("layers").get_to(l.layers);
    if (j.contains("default_layer")) j.at("default_layer").get_to(l.default_layer);
}

// NavMesh 序列化（user_data 含 Detour 指针，跳过）
inline void to_json(nlohmann::json& j, const NavMesh& n) {
    j = nlohmann::json{
        {"vertices", n.vertices},
        {"indices", n.indices},
        {"areas", n.areas},
        {"cell_size", n.cell_size},
        {"cell_height", n.cell_height},
        {"agent_radius", n.agent_radius},
        {"agent_height", n.agent_height},
        {"agent_max_climb", n.agent_max_climb},
        {"agent_max_slope", n.agent_max_slope}
    };
}
inline void from_json(const nlohmann::json& j, NavMesh& n) {
    if (j.contains("vertices")) j.at("vertices").get_to(n.vertices);
    if (j.contains("indices")) j.at("indices").get_to(n.indices);
    if (j.contains("areas")) j.at("areas").get_to(n.areas);
    if (j.contains("cell_size")) j.at("cell_size").get_to(n.cell_size);
    if (j.contains("cell_height")) j.at("cell_height").get_to(n.cell_height);
    if (j.contains("agent_radius")) j.at("agent_radius").get_to(n.agent_radius);
    if (j.contains("agent_height")) j.at("agent_height").get_to(n.agent_height);
    if (j.contains("agent_max_climb")) j.at("agent_max_climb").get_to(n.agent_max_climb);
    if (j.contains("agent_max_slope")) j.at("agent_max_slope").get_to(n.agent_max_slope);
}

// EnvironmentSettings 序列化（user_data 跳过）
inline void to_json(nlohmann::json& j, const EnvironmentSettings& e) {
    j = nlohmann::json{
        {"sky_type", e.sky_type},
        {"hdri_path", e.hdri_path},
        {"ground_color", {e.ground_color.r, e.ground_color.g, e.ground_color.b}},
        {"zenith_color", {e.zenith_color.r, e.zenith_color.g, e.zenith_color.b}},
        {"sun_direction", {e.sun_direction.x, e.sun_direction.y, e.sun_direction.z}},
        {"sun_color", {e.sun_color.r, e.sun_color.g, e.sun_color.b}},
        {"sun_intensity", e.sun_intensity},
        {"sun_angular_diameter", e.sun_angular_diameter},
        {"enable_atmosphere", e.enable_atmosphere},
        {"rayleigh_scale", e.rayleigh_scale},
        {"mie_scale", e.mie_scale},
        {"absorption", {e.absorption.r, e.absorption.g, e.absorption.b}},
        {"enable_fog", e.enable_fog},
        {"fog_color", {e.fog_color.r, e.fog_color.g, e.fog_color.b}},
        {"fog_density", e.fog_density},
        {"fog_start", e.fog_start},
        {"fog_end", e.fog_end},
        {"enable_ocean", e.enable_ocean},
        {"ocean_level", e.ocean_level},
        {"water_color", {e.water_color.r, e.water_color.g, e.water_color.b}},
        {"wave_amplitude", e.wave_amplitude},
        {"wave_wavelength", e.wave_wavelength},
        {"wave_speed", e.wave_speed},
        {"time_of_day", e.time_of_day},
        {"latitude", e.latitude},
        {"longitude", e.longitude},
        {"day_of_year", e.day_of_year}
    };
}
inline void from_json(const nlohmann::json& j, EnvironmentSettings& e) {
    if (j.contains("sky_type")) j.at("sky_type").get_to(e.sky_type);
    if (j.contains("hdri_path")) j.at("hdri_path").get_to(e.hdri_path);
    if (j.contains("ground_color")) { auto& v = j["ground_color"]; e.ground_color = {v[0], v[1], v[2]}; }
    if (j.contains("zenith_color")) { auto& v = j["zenith_color"]; e.zenith_color = {v[0], v[1], v[2]}; }
    if (j.contains("sun_direction")) { auto& v = j["sun_direction"]; e.sun_direction = {v[0], v[1], v[2]}; }
    if (j.contains("sun_color")) { auto& v = j["sun_color"]; e.sun_color = {v[0], v[1], v[2]}; }
    if (j.contains("sun_intensity")) j.at("sun_intensity").get_to(e.sun_intensity);
    if (j.contains("sun_angular_diameter")) j.at("sun_angular_diameter").get_to(e.sun_angular_diameter);
    if (j.contains("enable_atmosphere")) j.at("enable_atmosphere").get_to(e.enable_atmosphere);
    if (j.contains("rayleigh_scale")) j.at("rayleigh_scale").get_to(e.rayleigh_scale);
    if (j.contains("mie_scale")) j.at("mie_scale").get_to(e.mie_scale);
    if (j.contains("absorption")) { auto& v = j["absorption"]; e.absorption = {v[0], v[1], v[2]}; }
    if (j.contains("enable_fog")) j.at("enable_fog").get_to(e.enable_fog);
    if (j.contains("fog_color")) { auto& v = j["fog_color"]; e.fog_color = {v[0], v[1], v[2]}; }
    if (j.contains("fog_density")) j.at("fog_density").get_to(e.fog_density);
    if (j.contains("fog_start")) j.at("fog_start").get_to(e.fog_start);
    if (j.contains("fog_end")) j.at("fog_end").get_to(e.fog_end);
    if (j.contains("enable_ocean")) j.at("enable_ocean").get_to(e.enable_ocean);
    if (j.contains("ocean_level")) j.at("ocean_level").get_to(e.ocean_level);
    if (j.contains("water_color")) { auto& v = j["water_color"]; e.water_color = {v[0], v[1], v[2]}; }
    if (j.contains("wave_amplitude")) j.at("wave_amplitude").get_to(e.wave_amplitude);
    if (j.contains("wave_wavelength")) j.at("wave_wavelength").get_to(e.wave_wavelength);
    if (j.contains("wave_speed")) j.at("wave_speed").get_to(e.wave_speed);
    if (j.contains("time_of_day")) j.at("time_of_day").get_to(e.time_of_day);
    if (j.contains("latitude")) j.at("latitude").get_to(e.latitude);
    if (j.contains("longitude")) j.at("longitude").get_to(e.longitude);
    if (j.contains("day_of_year")) j.at("day_of_year").get_to(e.day_of_year);
}

// AssetRef<T> 序列化 — 仅序列化 product_id
template<typename T>
inline void to_json(nlohmann::json& j, const AssetRef<T>& r) {
    j = nlohmann::json{{"product_id", r.productId()}};
}
template<typename T>
inline void from_json(const nlohmann::json& j, AssetRef<T>& r) {
    r = AssetRef<T>(j.value("product_id", ""));
}

// SceneProduct 序列化（navmesh 可为空；user_data 跳过）
inline void to_json(nlohmann::json& j, const SceneProduct& s) {
    j = nlohmann::json{
        {"product_id", s.product_id},
        {"name", s.name},
        {"version_major", s.version_major},
        {"version_minor", s.version_minor},
        {"terrain_ref", s.terrain_ref},
        {"entities", s.entities},
        {"prefab_library", s.prefab_library},
        {"layers", s.layers},
        {"environment", s.environment}
    };
    if (s.navmesh) {
        j["navmesh"] = *s.navmesh;
    }
}
inline void from_json(const nlohmann::json& j, SceneProduct& s) {
    j.at("product_id").get_to(s.product_id);
    if (j.contains("name")) j.at("name").get_to(s.name);
    if (j.contains("version_major")) j.at("version_major").get_to(s.version_major);
    if (j.contains("version_minor")) j.at("version_minor").get_to(s.version_minor);
    if (j.contains("terrain_ref")) j.at("terrain_ref").get_to(s.terrain_ref);
    if (j.contains("entities")) j.at("entities").get_to(s.entities);
    if (j.contains("prefab_library")) j.at("prefab_library").get_to(s.prefab_library);
    if (j.contains("layers")) j.at("layers").get_to(s.layers);
    if (j.contains("environment")) j.at("environment").get_to(s.environment);
    if (j.contains("navmesh")) {
        s.navmesh = std::make_shared<NavMesh>();
        j.at("navmesh").get_to(*s.navmesh);
    }
}

} // namespace vsgraysim

#endif // VSGRAYSIM_PRODUCTS_SCENE_PRODUCT_H