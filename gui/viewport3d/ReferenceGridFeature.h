#pragma once
/// @file ReferenceGridFeature.h
/// @brief 无限参考网格 + 原点坐标轴特性 (Blender 式, 可贴地重铺)。
#ifndef VSGRAYSIM_VIEWPORT3D_REFERENCEGRIDFEATURE_H
#define VSGRAYSIM_VIEWPORT3D_REFERENCEGRIDFEATURE_H

#include "IViewportFeature.h"

#include <vsg/core/Array.h>
#include <vsg/core/ref_ptr.h>

#include <QColor>

namespace vsg { class Group; class MatrixTransform; class StateGroup; }

namespace vsgraysim { namespace viewport3d {

/// 无限参考网格 + 原点 XYZ 轴:
///   - 固定大小网格分块 (64 格/边) 挂于可移动变换下, 每帧吸附到相机下方
///     的网格单元 → 视觉上无限延伸;
///   - 网格线为细分折线, 按地形高度场逐顶点贴地重铺 (DYNAMIC 数组原地改写);
///   - 原点三轴 (与视口角落 gizmo 同色)。
///
/// 参数 (configure, 均可选):
///   {"show":true, "major_step":100.0, "minor_step":10.0,
///    "major_color":"#73809e", "minor_color":"#40485a"}
class ReferenceGridFeature : public IViewportFeature {
public:
    static constexpr const char* kId = "reference_grid";
    const char* id() const override { return kId; }
    void onAttach(ViewportContext& ctx) override;
    void onDetach(ViewportContext& ctx) override;
    void onFrame(ViewportContext& ctx, const FrameTick& tick) override;
    void configure(const nlohmann::json& params) override;
    nlohmann::json saveState() const override;

private:
    /// 按步长/颜色重建网格线几何与原点轴 (容器保持挂载, 内容原地替换)。
    void rebuild(ViewportContext& ctx);
    /// 采样地形高度重铺网格顶点 (顶点数组原地改写 + dirty)。
    void drape(ViewportContext& ctx);

    vsg::ref_ptr<vsg::Group>           root_;       ///< 网格+轴容器
    vsg::ref_ptr<vsg::MatrixTransform> tile_mt_;    ///< 跟随相机 (无限网格)
    vsg::ref_ptr<vsg::StateGroup>      minor_sg_;   ///< 细分网格线容器
    vsg::ref_ptr<vsg::StateGroup>      major_sg_;   ///< 粗分网格线容器
    vsg::ref_ptr<vsg::vec3Array>       minor_verts_;///< 细分线贴地顶点
    vsg::ref_ptr<vsg::vec3Array>       major_verts_;///< 粗分线贴地顶点

    double major_step_ = 100.0;                  ///< 粗分线间距 (m)
    double minor_step_ = 10.0;                   ///< 细分线间距 (m)
    QColor major_color_ = QColor(115, 128, 158); ///< 粗分线颜色
    QColor minor_color_ = QColor(64, 72, 90);    ///< 细分线颜色
    bool   show_ = true;

    double   tile_cx_ = 0.0;          ///< 当前分块原点 (世界)
    double   tile_cz_ = 0.0;
    bool     drape_dirty_ = true;     ///< 需重新贴地
    bool     rebuild_pending_ = false;///< configure 改参后待重建 (帧中执行)
    uint64_t terrain_gen_seen_ = 0;   ///< 已贴地对应的地形网格代数
    uint64_t heights_ver_seen_ = 0;   ///< 已贴地对应的高度场版本
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_REFERENCEGRIDFEATURE_H
