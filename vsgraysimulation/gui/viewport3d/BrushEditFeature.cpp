/// @file BrushEditFeature.cpp
/// @brief 地形笔刷编辑特性实现 (见 BrushEditFeature.h)。
#include "BrushEditFeature.h"

#include "FeatureRegistry.h"
#include "TerrainLayerFeature.h"
#include "ViewportContext.h"
#include "VsgQtWidget.h"

#include "vsgraysim/layer1_scene/TerrainSystem.h"

#include <vulkan/vulkan.h>

#include <vsg/maths/transform.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/utils/Builder.h>

#include <QCursor>

#include <spdlog/spdlog.h>

#include <cmath>

namespace vsgraysim { namespace viewport3d {

namespace {
constexpr double kCursorLift = 0.35;   ///< 光标环贴地抬升 (m)
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------
void BrushEditFeature::onAttach(ViewportContext& ctx)
{
    ctx_ = &ctx;
    buildCursor();
    if (cursor_mt_) {
        if (auto root = ctx.worldRoot()) root->addChild(cursor_mt_);
    }

    auto* w = ctx.widget();
    if (w) {
        w->setEditRayCallback([this](const vsg::dvec3& o, const vsg::dvec3& d) {
            applyRay(o, d);
        });
        w->setBrushDragActive(active());
        // 笔画开始 → 门面压撤销快照。
        stroke_conn_ = QObject::connect(w, &gui::VsgQtWidget::brushDragStarted,
                                        w, [this] {
                                            if (stroke_started_cb_)
                                                stroke_started_cb_();
                                        });
    }
    ctx.requestCompile();
}

void BrushEditFeature::onDetach(ViewportContext& ctx)
{
    if (auto* w = ctx.widget()) {
        w->setEditRayCallback(nullptr);
        w->setBrushDragActive(false);
        QObject::disconnect(stroke_conn_);
    }
    if (cursor_mt_) ctx.removeFromWorld(cursor_mt_);
    cursor_mt_ = {};
    ctx_ = nullptr;
}

// ---------------------------------------------------------------------------
// 帧驱动: 光标环跟随鼠标下的地形表面
// ---------------------------------------------------------------------------
void BrushEditFeature::onFrame(ViewportContext& ctx, const FrameTick& tick)
{
    (void)tick;
    updateCursor();
    (void)ctx;
}

void BrushEditFeature::configure(const nlohmann::json& params)
{
    if (!params.is_object()) return;
    if (params.contains("mode")) mode_ = params.value("mode", mode_);
    if (params.contains("radius")) radius_ = params.value("radius", radius_);
    if (params.contains("strength_pct"))
        strength_pct_ = params.value("strength_pct", strength_pct_);
    if (params.contains("flatten_target"))
        flatten_target_ = params.value("flatten_target", flatten_target_);
    if (ctx_ && ctx_->widget()) ctx_->widget()->setBrushDragActive(active());
    if (!active()) cursor_radius_shown_ = -1.0;
}

// ---------------------------------------------------------------------------
// 雕刻: 世界射线 → 地形局部拾取 → applyBrush
// ---------------------------------------------------------------------------
void BrushEditFeature::applyRay(const vsg::dvec3& origin, const vsg::dvec3& dir)
{
    if (!ctx_ || !active()) return;
    auto* layer = ctx_->featureAs<TerrainLayerFeature>(TerrainLayerFeature::kId);
    if (!layer || !layer->terrain()) return;
    auto* terrain = layer->terrain();

    // 世界射线 → 地形局部坐标 (地形居中于原点)。
    const double off = layer->offset();
    const vsg::dvec3 local_origin(origin.x + off, origin.y, origin.z + off);
    double wx = 0.0, wz = 0.0;
    if (!terrain->pick(local_origin, dir, wx, wz)) return;

    terrain->applyBrush(wx, wz, BrushMode(mode_), radius_,
                        strength_pct_ / 100.0, flatten_target_);
    terrain->touch();
    layer->notifyHeightsEdited();
}

// ---------------------------------------------------------------------------
// 光标环 (单位半径垫圈, 按笔刷半径缩放)
// ---------------------------------------------------------------------------
void BrushEditFeature::buildCursor()
{
    constexpr int seg = 64;
    auto pos = vsg::vec3Array::create(seg * 2);
    auto nrm = vsg::vec3Array::create(seg * 2);
    auto uv  = vsg::vec2Array::create(seg * 2);
    for (int i = 0; i < seg; ++i) {
        const double a = 2.0 * M_PI * double(i) / seg;
        const float c = float(std::cos(a)), s = float(std::sin(a));
        (*pos)[2 * i]     = vsg::vec3(0.90f * c, 0.0f, 0.90f * s);  // 内圈
        (*pos)[2 * i + 1] = vsg::vec3(c, 0.0f, s);                  // 外圈
        (*nrm)[2 * i] = (*nrm)[2 * i + 1] = vsg::vec3(0.0f, 1.0f, 0.0f);
        (*uv)[2 * i] = (*uv)[2 * i + 1] = vsg::vec2(0.5f, 0.5f);
    }
    auto idx = vsg::uintArray::create(seg * 6);
    for (int i = 0; i < seg; ++i) {
        const uint32_t a = 2u * uint32_t(i);
        const uint32_t b = a + 1;
        const uint32_t c = 2u * uint32_t((i + 1) % seg);
        const uint32_t d = c + 1;
        (*idx)[6 * i + 0] = a; (*idx)[6 * i + 1] = c; (*idx)[6 * i + 2] = b;
        (*idx)[6 * i + 3] = b; (*idx)[6 * i + 4] = c; (*idx)[6 * i + 5] = d;
    }
    auto vid = vsg::VertexIndexDraw::create();
    vid->assignArrays({pos, nrm, uv});
    vid->assignIndices(idx);
    vid->indexCount = seg * 6;
    vid->instanceCount = 1;

    // 小尺寸亮色纹理: 与地形/水面共用同一套成熟管线。
    auto ring_tex = vsg::ubvec4Array2D::create(
        2, 2, vsg::Data::Properties{VK_FORMAT_R8G8B8A8_UNORM});
    for (uint32_t ty = 0; ty < 2; ++ty)
        for (uint32_t tx = 0; tx < 2; ++tx)
            ring_tex->set(tx, ty, vsg::ubvec4(130, 215, 255, 255));

    vsg::StateInfo si;
    si.lighting = true;
    si.image = ring_tex;
    auto sg = vsg::Builder::create()->createStateGroup(si);
    sg->addChild(vid);

    cursor_mt_ = vsg::MatrixTransform::create();
    cursor_mt_->matrix = vsg::scale(0.0, 0.0, 0.0);   // 初始隐藏
    cursor_mt_->addChild(sg);
}

void BrushEditFeature::updateCursor()
{
    if (!ctx_ || !cursor_mt_) return;
    auto hide = [&]() {
        cursor_mt_->matrix = vsg::scale(0.0, 0.0, 0.0);
        cursor_radius_shown_ = 0.0;
    };

    auto* w = ctx_->widget();
    if (!w || !active() || w->pickModeActive()) {
        // 拾取工具激活时笔刷光标让位。
        if (cursor_radius_shown_ != 0.0) hide();
        return;
    }
    const QPoint local = w->mapFromGlobal(QCursor::pos());
    if (!w->rect().contains(local)) { hide(); return; }

    vsg::dvec3 o, d;
    if (!w->mouseRay(local, o, d)) { hide(); return; }

    auto* layer = ctx_->featureAs<TerrainLayerFeature>(TerrainLayerFeature::kId);
    if (!layer || !layer->terrain()) { hide(); return; }
    auto* terrain = layer->terrain();

    // 世界射线 → 地形局部坐标 (地形居中于原点)。
    const double off = layer->offset();
    const vsg::dvec3 local_origin(o.x + off, o.y, o.z + off);
    double wx = 0.0, wz = 0.0;
    if (!terrain->pick(local_origin, d, wx, wz)) { hide(); return; }

    const double h = terrain->heightAtWorld(wx, wz);
    // 光标环在世界空间 (地形居中于 0,0)。
    cursor_mt_->matrix =
        vsg::translate(wx - off, h + kCursorLift, wz - off) *
        vsg::scale(radius_, 1.0, radius_);
    cursor_radius_shown_ = radius_;
}

VSGVP_REGISTER_FEATURE(BrushEditFeature);

}} // namespace vsgraysim::viewport3d
