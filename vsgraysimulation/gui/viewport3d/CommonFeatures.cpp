/// @file CommonFeatures.cpp
/// @brief 四个基础视口特性实现 (见 CommonFeatures.h)。
#include "CommonFeatures.h"

#include "FeatureRegistry.h"
#include "ViewportContext.h"
#include "ViewportKit.h"
#include "VsgQtWidget.h"

#include <vsgImGui/imgui.h>

#include <vsg/app/Camera.h>
#include <vsg/lighting/AmbientLight.h>
#include <vsg/lighting/DirectionalLight.h>
#include <vsg/maths/transform.h>
#include <vsg/nodes/Group.h>

#include <cmath>
#include <spdlog/spdlog.h>

namespace vsgraysim { namespace viewport3d {

// ---------------------------------------------------------------------------
// LightsFeature — 默认灯光组 (环境光 + 太阳平行光)
// ---------------------------------------------------------------------------
void LightsFeature::onAttach(ViewportContext& ctx)
{
    ctx_ = &ctx;
    lights_ = gui::ViewportKit::createDefaultLights();
    applyParams();
    // rocky_sky 同批配置时 (预设中天空先于灯光挂载), 场景灯光由天空物理灯独占。
    surrendered_ = (ctx.findFeature("rocky_sky") != nullptr);
    if (!surrendered_) {
        if (auto root = ctx.worldRoot()) root->addChild(lights_);
    } else {
        spdlog::info("LightsFeature: rocky_sky 已接管场景光照, 默认灯光抑制");
    }
    ctx.requestCompile();
}

void LightsFeature::onDetach(ViewportContext& ctx)
{
    if (lights_) ctx.removeFromWorld(lights_);
    lights_ = {};
    ctx_ = nullptr;
    surrendered_ = false;
}

void LightsFeature::surrenderToSky(bool surrender)
{
    if (surrendered_ == surrender || !ctx_ || !lights_) return;
    surrendered_ = surrender;
    if (surrender) {
        ctx_->removeFromWorld(lights_);
        spdlog::info("LightsFeature: 场景光照让渡给 rocky_sky, 默认灯光已摘除");
    } else if (auto root = ctx_->worldRoot()) {
        root->addChild(lights_);
        ctx_->requestCompile();
        spdlog::info("LightsFeature: rocky_sky 卸载, 默认灯光已恢复");
    }
}

void LightsFeature::configure(const nlohmann::json& params)
{
    if (params.is_object()) params_.update(params);
    if (lights_) applyParams();
}

void LightsFeature::applyParams()
{
    if (!lights_ || !params_.is_object()) return;
    for (auto& child : lights_->children) {
        if (auto* amb = dynamic_cast<vsg::AmbientLight*>(child.get())) {
            if (params_.contains("ambient_intensity"))
                amb->intensity = params_.value("ambient_intensity", double(amb->intensity));
        } else if (auto* sun = dynamic_cast<vsg::DirectionalLight*>(child.get())) {
            if (params_.contains("sun_intensity"))
                sun->intensity = params_.value("sun_intensity", double(sun->intensity));
            if (params_.contains("sun_direction") && params_["sun_direction"].is_array() &&
                params_["sun_direction"].size() == 3) {
                const auto& d = params_["sun_direction"];
                sun->direction = vsg::dvec3(d[0].get<double>(), d[1].get<double>(),
                                            d[2].get<double>());
            }
        }
    }
}

// ---------------------------------------------------------------------------
// AxisGizmoFeature — 右下角 XYZ 轴向指示 (vsgImGui 前景层绘制)
// ---------------------------------------------------------------------------
void AxisGizmoFeature::onAttach(ViewportContext& ctx)
{
    ctx_ = &ctx;
}

void AxisGizmoFeature::onDetach(ViewportContext& ctx)
{
    (void)ctx;
    *imgui_alive_ = false;   // 作废残留 ImGui 回调 (见头文件说明)
    ctx_ = nullptr;
}

void AxisGizmoFeature::onFrame(ViewportContext& ctx, const FrameTick& tick)
{
    (void)tick;
    if (!ctx_) return;
    auto* w = ctx.widget();
    if (!w) return;

    // 惰性注册: vsgImGui 就绪后挂一次每帧绘制回调 (attach 时往往尚未初始化)
    if (!registered_ && w->imGuiReady()) {
        registered_ = true;
        auto alive = imgui_alive_;
        w->addImGuiDraw([this, alive]() -> bool {
            if (!alive || !*alive) return false;
            draw();
            return true;
        });
    }

    // 每帧喂相机视图矩阵 (驱动轴向随视角旋转)
    auto* cam = ctx.camera();
    if (!cam) return;
    if (auto* la = dynamic_cast<vsg::LookAt*>(cam->viewMatrix.get())) {
        view_     = la->transform();
        has_view_ = true;
    }
}

void AxisGizmoFeature::draw()
{
    if (!ctx_ || !has_view_) return;

    auto* w = ctx_->widget();
    if (!w) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl) return;

    // 不用 io.DisplaySize (vsgImGui 构造时缓存 extent, 窗口缩放后过期),
    // 每帧从 Qt 部件取实时尺寸 × DPR → Vulkan 设备像素坐标系。
    const float dpr = float(w->devicePixelRatioF());
    const QSize  ws = w->size();
    const float  W  = float(ws.width()) * dpr;
    const float  H  = float(ws.height()) * dpr;
    if (W <= 0.0f || H <= 0.0f) return;

    const float  R = 46.0f * dpr;   // 圆盘半径
    const float  M = 18.0f * dpr;   // 右下边距
    const ImVec2 c(W - R - M, H - R - M);

    // ---- 圆盘背景 (深蓝灰面板 + 明亮描边, 半透明, 与视口协调) ----
    dl->AddCircleFilled(c, R, IM_COL32(30, 36, 48, 210), 48);
    dl->AddCircle(c, R, IM_COL32(132, 150, 180, 235), 48, 1.8f * dpr);
    dl->AddCircle(c, R * 0.72f, IM_COL32(60, 72, 94, 170), 40, 1.0f * dpr);

    // ---- 三条世界轴经当前相机朝向投影 (与旧 QWidget 版相同的数学) ----
    struct Axis { vsg::dvec3 dir; ImU32 col; const char* label; };
    static const Axis kAxes[3] = {
        { vsg::dvec3(1.0, 0.0, 0.0), IM_COL32(224, 90, 78, 255), "X" },
        { vsg::dvec3(0.0, 1.0, 0.0), IM_COL32(84, 196, 106, 255), "Y" },
        { vsg::dvec3(0.0, 0.0, 1.0), IM_COL32(74, 144, 226, 255), "Z" },
    };

    struct Proj { ImVec2 dir; float z; const Axis* a; };
    Proj proj[3];
    for (int i = 0; i < 3; ++i) {
        const vsg::dvec4 v = view_ * vsg::dvec4(kAxes[i].dir, 0.0);
        // view 空间 y 向上而屏幕 y 向下, 翻转 y; 归一化避免轴长不均
        const double len = std::sqrt(v.x * v.x + v.y * v.y);
        const double s   = len > 1e-9 ? (1.0 / len) : 0.0;
        proj[i] = { ImVec2(float(v.x * s), float(-v.y * s)), float(v.z), &kAxes[i] };
    }

    const float len = R * 0.72f;
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& pr : proj) {
            const bool front = pr.z < 0.0f;
            if ((pass == 0) == front) continue;   // pass0: 背面, pass1: 正面
            ImU32 col = pr.a->col;
            if (!front) {
                const ImVec4 cv = ImGui::ColorConvertU32ToFloat4(col);
                col = ImGui::GetColorU32(ImVec4(cv.x * 0.4f, cv.y * 0.4f,
                                                cv.z * 0.4f, 0.55f));
            }
            const ImVec2 end(c.x + pr.dir.x * len, c.y + pr.dir.y * len);
            dl->AddLine(c, end, col, front ? 2.6f : 1.4f);
            dl->AddCircleFilled(end, front ? 4.5f * dpr : 3.0f * dpr, col, 12);
            // 轴标签 (默认字体, 略伸出端点之外)
            const ImVec2 lp(c.x + pr.dir.x * (len + 11.0f * dpr),
                            c.y + pr.dir.y * (len + 11.0f * dpr));
            dl->AddText(ImVec2(lp.x - 5.0f * dpr, lp.y - 9.0f * dpr),
                        col, pr.a->label);
        }
    }

    // 中心毂
    dl->AddCircleFilled(c, 2.5f * dpr, IM_COL32(210, 218, 230, 255), 10);
}

// ---------------------------------------------------------------------------
// CameraGuardFeature — 相机最低高度约束 (视点不陷入地下)
// ---------------------------------------------------------------------------
void CameraGuardFeature::onAttach(ViewportContext& ctx) { (void)ctx; }
void CameraGuardFeature::onDetach(ViewportContext& ctx) { (void)ctx; }

void CameraGuardFeature::onFrame(ViewportContext& ctx, const FrameTick& tick)
{
    (void)tick;
    auto* cam = ctx.camera();
    if (!cam) return;
    auto* lookAt = dynamic_cast<vsg::LookAt*>(cam->viewMatrix.get());
    if (!lookAt) return;

    double ground_h = 0.0;
    if (auto* tq = ctx.terrainQuery(); tq && tq->hasTerrain())
        ground_h = tq->heightAtWorld(lookAt->eye.x, lookAt->eye.z);

    const double min_h = ground_h + min_height_above_ground_;
    if (lookAt->eye.y < min_h) lookAt->eye.y = min_h;
}

void CameraGuardFeature::configure(const nlohmann::json& params)
{
    if (params.contains("min_height_above_ground"))
        min_height_above_ground_ =
            params.value("min_height_above_ground", min_height_above_ground_);
}

// ---------------------------------------------------------------------------
// 静态注册
// ---------------------------------------------------------------------------
VSGVP_REGISTER_FEATURE(LightsFeature);
VSGVP_REGISTER_FEATURE(AxisGizmoFeature);
VSGVP_REGISTER_FEATURE(CameraGuardFeature);

}} // namespace vsgraysim::viewport3d
