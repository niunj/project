#pragma once

#include <nlohmann/json_fwd.hpp>

namespace vsgraysim::earth {

struct VoxelEquipmentConfig;
struct VoxelSceneSnapshot;

// Read-only presentation of an already committed scene. Equipment statuses
// come from the published equipment-state packet for the same tick.
nlohmann::json scenePresentation(const VoxelEquipmentConfig&,const VoxelSceneSnapshot&,
    const nlohmann::json& equipmentStates,const VoxelSceneSnapshot* previous=nullptr);
void retainSceneSoundEvents(nlohmann::json& presentation,const nlohmann::json& previous,const VoxelSceneSnapshot&);

}
