#pragma once
#include <vsg/all.h>
#include <string>
#include <vector>
namespace rockyenv::sensor {
struct SceneInstance {
    std::string id;
    vsg::ref_ptr<vsg::VertexIndexDraw> geometry; // five-array thermal vertex contract
    vsg::dmat4 transform;
    bool visible=true;
    // Nonzero only for immutable submeshes sharing their owning object's pose.
    uint32_t geometryPart=0;
};
/// GPU layout, shared with gbuffer_compute.comp. Geometry stays mesh-local.
struct GpuSceneInstance {
    vsg::mat4 objectToWorld,worldToObject;
    vsg::uivec4 meta; // BVH root, visibility, triangle base, reserved
};
struct GpuBvhNode {
    vsg::vec4 lo,hi;
    vsg::uivec4 links; // left, right, primitive offset, primitive count (leaf)
};
static_assert(sizeof(GpuSceneInstance)==144 && sizeof(GpuBvhNode)==48,"GPU scene ABI");

/// Fixed-topology TLAS. Refit/update command preserves the descriptor handle.
/// The caller waits for the previous submission before updating instance memory.
class DynamicTlas : public vsg::Inherit<vsg::TopLevelAccelerationStructure,DynamicTlas> {
public:
    explicit DynamicTlas(vsg::Device* device);
    void compile(vsg::Context& context) override;
    void update(const std::vector<vsg::dmat4>& transforms,const std::vector<bool>& visible);
    vsg::ref_ptr<vsg::Command> updateCommand() const { return update_; }
private:
    void writeInstances();
    vsg::ref_ptr<vsg::Buffer> instancesBuffer_,scratch_;
    vsg::ref_ptr<vsg::Command> update_;
};
}
