#pragma once
/// @file BrushEngine.h
/// @brief Terrain brush tools with masks, pressure, projection, preview.

#ifndef VSGRAYSIM_TERRAIN_BRUSH_ENGINE_H
#define VSGRAYSIM_TERRAIN_BRUSH_ENGINE_H

#include "vsgraysim/products/TerrainProduct.h"
#include "vsgraysim/terrain/LayerStack.h"
#include "vsgraysim/project/ICommand.h"

#include <vsg/maths/vec3.h>

#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <optional>

namespace vsgraysim {

/// Brush mode
enum class TerrainBrushMode {
    Raise,      // Add height
    Lower,      // Subtract height
    Smooth,     // Smooth/average
    Flatten,    // Set to target height
    Set,        // Absolute set
    Noise,      // Add noise
    Erase       // Erase to base layer
};

/// Brush falloff curve
enum class FalloffType {
    Linear,
    Smooth,     // Smoothstep
    Spherical,  // 1 - r^2
    Sharp,      // Linear with sharp cutoff
    Custom      // User-defined curve
};

/// Brush parameters
struct BrushParams {
    TerrainBrushMode mode = TerrainBrushMode::Raise;
    double radius = 10.0;           // World units (meters)
    double strength = 0.5;          // 0-1
    double hardness = 0.5;          // 0-1 (falloff sharpness)
    double target_height = 0.0;     // For Flatten/Set mode
    FalloffType falloff = FalloffType::Smooth;
    std::vector<float> custom_curve; // 256 samples for custom falloff

    // Pressure sensitivity (for tablet input)
    bool pressure_sensitive = false;
    double pressure_radius_scale = 1.0;
    double pressure_strength_scale = 1.0;

    // Constraints
    bool clamp_to_base = false;     // Don't go below base layer
    double min_height = -1e9;
    double max_height = 1e9;

    // Projection
    bool project_to_surface = true; // Project brush center to terrain surface
    double max_projection_dist = 100.0;
};

/// Brush stroke point
struct StrokePoint {
    vsg::dvec3 world_pos;      // World position
    double pressure = 1.0;     // 0-1
    double timestamp = 0.0;
};

/// Brush preview (for real-time visualization)
struct BrushPreview {
    vsg::dvec3 center;         // World center
    double radius;             // World radius
    std::vector<vsg::dvec3> ring_vertices; // Ring geometry for preview
    std::vector<uint32_t> ring_indices;
    vsg::vec4 color = {1, 1, 0, 0.5};
    bool visible = false;
};

/// BrushEngine — handles brush strokes on LayerStack
class BrushEngine {
public:
    BrushEngine(LayerStack* layer_stack, TerrainProduct* terrain_product);
    ~BrushEngine() = default;

    // ---- Stroke handling ----

    /// Begin a new stroke
    void beginStroke(const vsg::dvec3& world_pos, double pressure = 1.0);

    /// Add point to current stroke
    void addStrokePoint(const vsg::dvec3& world_pos, double pressure = 1.0);

    /// End current stroke
    void endStroke();

    /// Apply single brush dab (for click-based editing)
    void applyDab(const vsg::dvec3& world_pos, double pressure = 1.0);

    // ---- Parameters ----

    void setParams(const BrushParams& params) { params_ = params; }
    const BrushParams& getParams() const { return params_; }
    BrushParams& getMutableParams() { return params_; }

    // ---- Preview ----

    /// Update preview position (call every frame)
    void updatePreview(const vsg::dvec3& world_pos);

    /// Get preview geometry for rendering
    const BrushPreview& getPreview() const { return preview_; }

    // ---- Layer targeting ----

    void setTargetLayer(const std::string& layer_id) { target_layer_id_ = layer_id; }
    const std::string& getTargetLayer() const { return target_layer_id_; }

    // ---- Undo/Redo support ----

    /// Create command for last stroke (for UndoRedoSystem)
    std::shared_ptr<ICommand> createStrokeCommand();

private:
    // Apply brush at a single point
    void applyAtPoint(const vsg::dvec3& world_pos, double pressure);

    // Compute falloff weight at distance
    double falloffWeight(double distance) const;

    // Get layer to edit
    HeightLayer* getTargetLayer();

    LayerStack* layer_stack_;
    TerrainProduct* terrain_product_;

    BrushParams params_;
    std::string target_layer_id_;

    // Current stroke
    std::vector<StrokePoint> current_stroke_;
    bool stroke_active_ = false;

    // Preview
    BrushPreview preview_;

    // History for undo
    struct StrokeRecord {
        std::string layer_id;
        std::vector<std::pair<TileKey, TileData>> before_tiles;
        std::vector<std::pair<TileKey, TileData>> after_tiles;
    };
    std::optional<StrokeRecord> last_stroke_record_;
};

} // namespace vsgraysim

#endif // VSGRAYSIM_TERRAIN_BRUSH_ENGINE_H