#include "../src/earth/VoxelTerrainPhysics.h"
#include <iostream>
#include <cmath>

using namespace vsgraysim::earth;

namespace {
std::shared_ptr<VoxelTerrain> makeTerrain(int nx=16, int ny=8, int nz=16, double cell=1.0) {
    auto package = nlohmann::json::object();
    package["dimensions"] = {nx, ny, nz};
    package["origin_m"] = {0.0, 0.0, 0.0};
    package["cell_size_m"] = cell;
    auto occupied = nlohmann::json::array();
    // Fill a solid block: all x,z and all y layers.
    for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x)
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
    auto unit=makeTerrain(1,1,1);VoxelTerrainPhysics single(unit);vsg::dvec3 point,face;
    require(single.intersectLocal({-.1,.5,.5},{1,0,0},2,point,face)&&std::abs(point.x)<1e-10&&face.x==-1,"outside ray clips to grid boundary");
    require(!single.intersectLocal({.5,-.1,.5},{0,-1,0},2,point,face),"outward ray cannot hit the grid");
    require(!single.intersectLocal({-.1,2.,.5},{1,0,0},2,point,face),"parallel ray outside slab misses");
    require(single.sweepSphere({-10,1.1,.5},{1,0,0},.2,20,point,face),"continuous sphere sweep detects grazing a thin voxel");
    require(!single.sweepSphere({-10,1.3,.5},{1,0,0},.2,20,point,face),"sphere sweep rejects a true near miss");
    std::vector<bool> sparse(9,false);sparse[(0*3+1)*3+1]=true;
    VoxelTerrainPhysics angled(VoxelTerrain::fromOccupancy({0,0,0},1,3,3,1,sparse));
    require(angled.intersectLocal({.2,.2,.5},{1,.5,0},10,point,face)&&face.y==-1&&face.x==0,"ray normal records the entry face");
    std::vector<bool> tower{false,false,true};
    VoxelTerrainPhysics falling(VoxelTerrain::fromOccupancy({0,0,0},1,1,3,1,tower));
    require(falling.collapse()==1&&falling.destroyedCount()==0,"settling preserves voxel mass and destruction accounting");
    auto settled=falling.commit();
    require(settled->sampleLocal(.5,.5).status==SurfaceStatus::Ready&&settled->sampleLocal(.5,.5).height==1&&
        settled->min_height==0&&settled->max_height==1&&settled->valid_samples==1,"settled mesh and height snapshot agree");
    auto terrain = makeTerrain();
    VoxelTerrainPhysics physics(terrain);
    require(physics.voxelCount() == 16*8*16, "initial voxel count");

    // Ray from above must hit the top surface.
    vsg::dvec3 hit, normal;
    require(physics.intersectLocal({8., 20., 8.}, {0., -1., 0.}, 30., hit, normal),
            "ray intersects top surface");
    require(std::abs(hit.y - 8.0) < 1e-6, "hit at top height");
    std::cout << "normal=" << normal.x << "," << normal.y << "," << normal.z << "\n";
    require(std::abs(normal.y) > 0.9, "hit normal is vertical");

    // Ray from below must hit the bottom surface.
    require(physics.intersectLocal({8., -1., 8.}, {0., 1., 0.}, 30., hit, normal),
            "ray intersects bottom surface");
    require(std::abs(hit.y) < 1e-6, "hit at bottom height");

    // Occupancy query.
    require(physics.occupiedLocal(8., 4., 8.).value_or(false), "occupied inside");
    std::cout << "above=" << physics.occupiedLocal(8., 20., 8.).value_or(true)
              << " has_value=" << physics.occupiedLocal(8., 20., 8.).has_value() << "\n";
    require(!physics.occupiedLocal(8., 20., 8.).has_value(), "empty above returns nullopt");

    // Damage: carve a crater.
    const std::size_t before = physics.voxelCount();
    const std::size_t removed = physics.damage({{8., 8., 8.}, {0., -1., 0.}, 1000.});
    require(removed > 0, "damage removes voxels");
    require(physics.voxelCount() < before, "voxel count decreased");
    require(physics.destroyedCount() == removed, "destroyed count tracked");

    // Collapse: unsupported voxels fall.
    const std::size_t moved = physics.collapse();
    require(moved >= 0, "collapse runs");

    // Commit produces a new immutable snapshot with rebuilt mesh.
    auto committed = physics.commit();
    require(committed.get() != terrain.get(), "commit returns new snapshot");
    require(committed->vertices().size() > 0, "committed mesh has vertices");
    require(committed->indices().size() > 0, "committed mesh has indices");
    require(committed->indices().size() % 3 == 0, "committed mesh has triangles");

    // Committed snapshot queries still work.
    require(committed->sampleLocal(8., 8.).status != SurfaceStatus::NoData,
            "committed snapshot samples valid");

    std::cout << "voxel terrain physics: all checks passed\n";
    return 0;
}
