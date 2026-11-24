#pragma once
/// @file ProjectSerializer.h
/// @brief Serialization for .vsgterrain and .vsgscene containers (ZIP + manifest + JSON).

#ifndef VSGRAYSIM_PROJECT_PROJECTSERIALIZER_H
#define VSGRAYSIM_PROJECT_PROJECTSERIALIZER_H

#include "vsgraysim/products/TerrainProduct.h"
#include "vsgraysim/products/SceneProduct.h"

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <any>
#include <optional>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace vsgraysim {

/// Manifest for .vsgterrain / .vsgscene containers
struct ContainerManifest {
    std::string container_type;       // "vsgterrain" or "vsgscene"
    uint32_t format_version = 1;      // Container format version
    std::string product_id;           // UUID of contained product
    std::string product_type;         // "TerrainProduct" or "SceneProduct"
    uint32_t product_version_major = 1;
    uint32_t product_version_minor = 0;
    std::string created_timestamp;    // ISO 8601
    std::string modified_timestamp;   // ISO 8601
    std::string creator;              // Application name/version
    std::unordered_map<std::string, std::string> file_hashes;  // filename -> SHA256
    std::unordered_map<std::string, std::string> metadata;     // Custom metadata (string only for JSON)

    // JSON serialization
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ContainerManifest,
        container_type, format_version, product_id, product_type,
        product_version_major, product_version_minor,
        created_timestamp, modified_timestamp, creator,
        file_hashes, metadata)
};

/// Serialization options
struct SerializeOptions {
    bool compress = true;             // Use ZIP compression
    int compression_level = 6;        // 1-9 (zlib)
    bool include_thumbnail = true;    // Generate 256x256 thumbnail
    bool validate_schema = true;      // Validate JSON against schema
    std::vector<std::string> exclude_patterns;  // Glob patterns to exclude
};

/// Deserialization options
struct DeserializeOptions {
    bool validate_schema = true;
    bool verify_hashes = true;
    bool lazy_load = true;            // Don't load all tiles into memory
};

/// ProjectSerializer — handles .vsgterrain and .vsgscene containers
class ProjectSerializer {
public:
    ProjectSerializer();
    ~ProjectSerializer() = default;

    // ---- TerrainProduct ----

    /// Save TerrainProduct to .vsgterrain container
    bool saveTerrain(const TerrainProductPtr& product, const std::string& path,
                     const SerializeOptions& options = {});

    /// Load TerrainProduct from .vsgterrain container
    TerrainProductPtr loadTerrain(const std::string& path,
                                  const DeserializeOptions& options = {});

    /// Quick inspect (read manifest only)
    std::optional<ContainerManifest> inspectTerrain(const std::string& path);

    // ---- SceneProduct ----

    /// Save SceneProduct to .vsgscene container
    bool saveScene(const SceneProductPtr& product, const std::string& path,
                   const SerializeOptions& options = {});

    /// Load SceneProduct from .vsgscene container
    SceneProductPtr loadScene(const std::string& path,
                              const DeserializeOptions& options = {});

    /// Quick inspect
    std::optional<ContainerManifest> inspectScene(const std::string& path);

    // ---- Generic container operations ----

    /// List all product files in container
    std::vector<std::string> listContainerContents(const std::string& path) const;

    /// Extract specific file from container
    bool extractFile(const std::string& container_path, const std::string& internal_path,
                     const std::string& output_path) const;

    /// Validate container integrity
    bool validateContainer(const std::string& path);

    // ---- Schema validation ----

    /// Set custom JSON schema for validation
    void setTerrainSchema(const nlohmann::json& schema);
    void setSceneSchema(const nlohmann::json& schema);

    /// Get current schemas
    const nlohmann::json& getTerrainSchema() const;
    const nlohmann::json& getSceneSchema() const;

private:
    nlohmann::json terrain_schema_;
    nlohmann::json scene_schema_;

    // Internal implementation
    bool writeContainer(const std::string& path, const ContainerManifest& manifest,
                        const std::unordered_map<std::string, std::string>& files,
                        const SerializeOptions& options);

    bool readContainer(const std::string& path, ContainerManifest& manifest,
                       std::unordered_map<std::string, std::string>& files,
                       const DeserializeOptions& options);

    std::string generateThumbnail(const TerrainProductPtr& product) const;
    std::string generateThumbnail(const SceneProductPtr& product) const;

    // JSON 序列化辅助（委托给产品头的 ADL to_json/from_json）
    nlohmann::json serializeTerrainProduct(const TerrainProductPtr& product) const;
    nlohmann::json serializeSceneProduct(const SceneProductPtr& product) const;
    TerrainProductPtr deserializeTerrainProduct(const nlohmann::json& json) const;
    SceneProductPtr deserializeSceneProduct(const nlohmann::json& json) const;
};

} // namespace vsgraysim

#endif // VSGRAYSIM_PROJECT_PROJECTSERIALIZER_H