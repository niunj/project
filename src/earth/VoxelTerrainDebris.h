#pragma once
#include "VoxelTerrainPhysics.h"
#include <vsgraysim/layer0_phys/PhysicsEngine.h>

#include <memory>
#include <string>
#include <vector>

namespace vsgraysim::earth {

/// Legacy AABB-only demonstration adapter. Production voxel scenes use
/// VoxelScene/VoxelDynamics, which transfer actual occupancy out of the parent
/// and publish fragment geometry/collision/sensor state atomically.
/// Large connected voxel clusters
/// (buildings, boulders, cliff sections) become Bullet rigid bodies so they
/// fall, roll and pile up with real dynamics instead of teleporting down.
///
/// The host creates one VoxelTerrainDebris per damage event, feeds it the
/// extracted clusters, and steps it with the simulation clock. The render
/// thread queries debrisTransforms() for the visual pose of each fragment.
class VoxelTerrainDebris {
public:
    struct Fragment {
        std::string id;
        vsg::dvec3 centre;        ///< initial local position (metres)
        vsg::dvec3 halfExtents;   ///< local AABB half-extents
        double mass = 0;          ///< kg (volume * material density)
    };

    VoxelTerrainDebris(vsgraysim::PhysicsEngine& physics,
                       const VoxelTerrainPhysics& terrain);

    /// Legacy approximation: spawn AABB bodies for the largest supplied clusters.
    /// This helper does not edit the source terrain or settle omitted clusters.
    /// Returns the number of fragments created.
    std::size_t spawn(const std::vector<VoxelTerrainPhysics::Cluster>& clusters,
                      std::size_t maxFragments = 16,
                      double materialDensityKgM3 = 2600.0 /* granite */);

    /// Advance the debris physics by dt seconds.
    void step(double dt) { physics_.step(dt); }

    /// Current world transform of each spawned fragment.
    std::vector<vsg::dmat4> transforms() const;

    std::size_t count() const { return fragments_.size(); }
    std::size_t totalVoxels() const { return totalVoxels_; }

private:
    vsgraysim::PhysicsEngine& physics_;
    double cell_size_ = 1.0;      ///< terrain voxel edge in metres
    std::vector<Fragment> fragments_;
    std::size_t totalVoxels_ = 0;
};

} // namespace vsgraysim::earth
