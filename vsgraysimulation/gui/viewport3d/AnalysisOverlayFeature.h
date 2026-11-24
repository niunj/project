#pragma once
/// @file AnalysisOverlayFeature.h
/// @brief 地形分析特性: 坡度/坡向/材质覆盖层 + 通视/剖面/距离两点分析。
#ifndef VSGRAYSIM_VIEWPORT3D_ANALYSISOVERLAYFEATURE_H
#define VSGRAYSIM_VIEWPORT3D_ANALYSISOVERLAYFEATURE_H

#include "IViewportFeature.h"

#include <vsg/core/Array.h>
#include <vsg/core/Array2D.h>
#include <vsg/core/ref_ptr.h>

#include <QColor>

#include <functional>
#include <vector>

namespace vsg { class Group; class Switch; class VertexIndexDraw; }

namespace vsgraysim { namespace viewport3d {

/// 地形分析:
///   - 覆盖层: 坡度/坡向/材质三着色模式, 独立网格贴地覆于地形之上
///     (挂在地形居中变换下, 共享局部坐标, 负深度偏移防 z-fight);
///   - 两点工具: 通视 (遮挡点标注) / 剖面 (采样序列回调) / 距离测量,
///     结果以 3D 线段呈现并经结果回调回传门面。
///
/// 参数 (configure, 均可选):
///   {"mode":int(0 正常 1 坡度 2 坡向 3 材质),
///    "pick_tool":int(0 无 1 通视 2 剖面 3 距离)}
class AnalysisOverlayFeature : public IViewportFeature {
public:
    static constexpr const char* kId = "analysis_overlay";
    const char* id() const override { return kId; }
    void onAttach(ViewportContext& ctx) override;
    void onDetach(ViewportContext& ctx) override;
    void onFrame(ViewportContext& ctx, const FrameTick& tick) override;
    void configure(const nlohmann::json& params) override;

    /// 分析结果回调: {"kind":"los"/"profile"/"distance", ...数据字段}。
    using ResultCallback = std::function<void(const nlohmann::json&)>;
    void setResultCallback(ResultCallback cb) { result_cb_ = std::move(cb); }

    /// 剖面采样序列回调 (距离/高程序列 + 水面高度, 供剖面曲线部件绘制)。
    using ProfileCallback =
        std::function<void(const std::vector<double>& dists,
                           const std::vector<double>& heights, double water)>;
    void setProfileCallback(ProfileCallback cb) { profile_cb_ = std::move(cb); }

    /// 两点拾取工具是否激活 (笔刷光标据此让位)。
    bool pickToolActive() const { return pick_tool_ != 0; }

private:
    /// 按当前网格分辨率 (重)建覆盖层网格。
    void ensureOverlay(ViewportContext& ctx);
    /// 按当前模式刷新覆盖层纹理与贴地顶点。
    void refreshOverlay(ViewportContext& ctx);
    /// 宿主拾取射线回调: 两点工具状态机。
    void onPickRay(const vsg::dvec3& origin, const vsg::dvec3& dir);
    void finishLos(double x1, double z1, double h1);
    void finishProfile(double x1, double z1, double h1);
    void finishDistance(double x1, double z1, double h1);
    /// 世界空间分析线段 (通视/剖面/距离)。
    void drawSegment(ViewportContext& ctx, const vsg::dvec3& a,
                     const vsg::dvec3& b, const QColor& color);
    void clearSegments();

    int  mode_ = 0;          ///< 0 正常 1 坡度 2 坡向 3 材质
    int  pick_tool_ = 0;     ///< 0 无 1 通视 2 剖面 3 距离
    bool overlay_dirty_ = true;

    // ---- 覆盖层网格 (挂在地形居中变换下) ----
    vsg::ref_ptr<vsg::Switch>            overlay_sw_;
    vsg::ref_ptr<vsg::VertexIndexDraw>   vid_;
    vsg::ref_ptr<vsg::vec3Array>         pos_;
    vsg::ref_ptr<vsg::vec3Array>         nrm_;
    vsg::ref_ptr<vsg::ubvec4Array2D>     tex_;
    uint64_t mesh_gen_seen_ = 0;   ///< 覆盖层网格对应的地形代数
    uint64_t content_gen_seen_ = 0;///< 内容刷新已见的地形代数
    uint64_t content_ver_seen_ = 0;///< 内容刷新已见的高度版本

    // ---- 两点分析线段 ----
    vsg::ref_ptr<vsg::Group> segments_;
    bool   start_set_ = false;
    double x0_ = 0.0, z0_ = 0.0, h0_ = 0.0;   ///< 起点 (地形局部)

    ResultCallback  result_cb_;
    ProfileCallback profile_cb_;
    ViewportContext* ctx_ = nullptr;
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_ANALYSISOVERLAYFEATURE_H
