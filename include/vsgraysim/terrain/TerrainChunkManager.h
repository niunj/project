#pragma once
/// @file TerrainChunkManager.h
/// @brief QuadTree/Clipmap terrain chunk management for streaming and LOD.

#ifndef VSGRAYSIM_TERRAIN_CHUNK_MANAGER_H
#define VSGRAYSIM_TERRAIN_CHUNK_MANAGER_H

#include "vsgraysim/products/TerrainProduct.h"
#include "vsgraysim/terrain/TileStore.h"

#include <vsg/nodes/Group.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/core/ref_ptr.h>

#include <unordered_map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <functional>

namespace vsgraysim {

/// Chunk LOD key
struct ChunkKey {
    TileKey tile_key;
    int lod;  // 0 = highest detail

    bool operator==(const ChunkKey& other) const {
        return tile_key == other.tile_key && lod == other.lod;
    }
};

/// Chunk data (CPU side)
struct ChunkData {
    ChunkKey key;
    std::vector<float> heights;       // (size+1)^2
    std::vector<vsg::vec3> normals;   // (size+1)^2
    std::vector<vsg::vec2> uvs;       // (size+1)^2
    std::vector<uint32_t> indices;    // triangle indices
    vsg::dbox bounds;                // World-space bounds
    bool dirty = false;
};

/// GPU chunk representation
struct GpuChunk {
    ChunkKey key;
    vsg::ref_ptr<vsg::vec3Array> positions;
    vsg::ref_ptr<vsg::vec3Array> normals;
    vsg::ref_ptr<vsg::vec2Array> uvs;
    vsg::ref_ptr<vsg::uintArray> indices;
    vsg::ref_ptr<vsg::VertexIndexDraw> draw;
    vsg::ref_ptr<vsg::StateGroup> state_group;
    bool uploaded = false;
};

/// TerrainChunkManager — handles quadtree chunk loading, LOD selection, GPU upload
class TerrainChunkManager {
public:
    struct Config {
        uint32_t chunk_size = 64;           // Vertices per edge (64 = 65x65 height samples)
        uint32_t max_lod = 5;               // Maximum LOD levels
        double lod_transition_threshold = 1.5;  // Screen-space error threshold
        size_t max_gpu_chunks = 1024;       // Max chunks in GPU memory
        size_t upload_budget_per_frame = 4; // Chunks to upload per frame
        bool enable_frustum_culling = true;
        bool enable_occlusion_culling = false;
    };

    explicit TerrainChunkManager(const Config& config, TerrainProductPtr product);
    ~TerrainChunkManager();

    // ---- LOD selection ----

    /// Update visible chunks based on camera
    void update(const vsg::dvec3& camera_pos, const vsg::dmat4& view_proj);

    /// Get visible GPU chunks for rendering
    std::vector<GpuChunk*> getVisibleChunks() const;

    // ---- Data management ----

    /// Request chunk load (async)
    void requestChunk(const ChunkKey& key, int priority = 0);

    /// Cancel chunk request
    void cancelChunk(const ChunkKey& key);

    /// Check if chunk is loaded (CPU)
    bool isChunkLoaded(const ChunkKey& key) const;

    /// Check if chunk is uploaded (GPU)
    bool isChunkUploaded(const ChunkKey& key) const;

    // ---- GPU resource management ----

    /// Upload dirty chunks to GPU (call once per frame)
    void uploadDirtyChunks();

    /// Evict least recently used chunks if over budget
    void evictExcessChunks();

    // ---- VSG scene graph ----

    /// Get root VSG node for rendering
    vsg::ref_ptr<vsg::Group> getRootNode() const { return root_node_; }

    // ---- Statistics ----

    struct Stats {
        size_t cpu_chunks_loaded = 0;
        size_t gpu_chunks_uploaded = 0;
        size_t chunks_culled = 0;
        size_t upload_queue_size = 0;
        double last_update_time_ms = 0.0;
    };
    Stats getStats() const;

private:
    // QuadTree helpers
    ChunkKey parentKey(const ChunkKey& key) const;
    std::array<ChunkKey, 4> childKeys(const ChunkKey& key) const;
    bool isKeyValid(const ChunkKey& key) const;

    // LOD selection
    double computeScreenError(const ChunkKey& key, const vsg::dvec3& camera_pos,
                              const vsg::dmat4& view_proj) const;

    void selectChunksRecursive(const ChunkKey& key, const vsg::dvec3& camera_pos,
                               const vsg::dmat4& view_proj,
                               std::vector<ChunkKey>& visible);

    // Chunk creation
    std::shared_ptr<ChunkData> createChunkData(const ChunkKey& key);
    GpuChunk* createGpuChunk(const ChunkKey& key);
    void uploadChunk(GpuChunk* chunk);

    // Async loading
    void loadingThreadFunc();
    void processLoadQueue();

    Config config_;
    TerrainProductPtr product_;
    TileStorePtr tile_store_;

    // CPU chunks
    mutable std::mutex cpu_mutex_;
    std::unordered_map<ChunkKey, std::shared_ptr<ChunkData>, std::hash<ChunkKey>> cpu_chunks_;

    // GPU chunks
    mutable std::mutex gpu_mutex_;
    std::unordered_map<ChunkKey, std::unique_ptr<GpuChunk>, std::hash<ChunkKey>> gpu_chunks_;
    std::vector<GpuChunk*> lru_list_;  // For eviction

    // Load queue
    struct LoadRequest {
        ChunkKey key;
        int priority;
        std::chrono::steady_clock::time_point time;
    };
    mutable std::mutex queue_mutex_;
    std::priority_queue<LoadRequest, std::vector<LoadRequest>,
        std::function<bool(const LoadRequest&, const LoadRequest&)>> load_queue_;

    std::thread loader_thread_;
    std::atomic<bool> loader_running_{true};

    // VSG scene graph
    vsg::ref_ptr<vsg::Group> root_node_;

    // Stats
    mutable std::mutex stats_mutex_;
    Stats stats_;
};

// Hash for ChunkKey
struct ChunkKeyHash {
    size_t operator()(const ChunkKey& k) const noexcept {
        return std::hash<TileKey>{}(k.tile_key) ^ (static_cast<size_t>(k.lod) << 1);
    }
};

} // namespace vsgraysim

#endif // VSGRAYSIM_TERRAIN_CHUNK_MANAGER_H