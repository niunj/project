/// @file SimulationDisplayFacade.cpp
/// @brief 仿真显示门面实现 (见 SimulationDisplayFacade.h)。
#include "SimulationDisplayFacade.h"

#include "PlaybackFeature.h"
#include "ScenarioPreviewFeature.h"
#include "Viewport3DView.h"
#include "EngagementVisualFeature.h"
#include "TrailFeature.h"

#include <vsg/app/Camera.h>
#include <vsg/nodes/MatrixTransform.h>

#include <spdlog/spdlog.h>

#include <cmath>
#include <fstream>

namespace vsgraysim { namespace viewport3d {

SimulationDisplayFacade::SimulationDisplayFacade(Viewport3DView* view,
                                                 QObject* parent)
    : QObject(parent), view_(view)
{
    if (!view_) return;
    // FPS 转发。
    connect(view_, &Viewport3DView::fpsUpdated, this,
            [this](double v) { emit fps(v); });
    // 跟随: 每帧让相机随目标平移。
    connect(view_, &Viewport3DView::frameTick, this,
            &SimulationDisplayFacade::onFrameTick);
    // 就绪后: 冲刷排队装载 + 绑定回放结束回调。
    connect(view_, &Viewport3DView::vsgReady, this, [this] {
        auto pending = std::move(pending_);
        pending_.clear();
        for (auto& fn : pending) fn();
        if (auto* pb = playback())
            pb->setFinishedCallback([this] { emit playbackFinished(); });
    });
}

PlaybackFeature* SimulationDisplayFacade::playback() const
{
    return view_ ? view_->featureAs<PlaybackFeature>(PlaybackFeature::kId)
                 : nullptr;
}

// ---------------------------------------------------------------------------
// 场景装载
// ---------------------------------------------------------------------------
bool SimulationDisplayFacade::loadScenarioFile(const QString& path,
                                               const QString& workspace_root)
{
    if (!view_ || !view_->isReady()) {
        // 启动注入: 排队待场景就绪。
        pending_.push_back([this, path, workspace_root] {
            loadScenarioFile(path, workspace_root);
        });
        return true;
    }
    auto* preview =
        view_->featureAs<ScenarioPreviewFeature>(ScenarioPreviewFeature::kId);
    if (!preview) {
        spdlog::warn("SimulationDisplayFacade: scenario_preview feature missing");
        return false;
    }
    if (auto* pb = playback())
        pb->setFinishedCallback([this] { emit playbackFinished(); });
    return preview->loadScenario(path.toStdString(), workspace_root.toStdString());
}

// ---------------------------------------------------------------------------
// 回放控制
// ---------------------------------------------------------------------------
void SimulationDisplayFacade::play()
{
    if (auto* pb = playback()) pb->play();
}

void SimulationDisplayFacade::pause()
{
    if (auto* pb = playback()) pb->pause();
}

void SimulationDisplayFacade::step()
{
    if (auto* pb = playback()) pb->step();
}

void SimulationDisplayFacade::setSpeed(double speed)
{
    if (auto* pb = playback()) pb->setSpeed(speed);
}

// ---------------------------------------------------------------------------
// 后端位姿下发
// ---------------------------------------------------------------------------
void SimulationDisplayFacade::updateEntityPose(const QString& id, double x,
                                               double y, double z,
                                               double yaw_deg)
{
    if (auto* pb = playback())
        pb->updateEntityPose(id.toStdString(), vsg::dvec3(x, y, z), yaw_deg);
    emit entityUpdated(id);
}

// ---------------------------------------------------------------------------
// 相机聚焦 / 跟随
// ---------------------------------------------------------------------------
void SimulationDisplayFacade::updateEntityPose(const QString& id,const vsg::dvec3& position,
                                               const vsg::dquat& orientation)
{
    if (auto* pb=playback()) pb->updateEntityPose(id.toStdString(),position,orientation);
    emit entityUpdated(id);
}

bool SimulationDisplayFacade::entityWorldPos(const QString& id,
                                             vsg::dvec3& out) const
{
    if (!view_) return false;
    auto* preview =
        view_->featureAs<ScenarioPreviewFeature>(ScenarioPreviewFeature::kId);
    if (!preview) return false;
    vsg::MatrixTransform* mt = preview->entityTransform(id.toStdString());
    if (!mt) return false;
    const vsg::dvec4 o = mt->matrix * vsg::dvec4(0, 0, 0, 1);
    if (std::abs(o.w) < 1e-12) return false;
    out = vsg::dvec3(o.x / o.w, o.y / o.w, o.z / o.w);
    return true;
}

void SimulationDisplayFacade::focusOnEntity(const QString& id)
{
    if (!view_) return;
    vsg::dvec3 pos;
    if (!entityWorldPos(id, pos)) return;
    view_->focusViewOn(pos);
}

void SimulationDisplayFacade::setFollowEntity(const QString& id)
{
    follow_id_ = id;
    if (!id.isEmpty()) focusOnEntity(id);   // 开始跟随前先定位一次
}

// ---------------------------------------------------------------------------
// 交战过程视觉 / 运动轨迹拖尾
// ---------------------------------------------------------------------------
void SimulationDisplayFacade::setEngagements(
    const std::vector<EngagementVisualFeature::Engagement>& engs)
{
    if (auto* f = view_ ? view_->featureAs<EngagementVisualFeature>(
                              EngagementVisualFeature::kId)
                        : nullptr)
        f->setEngagements(engs);
}

void SimulationDisplayFacade::setPlumes(
    const std::vector<EngagementVisualFeature::Plume>& plumes)
{
    if (auto* f = view_ ? view_->featureAs<EngagementVisualFeature>(
                              EngagementVisualFeature::kId)
                        : nullptr)
        f->setPlumes(plumes);
}

void SimulationDisplayFacade::addLaunch(const vsg::vec3& pos, int faction)
{
    if (auto* f = view_ ? view_->featureAs<EngagementVisualFeature>(
                              EngagementVisualFeature::kId)
                        : nullptr)
        f->addLaunch(pos, faction);
}

void SimulationDisplayFacade::addImpact(const vsg::vec3& pos, int faction)
{
    if (auto* f = view_ ? view_->featureAs<EngagementVisualFeature>(
                              EngagementVisualFeature::kId)
                        : nullptr)
        f->addImpact(pos, faction);
}

void SimulationDisplayFacade::addMiss(const vsg::vec3& pos, int faction)
{
    if (auto* f = view_ ? view_->featureAs<EngagementVisualFeature>(
                              EngagementVisualFeature::kId)
                        : nullptr)
        f->addMiss(pos, faction);
}

void SimulationDisplayFacade::setDamageSmokes(
    const std::vector<EngagementVisualFeature::DamageSmoke>& smokes)
{
    if (auto* f = view_ ? view_->featureAs<EngagementVisualFeature>(
                              EngagementVisualFeature::kId)
                        : nullptr)
        f->setDamageSmokes(smokes);
}

void SimulationDisplayFacade::addWreck(const std::string& id,
                                       const vsg::vec3& pos, int faction)
{
    if (auto* f = view_ ? view_->featureAs<EngagementVisualFeature>(
                              EngagementVisualFeature::kId)
                        : nullptr)
        f->addWreck(id, pos, faction);
}

void SimulationDisplayFacade::clearEngagements()
{
    if (auto* f = view_ ? view_->featureAs<EngagementVisualFeature>(
                              EngagementVisualFeature::kId)
                        : nullptr)
        f->clear();
}

void SimulationDisplayFacade::addTrailPoint(const std::string& id,
                                            const vsg::dvec3& pos, int faction,
                                            double time, std::optional<vsg::dvec3> velocity)
{
    if (auto* f = view_ ? view_->featureAs<TrailFeature>(TrailFeature::kId)
                        : nullptr)
        f->addPoint(id, pos, faction, time, velocity);
}

void SimulationDisplayFacade::clearTrails()
{
    if (auto* f = view_ ? view_->featureAs<TrailFeature>(TrailFeature::kId)
                        : nullptr)
        f->clear();
}

void SimulationDisplayFacade::onFrameTick(const FrameTick&)
{
    if (follow_id_.isEmpty() || !view_) return;
    vsg::dvec3 pos;
    if (!entityWorldPos(follow_id_, pos)) return;
    auto* cam = view_->camera();
    auto* lookAt = cam ? dynamic_cast<vsg::LookAt*>(cam->viewMatrix.get())
                       : nullptr;
    if (!lookAt) return;
    // 保持相机相对目标的偏移, 随目标平移 (即跟随)。
    const vsg::dvec3 delta = pos - lookAt->center;
    if (vsg::length(delta) < 1e-9) return;
    lookAt->center = pos;
    lookAt->eye   += delta;
}

}} // namespace vsgraysim::viewport3d
