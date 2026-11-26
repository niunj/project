#pragma once

#include <vsgraysim/earth/VoxelScene.h>
#include <string>

namespace vsgraysim::earth {

bool voxelUsdEditsAvailable() noexcept;

/// Export root targets and structures into an independent OpenUSD edit layer.
void saveVoxelUsdEdits(const std::string& path, const VoxelSceneSnapshot& scene);

/// Apply composed USD opinions to existing targets in one paused edit transaction.
void applyVoxelUsdEdits(const std::string& path, VoxelScene& scene);

}
