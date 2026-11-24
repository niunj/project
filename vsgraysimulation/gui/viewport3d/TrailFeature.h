#pragma once
/// @file TrailFeature.h
/// @brief 运动轨迹拖尾特性: 实体实时运动的时间拖尾折线。
///
/// 与 TrajectoryFeature (场景设计器/仿真的静态航路规划折线) 不同, 本特性
/// 逐帧累积平台的历史采样点形成"拖尾", 便于观察追迹/机动过程。仿真门面
/// 按固定周期调用 addPoint 喂位置, 内部保留最近若干采样点并整体重建线段。
#ifndef VSGRAYSIM_VIEWPORT3D_TRAILFEATURE_H
#define VSGRAYSIM_VIEWPORT3D_TRAILFEATURE_H

#include "IViewportFeature.h"
#include "ViewportKit.h"

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>

#include <deque>
#include <map>
#include <string>
#include <vector>

namespace vsg { class Group; }

namespace vsgraysim { namespace viewport3d {

class TrailFeature : public IViewportFeature {
public:
    static constexpr const char* kId = "trail";
    const char* id() const override { return kId; }
    void onAttach(ViewportContext& ctx) override;
    void onDetach(ViewportContext& ctx) override;
    /// 整体显隐开关 (从世界场景根增删本特性节点)。
    void setEnabled(bool on) override;
    /// 每帧驱动: 检查脏标记重建 + 累积时间。
    void onFrame(ViewportContext& ctx, const FrameTick& tick) override;

    /// 追加一个轨迹采样点 (faction: 0=中立,1=蓝,2=红; 与关系图一致)。
    /// 与末尾点几乎重合的目标点被忽略, 避免拖尾原地打结。
    void addPoint(const std::string& id, const vsg::vec3& pos, int faction);
    void removeTrack(const std::string& id);
    void clear();
    /// 单条轨迹最大采样点数 (超长自动丢弃最旧)。
    void setMaxPoints(int n) { max_pts_ = n < 2 ? 2 : n; }

private:
    struct Track {
        int faction = 0;
        std::deque<vsg::vec3> pts;
    };
    void rebuild();
    void setInWorld(bool add);

    ViewportContext* ctx_ = nullptr;
    vsg::ref_ptr<vsg::Group> root_;
    std::map<std::string, Track> tracks_;
    int max_pts_ = 400;
    bool in_world_ = false;
    bool dirty_ = false;        ///< addPoint 标记脏, onFrame 批量重建
    double elapsed_sec_ = 0.0;  ///< 全局时间 (onFrame 累积)
    /// 自定义 shader 条带节点的逐帧相机更新句柄。
    std::vector<gui::ViewportKit::ShaderHandle> shader_handles_;
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_TRAILFEATURE_H