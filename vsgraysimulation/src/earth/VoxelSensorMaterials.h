#pragma once
#include <nlohmann/json.hpp>
#include <vsg/maths/vec3.h>
#include <array>
#include <vector>
#include <string>
#include <memory>
namespace vsgraysim::earth {
class VoxelTerrain;
nlohmann::json normalizeVoxelMaterial(const nlohmann::json&);
// Original immutable cell chart; cropped damage fragments retain the same map.
class VoxelSensorMaterials {
public:
    static std::shared_ptr<const VoxelSensorMaterials> load(const nlohmann::json&,const VoxelTerrain&,const nlohmann::json& base,bool checkpoint=false);
    nlohmann::json encode()const;
    std::shared_ptr<const VoxelSensorMaterials> paint(const nlohmann::json& region,const nlohmann::json& material)const;
    std::shared_ptr<const VoxelSensorMaterials> settled(const VoxelTerrain&)const;
    const nlohmann::json& at(const vsg::dvec3& local)const;
    const std::vector<nlohmann::json>& palette()const{return palette_;}
    uint8_t index(const vsg::dvec3& local)const;
private:
    vsg::dvec3 origin_;double cell_=1;std::array<int,3> dimensions_{};
    std::vector<uint8_t> cells_;std::vector<nlohmann::json> palette_;
};
}
