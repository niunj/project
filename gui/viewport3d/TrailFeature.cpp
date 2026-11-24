/// @file TrailFeature.cpp
/// @brief 见 TrailFeature.h。
#include "TrailFeature.h"

#include "FeatureRegistry.h"
#include "ViewportContext.h"
#include "ViewportKit.h"

#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/StateGroup.h>

#include <algorithm>
#include <cmath>

namespace vsgraysim { namespace viewport3d {

namespace {
const gui::ViewportKit::LineStyle kTrailStyle{1.6f, true};
constexpr float kRibbonWidth = 3.5f;   ///< 拖尾条带宽度 (m)

/// 阵营 → 拖尾配色 (与关系图三方一致: 红 #ff5252 / 蓝 #448aff / 中立 #9e9e9e)。
QColor trailFactionColor(int faction)
{
    switch (faction) {
        case 2: return QColor(0xff, 0x52, 0x52);
        case 1: return QColor(0x44, 0x8a, 0xff);
        default: break;
    }
    return QColor(0x9e, 0x9e, 0x9e);
}
} // namespace

void TrailFeature::onAttach(ViewportContext& ctx)
{
    ctx_ = &ctx;
    root_ = vsg::Group::create();
    if (enabled_) setInWorld(true);
}

void TrailFeature::onDetach(ViewportContext& ctx)
{
    setInWorld(false);
    ctx_ = nullptr;
    root_ = {};
}

void TrailFeature::setEnabled(bool on)
{
    enabled_ = on;
    if (ctx_) { if (on) setInWorld(true); else setInWorld(false); }
}

void TrailFeature::setInWorld(bool add)
{
    if (!root_ || !ctx_) return;
    const bool now = in_world_;
    if (add && !now) {
        ctx_->contentRoot()->addChild(root_);
        in_world_ = true;
    } else if (!add && now) {
        ctx_->removeFromWorld(root_);
        in_world_ = false;
    }
    ctx_->requestCompile();
}

void TrailFeature::addPoint(const std::string& id, const vsg::vec3& pos,
                            int faction)
{
    Track& t = tracks_[id];
    t.faction = faction;
    if (!t.pts.empty()) {
        const vsg::vec3& b = t.pts.back();
        const vsg::vec3 d = b - pos;
        if (d.x * d.x + d.y * d.y + d.z * d.z < 0.0025f /*0.05m*/) return;
    }
    t.pts.push_back(pos);
    while (static_cast<int>(t.pts.size()) > max_pts_) t.pts.pop_front();
    dirty_ = true;   // 标记脏: onFrame 批量重建 (同帧多次 addPoint 只重建一次)
}

void TrailFeature::onFrame(ViewportContext& ctx, const FrameTick& tick)
{
    elapsed_sec_ += tick.dt_sec;
    if (dirty_) {
        dirty_ = false;
        rebuild();
    }
    // 刷新条带 shader 相机矩阵 (拖尾无额外动画参数, time 仅供渐隐参考)。
    if (!shader_handles_.empty()) {
        vsg::dmat4 vp; vsg::dvec3 cp;
        gui::ViewportKit::cameraMatrices(ctx.camera(), vp, cp);
        for (auto& h : shader_handles_)
            gui::ViewportKit::updateShaderNode(h, vp, cp,
                                               float(elapsed_sec_), 0.0f, 0.0f,
                                               ctx.contentModel());
    }
}

void TrailFeature::removeTrack(const std::string& id)
{
    tracks_.erase(id);
    rebuild();
}

void TrailFeature::clear()
{
    tracks_.clear();
    rebuild();
}

void TrailFeature::rebuild()
{
    if (!root_) return;
    root_->children.clear();
    shader_handles_.clear();

    for (const auto& [id, t] : tracks_) {
        (void)id;
        if (t.pts.size() < 2) continue;
        const QColor col = trailFactionColor(t.faction);

        // 渐隐拖尾: 水平条带网格替代折线分块着色,
        // 复用 trail_ribbon shader 沿长度方向渐隐 (旧段透明→新段不透明)。
        std::vector<vsg::vec3> pts(t.pts.begin(), t.pts.end());
        gui::ViewportKit::ShaderHandle sh;
        if (auto ribbon = gui::ViewportKit::makeTrailRibbon(
                pts, kRibbonWidth, col, &sh)) {
            root_->addChild(ribbon);
            if (sh.valid()) shader_handles_.push_back(sh);
        }

        // 方向箭头: 每隔 kArrowEvery 个采样点, 在运动方向上画 XZ 小箭头
        // (业务表现: 航向指示, 便于辨识机动方向)。
        constexpr int kArrowEvery = 16;
        std::vector<vsg::vec3> arrows;
        const float as = 2.2f;            // 箭头半长 (m)
        for (size_t i = kArrowEvery; i + 1 < t.pts.size(); i += size_t(kArrowEvery)) {
            const vsg::vec3 d = t.pts[i + 1] - t.pts[i - 1];
            const float len = std::sqrt(d.x * d.x + d.z * d.z);
            if (len < 1e-3f) continue;
            const float ux = d.x / len, uz = d.z / len;   // XZ 平面航向
            const vsg::vec3 tip = t.pts[i];
            arrows.push_back(tip);
            arrows.push_back({tip.x - (ux + 0.6f * uz) * as,
                              tip.y, tip.z - (uz - 0.6f * ux) * as});
            arrows.push_back(tip);
            arrows.push_back({tip.x - (ux - 0.6f * uz) * as,
                              tip.y, tip.z - (uz + 0.6f * ux) * as});
        }
        if (!arrows.empty())
            if (auto n = gui::ViewportKit::makeLineSegments(arrows, col, kTrailStyle))
                root_->addChild(n);
    }

    if (ctx_) ctx_->requestCompile();
}

VSGVP_REGISTER_FEATURE(TrailFeature);

}} // namespace vsgraysim::viewport3d