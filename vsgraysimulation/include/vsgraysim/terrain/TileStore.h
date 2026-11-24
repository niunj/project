#pragma once
/// @file TileStore.h
/// @brief Tile storage backends (filesystem, ZIP, memory).

#ifndef VSGRAYSIM_TERRAIN_TILESTORE_H
#define VSGRAYSIM_TERRAIN_TILESTORE_H

#include "vsgraysim/products/TerrainProduct.h"

#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <filesystem>

namespace vsgraysim {

/// File-based tile store (one file per tile)
class FileTileStore : public ITileStore {
public:
    explicit FileTileStore(const std::string& root_dir, bool create_dirs = true);
    ~FileTileStore() override = default;

    std::optional<TileData> read(const TileKey& key) const override;
    void write(const TileKey& key, const TileData& data) override;
    bool exists(const TileKey& key) const override;
    std::vector<TileKey> listKeys() const override;
    std::string describe() const override { return "FileTileStore: " + root_dir_; }
    void flush() override {}

private:
    std::string tilePath(const TileKey& key) const;
    std::string root_dir_;
};

/// ZIP-based tile store (all tiles in one .zip)
class ZipTileStore : public ITileStore {
public:
    explicit ZipTileStore(const std::string& zip_path, bool create = true);
    ~ZipTileStore() override;

    std::optional<TileData> read(const TileKey& key) const override;
    void write(const TileKey& key, const TileData& data) override;
    bool exists(const TileKey& key) const override;
    std::vector<TileKey> listKeys() const override;
    std::string describe() const override { return "ZipTileStore: " + zip_path_; }
    void flush() override;

private:
    std::string tileName(const TileKey& key) const;
    mutable std::string zip_path_;
    mutable int zip_handle_ = -1;  // Not thread-safe, use per-thread handles in practice
};

/// In-memory tile store (for testing/small terrains)
class MemoryTileStore : public ITileStore {
public:
    MemoryTileStore() = default;
    ~MemoryTileStore() override = default;

    std::optional<TileData> read(const TileKey& key) const override;
    void write(const TileKey& key, const TileData& data) override;
    bool exists(const TileKey& key) const override;
    std::vector<TileKey> listKeys() const override;
    std::string describe() const override { return "MemoryTileStore: " + std::to_string(tiles_.size()) + " tiles"; }
    void flush() override {}

    void clear() { std::lock_guard<std::mutex> lock(mutex_); tiles_.clear(); }

private:
    mutable std::mutex mutex_;
    std::unordered_map<TileKey, TileData> tiles_;
};

/// Factory for creating tile stores
class TileStoreFactory {
public:
    static TileStorePtr createFileStore(const std::string& root_dir);
    static TileStorePtr createZipStore(const std::string& zip_path);
    static TileStorePtr createMemoryStore();

    // Create appropriate store based on path extension
    static TileStorePtr createFromPath(const std::string& path);
};

} // namespace vsgraysim

#endif // VSGRAYSIM_TERRAIN_TILESTORE_H