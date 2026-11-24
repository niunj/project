/// @file DetectionEffectFeature.cpp
/// @brief 见 DetectionEffectFeature.h。
#include "DetectionEffectFeature.h"

#include "FeatureRegistry.h"
#include "ViewportContext.h"
#include "ViewportKit.h"
#include "OcclusionQueryService.h"

#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/StateGroup.h>

#include <algorithm>
#include <cmath>

namespace vsgraysim { namespace viewport3d {

namespace {
constexpr int   kCircleSegs = 96;    ///< 地面范围圈分段
constexpr int   kArcSegs    = 48;    ///< 搜索扇扫弧分段
constexpr float kRingDz     = 0.05f; ///< 探测圈贴地高度偏移
const gui::ViewportKit::LineStyle kDetStyle{2.0f, true};
const gui::ViewportKit::LineStyle kBeamStyle{1.4f, false};

/// 探测体积半球网格分段
constexpr int kHemAzSegs = 36;
constexpr int kHemElSegs = 12;

/// 特效条目是否完全一致 (500ms 快照重复喂入时免重建+免编译)。
bool sameEffect(const DetectionEffectFeature::Effect& a,
                const DetectionEffectFeature::Effect& b)
{
    return a.entity_id == b.entity_id && a.sensor_idx == b.sensor_idx &&
           a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.pos.z == b.pos.z &&
           a.yaw == b.yaw && a.az == b.az && a.el == b.el &&
           a.fov_h == b.fov_h && a.fov_v == b.fov_v && a.range == b.range &&
           a.color == b.color && a.show_sector == b.show_sector &&
           a.volume_shape == b.volume_shape && a.rt_occlusion == b.rt_occlusion &&
           a.lock_state == b.lock_state && a.track_az == b.track_az;
}
bool sameEffects(const std::vector<DetectionEffectFeature::Effect>& a,
                 const std::vector<DetectionEffectFeature::Effect>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!sameEffect(a[i], b[i])) return false;
    return true;
}
} // namespace

void DetectionEffectFeature::onAttach(ViewportContext& ctx)
{
    ctx_ = &ctx;
    root_ = vsg::Group::create();
    if (enabled_) setInWorld(true);
}

void DetectionEffectFeature::onDetach(ViewportContext& ctx)
{
    setInWorld(false);
    ctx_ = nullptr;
    root_ = {};
}

void DetectionEffectFeature::setEnabled(bool on)
{
    enabled_ = on;
    if (ctx_) { if (on) setInWorld(true); else setInWorld(false); }
}

void DetectionEffectFeature::setInWorld(bool add)
{
    if (!root_ || !ctx_) return;
    const bool now = in_world_;
    if (add && !now) {
        ctx_->contentRoot()->addChild(root_);
        in_world_ = true;
    } else if (!add && now) {
        ctx_->removeFromWorld(root_);
        in_world_ = false;
    }
    ctx_->requestCompile();
}

void DetectionEffectFeature::setEffects(const std::vector<Effect>& fx)
{
    if (sameEffects(fx_, fx)) return;   // 快照无变化: 跳过重建与编译
    fx_ = fx;
    rebuild();
}

void DetectionEffectFeature::clear()
{
    if (fx_.empty()) return;
    fx_.clear();
    rebuild();
}

void DetectionEffectFeature::rebuild()
{
    if (!root_) return;
    root_->children.clear();
    shader_handles_.clear();

    for (size_t ei = 0; ei < fx_.size(); ++ei) {
        auto& e = fx_[ei];
        // 锁定状态梯度: 搜索(本色) → 跟踪(提亮) → 锁定(橙红高亮 +
        // 波束指向目标 + 扫描停转)。
        const bool locked = (e.lock_state == Effect::LockState::Lock);
        QColor col = e.color;
        if (locked) col = QColor(0xff, 0x70, 0x43);
        else if (e.lock_state == Effect::LockState::Track)
            col = e.color.lighter(125);
        // 锁定时搜索方位中心强制指向目标。
        const double azW = locked ? e.track_az : (e.yaw + e.az);
        const vsg::vec3 c(float(e.pos.x), float(e.pos.y + kRingDz),
                          float(e.pos.z));

        // 0) 三维探测体积 (半球或扇形半球, 带遮挡雕刻)
        if (e.range > 0.0) {
            std::vector<float> occlusion;
            const float* occ_ptr = nullptr;

            if (e.rt_occlusion && occlusion_svc_ &&
                occlusion_svc_->hasTerrainFallback()) {
                // 执行遮挡查询
                OcclusionQuery q;
                q.origin = e.pos;
                q.max_dist = e.range;
                q.az_count = kHemAzSegs;
                q.el_count = kHemElSegs;

                // 根据体积形状确定角度范围
                if (e.volume_shape == Effect::VolumeShape::FullSphere) {
                    q.az_min_deg = 0.0;
                    q.az_max_deg = 360.0;
                    q.el_min_deg = -90.0;
                    q.el_max_deg = 90.0;
                } else if (e.volume_shape == Effect::VolumeShape::Hemisphere) {
                    q.az_min_deg = 0.0;
                    q.az_max_deg = 360.0;
                    q.el_min_deg = 0.0;
                    q.el_max_deg = 90.0;
                } else {  // SectorHemisphere
                    q.az_min_deg = azW - 0.5 * e.fov_h;
                    q.az_max_deg = azW + 0.5 * e.fov_h;
                    q.el_min_deg = 0.0;
                    q.el_max_deg = 90.0;
                }

                auto result = occlusion_svc_->query(q);
                occlusion = std::move(result.hit_distances);
                occ_ptr = occlusion.data();
            }

            // 构建半球体积网格
            double az_min, az_max, el_min, el_max;
            if (e.volume_shape == Effect::VolumeShape::FullSphere) {
                az_min = 0.0; az_max = 360.0;
                el_min = -90.0; el_max = 90.0;
            } else if (e.volume_shape == Effect::VolumeShape::Hemisphere) {
                az_min = 0.0; az_max = 360.0;
                el_min = 0.0; el_max = 90.0;
            } else {  // SectorHemisphere
                az_min = azW - 0.5 * e.fov_h;
                az_max = azW + 0.5 * e.fov_h;
                el_min = 0.0; el_max = 90.0;
            }

            gui::ViewportKit::ShaderHandle sh;
            if (auto hem = gui::ViewportKit::makeHemisphere(
                    e.pos, e.range, az_min, az_max, el_min, el_max,
                    kHemAzSegs, kHemElSegs, col,
                    occ_ptr ? &occlusion : nullptr, 0.22f, 0.02f, &sh)) {
                root_->addChild(hem);
                if (sh.valid())
                    shader_handles_.emplace_back(static_cast<int>(ei), sh);
            }
        }

        // 1) 地面辅助参考: 探测范围圈 + 刻度线
        {
            std::vector<vsg::vec3> pts;
            auto pushCircle = [&](double radius) {
                for (int i = 0; i < kCircleSegs; ++i) {
                    const double a0 = 2.0 * M_PI * double(i) / double(kCircleSegs);
                    const double a1 = 2.0 * M_PI * double(i + 1) / double(kCircleSegs);
                    pts.emplace_back(float(e.pos.x + radius * std::cos(a0)),
                                     float(e.pos.y + kRingDz),
                                     float(e.pos.z + radius * std::sin(a0)));
                    pts.emplace_back(float(e.pos.x + radius * std::cos(a1)),
                                     float(e.pos.y + kRingDz),
                                     float(e.pos.z + radius * std::sin(a1)));
                }
            };
            pushCircle(e.range);

            // 距离刻度
            {
                const double tick_len = std::clamp(e.range * 0.02, 2.0, 15.0);
                const double az = vsg::radians(azW);
                const double px = std::sin(az), pz = -std::cos(az);
                for (const double f : {0.25, 0.5, 0.75}) {
                    const double r = e.range * f;
                    pts.emplace_back(float(e.pos.x + r * px - tick_len * pz),
                                     float(e.pos.y + kRingDz),
                                     float(e.pos.z + r * pz + tick_len * px));
                    pts.emplace_back(float(e.pos.x + r * px + tick_len * pz),
                                     float(e.pos.y + kRingDz),
                                     float(e.pos.z + r * pz - tick_len * px));
                }
            }
            if (auto n = gui::ViewportKit::makeLineSegments(pts, col, kDetStyle))
                root_->addChild(n);
            // 锁定高亮环: 内圈强调 (锁定态独有, 与范围圈区分)。
            if (locked) {
                std::vector<vsg::vec3> lp;
                const double lr = e.range * 0.985;
                for (int i = 0; i < kCircleSegs; ++i) {
                    const double a0 = 2.0 * M_PI * double(i) / double(kCircleSegs);
                    const double a1 = 2.0 * M_PI * double(i + 1) / double(kCircleSegs);
                    lp.emplace_back(float(e.pos.x + lr * std::cos(a0)),
                                    float(e.pos.y + kRingDz * 2.0f),
                                    float(e.pos.z + lr * std::sin(a0)));
                    lp.emplace_back(float(e.pos.x + lr * std::cos(a1)),
                                    float(e.pos.y + kRingDz * 2.0f),
                                    float(e.pos.z + lr * std::sin(a1)));
                }
                if (auto ln = gui::ViewportKit::makeLineSegments(
                        lp, col.lighter(130), kBeamStyle))
                    root_->addChild(ln);
            }
        }

        // 2) 扫描波束 (视锥四棱台, 通过 MatrixTransform 实现旋转动画)
        if (e.fov_h > 0.0 && e.fov_v > 0.0 && e.range > 0.0) {
            const double azr = vsg::radians(azW), elr = vsg::radians(e.el);
            const vsg::dvec3 fwd(std::sin(azr) * std::cos(elr), std::sin(elr),
                                 -std::cos(azr) * std::cos(elr));
            const vsg::dvec3 right(std::cos(azr), 0.0, std::sin(azr));
            const vsg::dvec3 up = vsg::cross(right, fwd);
            const vsg::dvec3 apex(e.pos.x, e.pos.y, e.pos.z);
            if (auto walls = gui::ViewportKit::makeFrustumWalls(
                    apex, fwd, right, up, e.range, e.fov_h, e.fov_v,
                    col, locked ? 0.30f : 0.20f, 0.03f))
                root_->addChild(walls);
            if (auto wire = gui::ViewportKit::makeLineSegments(
                    gui::ViewportKit::frustumSegments(apex, fwd, right, up,
                                                      e.fov_h, e.fov_v, e.range),
                    col, kBeamStyle))
                root_->addChild(wire);
        }
    }

    if (ctx_) ctx_->requestCompile();
}

void DetectionEffectFeature::onFrame(ViewportContext& ctx, const FrameTick& tick)
{
    if (!enabled_ || fx_.empty()) return;

    elapsed_sec_ += tick.dt_sec;

    // 推进扫描方位 (经 push-constant 传入半球 shader, 无需重建几何)。
    // 锁定态停转并钉在目标方位; 跟踪态降速 (逼近目标的节奏感)。
    for (auto& e : fx_) {
        if (!e.scan_animation) continue;
        if (e.lock_state == Effect::LockState::Lock) {
            e.current_scan_az = e.track_az;
            continue;
        }
        const double sp = (e.lock_state == Effect::LockState::Track)
                        ? e.scan_speed * 0.35 : e.scan_speed;
        e.current_scan_az += sp * tick.dt_sec;
        if (e.current_scan_az >= 360.0) e.current_scan_az -= 360.0;
    }

    // 遮挡查询缓存失效 (地形高度变化时)
    if (occlusion_svc_) {
        auto* tq = ctx.terrainQuery();
        if (tq) {
            static uint64_t last_ver = 0;
            if (tq->heightsVersion() != last_ver) {
                last_ver = tq->heightsVersion();
                occlusion_svc_->invalidateCache();
            }
        }
    }

    // 刷新半球体积 shader 的相机矩阵 + 扫描方位角 (rad)。
    if (!shader_handles_.empty()) {
        vsg::dmat4 vp; vsg::dvec3 cp;
        gui::ViewportKit::cameraMatrices(ctx.camera(), vp, cp);
        for (auto& [idx, h] : shader_handles_) {
            float scan_az_rad = 0.0f;
            if (idx >= 0 && idx < static_cast<int>(fx_.size()))
                scan_az_rad = static_cast<float>(
                    vsg::radians(fx_[idx].current_scan_az));
            gui::ViewportKit::updateShaderNode(h, vp, cp,
                                               float(elapsed_sec_),
                                               scan_az_rad, 0.0f,
                                               ctx.contentModel());
        }
    }
}

VSGVP_REGISTER_FEATURE(DetectionEffectFeature);

}} // namespace vsgraysim::viewport3d
