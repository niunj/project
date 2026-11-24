#pragma once
/// @file PlaybackFeature.h
/// @brief 回放特性: 实体沿航迹运动 (新增效果)。
///
/// 两种驱动方式:
///   1. 时间驱动 (play/pause/setSpeed): 按航路点时间线性插值位置,
///      朝向沿航段方向; 播完触发 finished 回调。
///   2. 外部驱动 (updateEntityPose): 仿真后端回放逐帧下发位姿。
#ifndef VSGRAYSIM_VIEWPORT3D_PLAYBACKFEATURE_H
#define VSGRAYSIM_VIEWPORT3D_PLAYBACKFEATURE_H

#include "IViewportFeature.h"
#include "SceneEntities.h"

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/quat.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace vsg { class MatrixTransform; }

namespace vsgraysim { namespace viewport3d {

class PlaybackFeature : public IViewportFeature {
public:
    static constexpr const char* kId = "playback";
    const char* id() const override { return kId; }
    void onDetach(ViewportContext& ctx) override;
    void onAttach(ViewportContext& ctx) override;
    void onFrame(ViewportContext& ctx, const FrameTick& tick) override;

    // ---- 实体注册 (由场景装载方调用) ----
    void registerEntity(const std::string& id,
                        vsg::ref_ptr<vsg::MatrixTransform> mt, double scale);
    void unregisterEntity(const std::string& id);
    void clearEntities();
    void setWaypoints(const std::string& id, const std::vector<Waypoint>& wps);

    // ---- 时间驱动回放 ----
    void play();
    void pause();
    bool playing() const { return playing_; }
    void setSpeed(double s) { speed_ = s > 0.0 ? s : 0.001; }
    double speed() const { return speed_; }
    void setTime(double t);
    double time() const { return time_; }
    /// 单步推进 (暂停态手动步进)。
    void step(double dt_sec = 1.0 / 30.0);

    // ---- 外部驱动 (后端回放位姿) ----
    void updateEntityPose(const std::string& id, const vsg::dvec3& pos,
                          double yaw_deg);
    void updateEntityPose(const std::string& id, const vsg::dvec3& pos,
                          const vsg::dquat& orientation);

    /// 全部航迹播放完毕回调。
    using FinishedCallback = std::function<void()>;
    void setFinishedCallback(FinishedCallback cb) { finished_cb_ = std::move(cb); }

private:
    struct Entity {
        vsg::ref_ptr<vsg::MatrixTransform> mt;
        double scale = 1.0;
        std::vector<Waypoint> wps;   ///< 按 time 升序
    };

    /// 按插值结果更新单个实体变换矩阵。
    static void applyPose(Entity& e, const vsg::dvec3& pos, double yaw_deg);
    /// 推进给定秒数: 插值全部实体 + 检查播完。
    void advanceOnce(double dt);
    /// 所有航迹的最晚结束时间 (无航迹返回 0)。
    double endTime() const;

    std::map<std::string, Entity> entities_;
    FinishedCallback finished_cb_;
    double time_ = 0.0;
    double speed_ = 1.0;
    bool   playing_ = false;
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_PLAYBACKFEATURE_H
