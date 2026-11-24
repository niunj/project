/// @file ReferenceGridFeature.cpp
/// @brief 无限参考网格 + 原点坐标轴特性实现 (见 ReferenceGridFeature.h)。
#include "ReferenceGridFeature.h"

#include "FeatureRegistry.h"
#include "ViewportContext.h"
#include "ViewportKit.h"

#include <vsg/app/Camera.h>
#include <vsg/maths/transform.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/StateGroup.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace vsgraysim { namespace viewport3d {

namespace {

/// 相机跟随 "无限" 网格分块每边的格数: 分块原点每帧吸附到相机下方的
/// 网格单元, 视觉上网格向四周无限延伸。
constexpr int kGridCells = 64;

/// 贴地采样分辨率上限 (m): 网格线为细分折线, 每个顶点独立跟随高度场。
constexpr double kGridSampleStep = 20.0;

/// 贴地抬升 (m), 避免与地形表面 z-fighting。
constexpr double kGridDrapeLift = 0.35;

/// 网格/轴线统一样式: 线宽 1, 负深度偏移避免与地形 z-fight。
const gui::ViewportKit::LineStyle kGridLineStyle{1.0f, true};

} // anonymous namespace

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------
void ReferenceGridFeature::onAttach(ViewportContext& ctx)
{
    root_ = vsg::Group::create();

    // 固定大小网格分块挂在可移动变换下; 细分/粗分线各一个容器,
    // 内容重建时原地替换, 容器保持挂载。
    tile_mt_ = vsg::MatrixTransform::create();
    minor_sg_ = vsg::StateGroup::create();
    major_sg_ = vsg::StateGroup::create();
    tile_mt_->addChild(minor_sg_);
    tile_mt_->addChild(major_sg_);

    // 跟随内容根: rocky 地理模式下铺在锚点 ENU 切平面, 局部模式铺在世界原点。
    if (auto world = ctx.contentRoot()) world->addChild(root_);
    rebuild(ctx);
    spdlog::info("ReferenceGridFeature: infinite grid + XYZ axes built");
}

void ReferenceGridFeature::onDetach(ViewportContext& ctx)
{
    if (root_) ctx.removeFromWorld(root_);
    root_ = {};
    tile_mt_ = {};
    minor_sg_ = {};
    major_sg_ = {};
    minor_verts_ = {};
    major_verts_ = {};
}

// ---------------------------------------------------------------------------
// 帧驱动: 分块跟随相机 + 按需贴地重铺
// ---------------------------------------------------------------------------
void ReferenceGridFeature::onFrame(ViewportContext& ctx, const FrameTick& tick)
{
    (void)tick;
    if (rebuild_pending_) {
        rebuild_pending_ = false;
        rebuild(ctx);
    }
    if (!tile_mt_ || !show_) return;
    auto* cam = ctx.camera();
    if (!cam) return;
    auto* lookAt = dynamic_cast<vsg::LookAt*>(cam->viewMatrix.get());
    if (!lookAt) return;

    const double minor = std::max(0.1, minor_step_);
    const double half  = kGridCells * minor;
    const double cx = std::floor((lookAt->eye.x + half) / minor) * minor - half;
    const double cz = std::floor((lookAt->eye.z + half) / minor) * minor - half;
    if (cx != tile_cx_ || cz != tile_cz_) {
        tile_cx_ = cx;
        tile_cz_ = cz;
        drape_dirty_ = true;   // 分块移动 → 重新贴地
    }

    // 地形重建/编辑后网格代数或高度版本变化 → 重新贴地。
    if (auto* tq = ctx.terrainQuery(); tq && tq->hasTerrain()) {
        if (tq->nodeGeneration() != terrain_gen_seen_ ||
            tq->heightsVersion() != heights_ver_seen_) {
            terrain_gen_seen_ = tq->nodeGeneration();
            heights_ver_seen_ = tq->heightsVersion();
            drape_dirty_ = true;
        }
    }
    if (drape_dirty_) drape(ctx);
    tile_mt_->matrix = vsg::translate(cx, 0.0, cz);
}

// ---------------------------------------------------------------------------
// 重建网格线几何与原点轴
// ---------------------------------------------------------------------------
void ReferenceGridFeature::rebuild(ViewportContext& ctx)
{
    if (!root_ || !tile_mt_ || !minor_sg_ || !major_sg_) return;

    const double minor = std::max(0.1, minor_step_);
    const double major = std::max(minor, major_step_);
    const double half  = kGridCells * minor;       // 分块半边长 (m)
    const int    ratio = int(std::llround(major / minor));

    // 折线生成器: 每种间距下所有网格线沿 -half..+half 双向延伸,
    // 每 kGridSampleStep 细分一次以便贴地; `skip_ratio` 跳过承载粗分线的
    // 细分线。贴地采样步长封顶为 2 倍地形格宽, 否则线段中点会陷入山坡,
    // 在 +X/+Z 象限留下残影短线。
    auto* tq = ctx.terrainQuery();
    const double terrain_cell = (tq && tq->hasTerrain()) ? tq->cellSize() : 0.0;
    auto buildLines = [&](double spacing, int skip_ratio) {
        const int n = 2 * kGridCells + 1;
        const double step = (terrain_cell > 0.0)
                                ? std::min(kGridSampleStep, 2.0 * terrain_cell)
                                : kGridSampleStep;
        const int segs = int(std::ceil(2.0 * half / step));
        size_t kept = 0;
        for (int i = 0; i < n; ++i)
            if (skip_ratio <= 1 || i % skip_ratio != 0) ++kept;
        auto verts = vsg::vec3Array::create(kept * segs * 4);
        // 分块移动或地形变化时在 CPU 侧原地重铺。
        verts->properties.dataVariance = vsg::DYNAMIC_DATA;
        size_t k = 0;
        for (int i = 0; i < n; ++i) {
            if (skip_ratio > 1 && i % skip_ratio == 0) continue;
            const float p = float(-half + i * spacing);
            for (int s = 0; s < segs; ++s) {
                const float a = float(-half + s * step);
                const float b = float(std::min(double(half),
                                               -half + (s + 1) * step));
                // 平行 Z 的线段 (x = p)
                (*verts)[k++] = vsg::vec3(p, 0.0f, a);
                (*verts)[k++] = vsg::vec3(p, 0.0f, b);
                // 平行 X 的线段 (z = p)
                (*verts)[k++] = vsg::vec3(a, 0.0f, p);
                (*verts)[k++] = vsg::vec3(b, 0.0f, p);
            }
        }
        return verts;
    };

    // ---- 网格线内容 (容器保持挂载, 原地替换) ----
    minor_verts_ = (ratio > 1) ? buildLines(minor, ratio) : nullptr;
    major_verts_ = buildLines(major, 0);
    minor_sg_->children.clear();
    if (minor_verts_) {
        if (auto sg = gui::ViewportKit::makeLineSegments(
                minor_verts_, minor_color_, kGridLineStyle))
            minor_sg_->addChild(sg);
    }
    major_sg_->children.clear();
    if (auto sg = gui::ViewportKit::makeLineSegments(
            major_verts_, major_color_, kGridLineStyle))
        major_sg_->addChild(sg);
    // 下一帧强制重新贴地 (分块原点随之更新)。
    tile_cx_ = tile_cz_ = 1e300;
    drape_dirty_ = true;
    terrain_gen_seen_ = (tq && tq->hasTerrain()) ? tq->nodeGeneration() : 0;
    heights_ver_seen_ = (tq && tq->hasTerrain()) ? tq->heightsVersion() : 0;

    // ---- 可见性 + 原点轴 ----
    root_->children.clear();
    if (show_) {
        root_->addChild(tile_mt_);

        // 原点 XYZ 轴 (与视口角落 gizmo 同色)。
        const float axis_len = 80.0f;
        struct AxisDef { vsg::vec3 dir; QColor color; };
        const AxisDef axis_defs[3] = {
            { vsg::vec3(1.0f, 0.0f, 0.0f), QColor(224, 90, 78)   },
            { vsg::vec3(0.0f, 1.0f, 0.0f), QColor(84, 196, 106)  },
            { vsg::vec3(0.0f, 0.0f, 1.0f), QColor(74, 144, 226)  },
        };
        for (const auto& a : axis_defs) {
            auto verts = vsg::vec3Array::create(2);
            (*verts)[0] = vsg::vec3(0.0f, 0.0f, 0.0f);
            (*verts)[1] = a.dir * axis_len;
            if (auto sg = gui::ViewportKit::makeLineSegments(
                    verts, a.color, kGridLineStyle))
                root_->addChild(sg);
        }
    }
    ctx.requestCompile();
}

// ---------------------------------------------------------------------------
// 贴地重铺: 采样地形高度改写网格顶点 (DYNAMIC 数组原地更新)
// ---------------------------------------------------------------------------
void ReferenceGridFeature::drape(ViewportContext& ctx)
{
    drape_dirty_ = false;
    auto* tq = ctx.terrainQuery();
    if (!tq || !tq->hasTerrain()) return;   // 无地形时保持平面网格
    auto drapeArr = [&](vsg::ref_ptr<vsg::vec3Array> verts) {
        if (!verts) return;
        for (size_t i = 0; i < verts->size(); ++i) {
            vsg::vec3& v = (*verts)[i];
            v.y = float(tq->heightAtWorld(tile_cx_ + double(v.x),
                                          tile_cz_ + double(v.z)) +
                        kGridDrapeLift);
        }
        verts->dirty();
    };
    drapeArr(minor_verts_);
    drapeArr(major_verts_);
}

// ---------------------------------------------------------------------------
// 配置 / 状态
// ---------------------------------------------------------------------------
void ReferenceGridFeature::configure(const nlohmann::json& params)
{
    if (!params.is_object()) return;
    if (params.contains("show")) show_ = params.value("show", show_);
    if (params.contains("major_step"))
        major_step_ = params.value("major_step", major_step_);
    if (params.contains("minor_step"))
        minor_step_ = params.value("minor_step", minor_step_);
    if (params.contains("major_color")) {
        major_color_.setNamedColor(QString::fromStdString(
            params.value("major_color", std::string("#73809e"))));
    }
    if (params.contains("minor_color")) {
        minor_color_.setNamedColor(QString::fromStdString(
            params.value("minor_color", std::string("#40485a"))));
    }
    // 未挂载时参数已保存, onAttach 会按最新参数重建;
    // 已挂载则在下一帧重建 (configure 无上下文)。
    if (attached()) rebuild_pending_ = true;
}

nlohmann::json ReferenceGridFeature::saveState() const
{
    return {
        {"show", show_},
        {"major_step", major_step_},
        {"minor_step", minor_step_},
        {"major_color", major_color_.name().toStdString()},
        {"minor_color", minor_color_.name().toStdString()},
    };
}

VSGVP_REGISTER_FEATURE(ReferenceGridFeature);

}} // namespace vsgraysim::viewport3d
