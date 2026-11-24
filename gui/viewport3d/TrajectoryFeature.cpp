/// @file TrajectoryFeature.cpp
/// @brief 航迹线特性实现 (见 TrajectoryFeature.h)。
#include "TrajectoryFeature.h"

#include "FeatureRegistry.h"
#include "ViewportContext.h"
#include "ViewportKit.h"

#include <vsg/nodes/Group.h>
#include <vsg/nodes/StateGroup.h>

namespace vsgraysim { namespace viewport3d {

namespace {
/// 航迹线统一样式: 线宽 2, 负深度偏移避免被地形/模型遮挡。
const gui::ViewportKit::LineStyle kTrackLineStyle{2.0f, true};
}

void TrajectoryFeature::onAttach(ViewportContext& ctx)
{
    ctx_ = &ctx;
    root_ = vsg::Group::create();
    if (auto root = ctx.worldRoot()) root->addChild(root_);
}

void TrajectoryFeature::onDetach(ViewportContext& ctx)
{
    if (root_) ctx.removeFromWorld(root_);
    root_ = {};
    ctx_ = nullptr;
}

void TrajectoryFeature::setTracks(const std::vector<Track>& tracks)
{
    if (!root_) return;
    const bool had = !root_->children.empty();
    root_->children.clear();
    bool changed = false;
    for (const auto& tr : tracks) {
        if (tr.points.size() < 2) continue;
        std::vector<vsg::vec3> pts;
        pts.reserve((tr.points.size() - 1) * 2);
        for (size_t i = 1; i < tr.points.size(); ++i) {
            const auto& a = tr.points[i - 1];
            const auto& b = tr.points[i];
            pts.push_back(vsg::vec3(float(a.pos[0]), float(a.pos[1]), float(a.pos[2])));
            pts.push_back(vsg::vec3(float(b.pos[0]), float(b.pos[1]), float(b.pos[2])));
        }
        if (auto node = gui::ViewportKit::makeLineSegments(pts, tr.color,
                                                           kTrackLineStyle)) {
            root_->addChild(node);
            changed = true;
        }
    }
    if (changed || had) {
        if (auto* ctx = ctx_) ctx->requestCompile();
    }
}

void TrajectoryFeature::clear()
{
    if (root_ && !root_->children.empty()) {
        root_->children.clear();
        if (auto* ctx = ctx_) ctx->requestCompile();
    }
}

VSGVP_REGISTER_FEATURE(TrajectoryFeature);

}} // namespace vsgraysim::viewport3d
