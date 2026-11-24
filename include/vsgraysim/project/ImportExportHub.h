#pragma once
/// @file ImportExportHub.h
/// @brief Unified import/export pipeline for GIS, 3D models, and data formats.

#ifndef VSGRAYSIM_PROJECT_IMPEXP_H
#define VSGRAYSIM_PROJECT_IMPEXP_H

#include "vsgraysim/products/TerrainProduct.h"
#include "vsgraysim/products/SceneProduct.h"

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <future>
#include <any>
#include <nlohmann/json.hpp>

namespace vsgraysim {

/// Import/export job status
struct JobStatus {
    enum class State { Pending, Running, Completed, Failed, Cancelled } state = State::Pending;
    std::string job_id;
    std::string description;
    float progress = 0.0f;           // 0.0 - 1.0
    std::string current_step;
    std::string error_message;
    std::any result;                 // Job-specific result
};

/// Progress callback
using ProgressCallback = std::function<void(const JobStatus&)>;

/// GIS Import options
struct GisImportOptions {
    std::string target_crs = "EPSG:4978";  // Target CRS (ECEF recommended)
    double target_resolution = 1.0;        // Target resolution (meters)
    uint32_t tile_size = 64;               // Tile size for TerrainProduct
    bool generate_splat = true;            // Generate biome splat masks
    bool generate_normals = true;          // Generate normal maps
    bool generate_water_mask = true;       // Detect water from DEM
    double water_threshold = 0.0;          // Height threshold for water
    std::string resampling = "bilinear";   // "nearest", "bilinear", "cubic", "lanczos"
    bool fill_nodata = true;               // Interpolate NoData values
    double nodata_value = -9999.0;         // NoData marker in source
    vsg::dbox3 clip_bounds;                // Optional clipping bounds (empty = no clip)
    std::unordered_map<std::string, std::any> custom_options;
};

/// Model import options (glTF, OBJ, FBX, OSGB, etc.)
struct ModelImportOptions {
    std::string target_crs = "EPSG:4978";
    vsg::dvec3 position = {0, 0, 0};       // World position override
    vsg::dquat rotation = {0, 0, 0, 1};    // World rotation override
    vsg::dvec3 scale = {1, 1, 1};          // Scale factor
    bool generate_colliders = true;        // Generate physics colliders
    bool merge_meshes = false;             // Merge all meshes into one
    bool optimize_meshes = true;           // Mesh optimization (vertex cache, etc.)
    bool generate_lods = false;            // Generate LOD levels
    int lod_count = 3;                     // Number of LOD levels
    float lod_reduction = 0.5f;            // Reduction factor per LOD
    std::unordered_map<std::string, std::string> material_map;  // Material name remapping
};

/// Export options
struct ExportOptions {
    // Heightmap export
    TerrainProduct::HeightmapExportFormat heightmap_format = TerrainProduct::HeightmapExportFormat::GEOTIFF;
    // Mesh export
    TerrainProduct::MeshExportFormat mesh_format = TerrainProduct::MeshExportFormat::GLTF;
    int max_lod = 0;                       // 0 = highest resolution only
    vsg::dbox3 export_bounds;              // Empty = full extent
    bool include_splat = true;
    bool include_normals = true;
    bool include_water = true;
    bool include_vector_features = true;
};

/// ImportExportHub — unified async import/export pipeline
class ImportExportHub {
public:
    ImportExportHub();
    ~ImportExportHub() = default;

    // Non-copyable
    ImportExportHub(const ImportExportHub&) = delete;
    ImportExportHub& operator=(const ImportExportHub&) = delete;

    // ---- GIS/DEM Import (async) ----

    /// Import DEM/GeoTIFF/ASCII Grid/XYZ as TerrainProduct
    /// Returns job_id, use getJobStatus() to poll
    std::string importGIS(const std::vector<std::string>& input_paths,
                          const GisImportOptions& options = {},
                          ProgressCallback progress_cb = {});

    /// Import multiple DEM tiles and mosaic them
    std::string importGISMosaic(const std::vector<std::string>& input_paths,
                                const GisImportOptions& options = {},
                                ProgressCallback progress_cb = {});

    // ---- 3D Model Import ----

    /// Import 3D model as SceneProduct entity (glTF, OBJ, FBX, OSGB, etc.)
    std::string importModel(const std::string& input_path,
                            const ModelImportOptions& options = {},
                            ProgressCallback progress_cb = {});

    /// Import model as Prefab (for SceneProduct prefab library)
    std::string importPrefab(const std::string& input_path,
                             const ModelImportOptions& options = {},
                             ProgressCallback progress_cb = {});

    // ---- Export ----

    /// Export TerrainProduct to heightmap
    std::string exportHeightmap(const TerrainProductPtr& terrain,
                                const std::string& output_path,
                                const ExportOptions& options = {},
                                ProgressCallback progress_cb = {});

    /// Export TerrainProduct to mesh
    std::string exportMesh(const TerrainProductPtr& terrain,
                           const std::string& output_path,
                           const ExportOptions& options = {},
                           ProgressCallback progress_cb = {});

    /// Export SceneProduct to USD/glTF
    std::string exportScene(const SceneProductPtr& scene,
                            const std::string& output_path,
                            const ExportOptions& options = {},
                            ProgressCallback progress_cb = {});

    // ---- Job management ----

    /// Get job status
    JobStatus getJobStatus(const std::string& job_id) const;

    /// Wait for job completion (blocking)
    JobStatus waitForJob(const std::string& job_id, std::chrono::milliseconds timeout = std::chrono::hours(1));

    /// Cancel job
    bool cancelJob(const std::string& job_id);

    /// List active jobs
    std::vector<JobStatus> listJobs() const;

    // ---- Synchronous helpers (blocking) ----

    /// Synchronous GIS import (blocks until done)
    TerrainProductPtr importGISSync(const std::vector<std::string>& input_paths,
                                    const GisImportOptions& options = {});

    /// Synchronous model import
    SceneProductPtr importModelSync(const std::string& input_path,
                                    const ModelImportOptions& options = {});

private:
    struct Job {
        std::string id;
        JobStatus status;
        std::future<void> future;
        ProgressCallback progress_cb;
    };

    std::unordered_map<std::string, Job> jobs_;
    mutable std::mutex jobs_mutex_;
    uint64_t job_counter_ = 0;

    std::string generateJobId();
    void updateJobStatus(const std::string& job_id, const JobStatus& status);
};

} // namespace vsgraysim

#endif // VSGRAYSIM_PROJECT_IMPEXP_H