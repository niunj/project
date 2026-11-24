#include "../src/earth/VoxelTerrainPhysics.h"
#include "../src/earth/VoxelTerrainDebris.h"
#include "../src/earth/VoxelModelVoxelizer.h"
#include <vsgraysim/layer0_phys/PhysicsEngine.h>
#include <iostream>
#include <vsg/nodes/VertexIndexDraw.h>
#include <cmath>

using namespace vsgraysim::earth;
using vsgraysim::PhysicsEngine;

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << "\n"; std::exit(1); }
    std::cout << "PASS: " << message << "\n";
}
} // namespace

int main(int argc, char** argv) {
    const std::string modelPath = argc > 1 ? argv[1] : "models/Bunker/Bunker.obj";
    const std::string outDir = argc > 2 ? argv[2] : "/tmp/voxel_model";

    {
        auto mesh=vsg::VertexIndexDraw::create();
        auto positions=vsg::vec3Array::create({{.308f,-.413f,1.905f},{-1.814f,1.434f,-.842f},{-1.423f,-1.529f,-.766f},
            {-2.5f,-2.5f,-2.5f},{-2.5f,-2.5f,-2.5f},{-2.5f,-2.5f,-2.5f},
            {2.5f,2.5f,2.5f},{2.5f,2.5f,2.5f},{2.5f,2.5f,2.5f}});
        mesh->assignArrays({positions});
        VoxelModelConfig config;config.voxelSize=1;config.margin=0;config.fillInterior=false;
        auto model=voxelizeMesh(mesh,config);VoxelTerrainPhysics state(model.terrain);
        require(state.occupiedLocal(0,0,0)==false,"complete SAT rejects separated triangle/voxel");
    }
    {
        auto mesh=vsg::VertexIndexDraw::create();
        auto positions=vsg::vec3Array::create({{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
                                              {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}});
        auto indices=vsg::uintArray::create({0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,
                                            3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5});
        mesh->assignArrays({positions});mesh->assignIndices(indices);mesh->indexCount=36;
        VoxelModelConfig config;config.maxDimension=40;
        auto solid=voxelizeMesh(mesh,config);config.fillInterior=false;auto shell=voxelizeMesh(mesh,config);
        VoxelTerrainPhysics solidState(solid.terrain),shellState(shell.terrain);
        require(solidState.occupiedLocal(0,0,0)==true&&shellState.occupiedLocal(0,0,0)==false,"explicit solid versus shell voxelization");
        require(solid.occupiedCount>32768 && solid.occupiedCount>shell.occupiedCount,"programmatic voxelizer bypasses obsolete JSON limits");
    }
    // 1. Load and voxelise the model.
    const auto model = voxelizeFile(modelPath, VoxelModelConfig{.maxDimension = 64});
    require(model.occupiedCount > 0, "model voxelised");
    std::cout << "voxelised: " << model.nx << "x" << model.ny << "x" << model.nz
              << " cells, " << model.occupiedCount << " occupied, cell=" << model.cellSize << " m\n";

    // 2. Wrap in physics state.
    VoxelTerrainPhysics physics(model.terrain);
    require(physics.voxelCount() == model.occupiedCount, "physics state matches voxel model");

    // 3. Collision query: ray from above the model centre must hit the roof.
    const double cx = model.origin.x + model.nx * model.cellSize * .5;
    const double cz = model.origin.z + model.nz * model.cellSize * .5;
    const double cy = model.origin.y + model.ny * model.cellSize + 5.;
    vsg::dvec3 hit, normal;
    require(physics.intersectLocal({cx, cy, cz}, {0., -1., 0.}, 100., hit, normal),
            "ray hits voxel model");

    // 4. Damage: blast a crater in the model.
    const std::size_t before = physics.voxelCount();
    const auto modelCentre = vsg::dvec3{model.origin.x + model.nx*model.cellSize*.5,
                                        model.origin.y + model.ny*model.cellSize*.5,
                                        model.origin.z + model.nz*model.cellSize*.5};
    const std::size_t removed = physics.damage({modelCentre, {0., -1., 0.}, 500.});
    require(removed > 0, "damage removes voxels");
    require(physics.voxelCount() < before, "model voxel count decreased");
    std::cout << "damage removed " << removed << " voxels\n";

    // 5. Collapse: unsupported voxels fall.
    const std::size_t moved = physics.collapse();
    std::cout << "collapse moved " << moved << " voxels\n";

    // 6. Commit to a new immutable snapshot for rendering.
    auto committed = physics.commit();
    require(committed->vertices().size() > 0, "committed mesh has vertices");
    require(committed->indices().size() > 0, "committed mesh has triangles");
    require(committed->revision != model.terrain->revision, "revision advanced");

    // 7. Debris: extract clusters and spawn rigid bodies.
    PhysicsEngine bullet;
    bullet.setGravity({0, -9.81, 0});
    bullet.addGroundPlane(0.0);
    const auto clusters = physics.extractClusters(8);
    std::cout << "clusters=" << clusters.size() << " largest=" << (clusters.empty() ? 0 : clusters.front().count) << "\n";
    VoxelTerrainDebris debris(bullet, physics);
    const std::size_t spawned = debris.spawn(clusters, 8);
    std::cout << "debris spawned=" << spawned << "\n";
    if (spawned > 0) {
        const auto t0 = debris.transforms();
        bullet.step(0.25);
        const auto t1 = debris.transforms();
        require(t1.size() == t0.size(), "debris count stable");
        std::cout << "debris y0=" << t0[0][3][1] << " y1=" << t1[0][3][1] << "\n";
    }

    std::cout << "voxel model physics: all checks passed\n";
    return 0;
}
