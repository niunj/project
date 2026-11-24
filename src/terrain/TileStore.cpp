#include "vsgraysim/terrain/TileStore.h"

#include <fstream>
#include <filesystem>
#include <zip.h>
#include <nlohmann/json.hpp>

namespace vsgraysim {

// ---- FileTileStore ----

FileTileStore::FileTileStore(const std::string& root_dir, bool create_dirs)
    : root_dir_(root_dir) {
    if (create_dirs) {
        std::filesystem::create_directories(root_dir_);
    }
}

std::string FileTileStore::tilePath(const TileKey& key) const {
    // Directory structure: root/lod/tx/ty.tile
    return root_dir_ + "/lod" + std::to_string(key.lod) + "/" +
           std::to_string(key.tx) + "/" + std::to_string(key.ty) + ".tile";
}

std::optional<TileData> FileTileStore::read(const TileKey& key) const {
    std::string path = tilePath(key);
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;

    TileData tile;
    tile.key = key;

    // Read header
    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != 1) return std::nullopt;

    uint32_t tile_size;
    file.read(reinterpret_cast<char*>(&tile_size), sizeof(tile_size));

    size_t stride = tile_size + 1;
    tile.height.resize(stride * stride);
    file.read(reinterpret_cast<char*>(tile.height.data()),
              tile.height.size() * sizeof(float));

    // Read optional data flags
    uint8_t flags;
    file.read(reinterpret_cast<char*>(&flags), sizeof(flags));

    if (flags & 0x01) {  // splat
        tile.splat.resize(stride * stride * 4);
        file.read(reinterpret_cast<char*>(tile.splat.data()), tile.splat.size());
        tile.has_splat = true;
    }
    if (flags & 0x02) {  // normal
        tile.normal.resize(stride * stride * 2);
        file.read(reinterpret_cast<char*>(tile.normal.data()), tile.normal.size());
        tile.has_normal = true;
    }
    if (flags & 0x04) {  // water_mask
        tile.water_mask.resize(stride * stride);
        file.read(reinterpret_cast<char*>(tile.water_mask.data()),
                  tile.water_mask.size() * sizeof(float));
        tile.has_water = true;
    }

    // Read min/max
    file.read(reinterpret_cast<char*>(&tile.min_height), sizeof(tile.min_height));
    file.read(reinterpret_cast<char*>(&tile.max_height), sizeof(tile.max_height));

    return tile;
}

void FileTileStore::write(const TileKey& key, const TileData& data) {
    std::string path = tilePath(key);
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return;

    uint32_t version = 1;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    uint32_t tile_size = static_cast<uint32_t>(std::sqrt(data.height.size())) - 1;
    if (tile_size == 0) return;

    file.write(reinterpret_cast<const char*>(&tile_size), sizeof(tile_size));

    file.write(reinterpret_cast<const char*>(data.height.data()),
               data.height.size() * sizeof(float));

    uint8_t flags = 0;
    if (data.has_splat) flags |= 0x01;
    if (data.has_normal) flags |= 0x02;
    if (data.has_water) flags |= 0x04;
    file.write(reinterpret_cast<const char*>(&flags), sizeof(flags));

    if (data.has_splat) {
        file.write(reinterpret_cast<const char*>(data.splat.data()), data.splat.size());
    }
    if (data.has_normal) {
        file.write(reinterpret_cast<const char*>(data.normal.data()), data.normal.size());
    }
    if (data.has_water) {
        file.write(reinterpret_cast<const char*>(data.water_mask.data()),
                   data.water_mask.size() * sizeof(float));
    }

    file.write(reinterpret_cast<const char*>(&data.min_height), sizeof(data.min_height));
    file.write(reinterpret_cast<const char*>(&data.max_height), sizeof(data.max_height));
}

bool FileTileStore::exists(const TileKey& key) const {
    return std::filesystem::exists(tilePath(key));
}

std::vector<TileKey> FileTileStore::listKeys() const {
    std::vector<TileKey> keys;
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root_dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".tile") {
                // Parse path to extract key - simplified
            }
        }
    } catch (...) {}
    return keys;
}

// ---- ZipTileStore ----

ZipTileStore::ZipTileStore(const std::string& zip_path, bool create)
    : zip_path_(zip_path) {
}

ZipTileStore::~ZipTileStore() {
}

std::string ZipTileStore::tileName(const TileKey& key) const {
    return "tiles/lod" + std::to_string(key.lod) + "/" +
           std::to_string(key.tx) + "_" + std::to_string(key.ty) + ".tile";
}

std::optional<TileData> ZipTileStore::read(const TileKey& key) const {
    return std::nullopt;
}

void ZipTileStore::write(const TileKey& key, const TileData& data) {
}

bool ZipTileStore::exists(const TileKey& key) const {
    return false;
}

std::vector<TileKey> ZipTileStore::listKeys() const {
    return {};
}

void ZipTileStore::flush() {
}

// ---- MemoryTileStore ----

std::optional<TileData> MemoryTileStore::read(const TileKey& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tiles_.find(key);
    if (it != tiles_.end()) return it->second;
    return std::nullopt;
}

void MemoryTileStore::write(const TileKey& key, const TileData& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    tiles_[key] = data;
}

bool MemoryTileStore::exists(const TileKey& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tiles_.find(key) != tiles_.end();
}

std::vector<TileKey> MemoryTileStore::listKeys() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TileKey> keys;
    keys.reserve(tiles_.size());
    for (const auto& [key, _] : tiles_) keys.push_back(key);
    return keys;
}

// ---- TileStoreFactory ----

TileStorePtr TileStoreFactory::createFileStore(const std::string& root_dir) {
    return std::make_shared<FileTileStore>(root_dir);
}

TileStorePtr TileStoreFactory::createZipStore(const std::string& zip_path) {
    return std::make_shared<ZipTileStore>(zip_path);
}

TileStorePtr TileStoreFactory::createMemoryStore() {
    return std::make_shared<MemoryTileStore>();
}

TileStorePtr TileStoreFactory::createFromPath(const std::string& path) {
    std::filesystem::path p(path);
    if (p.extension() == ".zip") {
        return createZipStore(path);
    } else if (std::filesystem::is_directory(path) || !std::filesystem::exists(path)) {
        return createFileStore(path);
    }
    return createMemoryStore();
}

} // namespace vsgraysim