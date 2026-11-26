#pragma once
#include <vsgraysim/earth/EarthSurface.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>
#include <memory>
#include <vector>
#include <string>

namespace vsg { class Node; }

namespace vsgraysim::earth {

class VoxelTerrain;

/// Convert an arbitrary mesh (obj / osgb / vsgb / ive) into a voxel model
/// usable by VoxelTerrainPhysics. The result is a VoxelTerrain-compatible
/// occupancy grid so collision, damage and collapse share the same code
/// path as terrain.
///
/// Voxelisation: for each occupied voxel we test whether the voxel AABB
/// intersects any triangle of the source mesh. The resolution is chosen so
/// the largest dimension of the model fits the requested voxel count.
struct VoxelModelConfig {
    int maxDimension = 64;          ///< upper bound; auto fits the existing 4M-cell budget
    double voxelSize = 0;           ///< 0 = auto-derive from model bounds
    double margin = 0.02;           ///< extra padding around the model (metres)
    double unitsToMetres = 1.;
    std::string upAxis = "y"; // y-up native, z-up OSG -> (x,z,-y)
    bool fillInterior = true;      ///< flood-fill watertight shells; false keeps surface cells only
    bool sourceSurface = false;    ///< clip source triangles per occupied cell; retain thin structures
    unsigned workers = 4;          ///< bounded CPU atlas preparation; 1 is deterministic reference
};

struct VoxelModel {
    std::shared_ptr<VoxelTerrain> terrain;  ///< render snapshot
    vsg::dvec3 origin{0,0,0};               ///< world offset of voxel (0,0,0)
    double cellSize = 1.0;
    int nx = 0, ny = 0, nz = 0;
    std::size_t occupiedCount = 0;
};

/// Voxelise a loaded VSG mesh node.
VoxelModel voxelizeMesh(vsg::Node* mesh, const VoxelModelConfig& config = {});

/// Load any supported mesh format and voxelise it.
/// Supported: .obj (native), .glb/.gltf/.fbx/.dae/.ply/.stl/.3ds (Assimp),
/// .usd/.usda/.usdc (polygon meshes and PreviewSurface), .vsg/.vsgt/.vsgb.
/// USD stage units and up axis are applied before the optional model configuration transform.
VoxelModel voxelizeFile(const std::string& path, const VoxelModelConfig& config = {});

} // namespace vsgraysim::earth
