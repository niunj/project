#pragma once
/// @file IViewportFeature.h
/// @brief 视口特性插件接口 (L1 层)。
///
/// 每个可插拔的 3D 视口功能 (灯光/网格/地形/笔刷/视锥...) 实现一个
/// IViewportFeature, 通过 FeatureRegistry 静态注册后由 Viewport3DView
/// 按 ViewportConfig 组装。新增功能 = 新文件 + VSGVP_REGISTER_FEATURE 一行。
#ifndef VSGRAYSIM_VIEWPORT3D_IVIEWPORTFEATURE_H
#define VSGRAYSIM_VIEWPORT3D_IVIEWPORTFEATURE_H

#include "ViewportTypes.h"

#include <nlohmann/json.hpp>

namespace vsgraysim { namespace viewport3d {

class ViewportContext;

class IViewportFeature {
public:
    virtual ~IViewportFeature() = default;

    /// 特性唯一标识 (如 "lights"/"reference_grid"), 与注册表键一致。
    virtual const char* id() const = 0;

    /// VSG 场景就绪后挂载: 建场景节点 / 注册回调 / 注册查询接口。
    virtual void onAttach(ViewportContext& ctx) = 0;
    /// 卸载: 摘除节点 / 断开回调 (对象销毁前调用)。
    virtual void onDetach(ViewportContext& ctx) = 0;

    /// 每帧驱动 (帧循环在渲染前按注册顺序分发)。
    virtual void onFrame(ViewportContext& ctx, const FrameTick& tick) {
        (void)ctx; (void)tick;
    }
    /// 鼠标世界射线事件分发 (编辑/拾取模式)。
    virtual void onMouseRay(ViewportContext& ctx, MouseRayEvent& ev) {
        (void)ctx; (void)ev;
    }

    /// 运行时配置 (来自 ViewportConfig 条目或 configureFeature 调用)。
    virtual void configure(const nlohmann::json& params) { (void)params; }
    /// 导出当前状态 (用于配置持久化)。
    virtual nlohmann::json saveState() const { return nlohmann::json::object(); }

    /// 启停: 基类仅维护标志位, 需要隐藏场景节点的特性自行重写。
    virtual void setEnabled(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }
    bool attached() const { return attached_; }

protected:
    friend class Viewport3DView;
    bool enabled_  = true;
    bool attached_ = false;
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_IVIEWPORTFEATURE_H
