#pragma once
#include "VoxelScene.h"
#include <rocky_env/DynamicScene.h>
#include <rocky_env/SceneSource.h>
#include <rocky_env/SharedSceneResources.h>
namespace vsgraysim::earth {
struct VoxelSceneSensorData {
    VoxelSceneSnapshotPtr snapshot;
    std::vector<rockyenv::sensor::SceneInstance> instances;
    std::vector<rockyenv::sensor::RadarMaterial> materials;
    rockyenv::sensor::SharedSceneResources::TexArraySpec textures;
};
/// Same geometry, object transforms and material temperatures as the display.
/// Retain snapshot with a measurement; rebuild resources on contentRevision.
VoxelSceneSensorData makeVoxelSceneSensorData(VoxelSceneSnapshotPtr,bool splitTerrain=false);
}
