#pragma once
#include "PlanetWeather.h"
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
namespace vsgraysim::earth {
class EarthSurfaceSnapshot;class VoxelOcean;
// The pinned terrain mesh is shaded with imagery and the active voxel weather
// in a single pass. Surface geometry and measurement material are unchanged.
class TerrainCloudShadow {
public:
    TerrainCloudShadow(const EarthSurfaceSnapshot&,vsg::ref_ptr<vsg::VertexIndexDraw>,
                       std::shared_ptr<rockyenv::CloudGpuResources> cloud={}, const EarthSurfaceSnapshot* pinnedTexture=nullptr);
    void update(const PlanetWeatherFrame&,const vsg::vec3& horizon,const vsg::vec3& zenith,bool atmosphere,const VoxelOcean* ocean=nullptr);
    vsg::ref_ptr<vsg::StateGroup> node;
private:
    vsg::ref_ptr<vsg::vec4Array> parameters_;
};
}
