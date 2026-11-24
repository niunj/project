#include "VoxelTerrainDebris.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vsgraysim::earth {

VoxelTerrainDebris::VoxelTerrainDebris(vsgraysim::PhysicsEngine& physics,
                                       const VoxelTerrainPhysics& terrain)
    : physics_(physics), cell_size_(terrain.cellSize()) {
    if (terrain.voxelCount() == 0) throw std::invalid_argument("empty terrain has no debris");
}

std::size_t VoxelTerrainDebris::spawn(const std::vector<VoxelTerrainPhysics::Cluster>& clusters,
                                      std::size_t maxFragments,
                                      double materialDensityKgM3) {
    if (clusters.empty() || maxFragments == 0 || !std::isfinite(materialDensityKgM3) ||
        materialDensityKgM3 <= 0) return 0;

    const std::size_t count = std::min(clusters.size(), maxFragments);
    std::size_t spawned = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& cluster = clusters[i];
        if (cluster.count == 0) continue;

        const double voxelVolume = cell_size_ * cell_size_ * cell_size_;
        const double mass = double(cluster.count) * voxelVolume * materialDensityKgM3;

        Fragment fragment;
        fragment.id = "debris_" + std::to_string(i);
        fragment.centre = cluster.centre;
        fragment.halfExtents = cluster.halfExtents;
        fragment.mass = mass;

        simcore::RigidState initial;
        initial.position = fragment.centre;
        // Lift the fragment so its bottom face sits just above the terrain
        // surface; Bullet resolves any residual penetration on the first step
        // as an artificial upward pop.
        initial.position.y += fragment.halfExtents.y * 0.5 + 0.1;
        initial.velocity = {0, 0, 0};
        initial.angular_velocity = {0, 0, 0};
        initial.orientation = {0, 0, 0, 1};

        if (!physics_.createBoxBody(fragment.id, fragment.halfExtents, initial, mass)) {
            // A duplicate id means a previous damage event already spawned this
            // fragment; make the id unique and retry once.
            fragment.id += "_" + std::to_string(std::rand());
            if (!physics_.createBoxBody(fragment.id, fragment.halfExtents, initial, mass)) continue;
        }
        fragments_.push_back(std::move(fragment));
        totalVoxels_ += cluster.count;
        ++spawned;
    }
    return spawned;
}

std::vector<vsg::dmat4> VoxelTerrainDebris::transforms() const {
    std::vector<vsg::dmat4> result;
    result.reserve(fragments_.size());
    for (const auto& fragment : fragments_)
        result.push_back(physics_.transform(fragment.id));
    return result;
}

} // namespace vsgraysim::earth
