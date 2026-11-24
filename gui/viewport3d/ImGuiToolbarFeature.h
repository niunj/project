#pragma once
/// @file ImGuiToolbarFeature.h
/// @brief vsgimgui 工具条特性: 在 3D 视口内绘制 Dear ImGui 面板, 按软件定制
///        开关项 (平台标注/通信连线/参考网格/传感器视锥 等)。
///
/// 通过 configure(params) 声明工具项:
///   {"title":"场景设计","toggles":[
///      {"id":"relationship_visual","label":"标注/连线"},
///      {"id":"sensor_fov","label":"传感器视锥"}]}
/// 每项 checkbox 绑定同视口内其它特性的 setEnabled (运行时显隐切换)。
#ifndef VSGRAYSIM_VIEWPORT3D_IMGUITOOLBARFEATURE_H
#define VSGRAYSIM_VIEWPORT3D_IMGUITOOLBARFEATURE_H

#include "IViewportFeature.h"

#include <memory>
#include <string>
#include <vector>

namespace vsgraysim { namespace viewport3d {

class ViewportContext;

class ImGuiToolbarFeature : public IViewportFeature {
public:
    static constexpr char kId[] = "imgui_toolbar";

    struct Toggle { std::string id; std::string label; };

    const char* id() const override { return kId; }
    void onAttach(ViewportContext& ctx) override;
    void onDetach(ViewportContext& ctx) override;
    /// 声明工具项 (每个软件不同 id/标签)。
    void configure(const nlohmann::json& params) override;

private:
    void draw();                       ///< 每帧 ImGui 绘制
    ViewportContext* ctx_ = nullptr;
    std::string title_ = "显示控制";
    std::vector<Toggle> toggles_;
    bool open_ = true;                 ///< ImGui 窗口展开态 (本实例私有)
    bool placed_ = false;              ///< 首帧定位标志 (之后允许用户拖动)
    // 存活哨兵: vsgImGui::RenderImGui 只提供 add 无移除接口, 本对象析构后
    // 残留回调不得再触碰 this —— 由该共享标志守护 (onDetach 置 false)。
    std::shared_ptr<bool> imgui_alive_ = std::make_shared<bool>(true);
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_IMGUITOOLBARFEATURE_H