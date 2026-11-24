#include "../src/earth/VoxelTerrainPhysics.h"
#include "../src/earth/VoxelTerrainDebris.h"
#include <vsgraysim/layer0_phys/PhysicsEngine.h>
#include <iostream>
#include <cmath>

using namespace vsgraysim::earth;
using vsgraysim::PhysicsEngine;

namespace {
std::shared_ptr<VoxelTerrain> makeTerrain() {
    auto package = nlohmann::json::object();
    package["dimensions"] = {16, 8, 16};
    package["origin_m"] = {0.0, 0.0, 0.0};
    package["cell_size_m"] = 1.0;
    auto occupied = nlohmann::json::array();
    for (int z = 0; z < 16; ++z) for (int y = 0; y < 8; ++y) for (int x = 0; x < 16; ++x)
        occupied.push_back({x, y, z});
    package["occupied"] = occupied;
    auto config = nlohmann::json::object();
    config["strict"] = false;
    config["provenance_assumed"] = false;
    return VoxelTerrain::load(package, config);
}

void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << "\n"; std::exit(1); }
    std::cout << "PASS: " << message << "\n";
}
} // namespace

int main() {
    PhysicsEngine physics;
    physics.setGravity({0, -9.81, 0});
    physics.addGroundPlane(0.0);

    auto terrain = makeTerrain();
    VoxelTerrainPhysics voxelPhysics(terrain);

    // Blast a crater that separates a large cluster from the ground.
    const std::size_t removed = voxelPhysics.damage({{8., 8., 8.}, {0., -1., 0.}, 5000.});
    require(removed > 0, "crater removes voxels");

    // Extract the disconnected cluster above the crater.
    const auto clusters = voxelPhysics.extractClusters(4);
    require(!clusters.empty(), "damage produces clusters");
    require(clusters.front().count >= 4, "largest cluster has enough voxels");
    std::cout << "clusters=" << clusters.size()
              << " largest=" << clusters.front().count
              << " centre=(" << clusters.front().centre.x << ","
              << clusters.front().centre.y << "," << clusters.front().centre.z << ")\n";

    // Spawn the largest cluster as a Bullet rigid body and let it fall.
    VoxelTerrainDebris debris(physics, voxelPhysics);
    const std::size_t spawned = debris.spawn(clusters, 4);
    require(spawned > 0, "debris spawned as rigid bodies");

    const auto initial = debris.transforms();
    require(!initial.empty(), "debris has transforms");
    const double y0 = initial[0][3][1];
    require(y0 > 0, "debris starts above ground");

    physics.step(0.5);
    const auto after = debris.transforms();
    require(after.size() == initial.size(), "debris count stable");
    const double y1 = after[0][3][1];
    // Also query the authoritative body position directly.
    const double yBody = physics.position("debris_0").y;
    std::cout << "y0=" << y0 << " y1=" << y1 << " yBody=" << yBody << "\n";
    require(y1 < y0 || yBody < y0, "debris falls under gravity");

    std::cout << "voxel terrain debris: all checks passed\n";
    return 0;
}
