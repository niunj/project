#pragma once
#include "VoxelScene.h"
#include <rocky_env/EnvApi.h>
#include <vsg/core/Array.h>
#include <vsg/core/Array2D.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/Switch.h>
#include <vsg/nodes/VertexIndexDraw.h>
namespace rockyenv {struct CloudGpuResources;}
namespace vsgraysim::earth {
inline constexpr size_t voxelOceanUniformCount=796;
static_assert(voxelOceanUniformCount*sizeof(vsg::vec4)<=16384);
vsg::ref_ptr<vsg::floatArray2D> makeOceanReferenceImage(const VoxelOcean&);
void writeVoxelOceanField(vsg::vec4Array&,const VoxelOcean&,double seconds);
class VoxelOceanDisplay {
public:
    explicit VoxelOceanDisplay(const VoxelSceneSnapshot&,const VoxelOceanDisplay* reuse=nullptr,vsg::Device* device=nullptr,std::shared_ptr<rockyenv::CloudGpuResources> cloud={});
    vsg::ref_ptr<vsg::StateGroup> object(const VoxelSceneObject&,vsg::ref_ptr<vsg::VertexIndexDraw>);
    void update(const VoxelSceneSnapshot&,const vsg::dmat4& sceneViewProjection,const vsg::dvec3& eye,
                const rockyenv::AtmosphereState&,bool atmosphere=true,double weatherSeconds=-1.);
    vsg::ref_ptr<vsg::StateGroup> surface,background,depthSurface;
    vsg::ref_ptr<vsg::Switch> commands;
    vsg::ref_ptr<vsg::ImageInfo> foamImage()const;
    vsg::ref_ptr<vsg::Switch> spray;
private:
    struct Effects;
    std::shared_ptr<Effects> effects_;
    std::shared_ptr<rockyenv::CloudGpuResources> cloud_;
    vsg::ref_ptr<vsg::floatArray2D> referenceImage_;
    std::shared_ptr<const VoxelOcean> sourceOcean_;
    struct ObjectCache {const EarthSurfaceSnapshot* source=nullptr;nlohmann::json material;vsg::ref_ptr<vsg::StateGroup> node;vsg::ref_ptr<vsg::vec4Array> parameters;};
    std::map<std::string,ObjectCache> objects_,reuseObjects_;
    struct Binding {std::string id;vsg::ref_ptr<vsg::vec4Array> data;};std::vector<Binding> bindings_;
    vsg::ref_ptr<vsg::StateGroup> pipeline(const VoxelSceneObject*,vsg::ref_ptr<vsg::VertexIndexDraw>,int mode=1,bool depthOnly=false);
};
}
