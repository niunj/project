/// @file PlaybackFeature.cpp
/// @brief 回放特性实现 (见 PlaybackFeature.h)。
#include "PlaybackFeature.h"
#include <simcore/Orientation.h>

#include "FeatureRegistry.h"
#include "ViewportContext.h"
#include "ViewportKit.h"

#include <vsg/maths/transform.h>
#include <vsg/nodes/MatrixTransform.h>

#include <algorithm>
#include <cmath>

namespace vsgraysim { namespace viewport3d {

void PlaybackFeature::onAttach(ViewportContext& ctx) { (void)ctx; }
void PlaybackFeature::onDetach(ViewportContext& ctx) { (void)ctx; }

// ---------------------------------------------------------------------------
// 实体注册
// ---------------------------------------------------------------------------
void PlaybackFeature::registerEntity(const std::string& id,
                                     vsg::ref_ptr<vsg::MatrixTransform> mt,
                                     double scale)
{
    Entity& e = entities_[id];
    e.mt = std::move(mt);
    e.scale = scale;
}

void PlaybackFeature::unregisterEntity(const std::string& id)
{
    entities_.erase(id);
}

void PlaybackFeature::clearEntities()
{
    entities_.clear();
    time_ = 0.0;
    playing_ = false;
}

void PlaybackFeature::setWaypoints(const std::string& id,
                                   const std::vector<Waypoint>& wps)
{
    auto it = entities_.find(id);
    if (it == entities_.end()) return;
    it->second.wps = wps;
    std::sort(it->second.wps.begin(), it->second.wps.end(),
              [](const Waypoint& a, const Waypoint& b) { return a.time < b.time; });
}

// ---------------------------------------------------------------------------
// 时间驱动回放
// ---------------------------------------------------------------------------
void PlaybackFeature::play()
{
    if (endTime() <= 0.0) return;   // 无航迹可播
    if (time_ >= endTime()) time_ = 0.0;   // 播完重播
    playing_ = true;
}

void PlaybackFeature::pause() { playing_ = false; }

void PlaybackFeature::setTime(double t)
{
    time_ = std::max(0.0, t);
}

void PlaybackFeature::step(double dt_sec)
{
    advanceOnce(dt_sec);
}

void PlaybackFeature::onFrame(ViewportContext& ctx, const FrameTick& tick)
{
    (void)ctx;
    if (!playing_) return;
    advanceOnce(tick.dt_sec * speed_);
}

// ---------------------------------------------------------------------------
// 外部驱动 (后端回放位姿)
// ---------------------------------------------------------------------------
void PlaybackFeature::updateEntityPose(const std::string& id,
                                       const vsg::dvec3& pos, double yaw_deg)
{
    auto it = entities_.find(id);
    if (it == entities_.end()) return;
    applyPose(it->second, pos, yaw_deg);
}
void PlaybackFeature::updateEntityPose(const std::string& id,const vsg::dvec3& pos,const vsg::dquat& orientation)
{
    auto it=entities_.find(id);
    if (it==entities_.end() || !it->second.mt) return;
    const double scale=it->second.scale;
    it->second.mt->matrix=vsg::translate(pos)*vsg::rotate(simcore::normalized(orientation))*vsg::scale(scale,scale,scale);
}

// ---------------------------------------------------------------------------
// 内部
// ---------------------------------------------------------------------------
double PlaybackFeature::endTime() const
{
    double end = 0.0;
    for (const auto& kv : entities_)
        if (!kv.second.wps.empty())
            end = std::max(end, kv.second.wps.back().time);
    return end;
}

void PlaybackFeature::applyPose(Entity& e, const vsg::dvec3& pos, double yaw_deg)
{
    if (!e.mt) return;
    e.mt->matrix = gui::ViewportKit::makeEntityTransform(pos, yaw_deg, e.scale);
}

void PlaybackFeature::advanceOnce(double dt)
{
    time_ += dt;
    const double end = endTime();

    for (auto& kv : entities_) {
        Entity& e = kv.second;
        const auto& wps = e.wps;
        if (wps.size() < 2 || !e.mt) continue;

        if (time_ <= wps.front().time) {
            // 未出发: 停在起点, 朝向取首段方向。
            const double yaw = std::atan2(wps[1].pos[0] - wps[0].pos[0],
                                          wps[1].pos[2] - wps[0].pos[2]);
            applyPose(e, vsg::dvec3(wps[0].pos[0], wps[0].pos[1], wps[0].pos[2]),
                      vsg::degrees(yaw));
            continue;
        }
        // 定位当前航段 [i-1, i] 并线性插值。
        size_t i = wps.size() - 1;
        for (size_t k = 1; k < wps.size(); ++k) {
            if (time_ <= wps[k].time) { i = k; break; }
        }
        const auto& a = wps[i - 1];
        const auto& b = wps[i];
        const double seg_dt = std::max(1e-6, b.time - a.time);
        const double u = std::min(1.0, std::max(0.0, (time_ - a.time) / seg_dt));
        const vsg::dvec3 pos(a.pos[0] + (b.pos[0] - a.pos[0]) * u,
                             a.pos[1] + (b.pos[1] - a.pos[1]) * u,
                             a.pos[2] + (b.pos[2] - a.pos[2]) * u);
        const double yaw = std::atan2(b.pos[0] - a.pos[0], a.pos[2] - b.pos[2]);
        applyPose(e, pos, vsg::degrees(yaw));
    }

    if (end > 0.0 && time_ >= end) {
        time_ = end;
        playing_ = false;
        if (finished_cb_) finished_cb_();
    }
}

VSGVP_REGISTER_FEATURE(PlaybackFeature);

}} // namespace vsgraysim::viewport3d
