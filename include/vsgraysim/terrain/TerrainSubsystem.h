#pragma once
/// @file TerrainSubsystem.h
/// @brief TerrainSubsystem refactored to implement IEditorSubsystem + ISimulationSubsystem.

#ifndef VSGRAYSIM_TERRAIN_TERRAIN_SUBSYSTEM_H
#define VSGRAYSIM_TERRAIN_TERRAIN_SUBSYSTEM_H

#include "vsgraysim/subsystems/ISubsystem.h"
#include "vsgraysim/products/TerrainProduct.h"
#include "vsgraysim/terrain/LayerStack.h"
#include "vsgraysim/terrain/BrushEngine.h"
#include "vsgraysim/terrain/TileStore.h"

#include <vsg/nodes/Group.h>
#include <vsg/core/ref_ptr.h>

#include <memory>
#include <string>
#include <optional>

namespace vsgraysim {

/// TerrainSubsystem configuration
struct TerrainConfig {
    // Terrain spec
    std::string crs = "EPSG:4978";          // Coordinate reference system
    vsg::dbox bounds = {{-5000, -100, -5000}, {5000, 5000, 5000}};
    double resolution = 1.0;                // Meters per sample
    uint32_t tile_size = 64;                // Tile edge vertices

    // Default layers
    bool create_default_layers = true;

    // Tile store
    std::string tile_store_path;            // Empty = memory store
    bool tile_store_zip = false;            // Use ZIP instead of file system
};

/// CPU terrain-product editor adapter. Rendering is owned by RockyMapFeature;
/// the former, unimplemented TerrainChunkManager path is retired.
class TerrainSubsystem : public IDualModeSubsystem {
public:
    TerrainSubsystem();
    ~TerrainSubsystem() override;

    // ---- ISubsystem ----

    std::string name() const override { return "TerrainSubsystem"; }
    std::string version() const override { return "2.0.0"; }

    bool initialize(const nlohmann::json& config) override;
    void shutdown() override;
    bool isHealthy() const override { return product_ != nullptr; }
    nlohmann::json diagnostics() const override;

    // ---- IEditorSubsystem ----

    std::shared_ptr<void> produceProduct() const override;
    void consumeProduct(std::shared_ptr<const void> product) override;
    bool executeCommand(std::shared_ptr<void> command) override;
    std::string productTypeId() const override { return "TerrainProduct"; }
    bool isDirty() const override { return dirty_; }
    void setDirty(bool dirty) override { dirty_ = dirty; }

    // ---- ISimulationSubsystem ----

    void onPreTick(const TimeTick& tick) override;
    void onTick(const TimeTick& tick) override;
    void onPostTick(const TimeTick& tick) override;

    void* getMutableProduct(const std::string& product_type) override;
    const void* getConstProduct(const std::string& product_type) const override;

    int tickPriority() const override { return 0; }  // Terrain is read-only in sim

    // ---- Terrain-specific API ----

    /// Get terrain product (const)
    TerrainProductConstPtr getProduct() const { return product_; }

    /// Get mutable terrain product (editor only)
    TerrainProductPtr getMutableProduct() { return product_; }

    /// Get layer stack
    LayerStack& getLayerStack() { return *layer_stack_; }
    const LayerStack& getLayerStack() const { return *layer_stack_; }

    /// Get brush engine
    BrushEngine& getBrushEngine() { return *brush_engine_; }

    /// Regenerate procedural layers
    void regenerateProcedural();

    /// Save to .vsgterrain
    bool save(const std::string& path) const;

    /// Load from .vsgterrain
    bool load(const std::string& path);

    /// Export heightmap
    bool exportHeightmap(const std::string& path, HeightmapExportFormat fmt) const;

    /// Export mesh
    bool exportMesh(const std::string& path, MeshExportFormat fmt, int max_lod = 0) const;

    /// No independent terrain renderer: returns null. Use RockyMapFeature.
    vsg::ref_ptr<vsg::Group> getRootNode() const;

private:
    void createDefaultLayers();

    TerrainConfig config_;
    TerrainProductPtr product_;
    std::unique_ptr<LayerStack> layer_stack_;
    std::unique_ptr<BrushEngine> brush_engine_;
    TileStorePtr tile_store_;

    bool dirty_ = false;
    bool initialized_ = false;
};

} // namespace vsgraysim

#endif // VSGRAYSIM_TERRAIN_TERRAIN_SUBSYSTEM_H
