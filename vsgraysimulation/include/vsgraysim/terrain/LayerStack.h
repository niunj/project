#pragma once
/// @file LayerStack.h
/// @brief Height layer stack with blending modes (base, procedural, edit, mask).

#ifndef VSGRAYSIM_TERRAIN_LAYER_STACK_H
#define VSGRAYSIM_TERRAIN_LAYER_STACK_H

#include "vsgraysim/products/TerrainProduct.h"
#include "vsgraysim/terrain/TileStore.h"

#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace vsgraysim {

/// Layer blend mode
enum class BlendMode {
    Replace,      // Replace base
    Add,          // Add to base
    Subtract,     // Subtract from base
    Multiply,     // Multiply with base
    Overlay,      // Overlay blend
    Min,          // Minimum
    Max,          // Maximum
    Mask          // Use as mask (alpha)
};

/// Height layer
struct HeightLayer {
    std::string name;
    std::string id;                    // UUID
    bool visible = true;
    bool locked = false;
    float opacity = 1.0f;              // 0-1
    BlendMode blend_mode = BlendMode::Replace;
    int order = 0;                     // Lower = earlier in stack

    // Layer data (tile-based, same tiling as TerrainProduct)
    std::shared_ptr<ITileStore> tile_store;

    // Mask layer (optional, controls where this layer applies)
    std::shared_ptr<ITileStore> mask_store;

    // Layer type for UI
    enum class Type { Base, Procedural, Edit, Mask, Imported } type = Type::Edit;

    // Procedural parameters (for procedural layers)
    struct ProcParams {
        uint32_t seed = 0;
        double frequency = 0.01;
        double amplitude = 10.0;
        int octaves = 4;
        double lacunarity = 2.0;
        double gain = 0.5;
        std::string noise_type = "fbm";  // "fbm", "ridged", "cellular", "domain_warp"
    } proc_params;
};

/// LayerStack — manages ordered list of height layers with blending
class LayerStack {
public:
    LayerStack() = default;
    explicit LayerStack(uint32_t tile_size, double resolution);

    // ---- Layer management ----

    /// Add layer at end (or at specific order)
    std::string addLayer(const std::string& name, HeightLayer::Type type,
                         BlendMode blend = BlendMode::Replace);

    /// Remove layer by ID
    bool removeLayer(const std::string& id);

    /// Move layer to new order
    bool moveLayer(const std::string& id, int new_order);

    /// Get layer by ID
    HeightLayer* getLayer(const std::string& id);
    const HeightLayer* getLayer(const std::string& id) const;

    /// Get all layers (ordered by order)
    std::vector<HeightLayer*> getLayers();
    const std::vector<std::unique_ptr<HeightLayer>>& getLayers() const { return layers_; }

    // ---- Layer properties ----

    void setLayerVisible(const std::string& id, bool visible);
    void setLayerLocked(const std::string& id, bool locked);
    void setLayerOpacity(const std::string& id, float opacity);
    void setLayerBlendMode(const std::string& id, BlendMode mode);

    // ---- Height queries (composite) ----

    /// Get composite height at world coordinate (blends all visible layers)
    double heightAt(double wx, double wz) const;

    /// Get height from specific layer
    double heightAtLayer(const std::string& layer_id, double wx, double wz) const;

    // ---- Tile operations ----

    /// Read composite tile (blends all layers)
    std::optional<TileData> readCompositeTile(const TileKey& key) const;

    /// Write to specific layer tile
    void writeLayerTile(const std::string& layer_id, const TileKey& key,
                        const TileData& data);

    /// Read layer tile
    std::optional<TileData> readLayerTile(const std::string& layer_id,
                                           const TileKey& key) const;

    // ---- Procedural generation ----

    /// Generate procedural layer data
    void generateProceduralLayer(const std::string& layer_id);

    /// Regenerate all procedural layers
    void regenerateAllProcedural();

    // ---- Mask operations ----

    /// Set mask for layer (controls where layer applies)
    void setLayerMask(const std::string& layer_id,
                      std::shared_ptr<ITileStore> mask_store);

    // ---- Serialization ----

    nlohmann::json serialize() const;
    bool deserialize(const nlohmann::json& json);

private:
    uint32_t tile_size_ = 64;
    double resolution_ = 1.0;

    std::vector<std::unique_ptr<HeightLayer>> layers_;

    // Blend functions
    double blend(double base, double layer, BlendMode mode, float opacity) const;
    double blendTiles(const TileData& base,
                      const TileData& layer,
                      BlendMode mode, float opacity,
                      TileData& output) const;
};

} // namespace vsgraysim

#endif // VSGRAYSIM_TERRAIN_LAYER_STACK_H