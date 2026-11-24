#pragma once
/// @file TerrainProduct.h
/// @brief 地形数据产品 —— 不可变、版本化、瓦片寻址。
///
/// 这是 TerrainSystem 的**唯一输出契约**。所有消费者
/// (SceneSystem, Physics, Renderer, Exporters) 仅依赖此头文件。
///
/// 设计原则:
///   - 构造后不可变（编辑时写时复制）。
///   - 基于瓦片的寻址（四叉树 Morton 码）用于流式加载/LOD。
///   - 存储后端无关（文件系统、ZIP、数据库、云存储）。
///   - CRS 感知（EPSG 码 + 局部切平面原点）。
///   - 通过 `std::any` user_data 扩展管线特定载荷。
///
/// JSON 序列化支持：通过 nlohmann::json 的 ADL 实现 to_json/from_json。

#ifndef VSGRAYSIM_PRODUCTS_TERRAIN_PRODUCT_H
#define VSGRAYSIM_PRODUCTS_TERRAIN_PRODUCT_H

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include <vsg/maths/box.h>
#include <vsg/core/ref_ptr.h>

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <any>
#include <optional>

#include <nlohmann/json.hpp>

namespace vsgraysim {

// 物理碰撞形状缓存的前向声明
class btHeightfieldTerrainShape;

/// 导出格式枚举
enum class HeightmapExportFormat {
    PNG_16BIT,      // 16位灰度 PNG
    EXR_32BIT_FLOAT,// OpenEXR 32位浮点
    GEOTIFF,        // 带 CRS 元数据的 GeoTIFF
    RAW_BINARY,     // 原始二进制（小端 float32）
    CSV_XYZ         // ASCII XYZ 点云
};

enum class MeshExportFormat {
    GLTF,           // glTF 2.0 (内嵌缓冲区)
    OBJ,            // Wavefront OBJ + MTL
    USD,            // Universal Scene Description
    VSG_NATIVE      // VSG 二进制场景图
};

/// 瓦片坐标键（四叉树 Morton 顺序）
struct TileKey {
    uint64_t morton = 0;  // (lod << 56) | (morton_xy << 8) | tile_id
    int      lod = 0;     // 0 = 最高分辨率
    uint32_t tx = 0;      // 该 LOD 下的瓦片 X 索引
    uint32_t ty = 0;      // 该 LOD 下的瓦片 Y 索引

    bool operator==(const TileKey& other) const { return morton == other.morton; }
    bool operator<(const TileKey& other) const { return morton < other.morton; }
};

/// 瓦片数据载荷（高度 + 可选属性）
struct TileData {
    TileKey key;
    std::vector<float> height;      // (tile_size+1)² 采样，行优先
    std::vector<uint8_t> splat;     // 4通道生物群系掩码 (R=草, G=岩, B=雪, A=沙) —— 可选
    std::vector<uint8_t> normal;    // 压缩法线（八面体编码，2字节/顶点） —— 可选
    std::vector<float> water_mask;  // 水深/存在性 (0=干燥, >0=水深) —— 可选

    // 元数据
    double min_height = 0.0;
    double max_height = 0.0;
    bool   has_splat = false;
    bool   has_normal = false;
    bool   has_water = false;
};

/// 矢量要素（河流、道路、水体、边界）
struct VectorFeature {
    enum class Type { River, Lake, Ocean, Road, Boundary, Contour, Custom } type;
    std::string name;
    std::vector<vsg::dvec3> vertices;  // 世界坐标 (z = 高度)
    std::unordered_map<std::string, std::string> attributes;  // 如 "flow_rate", "width", "material"
    std::any user_data;                // 管线特定（如样条控制点）
};

/// 坐标参考系统描述
struct CRSInfo {
    std::string epsg_code;        // 如 "EPSG:4978" (ECEF), "EPSG:4326" (WGS84), "EPSG:3857" (Web Mercator)
    std::string wkt;              // 完整 WKT 表示
    vsg::dvec3 local_origin;      // 局部切平面原点 (ECEF 米) 用于 LOCAL_TANGENT
    bool is_projected = false;    // True 表示投影坐标(米)，False 表示地理坐标(度)
};

/// 瓦片存储后端抽象
class ITileStore {
public:
    virtual ~ITileStore() = default;

    /// 读取瓦片（阻塞）。未找到返回空 optional。
    virtual std::optional<TileData> read(const TileKey& key) const = 0;

    /// 写入瓦片（阻塞）。覆盖现有。
    virtual void write(const TileKey& key, const TileData& data) = 0;

    /// 检查存在性
    virtual bool exists(const TileKey& key) const = 0;

    /// 列出所有瓦片键（用于枚举/导出）
    virtual std::vector<TileKey> listKeys() const = 0;

    /// 获取存储信息（路径、压缩等）
    virtual std::string describe() const = 0;

    /// 刷新待写入数据
    virtual void flush() = 0;
};

using TileStorePtr = std::shared_ptr<ITileStore>;

/// TerrainProduct —— 完整、不可变的地形数据产品。
struct TerrainProduct {
    // 身份与版本
    std::string product_id;           // UUID v4
    std::string name;
    uint32_t version_major = 1;
    uint32_t version_minor = 0;

    // 空间参考
    CRSInfo crs;
    vsg::dbox world_bounds;          // 世界空间 AABB (米)
    double resolution = 1.0;          // 最高分辨率 (米/采样)
    uint32_t tile_size = 64;          // 瓦片边长顶点数 (瓦片为 (N+1)² 顶点)

    // 瓦片存储（懒加载）
    TileStorePtr tile_store;

    // 矢量要素（河流、海岸线等）
    std::vector<VectorFeature> vector_features;

    // 生物群系/材质规则（由 splat 通道引用）
    struct BiomeRule {
        std::string name;
        uint8_t splat_channel = 0;    // 0=R, 1=G, 2=B, 3=A
        double min_height = -1e9;
        double max_height = 1e9;
        double min_slope = 0.0;
        double max_slope = 90.0;
        vsg::vec4 color = {0.5f, 0.5f, 0.5f, 1.0f};
        std::string material_name;    // PBR 材质引用
    };
    std::vector<BiomeRule> biome_rules;

    // 物理碰撞形状（懒生成，可变缓存）
    mutable std::shared_ptr<btHeightfieldTerrainShape> collision_shape_cache;

    // 管线特定扩展
    std::unordered_map<std::string, std::any> user_data;

    // ---- 查询辅助 ----

    /// 获取世界坐标处的高度（双线性插值，按需加载瓦片）
    /// 越界或瓦片缺失返回 NaN。
    double heightAt(double wx, double wz) const;

    /// 获取世界坐标处的高度（const、不抛异常）
    bool tryHeightAt(double wx, double wz, double& out_height) const;

    /// 获取瓦片数据（从存储加载）
    std::optional<TileData> getTile(const TileKey& key) const;

    /// 获取包含世界坐标的瓦片键
    TileKey keyAt(double wx, double wz) const;

    /// 世界包围盒检查
    bool contains(double wx, double wz) const {
        return wx >= world_bounds.min.x && wx <= world_bounds.max.x &&
               wz >= world_bounds.min.z && wz <= world_bounds.max.z;
    }

    // ---- 导出辅助 ----

    /// 导出高程图到文件（以最大分辨率采样整个地形）
    bool exportHeightmap(const std::string& path, HeightmapExportFormat fmt) const;

    /// 导出网格（可选择 LOD）
    bool exportMesh(const std::string& path, MeshExportFormat fmt, int max_lod = 0) const;

    /// 导出矢量要素为 GeoJSON
    bool exportVectorFeatures(const std::string& path) const;

    // ---- 工厂 ----

    /// 创建带给定规格的空产品
    static std::shared_ptr<TerrainProduct> create(const CRSInfo& crs,
                                                  const vsg::dbox& bounds,
                                                  double resolution,
                                                  uint32_t tile_size = 64);

    /// 从 .vsgterrain 容器加载 (ZIP + manifest)
    static std::shared_ptr<TerrainProduct> load(const std::string& path);

    /// 保存到 .vsgterrain 容器
    bool save(const std::string& path) const;

    /// 生成 UUID v4
    static std::string generateUUID();
};

using TerrainProductPtr = std::shared_ptr<TerrainProduct>;
using TerrainProductConstPtr = std::shared_ptr<const TerrainProduct>;

} // namespace vsgraysim

// TileKey 的哈希特化
namespace std {
template<>
struct hash<vsgraysim::TileKey> {
    size_t operator()(const vsgraysim::TileKey& k) const noexcept {
        return std::hash<uint64_t>{}(k.morton);
    }
};
}

// nlohmann/json ADL 序列化支持
namespace vsgraysim {

// TileKey 序列化
inline void to_json(nlohmann::json& j, const TileKey& k) {
    j = nlohmann::json{{"morton", k.morton}, {"lod", k.lod}, {"tx", k.tx}, {"ty", k.ty}};
}
inline void from_json(const nlohmann::json& j, TileKey& k) {
    j.at("morton").get_to(k.morton);
    j.at("lod").get_to(k.lod);
    j.at("tx").get_to(k.tx);
    j.at("ty").get_to(k.ty);
}

// TileData 序列化
inline void to_json(nlohmann::json& j, const TileData& t) {
    j = nlohmann::json{
        {"key", t.key},
        {"height", t.height},
        {"splat", t.splat},
        {"normal", t.normal},
        {"water_mask", t.water_mask},
        {"min_height", t.min_height},
        {"max_height", t.max_height},
        {"has_splat", t.has_splat},
        {"has_normal", t.has_normal},
        {"has_water", t.has_water}
    };
}
inline void from_json(const nlohmann::json& j, TileData& t) {
    j.at("key").get_to(t.key);
    j.at("height").get_to(t.height);
    if (j.contains("splat")) j.at("splat").get_to(t.splat);
    if (j.contains("normal")) j.at("normal").get_to(t.normal);
    if (j.contains("water_mask")) j.at("water_mask").get_to(t.water_mask);
    j.at("min_height").get_to(t.min_height);
    j.at("max_height").get_to(t.max_height);
    j.at("has_splat").get_to(t.has_splat);
    j.at("has_normal").get_to(t.has_normal);
    j.at("has_water").get_to(t.has_water);
}

// VectorFeature 序列化
inline void to_json(nlohmann::json& j, const VectorFeature& v) {
    nlohmann::json vertices = nlohmann::json::array();
    for (const auto& vtx : v.vertices) {
        vertices.push_back({vtx.x, vtx.y, vtx.z});
    }
    j = nlohmann::json{
        {"type", static_cast<int>(v.type)},
        {"name", v.name},
        {"vertices", vertices},
        {"attributes", v.attributes}
    };
}
inline void from_json(const nlohmann::json& j, VectorFeature& v) {
    j.at("type").get_to(v.type);
    j.at("name").get_to(v.name);
    if (j.contains("vertices")) {
        for (const auto& vtx : j["vertices"]) {
            v.vertices.push_back({vtx[0], vtx[1], vtx[2]});
        }
    }
    if (j.contains("attributes")) j.at("attributes").get_to(v.attributes);
}

// CRSInfo 序列化
inline void to_json(nlohmann::json& j, const CRSInfo& c) {
    j = nlohmann::json{
        {"epsg_code", c.epsg_code},
        {"wkt", c.wkt},
        {"local_origin", {c.local_origin.x, c.local_origin.y, c.local_origin.z}},
        {"is_projected", c.is_projected}
    };
}
inline void from_json(const nlohmann::json& j, CRSInfo& c) {
    j.at("epsg_code").get_to(c.epsg_code);
    if (j.contains("wkt")) j.at("wkt").get_to(c.wkt);
    if (j.contains("local_origin")) {
        auto& lo = j["local_origin"];
        c.local_origin = {lo[0], lo[1], lo[2]};
    }
    j.at("is_projected").get_to(c.is_projected);
}

// BiomeRule 序列化
inline void to_json(nlohmann::json& j, const TerrainProduct::BiomeRule& b) {
    j = nlohmann::json{
        {"name", b.name},
        {"splat_channel", b.splat_channel},
        {"min_height", b.min_height},
        {"max_height", b.max_height},
        {"min_slope", b.min_slope},
        {"max_slope", b.max_slope},
        {"color", {b.color.r, b.color.g, b.color.b, b.color.a}},
        {"material_name", b.material_name}
    };
}
inline void from_json(const nlohmann::json& j, TerrainProduct::BiomeRule& b) {
    j.at("name").get_to(b.name);
    j.at("splat_channel").get_to(b.splat_channel);
    j.at("min_height").get_to(b.min_height);
    j.at("max_height").get_to(b.max_height);
    j.at("min_slope").get_to(b.min_slope);
    j.at("max_slope").get_to(b.max_slope);
    if (j.contains("color")) {
        auto& c = j["color"];
        b.color = vsg::vec4(c[0], c[1], c[2], c[3]);
    }
    if (j.contains("material_name")) j.at("material_name").get_to(b.material_name);
}

// TerrainProduct 序列化
// 注意：tile_store / collision_shape_cache / user_data 为运行时对象，跳过。
// 瓦片数据通过独立瓦片文件（Tiles/）懒加载，不嵌入 product.json。
inline void to_json(nlohmann::json& j, const TerrainProduct& t) {
    j = nlohmann::json{
        {"product_id", t.product_id},
        {"name", t.name},
        {"version_major", t.version_major},
        {"version_minor", t.version_minor},
        {"crs", t.crs},
        {"world_bounds", {
            {"min", {t.world_bounds.min.x, t.world_bounds.min.y, t.world_bounds.min.z}},
            {"max", {t.world_bounds.max.x, t.world_bounds.max.y, t.world_bounds.max.z}}
        }},
        {"resolution", t.resolution},
        {"tile_size", t.tile_size},
        {"vector_features", t.vector_features},
        {"biome_rules", t.biome_rules}
    };
}
inline void from_json(const nlohmann::json& j, TerrainProduct& t) {
    j.at("product_id").get_to(t.product_id);
    if (j.contains("name")) j.at("name").get_to(t.name);
    if (j.contains("version_major")) j.at("version_major").get_to(t.version_major);
    if (j.contains("version_minor")) j.at("version_minor").get_to(t.version_minor);
    if (j.contains("crs")) j.at("crs").get_to(t.crs);
    if (j.contains("world_bounds") && j["world_bounds"].is_object()) {
        auto& wb = j["world_bounds"];
        if (wb.contains("min")) { auto& v = wb["min"]; t.world_bounds.min = {v[0], v[1], v[2]}; }
        if (wb.contains("max")) { auto& v = wb["max"]; t.world_bounds.max = {v[0], v[1], v[2]}; }
    }
    if (j.contains("resolution")) j.at("resolution").get_to(t.resolution);
    if (j.contains("tile_size")) j.at("tile_size").get_to(t.tile_size);
    if (j.contains("vector_features")) j.at("vector_features").get_to(t.vector_features);
    if (j.contains("biome_rules")) j.at("biome_rules").get_to(t.biome_rules);
}

} // namespace vsgraysim

#endif // VSGRAYSIM_PRODUCTS_TERRAIN_PRODUCT_H