#pragma once

#include <array>
#include <future>
#include <memory>
#include <vsg/maths/box.h>
#include <vsg/maths/mat4.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/VertexIndexDraw.h>

namespace vsg { class Camera; class Viewer; }
namespace vsgraysim::viewport3d {

/// Viewport-local geometry residency for a static, georeferenced terrain draw.
/// Page vertices are copied from the published draw, in its original chart.
class TerrainDisplayPages {
public:
    struct Stats {
        size_t visible = 0, resident = 0, loads = 0, evictions = 0;
        size_t residentBytes = 0, originalBytes = 0;
    };

    TerrainDisplayPages(vsg::ref_ptr<vsg::VertexIndexDraw> source, double splitX, double splitZ, double fringe);
    TerrainDisplayPages(const TerrainDisplayPages&) = delete;
    TerrainDisplayPages& operator=(const TerrainDisplayPages&) = delete;

    void update(vsg::Viewer&, const vsg::Camera&, const vsg::dmat4& localToWorld);
    const Stats& stats() const { return stats_; }
    vsg::ref_ptr<vsg::Group> node() const { return node_; }
    vsg::ref_ptr<vsg::VertexIndexDraw> source() const { return source_; }

private:
    struct Page {
        std::shared_ptr<std::vector<uint32_t>> sourceIndices;
        vsg::dbox bounds;
        std::future<vsg::ref_ptr<vsg::VertexIndexDraw>> preparation;
        vsg::ref_ptr<vsg::VertexIndexDraw> draw;
        unsigned outsideFrames = 0;
    };

    void prepare(Page&);
    vsg::ref_ptr<vsg::Group> node_ = vsg::Group::create();
    vsg::ref_ptr<vsg::VertexIndexDraw> source_;
    std::array<Page, 4> pages_;
    double fringe_;
    Stats stats_;
};

}
