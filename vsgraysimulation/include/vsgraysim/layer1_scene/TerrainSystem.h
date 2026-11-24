#pragma once
/// @file TerrainSystem.h
/// @brief Editable procedural terrain for the VSG scene (Layer 1).
///
/// Data model
/// ----------
/// A CPU height-field grid (N x N samples, world extent `extent` meters)
/// is the single source of truth.  It is rendered as a dynamic
/// `vsg::VertexIndexDraw` whose vertex buffer is re-uploaded to the GPU on
/// every edit via `vsg::DYNAMIC_DATA` + `dirty()` (vsg::TransferTask).
///
/// Features
/// --------
///   - Procedural terrain generation: flat / plains / hills / mountains /
///     desert / canyon / island (seeded value-noise fBm & ridged noise),
///     plus composite biome presets gobi / swamp / ocean that bundle their
///     characteristic water body.
///   - Procedural water: ocean / lake / river / pond.  The water surface
///     mesh is derived from the height-field contour at the water level,
///     so it always follows the carved banks / river bed exactly.
///   - Brush editing: raise / lower / smooth / flatten / noise / plateau /
///     erode with a radial fall-off, plus ray-cast picking (`pick()`).
///   - Height-map save / load (custom binary .ter format) and raw
///     height-field snapshot access for host-side undo/redo.
///
/// Visualisation
/// -------------
/// The terrain is textured with a procedural 256x256 colour map that is
/// re-derived from the height-field after every generation/water/brush
/// change: sand near the water line, grass on flat ground, rock on steep
/// slopes and snow on high peaks.  Water uses a translucent procedural
/// texture.
#ifndef VSGRAYSIM_LAYER1_SCENE_TERRAIN_SYSTEM_H
#define VSGRAYSIM_LAYER1_SCENE_TERRAIN_SYSTEM_H

#include <cstdint>
#include <string>
#include <vector>

#include <vsg/core/ref_ptr.h>
#include <vsg/core/Data.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Node.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>

namespace vsgraysim {

enum class TerrainType { Flat = 0, Plains, Hills, Mountains, Desert, Canyon, Island,
                         Gobi, Swamp, Ocean };
enum class WaterType  { None = 0, Ocean, Lake, River, Pond };
enum class BrushMode  { None = 0, Raise, Lower, Smooth, Flatten, Noise, Plateau, Erode };

class TerrainSystem {
public:
    explicit TerrainSystem(uint32_t grid = 256, double extent = 1000.0,
                           double height_scale = 60.0);

    // ------------------------------------------------------------------
    // Parameters (grid changes rebuild the mesh -> recompile needed)
    // ------------------------------------------------------------------
    void      setGrid(uint32_t n);            ///< samples per side
    uint32_t  grid() const { return n_; }
    void      setExtent(double e);            ///< world size (m)
    double    extent() const { return extent_; }
    void      setHeightScale(double s);       ///< max terrain height (m)
    double    heightScale() const { return height_scale_; }
    void      setSeed(uint32_t s) { seed_ = s; }
    uint32_t  seed() const { return seed_; }
    double    cellSize() const { return cell_size_; }

    // ------------------------------------------------------------------
    // Procedural generation
    // ------------------------------------------------------------------
    void       generate(TerrainType type);    ///< regenerate heights (keeps water type)
    TerrainType terrainType() const { return terrain_type_; }

    // ------------------------------------------------------------------
    // Water
    // ------------------------------------------------------------------
    void       applyWater(WaterType type);    ///< carve features + rebuild water mesh
    void       setWaterLevel(double level);   ///< water surface altitude (m)
    WaterType  waterType() const { return water_type_; }
    double     waterLevel() const { return water_level_; }

    // ------------------------------------------------------------------
    // Brush editing
    // ------------------------------------------------------------------
    /// Apply a brush stroke centred at world (x, z).
    /// `target_height` is used by Flatten (absolute metres).
    void applyBrush(double world_x, double world_z, BrushMode mode,
                    double radius, double strength, double target_height = 0.0);

    /// The Plateau brush latches its target height on the first sample
    /// after this call (typically at stroke start).
    void resetPlateauCapture() { plateau_captured_ = false; }

    // ------------------------------------------------------------------
    // Height-field snapshots (host-side undo/redo)
    // ------------------------------------------------------------------
    const std::vector<double>& heightsData() const { return heights_; }
    /// Replace heights (must match current grid size); refreshes mesh.
    bool setHeightsData(const std::vector<double>& h);

    /// Ray cast (world space) against the height-field.  Returns the
    /// world hit coordinates.  Step-based DDA with bisection refinement.
    bool pick(const vsg::dvec3& origin, const vsg::dvec3& dir,
              double& world_x, double& world_z,
              double max_dist = 2.0e4) const;

    // ------------------------------------------------------------------
    // Queries
    // ------------------------------------------------------------------
    double heightAtWorld(double wx, double wz) const;  ///< bilinear sample
    void   heightRange(double& lo, double& hi) const;
    vsg::vec3 normalAtWorld(double wx, double wz) const;

    // ------------------------------------------------------------------
    // Scene integration
    // ------------------------------------------------------------------
    /// Root group: {terrain StateGroup, water StateGroup}.  The object is
    /// stable across regenerations; only the arrays inside are replaced.
    vsg::ref_ptr<vsg::Node> node() const { return root_; }
    /// Incremented when the mesh is rebuilt (grid change).  The host app
    /// must call viewer->compile() when this changes.
    uint64_t nodeGeneration() const { return node_gen_; }

    /// Rebuild height->normals->water from the internal heights, and mark
    /// the procedural colour texture dirty.  Cheap; call after brushing.
    void touch() { updateVertices(); refreshWaterMesh(); tex_dirty_ = true; }

    /// Called once per frame by the host: uploads the colour texture if a
    /// brush stroke dirtied it.  (Cost: 256x256 texel regen, <1ms.)
    void flushPending();

    // ------------------------------------------------------------------
    // Persistence
    // ------------------------------------------------------------------
    bool saveHeightmap(const std::string& path) const;
    bool loadHeightmap(const std::string& path);

private:
    void   buildNodes();                    ///< (re)create StateGroups + arrays
    void   updateVertices();                ///< heights_ -> vertex buffer + normals
    void   recomputeNormals();
    void   refreshWaterMesh();              ///< water contour at water_level_
    void   carveFeatures(WaterType type);   ///< lake/river/pond depression carving
    void   computeWaterLevel_();            ///< default water level = 12% above min
    void   regenerateColorTexture();
    double sampleBilinear(double wx, double wz) const;

    uint32_t       n_ = 256;
    double         extent_ = 1000.0;
    double         height_scale_ = 60.0;
    double         cell_size_ = 0.0;
    uint32_t       seed_ = 20260810;
    TerrainType    terrain_type_ = TerrainType::Plains;
    WaterType      water_type_ = WaterType::None;
    double         water_level_ = 6.0;

    std::vector<double>      heights_;      ///< row-major, index = z*n + x, metres
    std::vector<vsg::vec3>   normals_;      ///< per-vertex world normals

    vsg::ref_ptr<vsg::Group>              root_;
    vsg::ref_ptr<vsg::StateGroup>         terrain_sg_;
    vsg::ref_ptr<vsg::StateGroup>         water_sg_;
    vsg::ref_ptr<vsg::VertexIndexDraw>    terrain_vid_;
    vsg::ref_ptr<vsg::VertexIndexDraw>    water_vid_;
    vsg::ref_ptr<vsg::vec3Array>          t_pos_;
    vsg::ref_ptr<vsg::vec3Array>          t_nrm_;
    vsg::ref_ptr<vsg::vec2Array>          t_uv_;
    vsg::ref_ptr<vsg::uintArray>          t_idx_;
    vsg::ref_ptr<vsg::vec3Array>          w_pos_;
    vsg::ref_ptr<vsg::vec3Array>          w_nrm_;
    vsg::ref_ptr<vsg::vec2Array>          w_uv_;
    vsg::ref_ptr<vsg::uintArray>          w_idx_;

    vsg::ref_ptr<vsg::ubvec4Array2D>      terrain_tex_;   ///< 256x256, DYNAMIC
    vsg::ref_ptr<vsg::ubvec4Array2D>      water_tex_;     ///< 256x256

    uint64_t   node_gen_ = 0;
    bool       tex_dirty_ = false;

    // Plateau brush latch
    bool       plateau_captured_ = false;
    double     plateau_capture_h_ = 0.0;
};

} // namespace vsgraysim

#endif // VSGRAYSIM_LAYER1_SCENE_TERRAIN_SYSTEM_H
