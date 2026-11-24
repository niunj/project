#pragma once
#include "TiledVoxelTerrain.h"
#include "VoxelTerrainPhysics.h"
#include <memory>
#include <optional>

namespace vsgraysim::earth {

/// Mutable physics for a tiled (real-DEM) voxel terrain. Bridges the tiled
/// read-only volume into the single-grid VoxelTerrainPhysics pipeline:
///
///   extractDamageWindow -> VoxelTerrain::fromOccupancy
///     -> VoxelTerrainPhysics (damage / collapse / clusters / debris)
///     -> applyDamage (runs written back as a per-column override layer)
///
/// The override layer shadows the baked tile columns for occupancy queries
/// (occupiedGeodetic / columnTopGeodetic) and increments the terrain
/// revision so render/sensor consumers can pick up the new state.
class TiledVoxelTerrainPhysics {
public:
    /// Window half-span in metres around the damage focus.
    static constexpr double kDefaultHalfSpan = 512.0;

    explicit TiledVoxelTerrainPhysics(std::shared_ptr<const TiledVoxelTerrain> terrain);

    /// Build the local physics grid around a geodetic focus. Returns false
    /// when the focus is outside terrain residency.
    bool focus(double lat, double lon,
               double halfSpanMetres = kDefaultHalfSpan,
               std::optional<double> bottomMetres = std::nullopt);

    /// Direct access to the single-grid physics pipeline (damage, collapse,
    /// extractClusters, debris). Null until focus() succeeds.
    VoxelTerrainPhysics* grid() { return grid_ ? &*grid_ : nullptr; }
    const VoxelTerrainPhysics* grid() const { return grid_ ? &*grid_ : nullptr; }

    /// Write the current (possibly damaged) state back into the tiled
    /// terrain override layer. Returns the number of changed columns.
    std::size_t commit();
    /// Publish this new snapshot explicitly to rendering and sensor owners.
    /// The source snapshot and all previously published snapshots stay valid.
    std::shared_ptr<const TiledVoxelTerrain> snapshot() const {return terrain_;}

    /// Local ENU origin of the active window (metres).
    const vsg::dvec3& windowOrigin() const { return window_.min; }

private:
    std::vector<std::vector<std::array<double,2>>> currentRuns() const;

    std::shared_ptr<TiledVoxelTerrain> terrain_;
    TiledVoxelTerrain::DamageWindow window_{};
    std::shared_ptr<VoxelTerrain> snapshot_;      ///< undamaged copy for diffing
    std::optional<VoxelTerrainPhysics> grid_;
    bool focused_ = false;
};

} // namespace vsgraysim::earth
