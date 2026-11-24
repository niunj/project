#pragma once
/// @file BrushEditFeature.h
/// @brief 地形笔刷编辑特性: 8 种笔刷模式 + 拖拽雕刻 + 3D 光标环。
#ifndef VSGRAYSIM_VIEWPORT3D_BRUSHEDITFEATURE_H
#define VSGRAYSIM_VIEWPORT3D_BRUSHEDITFEATURE_H

#include "IViewportFeature.h"

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>

#include <QMetaObject>

#include <functional>

namespace vsg { class MatrixTransform; }

namespace vsgraysim { namespace viewport3d {

/// 笔刷编辑: 左键拖拽经宿主射线回调雕刻地形 (世界射线 → 地形拾取 →
/// applyBrush), 同时在鼠标下方显示随半径缩放的光标环。
/// 撤销快照由宿主在笔画开始回调中自行压栈 (见 setStrokeStartedCallback)。
///
/// 参数 (configure, 均可选):
///   {"mode":int(BrushMode 0..7), "radius":25.0,
///    "strength_pct":25.0, "flatten_target":8.0}
class BrushEditFeature : public IViewportFeature {
public:
    static constexpr const char* kId = "brush_edit";
    const char* id() const override { return kId; }
    void onAttach(ViewportContext& ctx) override;
    void onDetach(ViewportContext& ctx) override;
    void onFrame(ViewportContext& ctx, const FrameTick& tick) override;
    void configure(const nlohmann::json& params) override;

    /// 笔画开始 (左键按下拖拽) 回调 → 门面在此压撤销快照。
    using StrokeCallback = std::function<void()>;
    void setStrokeStartedCallback(StrokeCallback cb)
    {
        stroke_started_cb_ = std::move(cb);
    }

    /// 笔刷是否处于激活模式 (mode != None)。
    bool active() const { return mode_ != 0; }

private:
    /// 宿主编辑射线回调: 世界射线 → 地形局部拾取 → applyBrush。
    void applyRay(const vsg::dvec3& origin, const vsg::dvec3& dir);
    /// 构建单位半径光标环 (按笔刷半径缩放)。
    void buildCursor();
    /// 每帧: 光标环跟随鼠标下的地形表面。
    void updateCursor();

    int    mode_ = 0;            ///< BrushMode (0 = None)
    double radius_ = 25.0;       ///< 笔刷半径 (m)
    double strength_pct_ = 25.0; ///< 笔刷强度 (百分比 1..100)
    double flatten_target_ = 8.0;///< Flatten 笔刷台地高度 (m)

    vsg::ref_ptr<vsg::MatrixTransform> cursor_mt_;
    double  cursor_radius_shown_ = -1.0;
    StrokeCallback stroke_started_cb_;
    QMetaObject::Connection stroke_conn_;
    ViewportContext* ctx_ = nullptr;   ///< onAttach 注入, 供宿主回调使用
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_BRUSHEDITFEATURE_H
