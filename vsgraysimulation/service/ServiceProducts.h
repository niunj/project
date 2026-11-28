#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <filesystem>
#include <set>

namespace vsgraysim::service {
void refreshPreviewProducts(const std::filesystem::path& directory,const std::set<uint32_t>& desired,
                            const nlohmann::json& platforms,const nlohmann::json& navigation,
                            uint64_t tick,uint64_t contentRevision,double simulationTime);
}
