/// @file RelationshipVisualFeature.cpp
/// @brief 见 RelationshipVisualFeature.h。
#include "RelationshipVisualFeature.h"

#include "FeatureRegistry.h"
#include "ViewportContext.h"
#include "ViewportKit.h"

#include "display/RelationshipGraphView.h"   // 复用关系配色/线型词汇表

#include <spdlog/spdlog.h>
#include <vsg/app/Camera.h>
#include <vsg/maths/transform.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/Switch.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/io/read.h>
#include <vsg/text/Text.h>
#include <vsg/text/StandardLayout.h>
#include <vsg/core/Value.h>
#include <vsg/core/Data.h>

#include "VsgQtWidget.h"

namespace vsgraysim { namespace viewport3d {

namespace {
constexpr float kMarkerHeight  = 3.0f;   ///< 阵营立杆高度
constexpr float kRingRadius    = 0.8f;   ///< 底部阵营环半径
const gui::ViewportKit::LineStyle kMarkerLineStyle{2.0f, true};
constexpr double kLabelLodDist = 400.0;  ///< 标注 LOD 显示距离 (m)

/// 图数据是否完全一致 (快照周期重复喂入时免重建+免编译)。
bool sameVec(const vsg::vec3& a, const vsg::vec3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
bool sameGraph(const std::vector<RelationshipVisualFeature::Marker>& m1,
               const std::vector<RelationshipVisualFeature::Marker>& m2)
{
    if (m1.size() != m2.size()) return false;
    for (size_t i = 0; i < m1.size(); ++i) {
        const auto& a = m1[i]; const auto& b = m2[i];
        if (a.id != b.id || a.faction != b.faction || a.label != b.label ||
            !sameVec(a.pos, b.pos)) return false;
    }
    return true;
}
bool sameLinks(const std::vector<RelationshipVisualFeature::Link>& l1,
               const std::vector<RelationshipVisualFeature::Link>& l2)
{
    if (l1.size() != l2.size()) return false;
    for (size_t i = 0; i < l1.size(); ++i) {
        if (l1[i].relation != l2[i].relation ||
            !sameVec(l1[i].from, l2[i].from) ||
            !sameVec(l1[i].to, l2[i].to) ||
            l1[i].active != l2[i].active ||
            l1[i].connected != l2[i].connected ||
            l1[i].data_rate != l2[i].data_rate) return false;
    }
    return true;
}

/// 关系类型 → 链路配色 (与关系图 Dock 同族: 制导/火控橙, 通信青)。
QColor linkRelationColor(const std::string& relation)
{
    if (relation == "guides" || relation == "fire_control" ||
        relation == "guidance") return QColor(0xff, 0xa7, 0x26);
    if (relation == "communicates" || relation == "communication" ||
        relation == "link")       return QColor(0x4d, 0xd0, 0xe1);
    return QColor(0x9e, 0x9e, 0x9e);
}

/// 关系类型 → 是否为制导/火控 (影响管道颜色 param2)。
bool isGuidanceLink(const std::string& relation)
{
    return relation == "guides" || relation == "fire_control" ||
           relation == "guidance";
}
}

void RelationshipVisualFeature::onAttach(ViewportContext& ctx)
{
    ctx_ = &ctx;
    root_ = vsg::Group::create();
    if (enabled_) setInWorld(true);
}

void RelationshipVisualFeature::onDetach(ViewportContext& ctx)
{
    setInWorld(false);
    ctx_ = nullptr;
    root_ = {};
}

void RelationshipVisualFeature::setEnabled(bool on)
{
    enabled_ = on;
    if (ctx_) { if (on) setInWorld(true); else setInWorld(false); }
    else if (root_ && on) { /* vsgReady 前: 排队, 挂载时按 enabled_ 处理 */ }
}

void RelationshipVisualFeature::setInWorld(bool add)
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

void RelationshipVisualFeature::setGraph(const std::vector<Marker>& markers)
{
    if (sameGraph(markers_, markers)) return;  // 无变化: 跳过
    markers_ = markers;
    rebuild();
}

void RelationshipVisualFeature::setLinks(const std::vector<Link>& links)
{
    if (sameLinks(links_, links)) return;      // 无变化: 跳过
    links_ = links;
    rebuild();
}

void RelationshipVisualFeature::clear()
{
    if (markers_.empty() && links_.empty()) return;
    markers_.clear();
    links_.clear();
    rebuild();
}

void RelationshipVisualFeature::rebuild()
{
    if (!root_) return;
    root_->children.clear();
    labels_.clear();
    shader_refs_.clear();

    // 平台标注: 阵营光晕穹顶 + 底部立杆 + 阵营环
    std::vector<vsg::vec3> ped_pts, ring_pts;
    for (const auto& mk : markers_) {
        const QColor fc = gui::display::RelationshipGraphView::factionColor(mk.faction);
        const vsg::vec3 top(mk.pos.x, mk.pos.y + kMarkerHeight, mk.pos.z);
        ped_pts.push_back(mk.pos); ped_pts.push_back(top);

        // 半透明阵营光晕穹顶 (替代旧光柱 + 底盘)。
        // param1 编码阵营: 红=0.0 / 蓝=0.5 / 中立=1.0。
        gui::ViewportKit::ShaderHandle hh;
        if (auto halo = gui::ViewportKit::makeFactionHalo(
                mk.pos, 4.0, 6.0, fc, &hh)) {
            root_->addChild(halo);
            if (hh.valid()) {
                const float fp = (mk.faction == 2) ? 0.0f
                               : (mk.faction == 1) ? 0.5f : 1.0f;
                shader_refs_.push_back({hh, fp, 1.0f});
            }
        }

        // 底部阵营方环 (细线框, 辅助标识)
        const float q = kRingRadius;
        ring_pts.push_back({mk.pos.x - q, mk.pos.y + 0.05f, mk.pos.z - q});
        ring_pts.push_back({mk.pos.x + q, mk.pos.y + 0.05f, mk.pos.z - q});
        ring_pts.push_back({mk.pos.x + q, mk.pos.y + 0.05f, mk.pos.z - q});
        ring_pts.push_back({mk.pos.x + q, mk.pos.y + 0.05f, mk.pos.z + q});
        ring_pts.push_back({mk.pos.x + q, mk.pos.y + 0.05f, mk.pos.z + q});
        ring_pts.push_back({mk.pos.x - q, mk.pos.y + 0.05f, mk.pos.z + q});
        ring_pts.push_back({mk.pos.x - q, mk.pos.y + 0.05f, mk.pos.z - q});
    }
    if (auto n = gui::ViewportKit::makeLineSegments(ped_pts, QColor(0xd8, 0xda, 0xe0),
                                                    kMarkerLineStyle))
        root_->addChild(n);
    if (auto n = gui::ViewportKit::makeLineSegments(ring_pts, QColor(0x9b, 0xa3, 0xb0),
                                                    kMarkerLineStyle))
        root_->addChild(n);

    // 关系链路 (通信/制导等): 半透明管道 + 数据流动画
    // 替代旧的贝塞尔拱线, 现为圆柱体管道带数据流效果。
    for (const auto& lk : links_) {
        const QColor col = linkRelationColor(lk.relation);
        const bool guidance = isGuidanceLink(lk.relation);
        const vsg::vec3 a = lk.from, b = lk.to;
        const float dist = vsg::length(b - a);
        if (dist < 1e-3f) continue;

        // 管道半径: 距离自适应 (短链路细, 长链路略粗)
        const float radius = std::clamp(dist * 0.008f, 0.15f, 2.0f);
        // 管道分段: 距离越长分段越多
        const int segs = std::clamp(int(dist / 10.0f) + 2, 4, 32);

        gui::ViewportKit::ShaderHandle th;
        if (auto tube = gui::ViewportKit::makeCommTube(
                a, b, radius, segs, 12, col, 0.25f, &th)) {
            root_->addChild(tube);
            if (th.valid())
                shader_refs_.push_back({th,
                                        lk.connected ? 1.0f : 0.0f,
                                        static_cast<float>(lk.data_rate)});
        }

        // 端点短杆 (垂直小刻度, 标识链路两端锚点)
        std::vector<vsg::vec3> end_pts;
        end_pts.push_back(a + vsg::vec3(0, -1.5f, 0));
        end_pts.push_back(a + vsg::vec3(0, 1.5f, 0));
        end_pts.push_back(b + vsg::vec3(0, -1.5f, 0));
        end_pts.push_back(b + vsg::vec3(0, 1.5f, 0));
        if (auto n = gui::ViewportKit::makeLineSegments(end_pts, col, kMarkerLineStyle))
            root_->addChild(n);
    }

    // 平台公告牌: 相机朝向中文标注 (如 "红方导弹") + 阵营色背板。
    if (!markers_.empty()) {
        if (!font_) {   // 惰性加载中文字体
            const std::string fp =
                gui::ViewportKit::resolveAssetPath("fonts/NotoSansCJKSC.ttf");
            font_ = vsg::read_cast<vsg::Font>(fp, (ctx_ ? ctx_->options() : nullptr));
            if (!font_)
                spdlog::warn("RelationshipVisualFeature: 无法加载中文字体 {}", fp);
        }
        if (font_) {
            for (const auto& mk : markers_) {
                if (mk.label.empty()) continue;
                const QColor fc =
                    gui::display::RelationshipGraphView::factionColor(mk.faction);
                // 阵营色半透明背板 (小矩形, 在文字下方)
                const vsg::vec3 bp(mk.pos.x, mk.pos.y + kMarkerHeight + 0.4f,
                                   mk.pos.z);
                const float bw = float(mk.label.size()) * 0.6f + 0.4f;
                std::vector<vsg::vec3> bg_pts;
                std::vector<vsg::vec2> bg_uvs;
                auto quad = [&](const vsg::vec3& p0, const vsg::vec3& p1,
                                const vsg::vec3& p2, const vsg::vec3& p3) {
                    bg_pts.push_back(p0); bg_uvs.push_back({0.0f, 0.0f});
                    bg_pts.push_back(p1); bg_uvs.push_back({1.0f, 0.0f});
                    bg_pts.push_back(p2); bg_uvs.push_back({1.0f, 1.0f});
                    bg_pts.push_back(p0); bg_uvs.push_back({0.0f, 0.0f});
                    bg_pts.push_back(p2); bg_uvs.push_back({1.0f, 1.0f});
                    bg_pts.push_back(p3); bg_uvs.push_back({0.0f, 1.0f});
                };
                const float bh = 0.8f;
                // 背板面向相机 (简化: 水平面)
                quad({bp.x - bw, bp.y, bp.z - 0.3f},
                     {bp.x + bw, bp.y, bp.z - 0.3f},
                     {bp.x + bw, bp.y + bh, bp.z - 0.3f},
                     {bp.x - bw, bp.y + bh, bp.z - 0.3f});
                if (auto bg = gui::ViewportKit::makeBlendedMesh(
                        bg_pts, bg_uvs, fc, 0.55f, 0.25f, {1.0f, false}))
                    root_->addChild(bg);

                auto layout = vsg::StandardLayout::create();
                layout->billboard = true;   // 相机朝向公告牌
                layout->position  = vsg::vec3(mk.pos.x, mk.pos.y + kMarkerHeight + 0.6f,
                                              mk.pos.z);
                layout->horizontalAlignment = vsg::StandardLayout::CENTER_ALIGNMENT;
                layout->verticalAlignment   = vsg::StandardLayout::CENTER_ALIGNMENT;
                layout->color = vsg::vec4(float(fc.redF()), float(fc.greenF()),
                                          float(fc.blueF()), 1.0f);
                auto text = vsg::Text::create();
                text->font   = font_;
                text->layout = layout;
                text->text   = vsg::stringValue::create(mk.label);
                text->setup(16, (ctx_ ? ctx_->options() : nullptr));
                // Switch 包裹 (供每帧 LOD 显隐, 见 onFrame)。
                auto sw = vsg::Switch::create();
                sw->addChild(vsg::Mask(0xFFFFFFFFull), text);
                root_->addChild(sw);
                labels_.push_back({sw, layout->position});
            }
        }
    }

    if (auto* ctx = ctx_) ctx->requestCompile();
}

void RelationshipVisualFeature::onFrame(ViewportContext& ctx, const FrameTick& tick)
{
    elapsed_sec_ += tick.dt_sec;

    // 标注 LOD: 距相机超距的公告牌 mask=0 摘除 (防实体多时标注糊屏)。
    if (!labels_.empty()) {
        vsg::vec3 eye(0.0f, 0.0f, 0.0f);
        if (auto* wdg = ctx.widget())
            if (auto cam = wdg->camera())
                if (auto lt = cam->viewMatrix.cast<vsg::LookAt>())
                    eye = lt->eye;
        for (auto& lr : labels_) {
            if (!lr.node) continue;
            const vsg::vec3 d = lr.pos - eye;
            lr.node->setAllChildren(vsg::length(d) <= kLabelLodDist);
        }
    }

    // 刷新管道/光晕 shader 的相机矩阵 + 数据流时间 + 通断/阵营参数。
    if (!shader_refs_.empty()) {
        vsg::dmat4 vp; vsg::dvec3 cp;
        gui::ViewportKit::cameraMatrices(ctx.camera(), vp, cp);
        for (auto& sr : shader_refs_)
            gui::ViewportKit::updateShaderNode(
                sr.h, vp, cp,
                static_cast<float>(elapsed_sec_ * sr.time_scale),
                sr.param1, 0.0f,
                ctx.contentModel());
    }
}

VSGVP_REGISTER_FEATURE(RelationshipVisualFeature);

}} // namespace vsgraysim::viewport3d
