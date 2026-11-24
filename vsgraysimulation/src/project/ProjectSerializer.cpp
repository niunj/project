#include "vsgraysim/project/ProjectSerializer.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <chrono>
#include <ctime>

#include <nlohmann/json.hpp>

#include <zip.h>
#include <zlib.h>

namespace vsgraysim {

// Helper: current timestamp ISO 8601
static std::string currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&in_time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// Helper: SHA256 hash of file (simplified)
static std::string fileHash(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return std::to_string(std::hash<std::string>{}(ss.str()));
}

// Helper: write string to zip entry
static bool zipWriteString(zip_t* zip, const std::string& name, const std::string& content) {
    zip_source_t* source = zip_source_buffer(zip, content.data(), content.size(), 0);
    if (!source) return false;
    if (zip_file_add(zip, name.c_str(), source, ZIP_FL_OVERWRITE) < 0) {
        zip_source_free(source);
        return false;
    }
    return true;
}

// Helper: read string from zip entry
static std::optional<std::string> zipReadString(zip_t* zip, const std::string& name) {
    zip_file_t* file = zip_fopen(zip, name.c_str(), 0);
    if (!file) return std::nullopt;

    std::string content;
    char buffer[4096];
    zip_int64_t n;
    while ((n = zip_fread(file, buffer, sizeof(buffer))) > 0) {
        content.append(buffer, n);
    }
    zip_fclose(file);
    return content;
}

// Default JSON schemas
static nlohmann::json defaultTerrainSchema() {
    return R"({
        "type": "object",
        "properties": {
            "product_id": {"type": "string"},
            "name": {"type": "string"},
            "crs": {"type": "object"},
            "world_bounds": {"type": "object"},
            "resolution": {"type": "number"},
            "tile_size": {"type": "integer"}
        },
        "required": ["product_id", "crs", "world_bounds", "resolution"]
    })"_json;
}

static nlohmann::json defaultSceneSchema() {
    return R"({
        "type": "object",
        "properties": {
            "product_id": {"type": "string"},
            "name": {"type": "string"},
            "terrain_ref": {"type": "object"},
            "entities": {"type": "array"},
            "prefab_library": {"type": "object"}
        },
        "required": ["product_id"]
    })"_json;
}

ProjectSerializer::ProjectSerializer() {
    terrain_schema_ = defaultTerrainSchema();
    scene_schema_ = defaultSceneSchema();
}

bool ProjectSerializer::saveTerrain(const TerrainProductPtr& product, const std::string& path,
                                    const SerializeOptions& options) {
    if (!product) return false;

    // Build manifest
    ContainerManifest manifest;
    manifest.container_type = "vsgterrain";
    manifest.format_version = 1;
    manifest.product_id = product->product_id;
    manifest.product_type = "TerrainProduct";
    manifest.product_version_major = product->version_major;
    manifest.product_version_minor = product->version_minor;
    manifest.created_timestamp = currentTimestamp();
    manifest.modified_timestamp = currentTimestamp();
    manifest.creator = "vsgraysim/1.0";

    // Serialize product to JSON using manual serialization
    nlohmann::json json = serializeTerrainProduct(product);

    std::string product_json = json.dump(options.validate_schema ? 2 : -1);

    // Collect files to write
    std::unordered_map<std::string, std::string> files;
    files["manifest.json"] = nlohmann::json(manifest).dump(2);
    files["product.json"] = product_json;

    return writeContainer(path, manifest, files, options);
}

bool ProjectSerializer::saveScene(const SceneProductPtr& product, const std::string& path,
                                  const SerializeOptions& options) {
    if (!product) return false;

    ContainerManifest manifest;
    manifest.container_type = "vsgscene";
    manifest.format_version = 1;
    manifest.product_id = product->product_id;
    manifest.product_type = "SceneProduct";
    manifest.product_version_major = product->version_major;
    manifest.product_version_minor = product->version_minor;
    manifest.created_timestamp = currentTimestamp();
    manifest.modified_timestamp = currentTimestamp();
    manifest.creator = "vsgraysim/1.0";

    nlohmann::json json = serializeSceneProduct(product);
    std::string product_json = json.dump(options.validate_schema ? 2 : -1);

std::unordered_map<std::string, std::string> files;
    files["manifest.json"] = nlohmann::json(manifest).dump(2);
    files["product.json"] = product_json;

    return writeContainer(path, manifest, files, options);
}

TerrainProductPtr ProjectSerializer::loadTerrain(const std::string& path,
                                                 const DeserializeOptions& options) {
    ContainerManifest manifest;
    std::unordered_map<std::string, std::string> files;

    if (!readContainer(path, manifest, files, options)) {
        return nullptr;
    }

    if (manifest.container_type != "vsgterrain") {
        return nullptr;
    }

    auto it = files.find("product.json");
    if (it == files.end()) return nullptr;

    nlohmann::json json = nlohmann::json::parse(it->second);

    return deserializeTerrainProduct(json);
}

SceneProductPtr ProjectSerializer::loadScene(const std::string& path,
                                             const DeserializeOptions& options) {
    ContainerManifest manifest;
    std::unordered_map<std::string, std::string> files;

    if (!readContainer(path, manifest, files, options)) {
        return nullptr;
    }

    if (manifest.container_type != "vsgscene") {
        return nullptr;
    }

    auto it = files.find("product.json");
    if (it == files.end()) return nullptr;

    nlohmann::json json = nlohmann::json::parse(it->second);

    return deserializeSceneProduct(json);
}

std::optional<ContainerManifest> ProjectSerializer::inspectTerrain(const std::string& path) {
    ContainerManifest manifest;
    std::unordered_map<std::string, std::string> files;
    DeserializeOptions opts;
    opts.validate_schema = false;
    opts.verify_hashes = false;

    if (readContainer(path, manifest, files, opts)) {
        return manifest;
    }
    return std::nullopt;
}

std::optional<ContainerManifest> ProjectSerializer::inspectScene(const std::string& path) {
    ContainerManifest manifest;
    std::unordered_map<std::string, std::string> files;
    DeserializeOptions opts;
    opts.validate_schema = false;
    opts.verify_hashes = false;

    if (readContainer(path, manifest, files, opts)) {
        return manifest;
    }
    return std::nullopt;
}

std::vector<std::string> ProjectSerializer::listContainerContents(const std::string& path) const {
    std::vector<std::string> contents;
    int err = 0;
    zip_t* zip = zip_open(path.c_str(), ZIP_RDONLY, &err);
    if (!zip) return contents;

    zip_int64_t num = zip_get_num_entries(zip, 0);
    for (zip_int64_t i = 0; i < num; ++i) {
        const char* name = zip_get_name(zip, i, 0);
        if (name) contents.push_back(name);
    }
    zip_close(zip);
    return contents;
}

bool ProjectSerializer::extractFile(const std::string& container_path,
                                    const std::string& internal_path,
                                    const std::string& output_path) const {
    int err = 0;
    zip_t* zip = zip_open(container_path.c_str(), ZIP_RDONLY, &err);
    if (!zip) return false;

    zip_file_t* file = zip_fopen(zip, internal_path.c_str(), 0);
    if (!file) {
        zip_close(zip);
        return false;
    }

    std::ofstream out(output_path, std::ios::binary);
    char buffer[4096];
    zip_int64_t n;
    while ((n = zip_fread(file, buffer, sizeof(buffer))) > 0) {
        out.write(buffer, n);
    }
    zip_fclose(file);
    zip_close(zip);
    return true;
}

bool ProjectSerializer::validateContainer(const std::string& path) {
    auto manifest = inspectTerrain(path);
    if (!manifest) {
        manifest = inspectScene(path);
    }
    return manifest.has_value();
}

void ProjectSerializer::setTerrainSchema(const nlohmann::json& schema) {
    terrain_schema_ = schema;
}

void ProjectSerializer::setSceneSchema(const nlohmann::json& schema) {
    scene_schema_ = schema;
}

const nlohmann::json& ProjectSerializer::getTerrainSchema() const {
    return terrain_schema_;
}

const nlohmann::json& ProjectSerializer::getSceneSchema() const {
    return scene_schema_;
}

// ---- JSON 序列化（通过产品头的 ADL to_json/from_json） ----

nlohmann::json ProjectSerializer::serializeTerrainProduct(const TerrainProductPtr& product) const {
    return *product;
}

nlohmann::json ProjectSerializer::serializeSceneProduct(const SceneProductPtr& product) const {
    return *product;
}

TerrainProductPtr ProjectSerializer::deserializeTerrainProduct(const nlohmann::json& json) const {
    auto product = std::make_shared<TerrainProduct>();
    json.get_to(*product);
    return product;
}

SceneProductPtr ProjectSerializer::deserializeSceneProduct(const nlohmann::json& json) const {
    auto product = std::make_shared<SceneProduct>();
    json.get_to(*product);
    return product;
}

bool ProjectSerializer::writeContainer(const std::string& path, const ContainerManifest& manifest,
                                       const std::unordered_map<std::string, std::string>& files,
                                       const SerializeOptions& options) {
    int err = 0;
    zip_t* zip = zip_open(path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!zip) return false;

    // Set compression using zip_set_file_compression
    if (options.compress) {
        // Note: libzip doesn't have zip_set_default_compression
        // Compression is set per file when adding
    }

    // Write manifest
    if (!zipWriteString(zip, "manifest.json", nlohmann::json(manifest).dump(2))) {
        zip_close(zip);
        return false;
    }

    // Write all files
    for (const auto& [name, content] : files) {
        if (!zipWriteString(zip, name, content)) {
            zip_close(zip);
            return false;
        }
    }

    zip_close(zip);
    return true;
}

bool ProjectSerializer::readContainer(const std::string& path, ContainerManifest& manifest,
                                      std::unordered_map<std::string, std::string>& files,
                                      const DeserializeOptions& options) {
    int err = 0;
    zip_t* zip = zip_open(path.c_str(), ZIP_RDONLY, &err);
    if (!zip) return false;

    // Read manifest
    auto manifest_str = zipReadString(zip, "manifest.json");
    if (!manifest_str) {
        zip_close(zip);
        return false;
    }

    try {
        nlohmann::json mjson = nlohmann::json::parse(*manifest_str);
        mjson.get_to(manifest);
    } catch (...) {
        zip_close(zip);
        return false;
    }

    // Read all other files
    zip_int64_t num = zip_get_num_entries(zip, 0);
    for (zip_int64_t i = 0; i < num; ++i) {
        const char* name = zip_get_name(zip, i, 0);
        if (name && std::string(name) != "manifest.json") {
            auto content = zipReadString(zip, name);
            if (content) files[name] = *content;
        }
    }

    zip_close(zip);
    return true;
}

std::string ProjectSerializer::generateThumbnail(const TerrainProductPtr&) const {
    return "";
}

std::string ProjectSerializer::generateThumbnail(const SceneProductPtr&) const {
    return "";
}

} // namespace vsgraysim