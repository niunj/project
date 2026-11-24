#pragma once
/// @file AssetIndex.h
/// @brief Asset indexing and resolution for cross-product references.

#ifndef VSGRAYSIM_PROJECT_ASSETINDEX_H
#define VSGRAYSIM_PROJECT_ASSETINDEX_H

#include "vsgraysim/products/TerrainProduct.h"
#include "vsgraysim/products/SceneProduct.h"

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>
#include <mutex>
#include <filesystem>

namespace vsgraysim {

/// Asset metadata
struct AssetInfo {
    std::string product_id;           // UUID
    std::string product_type;         // "TerrainProduct", "SceneProduct"
    std::string name;
    std::string path;                 // File path
    std::string version;
    std::string modified;             // ISO 8601
    uint64_t size = 0;                // File size
    std::unordered_map<std::string, std::string> tags;  // Searchable tags
};

/// AssetIndex — manages product discovery and resolution
class AssetIndex {
public:
    AssetIndex() = default;
    ~AssetIndex() = default;

    // Non-copyable
    AssetIndex(const AssetIndex&) = delete;
    AssetIndex& operator=(const AssetIndex&) = delete;

    // ---- Index management ----

    /// Add directory to watch/index
    void addSearchPath(const std::string& path, bool recursive = true);

    /// Remove search path
    void removeSearchPath(const std::string& path);

    /// Scan all search paths (rebuild index)
    void scan();

    /// Scan specific path
    void scanPath(const std::string& path);

    /// Get all indexed assets
    std::vector<AssetInfo> allAssets() const;

    /// Find assets by type
    std::vector<AssetInfo> findByType(const std::string& product_type) const;

    /// Find assets by tag
    std::vector<AssetInfo> findByTag(const std::string& tag, const std::string& value = "") const;

    /// Find asset by product ID
    std::optional<AssetInfo> findById(const std::string& product_id) const;

    /// Search by name (substring, case-insensitive)
    std::vector<AssetInfo> searchByName(const std::string& query) const;

    // ---- Resolution ----

    /// Resolve AssetRef to loaded product (loads if not cached)
    template<typename T>
    std::shared_ptr<const T> resolve(const std::string& product_id) const {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = product_cache_.find(product_id);
        if (it != product_cache_.end()) {
            return std::static_pointer_cast<const T>(it->second);
        }
        // Not in cache, try to load from index
        auto info = findById(product_id);
        if (info) {
            return loadProduct<T>(info->path);
        }
        return nullptr;
    }

    /// Preload product into cache
    template<typename T>
    void preload(const std::string& product_id) {
        auto product = resolve<T>(product_id);
        if (product) {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            product_cache_[product_id] = product;
        }
    }

    /// Clear cache
    void clearCache() {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        product_cache_.clear();
    }

    // ---- Callbacks ----

    using AssetIndexedCallback = std::function<void(const AssetInfo&)>;
    using AssetRemovedCallback = std::function<void(const std::string& product_id)>;

    void setAssetIndexedCallback(AssetIndexedCallback cb) { indexed_cb_ = std::move(cb); }
    void setAssetRemovedCallback(AssetRemovedCallback cb) { removed_cb_ = std::move(cb); }

private:
    template<typename T>
    std::shared_ptr<const T> loadProduct(const std::string& path) const {
        // This would use ProjectSerializer to load
        // Implementation in .cpp with proper includes
        return nullptr;
    }

    std::vector<std::string> search_paths_;
    std::unordered_map<std::string, AssetInfo> index_;  // product_id -> AssetInfo
    mutable std::unordered_map<std::string, std::shared_ptr<const void>> product_cache_;
    mutable std::mutex cache_mutex_;
    mutable std::mutex index_mutex_;

    AssetIndexedCallback indexed_cb_;
    AssetRemovedCallback removed_cb_;

    void indexFile(const std::string& path);
    void removeFromIndex(const std::string& product_id);
};

/// Global asset index accessor
AssetIndex& globalAssetIndex();

} // namespace vsgraysim

#endif // VSGRAYSIM_PROJECT_ASSETINDEX_H