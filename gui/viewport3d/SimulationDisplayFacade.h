#pragma once
/// @file SimulationDisplayFacade.h
/// @brief 仿真显示门面 (L3 层): 仿真控制台的视口接口支持 + 数据回馈。
///
/// 前置: view 已 applyConfig(ViewportConfig::fromPreset("simulation_display"))。
/// 场景装载经 `scenario_preview` 特性构建 3D 预览 (实体+地形+视锥);
/// 回放运动由 `playback` 特性提供 (时间驱动或后端位姿下发)。
#ifndef VSGRAYSIM_VIEWPORT3D_SIMULATIONDISPLAYFACADE_H
#define VSGRAYSIM_VIEWPORT3D_SIMULATIONDISPLAYFACADE_H

#include <QObject>
#include <QString>

#include <vsg/maths/vec3.h>
#include <vsg/maths/quat.h>

#include "ViewportTypes.h"
#include "EngagementVisualFeature.h"
#include "TrailFeature.h"

#include <functional>
#include <string>
#include <vector>

namespace vsgraysim { namespace viewport3d {

class Viewport3DView;
class PlaybackFeature;

class SimulationDisplayFacade : public QObject {
    Q_OBJECT
public:
    explicit SimulationDisplayFacade(Viewport3DView* view,
                                     QObject* parent = nullptr);

    // ---- 场景装载 ----
    /// 装载场景文件并构建 3D 预览; vsgReady 前调用自动排队。
    /// `workspace_root` 用于 terrain_ref 解析。
    bool loadScenarioFile(const QString& path, const QString& workspace_root);

    // ---- 回放控制 (沿航迹运动) ----
    void play();
    void pause();
    void step();                 ///< 暂停态单步推进
    void setSpeed(double speed); ///< 播放倍速 (> 0)

    // ---- 后端位姿下发 (现有回放链路桥接) ----
    void updateEntityPose(const QString& id, double x, double y, double z,
                          double yaw_deg);
    void updateEntityPose(const QString& id, const vsg::dvec3& position,
                          const vsg::dquat& orientation);

    // ---- 相机聚焦 / 跟随 (双击场景树联动) ----
    /// 把相机视点定位到指定实体。
    void focusOnEntity(const QString& id);
    /// 设定要跟随的实体 id (空串则停止跟随); 跟随期间相机始终跟随目标运动。
    void setFollowEntity(const QString& id);
    QString followedEntityId() const { return follow_id_; }

    // ---- 交战过程视觉 (制导连线 + 弹道 + 发射/命中特效 + 尾焰 + 残骸) ----
    void setEngagements(const std::vector<EngagementVisualFeature::Engagement>& engs);
    /// 全量刷新在飞导弹尾焰 (位置 + 飞行方向)。
    void setPlumes(const std::vector<EngagementVisualFeature::Plume>& plumes);
    void addLaunch(const vsg::vec3& pos, int faction);
    void addImpact(const vsg::vec3& pos, int faction);
    void addMiss(const vsg::vec3& pos, int faction);
    /// 全量刷新受损平台冒烟。
    void setDamageSmokes(const std::vector<EngagementVisualFeature::DamageSmoke>& smokes);
    void addWreck(const std::string& id, const vsg::vec3& pos, int faction);
    void clearEngagements();

    // ---- 实体运动轨迹拖尾 ----
    void addTrailPoint(const std::string& id, const vsg::vec3& pos, int faction);
    void clearTrails();

signals:
    /// 实体位姿已更新 (后端下发或回放插值)。
    void entityUpdated(const QString& id);
    /// 全部航迹播放完毕。
    void playbackFinished();
    /// FPS 统计更新 (转发视口)。
    void fps(double fps);

private:
    PlaybackFeature* playback() const;
    /// 取某实体当前世界坐标 (scenario_preview 变换节点); 不存在返回 false。
    bool entityWorldPos(const QString& id, vsg::dvec3& out) const;
    /// 每帧跟随逻辑 (连接 view 的 frameTick)。
    void onFrameTick(const vsgraysim::viewport3d::FrameTick& tick);

    Viewport3DView* view_ = nullptr;
    QString follow_id_;
    std::vector<std::function<void()>> pending_;
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_SIMULATIONDISPLAYFACADE_H
