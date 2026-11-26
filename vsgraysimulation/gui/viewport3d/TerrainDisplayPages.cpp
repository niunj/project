#include "TerrainDisplayPages.h"

#include <vsgraysim/earth/SceneTasks.h>
#include <vsg/all.h>

#include <chrono>
#include <limits>
#include <stdexcept>

namespace vsgraysim::viewport3d {
namespace {

using Draw = vsg::VertexIndexDraw;
constexpr unsigned retention = 8;

bool intersects(const vsg::dbox& bounds, const std::array<vsg::dvec4, 6>& planes, double fringe) {
    for (const auto& plane : planes) {
        const vsg::dvec3 normal{plane.x, plane.y, plane.z};
        const double support =
            plane.x * (plane.x >= 0 ? bounds.max.x : bounds.min.x) +
            plane.y * (plane.y >= 0 ? bounds.max.y : bounds.min.y) +
            plane.z * (plane.z >= 0 ? bounds.max.z : bounds.min.z);
        if (support + plane.w < -(fringe + .01) * vsg::length(normal)) return false;
    }
    return true;
}

size_t bytes(const Draw& draw) {
    return draw.indices->data->dataSize() + draw.arrays[0]->data->dataSize() +
        draw.arrays[1]->data->dataSize() + draw.arrays[2]->data->dataSize();
}

vsg::ref_ptr<Draw> buildPage(const Draw& source, const std::vector<uint32_t>& indices) {
    const auto vertices = source.arrays[0]->data.cast<vsg::vec3Array>();
    const auto normals = source.arrays[1]->data.cast<vsg::vec3Array>();
    const auto uv = source.arrays[2]->data.cast<vsg::vec2Array>();
    auto local = vsg::uintArray::create(indices.size());
    const auto unset = std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> mapping(vertices->size(), unset), originals;
    for (size_t i = 0; i < indices.size(); ++i) {
        const auto global = indices[i];
        auto& index = mapping[global];
        if (index == unset) {
            index = uint32_t(originals.size());
            originals.push_back(global);
        }
        (*local)[i] = index;
    }
    auto positions = vsg::vec3Array::create(originals.size());
    auto directions = vsg::vec3Array::create(originals.size());
    auto coords = vsg::vec2Array::create(originals.size());
    for (size_t i = 0; i < originals.size(); ++i) {
        (*positions)[i] = (*vertices)[originals[i]];
        (*directions)[i] = (*normals)[originals[i]];
        (*coords)[i] = (*uv)[originals[i]];
    }
    auto draw = Draw::create();
    draw->assignArrays({positions, directions, coords});
    draw->assignIndices(local);
    draw->indexCount = uint32_t(local->size());
    draw->instanceCount = 1;
    return draw;
}
}

TerrainDisplayPages::TerrainDisplayPages(vsg::ref_ptr<Draw> source, double splitX, double splitZ, double fringe)
    : source_(std::move(source)), fringe_(fringe) {
    if (!source_ || source_->arrays.size() < 3 || !source_->indices)
        throw std::invalid_argument("terrain page source draw is incomplete");
    const auto vertices = source_->arrays[0]->data.cast<vsg::vec3Array>();
    const auto normals = source_->arrays[1]->data.cast<vsg::vec3Array>();
    const auto uv = source_->arrays[2]->data.cast<vsg::vec2Array>();
    const auto indices = source_->indices->data.cast<vsg::uintArray>();
    if (!vertices || !normals || !uv || !indices || normals->size() != vertices->size() ||
        uv->size() != vertices->size() || indices->size() % 3 || indices->size() != source_->indexCount)
        throw std::invalid_argument("terrain page source has inconsistent arrays");
    stats_.originalBytes = bytes(*source_);
    for (auto& page : pages_) page.sourceIndices = std::make_shared<std::vector<uint32_t>>();
    for (size_t i = 0; i < indices->size(); i += 3) {
        const std::array<uint32_t, 3> triangle{(*indices)[i], (*indices)[i+1], (*indices)[i+2]};
        if (triangle[0] >= vertices->size() || triangle[1] >= vertices->size() || triangle[2] >= vertices->size())
            throw std::invalid_argument("terrain page source has an invalid triangle");
        const auto a = vsg::dvec3((*vertices)[triangle[0]]);
        const auto b = vsg::dvec3((*vertices)[triangle[1]]);
        const auto c = vsg::dvec3((*vertices)[triangle[2]]);
        const auto center = (a + b + c) / 3.;
        auto& page = pages_[size_t(center.x >= splitX) + 2 * size_t(center.z >= splitZ)];
        page.sourceIndices->insert(page.sourceIndices->end(), triangle.begin(), triangle.end());
        page.bounds.add(a);
        page.bounds.add(b);
        page.bounds.add(c);
    }
}

void TerrainDisplayPages::prepare(Page& page) {
    if (page.draw || page.preparation.valid()) return;
    static earth::SceneTasks tasks(2, 1);
    page.preparation = tasks.submit(earth::SceneTaskKind::Terrain,
        [source = source_, indices = page.sourceIndices] { return buildPage(*source, *indices); });
}

void TerrainDisplayPages::update(vsg::Viewer& viewer, const vsg::Camera& camera, const vsg::dmat4& localToWorld) {
    if (!viewer.compileManager) return;
    const auto clip = vsg::transpose(camera.projectionMatrix->transform() * camera.viewMatrix->transform() * localToWorld);
    const std::array<vsg::dvec4, 6> planes{clip[3]+clip[0], clip[3]-clip[0], clip[3]+clip[1],
        clip[3]-clip[1], clip[2], clip[3]-clip[2]};
    std::array<bool, 4> visible{}, near{};
    stats_.visible = 0;
    for (size_t index = 0; index < pages_.size(); ++index) {
        auto& page = pages_[index];
        if (page.sourceIndices->empty()) continue;
        visible[index] = intersects(page.bounds, planes, 0);
        near[index] = visible[index] || intersects(page.bounds, planes, fringe_);
        if (visible[index]) {
            ++stats_.visible;
            prepare(page);
        }
        if (near[index]) page.outsideFrames = 0;
        else if (page.outsideFrames < retention) ++page.outsideFrames;
    }

    std::array<vsg::ref_ptr<Draw>, 4> requested;
    vsg::ref_ptr<vsg::Group> additions;
    bool changed = false;
    for (size_t index = 0; index < pages_.size(); ++index) {
        auto& page = pages_[index];
        if (near[index]) prepare(page);
        if (page.preparation.valid() && (visible[index] ||
            page.preparation.wait_for(std::chrono::seconds(0)) == std::future_status::ready)) {
            auto candidate = page.preparation.get();
            if (near[index]) {
                if (!additions) additions = vsg::Group::create();
                additions->addChild(candidate);
                requested[index] = std::move(candidate);
            }
        }
        if (page.draw && page.outsideFrames < retention) requested[index] = page.draw;
        changed |= requested[index] != page.draw;
    }
    if (!changed) return;
    // Retire the old buffers only after submitted draws have released them.
    viewer.deviceWaitIdle();
    if (additions) {
        auto compiled = viewer.compileManager->compile(additions);
        if (!compiled) throw std::runtime_error("terrain page upload failed: " + compiled.message);
        vsg::updateViewer(viewer, compiled);
    }
    node_->children.clear();
    stats_.resident = stats_.residentBytes = 0;
    for (size_t index = 0; index < pages_.size(); ++index) {
        auto& page = pages_[index];
        if (!page.draw && requested[index]) ++stats_.loads;
        if (page.draw && !requested[index]) ++stats_.evictions;
        page.draw = std::move(requested[index]);
        if (!page.draw) continue;
        node_->addChild(page.draw);
        ++stats_.resident;
        stats_.residentBytes += bytes(*page.draw);
    }
}

}
