/// @file ScenarioPreviewFeature.cpp
/// @brief 场景装载预览特性实现 (见 ScenarioPreviewFeature.h)。
#include "ScenarioPreviewFeature.h"

#include "IViewportFeature.h"
#include "FeatureRegistry.h"
#include "PlaybackFeature.h"
#include "SensorFovFeature.h"
#include "TerrainLayerFeature.h"
#include "ViewportContext.h"
#include "ViewportKit.h"
#include "EarthView.h"
#include <vsgraysim/earth/EarthDataService.h>

#include "vsgraysim/workspace/Workspace.h"
#include <simcore/Orientation.h>

#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/StateGroup.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace vsgraysim { namespace viewport3d {

void ScenarioPreviewFeature::onAttach(ViewportContext& ctx)
{
    ctx_ = &ctx;
    entities_root_ = vsg::Group::create();
    // rocky 地球底座启用时挂到锚点 ENU 内容根 (实体落到地球表面);
    // 否则回退世界根 (传统局部平面)。
    if (auto root = ctx.contentRoot()) root->addChild(entities_root_);
}

void ScenarioPreviewFeature::onDetach(ViewportContext& ctx)
{
    if (entities_root_) ctx.removeFromWorld(entities_root_);
    entities_root_ = {};
    plat_mt_.clear();
    ctx_ = nullptr;
}

vsg::MatrixTransform* ScenarioPreviewFeature::entityTransform(
    const std::string& object_id) const
{
    auto it = plat_mt_.find(object_id);
    return it != plat_mt_.end() ? it->second.get() : nullptr;
}

void ScenarioPreviewFeature::clear()
{
    plat_mt_.clear();
    if (entities_root_) entities_root_->children.clear();
    if (ctx_) {
        if (auto* fov = ctx_->featureAs<SensorFovFeature>(SensorFovFeature::kId))
            fov->clearWorldFovs();
        if (auto* pb = ctx_->featureAs<PlaybackFeature>(PlaybackFeature::kId))
            pb->clearEntities();
    }
}

bool ScenarioPreviewFeature::loadScenario(const std::string& scenario_path,
                                          const std::string& workspace_root)
{
    if (!ctx_) return false;
    namespace fs = std::filesystem;

    std::ifstream in(scenario_path);
    nlohmann::json j;
    try {
        if (in) in >> j;
    } catch (const std::exception&) { return false; }
    if (!j.is_object()) return false;

    if (auto* globe=ctx_->featureAs<EarthView>(EarthView::kId)) {
        std::string error;
        if(!globe->loadEarthScenario(j,scenario_path,error)) {
            spdlog::error("Scenario earth handoff rejected: {}",error);return false;
        }
    }

    clear();

    // ---- 地面: rocky 地球底座优先 (真实地形); 否则用 terrain_ref 高度场 ----
    auto* layer = ctx_->featureAs<TerrainLayerFeature>(TerrainLayerFeature::kId);
    if (layer) layer->clearTerrain();
    bool has_ground = false;
    if (ctx_->geoActive()) {
        has_ground = true;
        const auto reference=earth::EarthDataService::resolveMapPath(j,scenario_path);
        if(!reference.empty() && reference.size()>=4 && reference.substr(reference.size()-4)==".ter") {
            if(layer) {layer->configure({{"centered",true}});has_ground=layer->loadHeightmapFile(reference);}
        }
    } else if (j.contains("terrain_ref")) {
        const std::string resolved = Workspace::resolveTerrainRef(
            scenario_path, workspace_root);
        if (!resolved.empty()) {
            fs::path ter(resolved);
            ter.replace_extension(".ter");
            if (layer && fs::exists(ter) && layer->loadHeightmapFile(ter.string()))
                has_ground = true;
        } else {
            spdlog::warn("ScenarioPreview: terrain_ref not resolvable in '{}'",
                         scenario_path);
        }
    }
    // ---- 实体模型 + 变换 ----
    // (参考网格 reference_grid 可贴地重铺, 有无地形均保持可见, 无需切换。)
    auto* playback = ctx_->featureAs<PlaybackFeature>(PlaybackFeature::kId);
    std::map<std::string, vsg::dvec3> plat_pos;
    std::map<std::string, vsg::dquat> plat_rotation;
    for (const auto& jo : j.value("objects", nlohmann::json::array())) {
        const std::string mesh = jo.value("mesh_path", std::string());
        if (mesh.empty()) continue;
        auto node = gui::ViewportKit::loadModelNode(
            gui::ViewportKit::resolveAssetPath(mesh), ctx_->options());
        if (!node) continue;

        double pos[3] = {0, 0, 0};
        vsg::dvec3 angles{};
        const double scale = jo.value("scale", 1.0);
        const auto phys = jo.value("physics", nlohmann::json::object());
        const auto ip = phys.value("initial_position", nlohmann::json::array());
        if (ip.is_array() && ip.size() == 3)
            for (int k = 0; k < 3; ++k) pos[k] = ip[k].get<double>();
        const auto ir = phys.value("initial_rotation_deg", nlohmann::json::array());
        if (ir.is_array() && ir.size() == 3)
            angles={ir[0].get<double>(),ir[1].get<double>(),ir[2].get<double>()};

        auto mt = vsg::MatrixTransform::create();
        mt->matrix = vsg::translate(pos[0],pos[1],pos[2]) *
            vsg::rotate(simcore::headingRotation(angles.y,angles.x,angles.z)) * vsg::scale(scale);
        mt->addChild(node);
        if (entities_root_) entities_root_->addChild(mt);

        const std::string oid = jo.value("object_id", std::string());
        plat_mt_[oid] = mt;
        plat_pos[oid] = vsg::dvec3(pos[0], pos[1], pos[2]);
        plat_rotation[oid]=simcore::headingRotation(angles.y,angles.x,angles.z);

        // 回放运动: 注册实体 + 航迹 (沿航路点时间插值)。
        if (playback) {
            playback->registerEntity(oid, mt, scale);
            if (jo.contains("waypoints") && jo["waypoints"].is_array()) {
                std::vector<Waypoint> wps;
                for (const auto& jw : jo["waypoints"]) {
                    Waypoint w;
                    w.time = jw.value("time", 0.0);
                    const auto jp = jw.value("position", nlohmann::json::array());
                    if (jp.is_array() && jp.size() == 3)
                        for (int k = 0; k < 3; ++k)
                            w.pos[k] = jp[size_t(k)].get<double>();
                    wps.push_back(w);
                }
                playback->setWaypoints(oid, wps);
            }
        }
    }

    // ---- 传感器视锥线框 (世界空间, 按类型着色, 作用距离取自孔径配置) ----
    std::vector<SensorFovFeature::WorldFov> fovs;
    const auto sensors = j.value("sensors", nlohmann::json::object());
    for (const auto& js : sensors.value("sensor_list", nlohmann::json::array())) {
        const std::string plat = js.value("platform_object", std::string());
        vsg::dvec3 base(0, 0, 0);
        if (auto it = plat_pos.find(plat); it != plat_pos.end()) base = it->second;
        const auto ap = js.value("aperture", nlohmann::json::object());
        vsg::dvec3 off(0, 2.0, 0), dir(0, 0, 1);
        const auto po = ap.value("position_offset", nlohmann::json::array());
        if (po.is_array() && po.size() == 3)
            off = vsg::dvec3(po[0].get<double>(), po[1].get<double>(), po[2].get<double>());
        const auto dr = ap.value("direction", nlohmann::json::array());
        if (dr.is_array() && dr.size() == 3)
            dir = vsg::dvec3(dr[0].get<double>(), dr[1].get<double>(), dr[2].get<double>());

        SensorFovFeature::WorldFov f;
        if (auto it=plat_rotation.find(plat);it!=plat_rotation.end()) {
            // Mount offsets are metres in the platform frame, independent of mesh scale.
            f.origin=base+it->second*off;
            f.fwd=it->second*dir;
        } else { f.origin=base+off; f.fwd=dir; }
        f.fov_h = ap.value("fov_horizontal", 60.0);
        f.fov_v = ap.value("fov_vertical", 30.0);
        f.range = std::max(1.0, ap.value("range_m", 8000.0));
        f.color = gui::ViewportKit::sensorTypeColor(js.value("type", std::string("radar")));
        // B7 §7.2 扫描机构: scan 段 → 扫描扇形 + 摆动扫描线 (SensorFovFeature)
        if (js.contains("scan")) {
            const auto& jsc = js["scan"];
            f.scan_mode = jsc.value("mode", std::string());
            f.scan_az_center = jsc.value("az_center", 0.0);
            f.scan_az_extent = jsc.value("az_extent", 0.0);
            f.scan_az_rate = jsc.value("az_rate", 0.0);
        }
        fovs.push_back(f);
    }
    if (auto* fov = ctx_->featureAs<SensorFovFeature>(SensorFovFeature::kId))
        fov->setWorldFovs(fovs);

    ctx_->requestCompile();
    ctx_->resetCamera();
    spdlog::info("ScenarioPreview: scene built for '{}' ({} entities, ground={})",
                 scenario_path, plat_mt_.size(), has_ground);
    return true;
}

VSGVP_REGISTER_FEATURE(ScenarioPreviewFeature);

}} // namespace vsgraysim::viewport3d
