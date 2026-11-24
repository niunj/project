#pragma once
/// @file CommonFeatures.h
/// @brief 四个基础视口特性: 默认灯光 / 兜底地面 / 轴向指示 / 相机限高。
#ifndef VSGRAYSIM_VIEWPORT3D_COMMONFEATURES_H
#define VSGRAYSIM_VIEWPORT3D_COMMONFEATURES_H

#include "IViewportFeature.h"

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/mat4.h>

#include <memory>

namespace vsg { class Group; class Node; }

namespace vsgraysim { namespace viewport3d {

/// 默认灯光组 (环境光 0.35 + 太阳平行光 1.2)。
/// 参数: {"sun_direction":[x,y,z], "ambient_intensity":f, "sun_intensity":f}
/// 注意: rocky_sky 挂载后由物理太阳/环境光独占场景灯光, 本组自动抑制
/// (避免双太阳/双环境光叠加导致夜不黑、昼过曝)。
class LightsFeature : public IViewportFeature {
public:
    static constexpr const char* kId = "lights";
    const char* id() const override { return kId; }
    void onAttach(ViewportContext& ctx) override;
    void onDetach(ViewportContext& ctx) override;
    void configure(const nlohmann::json& params) override;

    /// 天空模块接管/归还场景光照: true 时从场景摘除默认灯, false 时恢复。
    void surrenderToSky(bool surrender);
private:
    /// 将 params_ 中的光照参数应用到已建灯光节点。
    void applyParams();
    vsg::ref_ptr<vsg::Group> lights_;
    nlohmann::json params_;
    ViewportContext* ctx_ = nullptr;
    bool surrendered_ = false;   ///< 灯光已让渡给 rocky_sky (不挂场景)
};

/// 右下角 Blender 式 XYZ 轴向指示。
/// 经 vsgImGui 前景绘制层渲染 (Vulkan render pass 内, 所有部署形态下可靠可见;
/// QWidget overlay 在 WA_PaintOnScreen 的 Vulkan 原生表面上会被 present 旁路)。
class AxisGizmoFeature : public IViewportFeature {
public:
    static constexpr const char* kId = "axis_gizmo";
    const char* id() const override { return kId; }
    void onAttach(ViewportContext& ctx) override;
    void onDetach(ViewportContext& ctx) override;
    void onFrame(ViewportContext& ctx, const FrameTick& tick) override;
private:
    void draw();                       ///< 每帧 ImGui 前景层绘制
    ViewportContext* ctx_ = nullptr;
    vsg::dmat4 view_ = vsg::dmat4(1.0);
    bool has_view_   = false;
    bool registered_ = false;          ///< ImGui 回调惰性注册标志 (等待 imGuiReady)
    // 存活哨兵: vsgImGui::RenderImGui 只提供 add 无移除接口, 本对象析构后
    // 残留回调不得再触碰 this —— 由该共享标志守护 (onDetach 置 false)。
    std::shared_ptr<bool> imgui_alive_ = std::make_shared<bool>(true);
};

/// 相机最低高度约束 (视点不陷入地下)。
/// 参数: {"min_height_above_ground":2.0}; 有地形时按地形高度, 无地形按 y=0。
class CameraGuardFeature : public IViewportFeature {
public:
    static constexpr const char* kId = "camera_guard";
    const char* id() const override { return kId; }
    void onAttach(ViewportContext& ctx) override;
    void onDetach(ViewportContext& ctx) override;
    void onFrame(ViewportContext& ctx, const FrameTick& tick) override;
    void configure(const nlohmann::json& params) override;
private:
    double min_height_above_ground_ = 2.0;
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_COMMONFEATURES_H
