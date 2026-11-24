/// @file AnalysisOverlayFeature.cpp
/// @brief 地形分析特性实现 (见 AnalysisOverlayFeature.h)。
#include "AnalysisOverlayFeature.h"

#include "FeatureRegistry.h"
#include "TerrainAnalyzer.h"
#include "TerrainLayerFeature.h"
#include "ViewportContext.h"
#include "ViewportKit.h"
#include "VsgQtWidget.h"

#include "vsgraysim/layer1_scene/TerrainSystem.h"

#include <vulkan/vulkan.h>

#include <vsg/maths/transform.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/Switch.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/RasterizationState.h>
#include <vsg/utils/Builder.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace vsgraysim { namespace viewport3d {

namespace {

/// 分析覆盖层颜色纹理分辨率 (与地形自身程序化纹理一致)。
constexpr uint32_t kAnalysisTexSize = 256;

/// 覆盖层贴地抬升 (m)。
constexpr double kAnalysisOverlayLift = 0.6;

/// 通视分析的眼点/目标点离地高度 (m)。
constexpr double kLosEyeLift = 1.7;

/// 分析线段样式: 线宽 1, 负深度偏移避免与地形 z-fight。
const gui::ViewportKit::LineStyle kAnalysisLineStyle{1.0f, true};

} // anonymous namespace

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------
void AnalysisOverlayFeature::onAttach(ViewportContext& ctx)
{
    ctx_ = &ctx;
    if (auto* w = ctx.widget()) {
        w->setPickRayCallback([this](const vsg::dvec3& o, const vsg::dvec3& d) {
            onPickRay(o, d);
        });
        w->setPickModeActive(pick_tool_ != 0);
    }
}

void AnalysisOverlayFeature::onDetach(ViewportContext& ctx)
{
    if (auto* w = ctx.widget()) {
        w->setPickRayCallback(nullptr);
        w->setPickModeActive(false);
    }
    clearSegments();
    if (segments_) ctx.removeFromWorld(segments_);
    segments_ = {};
    // 覆盖层挂在地形居中变换下, 随地形层卸载一并消失。
    overlay_sw_ = {};
    vid_ = {};
    pos_ = {};
    nrm_ = {};
    tex_ = {};
    ctx_ = nullptr;
}

// ---------------------------------------------------------------------------
// 帧驱动: 地形变化或模式切换后刷新覆盖层
// ---------------------------------------------------------------------------
void AnalysisOverlayFeature::onFrame(ViewportContext& ctx, const FrameTick& tick)
{
    (void)tick;
    if (auto* tq = ctx.terrainQuery(); tq && tq->hasTerrain()) {
        if (tq->nodeGeneration() != content_gen_seen_ ||
            tq->heightsVersion() != content_ver_seen_) {
            content_gen_seen_ = tq->nodeGeneration();
            content_ver_seen_ = tq->heightsVersion();
            overlay_dirty_ = true;
        }
    }
    if (overlay_dirty_) refreshOverlay(ctx);
}

void AnalysisOverlayFeature::configure(const nlohmann::json& params)
{
    if (!params.is_object()) return;
    if (params.contains("mode")) {
        mode_ = params.value("mode", mode_);
        overlay_dirty_ = true;
    }
    if (params.contains("pick_tool")) {
        pick_tool_ = params.value("pick_tool", pick_tool_);
        start_set_ = false;
        if (ctx_ && ctx_->widget())
            ctx_->widget()->setPickModeActive(pick_tool_ != 0);
    }
}

// ---------------------------------------------------------------------------
// 覆盖层网格构建 (与地形同分辨率, 挂在地形居中变换下)
// ---------------------------------------------------------------------------
void AnalysisOverlayFeature::ensureOverlay(ViewportContext& ctx)
{
    auto* layer = ctx.featureAs<TerrainLayerFeature>(TerrainLayerFeature::kId);
    if (!layer || !layer->terrain() || !layer->terrainTransform()) return;
    auto* terrain = layer->terrain();
    const uint32_t n = terrain->grid();
    if (n < 2) return;
    if (vid_ && mesh_gen_seen_ == terrain->nodeGeneration()) return;

    // (重)建覆盖层网格。挂在地形居中变换下 → 共享地形局部坐标。
    const size_t nv = size_t(n) * n;
    const size_t nc = size_t(n - 1) * (n - 1);

    pos_ = vsg::vec3Array::create(nv);
    pos_->properties.dataVariance = vsg::DYNAMIC_DATA;
    nrm_ = vsg::vec3Array::create(nv);
    nrm_->properties.dataVariance = vsg::DYNAMIC_DATA;
    auto uv = vsg::vec2Array::create(nv);
    for (uint32_t j = 0; j < n; ++j)
        for (uint32_t i = 0; i < n; ++i)
            (*uv)[size_t(j) * n + i] =
                vsg::vec2(float(i) / float(n - 1), float(j) / float(n - 1));

    auto idx = vsg::uintArray::create(nc * 6);
    {
        uint32_t* ip = reinterpret_cast<uint32_t*>(idx->dataPointer());
        size_t k = 0;
        for (uint32_t j = 0; j < n - 1; ++j) {
            for (uint32_t i = 0; i < n - 1; ++i) {
                const uint32_t a = j * n + i;
                const uint32_t b = a + 1;
                const uint32_t c = a + n;
                const uint32_t d = c + 1;
                ip[k++] = a; ip[k++] = c; ip[k++] = b;
                ip[k++] = b; ip[k++] = c; ip[k++] = d;
            }
        }
    }

    vid_ = vsg::VertexIndexDraw::create();
    vid_->assignArrays({pos_, nrm_, uv});
    vid_->assignIndices(idx);
    vid_->indexCount = static_cast<uint32_t>(nc * 6);
    vid_->instanceCount = 1;

    tex_ = vsg::ubvec4Array2D::create(
        kAnalysisTexSize, kAnalysisTexSize,
        vsg::Data::Properties{VK_FORMAT_R8G8B8A8_UNORM});
    tex_->properties.dataVariance = vsg::DYNAMIC_DATA;

    // 纹理管线 (与地形相同的 Builder 模式)。
    vsg::StateInfo si;
    si.lighting = true;
    si.image = tex_;
    auto sg = vsg::Builder::create()->createStateGroup(si);

    // 负深度偏移把覆盖层拉向观察者, 掠射角下也不与地形 z-fight。
    auto patch_states = [](const auto& cmds) {
        for (auto& sc : cmds) {
            if (auto rs = sc.template cast<vsg::RasterizationState>()) {
                rs->cullMode = VK_CULL_MODE_NONE;
                rs->depthBiasEnable = VK_TRUE;
                rs->depthBiasConstantFactor = -1.5f;
                rs->depthBiasSlopeFactor = -1.5f;
            }
        }
    };
    patch_states(sg->stateCommands);
    for (auto& sc : sg->stateCommands) {
        if (auto bgp = sc.cast<vsg::BindGraphicsPipeline>())
            patch_states(bgp->pipeline->pipelineStates);
    }
    sg->addChild(vid_);

    if (!overlay_sw_) {
        overlay_sw_ = vsg::Switch::create();
        layer->terrainTransform()->addChild(overlay_sw_);
    }
    overlay_sw_->children.clear();
    overlay_sw_->addChild(false, sg);   // 刷新前保持隐藏
    mesh_gen_seen_ = terrain->nodeGeneration();

    ctx.requestCompile();
}

// ---------------------------------------------------------------------------
// 覆盖层内容刷新 (纹理着色 + 贴地顶点 + 中心差分法线)
// ---------------------------------------------------------------------------
void AnalysisOverlayFeature::refreshOverlay(ViewportContext& ctx)
{
    auto* layer = ctx.featureAs<TerrainLayerFeature>(TerrainLayerFeature::kId);
    if (!layer || !layer->terrain() || !layer->terrainTransform())
        return;   // 场景未就绪: 保持 dirty 下帧重试
    auto* terrain = layer->terrain();

    if (mode_ == 0) {   // Normal → 隐藏覆盖层
        overlay_dirty_ = false;
        if (overlay_sw_) overlay_sw_->setAllChildren(false);
        return;
    }
    ensureOverlay(ctx);
    if (!vid_ || !tex_ || !pos_ || !nrm_) return;   // 构建失败: 下帧重试
    overlay_dirty_ = false;

    const auto& heights = terrain->heightsData();
    const uint32_t n = terrain->grid();
    const double extent = terrain->extent();

    // ---- 分析图 → RGB 颜色数组 ----
    std::vector<uint8_t> rgb;
    if (mode_ == 1) {
        rgb = gui::TerrainAnalyzer::slopeColors(
            gui::TerrainAnalyzer::slopeMap(heights, n, extent), n);
    } else if (mode_ == 2) {
        rgb = gui::TerrainAnalyzer::aspectColors(
            gui::TerrainAnalyzer::aspectMap(heights, n, extent), n);
    } else {
        rgb = gui::TerrainAnalyzer::materialColors(
            gui::TerrainAnalyzer::materialMap(heights, n, extent,
                                              terrain->waterLevel()), n);
    }
    if (rgb.empty()) return;

    // ---- 颜色数组 → 纹理 (最近邻重采样, 纹理方向与地形纹理一致) ----
    for (uint32_t ty = 0; ty < kAnalysisTexSize; ++ty) {
        const uint32_t j = std::min(
            n - 1, uint32_t(std::lround(
                        (double(ty) + 0.5) / kAnalysisTexSize * double(n - 1))));
        for (uint32_t tx = 0; tx < kAnalysisTexSize; ++tx) {
            const uint32_t i = std::min(
                n - 1, uint32_t(std::lround(
                            (double(tx) + 0.5) / kAnalysisTexSize * double(n - 1))));
            const size_t k = size_t(j) * n + i;
            tex_->set(tx, ty,
                      vsg::ubvec4(rgb[3 * k], rgb[3 * k + 1], rgb[3 * k + 2], 255));
        }
    }
    tex_->dirty();

    // ---- 覆盖层贴地 (顶点 + 中心差分法线, 同 TerrainSystem) ----
    const double cell = terrain->cellSize();
    if (cell <= 0.0) return;
    const double two_cell = 2.0 * cell;
    for (uint32_t j = 0; j < n; ++j) {
        const uint32_t jm = (j > 0) ? j - 1 : 0;
        const uint32_t jp = (j + 1 < n) ? j + 1 : n - 1;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t im = (i > 0) ? i - 1 : 0;
            const uint32_t ip = (i + 1 < n) ? i + 1 : n - 1;
            const size_t k = size_t(j) * n + i;
            (*pos_)[k] = vsg::vec3(float(i * cell),
                                   float(heights[k] + kAnalysisOverlayLift),
                                   float(j * cell));
            const double hx = heights[size_t(j) * n + ip] -
                              heights[size_t(j) * n + im];
            const double hz = heights[size_t(jp) * n + i] -
                              heights[size_t(jm) * n + i];
            vsg::vec3 nv(float(-hx / two_cell), 1.0f, float(-hz / two_cell));
            const float len = std::sqrt(nv.x * nv.x + nv.y * nv.y + nv.z * nv.z);
            if (len > 1e-12f) nv /= len;
            (*nrm_)[k] = nv;
        }
    }
    pos_->dirty();
    nrm_->dirty();

    overlay_sw_->setAllChildren(true);
}

// ---------------------------------------------------------------------------
// 两点拾取工具 (通视 / 剖面 / 距离)
// ---------------------------------------------------------------------------
void AnalysisOverlayFeature::onPickRay(const vsg::dvec3& origin,
                                       const vsg::dvec3& dir)
{
    if (!ctx_ || pick_tool_ == 0) return;
    auto* layer = ctx_->featureAs<TerrainLayerFeature>(TerrainLayerFeature::kId);
    if (!layer || !layer->terrain()) return;
    auto* terrain = layer->terrain();

    // 世界射线 → 地形局部坐标 (地形居中于原点)。
    const double off = layer->offset();
    const vsg::dvec3 local_origin(origin.x + off, origin.y, origin.z + off);
    double wx = 0.0, wz = 0.0;
    if (!terrain->pick(local_origin, dir, wx, wz)) {
        if (result_cb_) result_cb_({{"kind", "pick_miss"}});
        return;
    }
    const double h = terrain->heightAtWorld(wx, wz);

    if (!start_set_) {
        x0_ = wx; z0_ = wz; h0_ = h;
        start_set_ = true;
        if (result_cb_)
            result_cb_({{"kind", "pick_start"},
                        {"x", wx - off}, {"z", wz - off}, {"h", h}});
        return;
    }

    switch (pick_tool_) {
    case 1: finishLos(wx, wz, h); break;
    case 2: finishProfile(wx, wz, h); break;
    case 3: finishDistance(wx, wz, h); break;
    default: break;
    }
    start_set_ = false;
}

void AnalysisOverlayFeature::finishLos(double x1, double z1, double h1)
{
    auto* layer = ctx_->featureAs<TerrainLayerFeature>(TerrainLayerFeature::kId);
    auto* terrain = layer->terrain();
    const double off = layer->offset();

    const auto res = gui::TerrainAnalyzer::lineOfSight(
        terrain->heightsData(), terrain->grid(), terrain->extent(),
        x0_, z0_, x1, z1, kLosEyeLift, kLosEyeLift, 256);

    const vsg::dvec3 a(x0_ - off, h0_ + kLosEyeLift, z0_ - off);
    const vsg::dvec3 b(x1 - off, h1 + kLosEyeLift, z1 - off);
    clearSegments();

    nlohmann::json j;
    j["kind"] = "los";
    j["visible"] = res.visible;
    j["eye_lift"] = kLosEyeLift;
    if (res.visible) {
        drawSegment(*ctx_, a, b, QColor(96, 226, 120));
    } else {
        const vsg::dvec3 blk(res.blocker_x - off, res.blocker_h + 0.3,
                             res.blocker_z - off);
        drawSegment(*ctx_, a, blk, QColor(255, 92, 84));
        drawSegment(*ctx_, blk, b, QColor(120, 52, 52));
        j["blocker_t"] = res.blocker_t;
        j["blocker_x"] = res.blocker_x - off;
        j["blocker_z"] = res.blocker_z - off;
        j["blocker_h"] = res.blocker_h;
    }
    if (result_cb_) result_cb_(j);
}

void AnalysisOverlayFeature::finishProfile(double x1, double z1, double h1)
{
    auto* layer = ctx_->featureAs<TerrainLayerFeature>(TerrainLayerFeature::kId);
    auto* terrain = layer->terrain();
    const double off = layer->offset();

    const auto samples = gui::TerrainAnalyzer::profile(
        terrain->heightsData(), terrain->grid(), terrain->extent(),
        x0_, z0_, x1, z1, 200);
    const double length = samples.empty() ? 0.0 : samples.back().dist;

    if (profile_cb_) {
        std::vector<double> dists, heights;
        dists.reserve(samples.size());
        heights.reserve(samples.size());
        for (const auto& s : samples) {
            dists.push_back(s.dist);
            heights.push_back(s.height);
        }
        profile_cb_(dists, heights, terrain->waterLevel());
    }

    // 3D 视口中绘制剖面轨迹参考线。
    clearSegments();
    drawSegment(*ctx_,
                vsg::dvec3(x0_ - off, h0_ + 0.5, z0_ - off),
                vsg::dvec3(x1 - off, h1 + 0.5, z1 - off),
                QColor(127, 208, 255));
    if (result_cb_)
        result_cb_({{"kind", "profile"},
                    {"length", length}, {"dh", h1 - h0_}});
}

void AnalysisOverlayFeature::finishDistance(double x1, double z1, double h1)
{
    auto* layer = ctx_->featureAs<TerrainLayerFeature>(TerrainLayerFeature::kId);
    const double off = layer->offset();

    const double dx = x1 - x0_;
    const double dz = z1 - z0_;
    const double dy = h1 - h0_;
    const double horiz  = std::sqrt(dx * dx + dz * dz);
    const double direct = std::sqrt(horiz * horiz + dy * dy);

    clearSegments();
    drawSegment(*ctx_,
                vsg::dvec3(x0_ - off, h0_ + 0.5, z0_ - off),
                vsg::dvec3(x1 - off, h1 + 0.5, z1 - off),
                QColor(255, 208, 84));
    if (result_cb_)
        result_cb_({{"kind", "distance"},
                    {"horiz", horiz}, {"dy", dy}, {"direct", direct}});
}

// ---------------------------------------------------------------------------
// 分析线段 (世界空间)
// ---------------------------------------------------------------------------
void AnalysisOverlayFeature::drawSegment(ViewportContext& ctx,
                                         const vsg::dvec3& a,
                                         const vsg::dvec3& b,
                                         const QColor& color)
{
    if (!segments_) {
        segments_ = vsg::Group::create();
        if (auto root = ctx.worldRoot()) root->addChild(segments_);
    }
    auto verts = vsg::vec3Array::create(2);
    (*verts)[0] = vsg::vec3(float(a.x), float(a.y), float(a.z));
    (*verts)[1] = vsg::vec3(float(b.x), float(b.y), float(b.z));
    if (auto sg = gui::ViewportKit::makeLineSegments(
            verts, color, kAnalysisLineStyle))
        segments_->addChild(sg);
    ctx.requestCompile();
}

void AnalysisOverlayFeature::clearSegments()
{
    if (segments_) segments_->children.clear();
}

VSGVP_REGISTER_FEATURE(AnalysisOverlayFeature);

}} // namespace vsgraysim::viewport3d
