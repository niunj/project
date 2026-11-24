#pragma once
#include <nlohmann/json.hpp>
#include <vsg/nodes/Node.h>
#include <vsg/maths/vec3.h>
#include <memory>

namespace rocky { class VSGContextSingleton; class VSGContextImpl; class Layer; }
namespace vsgraysim::viewport3d {
class ViewportContext;
class RockyGlobeBackend {
public:
    static void configureEnvironment(const nlohmann::json& params);
    void attachContext(ViewportContext&);
    void ensureMap(const nlohmann::json& params);
    void selectMap(const std::string& path);
    bool intersect(const vsg::dvec3& origin, const vsg::dvec3& direction, vsg::dvec3& hit) const;
    bool elevation(double lat, double lon, double& height) const;
    vsg::ref_ptr<vsg::Node> node() const { return map_node_; }
    void* skyContext() const { return rocky_context_; }
    uint64_t revision() const { return map_revision_; }
private:
    std::shared_ptr<rocky::VSGContextSingleton> rocky_singleton_;
    rocky::VSGContextImpl* rocky_context_ = nullptr;
    vsg::ref_ptr<vsg::Node> map_node_;
    std::vector<std::shared_ptr<rocky::Layer>> base_layers_;
    uint64_t map_revision_ = 0;
};
}
