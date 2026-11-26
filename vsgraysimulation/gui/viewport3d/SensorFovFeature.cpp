/// @file SensorFovFeature.cpp
/// @brief 传感器视锥特性实现 (见 SensorFovFeature.h)。
#include "SensorFovFeature.h"

#include "FeatureRegistry.h"
#include "ViewportContext.h"
#include "ViewportKit.h"
#include "VoxelSceneFeature.h"
#include <vsgraysim/model_packages/equipment/VisualRegistry.h>

#pragma push_macro("slots")
#undef slots
#include <vsgraysim/earth/VoxelEquipmentRuntime.h>
#pragma pop_macro("slots")
#include <simcore/GeoFrame.h>
#include <vsg/maths/transform.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/StateGroup.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace vsgraysim { namespace viewport3d {

namespace {
/// 航迹线/视锥统一样式: 线宽 2, 负深度偏移避免被地形/模型遮挡。
const gui::ViewportKit::LineStyle kFovLineStyle{2.0f, true};

/// 视锥列表是否一致 (快照周期重复喂入时免重建+免编译)。
bool sameWorldFovs(const std::vector<SensorFovFeature::WorldFov>& a,
                   const std::vector<SensorFovFeature::WorldFov>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i]; const auto& y = b[i];
        if (x.origin != y.origin || x.fwd != y.fwd ||
            x.fov_h != y.fov_h || x.fov_v != y.fov_v ||
            x.range != y.range || x.color != y.color ||
            x.scan_mode != y.scan_mode || x.scan_az_center != y.scan_az_center ||
            x.scan_az_extent != y.scan_az_extent || x.scan_az_rate != y.scan_az_rate)
            return false;
    }
    return true;
}
} // anonymous namespace

void SensorFovFeature::onAttach(ViewportContext& ctx)
{
    ctx_ = &ctx;
    holder_ = vsg::Switch::create();
    world_root_ = vsg::Group::create();
    equipment_root_ = vsg::Group::create();
    holder_->addChild(enabled_, world_root_);
    holder_->addChild(enabled_, equipment_root_);
    if (auto root = ctx.contentRoot()) root->addChild(holder_);
}

void SensorFovFeature::onDetach(ViewportContext& ctx)
{
    if (holder_) ctx.removeFromWorld(holder_);
    holder_ = {};
    world_root_ = {};
    equipment_root_ = {};
    equipment_.clear();
    last_equipment_source_ = nullptr;
    last_fovs_.clear();
    sweeps_.clear();
    ctx_ = nullptr;
}

void SensorFovFeature::setEnabled(bool on)
{
    IViewportFeature::setEnabled(on);
    if (holder_) holder_->setAllChildren(on);
    if (on && ctx_) ctx_->requestCompile();
}

void SensorFovFeature::configure(const nlohmann::json& params)
{
    if (const auto it = params.find("preview_equipment"); it != params.end())
        preview_equipment_ = std::make_shared<const earth::VoxelEquipmentConfig>(
            earth::VoxelEquipmentConfig::parse(*it));
    else
        preview_equipment_.reset();
    last_equipment_source_ = nullptr;
}

std::optional<SensorFovFeature::EquipmentPose> SensorFovFeature::displayedEquipmentPose(uint32_t id) const
{
    const auto it = equipment_.find(id);
    if (!enabled_ || it == equipment_.end()) return {};
    return it->second.pose;
}

void SensorFovFeature::syncEquipment(ViewportContext& ctx)
{
    const auto* source = ctx.featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);
    if (source && !source->enabled()) source = nullptr;
    const auto generation = source ? source->equipmentVisualGeneration() : 0;
    const auto snapshot = source ? source->snapshot() : nullptr;
    const auto tick = snapshot ? snapshot->tick : 0;
    const auto revision = snapshot ? snapshot->revision : 0;
    const auto contentModel = ctx.contentModel();
    if (!equipment_root_ || (source == last_equipment_source_ &&
        generation == equipment_generation_ && tick == equipment_tick_ &&
        revision == equipment_revision_ && contentModel == equipment_content_model_)) return;
    last_equipment_source_ = source;
    equipment_generation_ = generation;
    equipment_tick_ = tick;
    equipment_revision_ = revision;
    equipment_content_model_ = contentModel;

    const auto* data = snapshot ? &source->equipmentVisuals() : nullptr;
    nlohmann::json preview;
    if (snapshot && source->runtime() && preview_equipment_) {
        preview = earth::voxelEquipmentVisualTelemetry(*preview_equipment_,*snapshot);
        data = &preview;
    }
    std::set<uint32_t> seen;
    bool needsCompile = false;
    if (data && data->is_object() && data->contains("items") && (*data)["items"].is_array() &&
        (*data)["items"].size() <= 16 && data->contains("version") && (*data)["version"] == 1 &&
        data->contains("tick") && (*data)["tick"] == snapshot->tick &&
        (!data->contains("state_revision") || (*data)["state_revision"] == snapshot->revision) &&
        data->contains("content_revision") && (*data)["content_revision"] == snapshot->contentRevision) {
        const auto fromScene = vsg::inverse(contentModel) * simcore::geo::makeEnuToWorldMatrix(snapshot->anchor);
        const double seconds = snapshot->timeSeconds();
        const auto readVector = [](const nlohmann::json& item, const char* key) {
            const auto v = item.at(key).get<std::array<double,3>>();
            return vsg::dvec3(v[0],v[1],v[2]);
        };
        const auto finite = [](const vsg::dvec3& v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        };
        for (const auto& item : (*data)["items"]) {
            try {
                const auto id = item.at("id").get<uint32_t>();
                const auto* model = model_packages::equipmentVisual(item.at("model_id").get<std::string>());
                if (!model || seen.count(id)) continue;
                const model_sdk::ApertureDisplay aperture{
                    item.at("horizontal_deg").get<double>(), item.at("vertical_deg").get<double>(),
                    item.at("range_m").get<double>(), item.at("preview_period_seconds").get<double>(),
                    item.at("phase_deg").get<double>()};
                const auto style = model->style(aperture);
                if (!(style.horizontalDegrees > 0 && style.horizontalDegrees < 180 &&
                    style.verticalDegrees > 0 && style.verticalDegrees < 180 &&
                    style.rangeMetres > 0 && style.rangeMetres <= 1000000)) continue;
                if (!std::isfinite(aperture.previewPeriodSeconds) || !std::isfinite(aperture.phaseDegrees)) continue;
                const auto origin = readVector(item,"origin");
                const auto rawForward = readVector(item,"forward");
                const auto rawUp = readVector(item,"up");
                const auto cross = vsg::cross(rawUp,rawForward);
                if (!finite(origin) || !finite(rawForward) || !finite(cross) ||
                    vsg::length(rawForward) < 1e-8 || vsg::length(cross) < 1e-8) continue;
                const auto forward = vsg::normalize(rawForward);
                const auto right = vsg::normalize(cross);
                const auto up = vsg::cross(forward,right);
                const vsg::dmat4 basis(right.x,right.y,right.z,0., up.x,up.y,up.z,0.,
                    forward.x,forward.y,forward.z,0., origin.x,origin.y,origin.z,1.);
                const auto angle = item.find("preview_angle_deg");
                const double azimuth = angle==item.end()?model->azimuthDegrees(aperture,seconds):angle->get<double>();
                if (!std::isfinite(azimuth)) continue;
                const auto swing = vsg::rotate(vsg::radians(azimuth),0.,1.,0.);
                auto& overlay = equipment_[id];
                const auto& old = overlay.style;
                if (!overlay.mount || old.rgb != style.rgb || old.azimuthRing != style.azimuthRing ||
                    old.rangeMetres != style.rangeMetres || old.horizontalDegrees != style.horizontalDegrees ||
                    old.verticalDegrees != style.verticalDegrees) {
                    if (overlay.mount) {
                        auto& children = equipment_root_->children;
                        children.erase(std::remove(children.begin(),children.end(),overlay.mount),children.end());
                    }
                    overlay.style = style;
                    overlay.mount = vsg::MatrixTransform::create();
                    overlay.beam = vsg::MatrixTransform::create();
                    const QColor color(style.rgb[0],style.rgb[1],style.rgb[2]);
                    const vsg::dvec3 zero{}, x(1.,0.,0.), y(0.,1.,0.), z(0.,0.,1.);
                    auto points = gui::ViewportKit::frustumSegments(zero,z,x,y,style.horizontalDegrees,
                        style.verticalDegrees,style.rangeMetres,false,true);
                    if (auto lines = gui::ViewportKit::makeLineSegments(points,color,kFovLineStyle))
                        overlay.beam->addChild(lines);
                    if (auto walls = gui::ViewportKit::makeFrustumWalls(zero,z,x,y,style.rangeMetres,
                        style.horizontalDegrees,style.verticalDegrees,color,0.24f,0.07f))
                        overlay.beam->addChild(walls);
                    overlay.mount->addChild(overlay.beam);
                    if (style.azimuthRing) {
                        constexpr size_t segments = 64;
                        points.clear();
                        points.reserve(2*segments);
                        const auto edge = [&](size_t index) {
                            const double a = index * (2. * vsg::PI / segments);
                            return vsg::vec3(float(style.rangeMetres*std::sin(a)),0.f,
                                             float(style.rangeMetres*std::cos(a)));
                        };
                        for (size_t i=0;i<segments;++i) {
                            points.push_back(edge(i));
                            points.push_back(edge(i+1));
                        }
                        const auto ringColor = color.darker(145);
                        if (auto ring = gui::ViewportKit::makeLineSegments(points,ringColor))
                            overlay.mount->addChild(ring);
                    }
                    equipment_root_->addChild(overlay.mount);
                    ++equipment_geometry_builds_;
                    needsCompile = true;
                }
                overlay.mount->matrix = fromScene*basis;
                overlay.beam->matrix = swing;
                const auto direction = basis*swing*vsg::dvec4(0.,0.,1.,0.);
                overlay.pose = {origin,{direction.x,direction.y,direction.z},azimuth,seconds};
                seen.insert(id);
            } catch (const nlohmann::json::exception&) {
                continue;
            }
        }
    }
    auto& children = equipment_root_->children;
    for (auto it = equipment_.begin(); it != equipment_.end();) {
        if (seen.count(it->first)) {
            ++it;
        } else {
            children.erase(std::remove(children.begin(),children.end(),it->second.mount),children.end());
            it = equipment_.erase(it);
        }
    }
    if (needsCompile) ctx.requestCompile();
}

// ---------------------------------------------------------------------------
// 局部空间视锥 (实体坐标系, 依据方位角/俯仰角/水平/垂直视场角/安装高度)
// ---------------------------------------------------------------------------
vsg::ref_ptr<vsg::Node> SensorFovFeature::makeLocalConeNode(const FovSensor& s,
                                                            const QColor& color,
                                                            bool mid_plane,
                                                            bool axis_line)
{
    const float az = vsg::radians(float(s.az_deg));
    const float el = vsg::radians(float(s.el_deg));
    // North is local -Z; azimuth increases clockwise toward +X (east).
    const vsg::vec3 fwd = vsg::normalize(vsg::vec3(
        std::sin(az) * std::cos(el), std::sin(el), -std::cos(az) * std::cos(el)));
    const vsg::vec3 right(std::cos(az), 0.0f, std::sin(az));
    const vsg::vec3 up = vsg::cross(right, fwd);
    const double r = std::max(1.0, s.range_m);
    const auto pts = gui::ViewportKit::frustumSegments(
        vsg::dvec3(0.0, s.offset_y, 0.0),
        vsg::dvec3(fwd.x, fwd.y, fwd.z),
        vsg::dvec3(right.x, right.y, right.z),
        vsg::dvec3(up.x, up.y, up.z),
        s.fov_h, s.fov_v, r, mid_plane, axis_line);
    auto group = vsg::Group::create();
    if (auto lines = gui::ViewportKit::makeLineSegments(pts, color, kFovLineStyle))
        group->addChild(lines);
    // 半透明视锥侧壁填充 (沿深度渐隐)。
    if (auto walls = gui::ViewportKit::makeFrustumWalls(
            vsg::dvec3(0.0, s.offset_y, 0.0),
            vsg::dvec3(fwd.x, fwd.y, fwd.z),
            vsg::dvec3(right.x, right.y, right.z),
            vsg::dvec3(up.x, up.y, up.z),
            r, s.fov_h, s.fov_v, color, 0.26f, 0.04f))
        group->addChild(walls);
    return group;
}

// ---------------------------------------------------------------------------
// 世界空间视锥组 (仿真预览)
// ---------------------------------------------------------------------------
void SensorFovFeature::setWorldFovs(const std::vector<WorldFov>& fovs)
{
    if (!world_root_) return;
    if (sameWorldFovs(last_fovs_, fovs)) return;   // 快照无变化: 跳过重建
    last_fovs_ = fovs;
    sweeps_.clear();
    const bool had = !world_root_->children.empty();
    world_root_->children.clear();
    bool changed = false;
    for (const auto& f : fovs) {
        vsg::dvec3 fwd = f.fwd;
        if (vsg::length(fwd) < 1e-9) fwd = vsg::dvec3(0.0, 0.0, 1.0);
        else fwd = vsg::normalize(fwd);
        vsg::dvec3 right, up;
        gui::ViewportKit::frustumBasis(fwd, right, up);
        if (auto lines = gui::ViewportKit::makeLineSegments(
                gui::ViewportKit::frustumSegments(f.origin, fwd, right, up,
                                                  f.fov_h, f.fov_v, f.range),
                f.color)) {
            world_root_->addChild(lines);
            changed = true;
        }
        // 半透明视锥侧壁填充 (沿深度渐隐)。
        if (auto walls = gui::ViewportKit::makeFrustumWalls(
                f.origin, fwd, right, up, f.range, f.fov_h, f.fov_v,
                f.color, 0.22f, 0.03f)) {
            world_root_->addChild(walls);
            changed = true;
        }
        // B7 §7.2 扫描机构: 扫描范围扇形 (贴地投影, 半透明) + 摆动扫描线。
        if (!f.scan_mode.empty() && f.scan_mode != "fixed" && f.scan_az_extent > 0.0) {
            // 世界方位约定 (makeFilledSector 同款): az=0 → -Z，北起顺时针。
            const double baseAz =
                (std::abs(fwd.x) > 1e-9 || std::abs(fwd.z) > 1e-9)
                    ? std::atan2(fwd.x, -fwd.z) * 180.0 / M_PI : 0.0;
            const vsg::vec3 center(float(f.origin.x), 0.0f, float(f.origin.z));
            const double azC = baseAz + f.scan_az_center;
            // 扇形填充 (半透明, 径向渐隐 —— 表现扫描覆盖范围)
            if (auto fan = gui::ViewportKit::makeFilledSector(
                    center, f.range, azC, f.scan_az_extent, f.color,
                    0.16f, 0.02f)) {
                world_root_->addChild(fan);
                changed = true;
            }
            // 扇形两直边 (界线)
            const double a0 = vsg::radians(azC - f.scan_az_extent / 2.0);
            const double a1 = vsg::radians(azC + f.scan_az_extent / 2.0);
            const auto edge = [&](double a, double r) {
                return vsg::vec3(center.x + float(std::sin(a) * r),
                                 center.y, center.z + float(std::cos(a) * r));
            };
            std::vector<vsg::vec3> edges{edge(a0, 0.0f), edge(a0, float(f.range)),
                                         edge(a1, 0.0f), edge(a1, float(f.range))};
            if (auto el = gui::ViewportKit::makeLineSegments(edges, f.color, kFovLineStyle)) {
                world_root_->addChild(el);
                changed = true;
            }
            // 摆动扫描线 (onFrame 逐帧改写端点)
            auto verts = vsg::vec3Array::create(2);
            verts->set(0, center);
            verts->set(1, edge(a0, float(f.range)));
            if (auto sw = gui::ViewportKit::makeLineSegments(verts, f.color, kFovLineStyle)) {
                world_root_->addChild(sw);
                SweepLine sl;
                sl.verts = verts;
                sl.center = center;
                sl.radius = f.range;
                sl.base_az = baseAz;
                sl.az_center = f.scan_az_center;
                sl.az_extent = f.scan_az_extent;
                sl.az_rate = f.scan_az_rate;
                sl.circular = (f.scan_mode == "circular");
                sweeps_.push_back(std::move(sl));
                changed = true;
            }
        }
    }
    if ((changed || had) && ctx_) ctx_->requestCompile();
}

void SensorFovFeature::clearWorldFovs()
{
    last_fovs_.clear();
    sweeps_.clear();
    if (world_root_ && !world_root_->children.empty()) {
        world_root_->children.clear();
        if (ctx_) ctx_->requestCompile();
    }
}

void SensorFovFeature::onFrame(ViewportContext& ctx, const FrameTick& tick)
{
    syncEquipment(ctx);
    if (sweeps_.empty()) return;
    // §7.2 指向时间函数 (表现层简化: sector 三角波 / circular 匀速周扫,
    // 其余模式按同速率往复近似; 精确 8 模式指向在仿真端 GimbalModel)。
    const double t = tick.elapsed_sec;
    for (auto& sw : sweeps_) {
        if (!sw.verts) continue;
        double az;
        if (sw.circular) {
            az = sw.base_az + sw.az_center + sw.az_rate * 6.0 * t;   // rpm→°/s
        } else {
            const double e = std::max(sw.az_extent, 1e-6);
            const double span = 2.0 * e;
            double x = std::fmod(std::abs(sw.az_rate) * t, span);
            if (x < 0.0) x += span;
            const double off = (x <= e) ? x : span - x;
            az = sw.base_az + sw.az_center - e / 2.0 + off;
        }
        const double a = vsg::radians(az);
        sw.verts->set(0, sw.center);
        sw.verts->set(1, vsg::vec3(sw.center.x + float(std::sin(a) * sw.radius),
                                   sw.center.y,
                                   sw.center.z + float(std::cos(a) * sw.radius)));
        sw.verts->dirty();
    }
}

VSGVP_REGISTER_FEATURE(SensorFovFeature);

}} // namespace vsgraysim::viewport3d
