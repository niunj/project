#pragma once
#include "VoxelTerrain.h"

#include <vsg/maths/box.h>
#include <vsg/maths/vec3.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace vsgraysim::earth {

/// Mutable physical state for a voxel terrain. It wraps the immutable
/// VoxelTerrain snapshot and tracks damage per voxel so that collision
/// queries, cratering and structural collapse can be simulated without
/// touching the rendering surface until an explicit rebuild is committed.
///
/// Threading: not thread-safe by design. The host calls all mutation and
/// query methods on the simulation thread, then calls commit() to produce a
/// new immutable VoxelTerrain snapshot for the render thread.
class VoxelTerrainPhysics {
public:
    struct DamageEvent {
        vsg::dvec3 point;       ///< impact point in local metres
        vsg::dvec3 velocity;    ///< impact velocity in m/s
        double energy = 0.0;    ///< kinetic energy in Joules (used for crater size)
        double radius = 0.0;    ///< explicit crater radius override in metres; 0 = derive from energy
    };

    explicit VoxelTerrainPhysics(std::shared_ptr<const VoxelTerrain> source);

    /// Ray-cast against the current (possibly damaged) voxel state.
    /// Returns true and fills hit/normal when the ray intersects any
    /// occupied voxel face.
    bool intersectLocal(const vsg::dvec3& origin, const vsg::dvec3& direction,
                        double max_distance, vsg::dvec3& hit, vsg::dvec3& normal) const;

    /// Continuous sphere-cast: find the first voxel whose AABB intersects the swept
    /// sphere. Used for projectile impact detection.
    bool sweepSphere(const vsg::dvec3& origin, const vsg::dvec3& direction,
                     double radius, double max_distance,
                     vsg::dvec3& hit, vsg::dvec3& normal) const;

    /// Query occupancy at a local point. Returns nullopt outside coverage.
    std::optional<bool> occupiedLocal(double x, double y, double z) const;

    /// Apply a damage event: carve out a spherical crater around the impact
    /// point. Voxels fully inside the crater radius are removed; voxels
    /// intersecting the boundary are removed if their centre lies inside.
    /// Returns the number of voxels destroyed.
    std::size_t damage(const DamageEvent& event);

    /// Collapse unsupported voxels. A voxel is supported when it has an
    /// occupied neighbour below, or is at the base layer. Unsupported voxels
    /// settle to the lowest supported layer in one call. Returns the number of voxels
    /// that moved.
    std::size_t collapse();

    /// Extract connected voxel clusters (6-neighbourhood flood fill).
    /// Returns one entry per cluster, sorted by descending size.
    struct Cluster {
        std::vector<std::array<int,3>> voxels;
        vsg::dvec3 centre;          ///< local centroid
        vsg::dvec3 halfExtents;     ///< local AABB half-extents
        std::size_t count = 0;
    };
    std::vector<Cluster> extractClusters(std::size_t minimumVoxels = 64) const;

    /// Commit the current state to a new immutable VoxelTerrain snapshot.
    /// The render thread swaps to this snapshot atomically.
    std::shared_ptr<VoxelTerrain> commit() const;

    uint64_t revision() const { return revision_; }
    std::size_t voxelCount() const;
    std::size_t destroyedCount() const { return destroyed_; }

    int columnsX() const { return columns_x_; }
    int columnsZ() const { return columns_z_; }
    int layers() const { return layers_; }
    double cellSize() const { return cell_size_; }
    double baseY() const { return base_y_; }
    /// Local AABB of the voxel volume (metres). y spans [base_y_, base_y_ + layers*cell).
    vsg::dbox bounds() const {
        return vsg::dbox{{min_x_, base_y_, min_z_},
                         {max_x_, base_y_ + double(layers_) * cell_size_, max_z_}};
    }

private:
    std::shared_ptr<const VoxelTerrain> source_;
    std::vector<bool> occupied_;
    std::vector<double> top_;
    double base_y_ = 0;
    int layers_ = 0;
    int columns_x_ = 0;
    int columns_z_ = 0;
    double cell_size_ = 1;
    double min_x_ = 0, max_x_ = 0, min_z_ = 0, max_z_ = 0;
    uint64_t revision_ = 1;
    std::size_t destroyed_ = 0;

    int index(int x, int y, int z) const { return (z * layers_ + y) * columns_x_ + x; }
    bool occupied(int x, int y, int z) const {
        if (x < 0 || x >= columns_x_ || y < 0 || y >= layers_ || z < 0 || z >= columns_z_) return false;
        return occupied_[index(x, y, z)];
    }
    void setOccupied(int x, int y, int z, bool value);
    void recomputeTop(int x, int z);
    vsg::dvec3 voxelCentre(int x, int y, int z) const;
    bool rayVoxel(const vsg::dvec3& origin, const vsg::dvec3& ray, double max_dist,
                  vsg::dvec3& hit, vsg::dvec3& normal) const;
};

} // namespace vsgraysim::earth
