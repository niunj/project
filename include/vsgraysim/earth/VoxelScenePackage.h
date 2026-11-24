#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace vsgraysim::earth {
// Small versioned manifest plus immutable compressed CBOR assets. Scene
// occupancy/material/checkpoint semantics are those of VoxelScene, unchanged.
class VoxelScenePackage {
  public:
    static constexpr const char *format = "vsgraysim-scene-package-v1";
    static bool extension(const std::string &);
    static nlohmann::json inspect(const std::string &);
    static nlohmann::json checkpoint(const std::string &);
    static nlohmann::json save(const std::string &, const nlohmann::json &topic,
                               const nlohmann::json &checkpoint);
    static std::string resolveResource(const std::string &, const std::string &manifest);
    static std::string sha256(const std::vector<uint8_t> &);
};
} // namespace vsgraysim::earth
