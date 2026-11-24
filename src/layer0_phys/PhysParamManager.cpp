#include "vsgraysim/layer0_phys/PhysParamManager.h"

#include <fstream>
#include <stdexcept>
#include <cmath>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <simcore/ObjectId.h>
#include <limits>
#include <algorithm>

namespace vsgraysim {

using json = nlohmann::json;

namespace {

GeoUnitPhysParams parseGroupParams(const std::string& object_id,
                                   uint32_t face_start,
                                   const json& vis_j,
                                   const json& lwir_j,
                                   const json& radar_j,
                                   const json& sar_j) {
    GeoUnitPhysParams p{};
    p.object_id = simcore::objectId(object_id);
    p.geo_unit_id = face_start; // overwritten per-face in buildGeoUnitArray
    p.band_mask = BAND_VIS | BAND_IR | BAND_RADAR | BAND_SAR;

    // Visible / laser
    std::string brdf = vis_j.value("brdf_type", "diffuse");
    if      (brdf == "roughmetal") p.vis_brdf_type = BRDF_ROUGH_METAL;
    else if (brdf == "mirror")     p.vis_brdf_type = BRDF_MIRROR;
    else if (brdf == "rubber")     p.vis_brdf_type = BRDF_RUBBER;
    else                           p.vis_brdf_type = BRDF_DIFFUSE;
    p.vis_albedo      = vis_j.value("albedo", 0.2f);
    p.laser_scatter   = vis_j.value("laser_scatter", 0.0f);
    p.laser_absorb    = vis_j.value("laser_absorb", 0.0f);

    // Infrared
    p.ir_emissivity[0] = lwir_j.value("emissivity", 0.9f);  // LWIR
    p.ir_emissivity[1] = lwir_j.value("emissivity_swir", lwir_j.value("emissivity", 0.9f));
    p.ir_base_temp     = lwir_j.value("base_temp", 293.15f);
    p.ir_thermal_conduct = lwir_j.value("thermal_conduct", 0.0f);
    p.ir_temp_tex_id   = lwir_j.value("temp_tex_id", 0u);

    // Radar
    p.radar_rcs_lut_id  = radar_j.value("rcs_lut_id", 0u);
    p.radar_epsilon_real = radar_j.value("dielectric_real", 1.0f);
    p.radar_epsilon_imag = radar_j.value("dielectric_imag", 0.0f);
    p.radar_surface_rough = radar_j.value("surface_rough", 0.5f);

    // SAR
    p.sar_sigma0_db     = sar_j.value("sigma0_db", -10.0f);
    p.sar_polar_couple  = sar_j.value("polar_couple", 0.0f);
    p.sar_doppler_corr  = sar_j.value("doppler_corr", 0.0f);

    p.rcs_modify_factor = 1.0f;
    p.temp_modify_offset = 0.0f;
    p.padding[0] = p.padding[1] = p.padding[2] = 0;
    return p;
}

} // anonymous namespace

void PhysParamManager::loadObjectConfig(const std::string& json_path) {
    std::ifstream ifs(json_path);
    if (!ifs) {
        throw std::runtime_error("PhysParamManager: cannot open " + json_path);
    }
    json j;
    ifs >> j;
    loadObjectConfigFromJson(j);
}

void PhysParamManager::loadObjectConfigFromJson(const json& j) {
    ObjectPhysConfig cfg;
    cfg.object_id = j.at("object_id").get<std::string>();
    if (cfg.object_id.empty() || object_index_.count(cfg.object_id)) throw std::invalid_argument("empty or duplicate object id");
    for (const auto& object:object_configs_)
        if (simcore::objectId(object.object_id)==simcore::objectId(cfg.object_id))
            throw std::invalid_argument("object id hash collision");
    cfg.mesh_path = j.value("mesh_path", std::string{});
    cfg.scale = j.value("scale",1.0);
    if (!std::isfinite(cfg.scale) || cfg.scale<=0) throw std::invalid_argument("object scale must be finite and positive");
    cfg.face_count = j.value("face_count", 0u);
    cfg.temp_update_rate_hz = j.value("temp_update_rate", 10u);
    cfg.rcs_modify_factor  = j.value("rcs_modify_factor", 1.0f);

    // Optional rigid-body physics section: {
    //   "physics": { "mass": 5000, "initial_position":[x,y,z],
    //                "initial_rotation_deg":[rx,ry,rz],
    //                "initial_velocity":[vx,vy,vz],
    //                "restitution":0.1, "friction":0.5,
    //                "convex_hull": true|false }
    // }
    if (j.contains("physics")) {
        const auto& ph = j.at("physics");
        cfg.physics_enabled = true;
        auto read_vec3 = [](const json& a, vsg::dvec3& out) {
            if (a.is_array() && a.size() >= 3) {
                out = vsg::dvec3(a[0].get<double>(), a[1].get<double>(), a[2].get<double>());
            }
        };
        read_vec3(ph.value("initial_position", json::array({0,0,0})), cfg.initial_position);
        read_vec3(ph.value("initial_rotation_deg", json::array({0,0,0})), cfg.initial_rotation_deg);
        read_vec3(ph.value("initial_velocity", json::array({0,0,0})), cfg.initial_velocity);
        cfg.mass            = ph.value("mass", 0.0f);
        cfg.restitution     = ph.value("restitution", 0.1f);
        cfg.friction        = ph.value("friction", 0.5f);
        cfg.use_convex_hull = ph.value("convex_hull", false);
        spdlog::info("Layer0: object '{}' physics enabled (mass={}, pos=({:.1f},{:.1f},{:.1f}), vel=({:.1f},{:.1f},{:.1f}))",
                     cfg.object_id, cfg.mass,
                     cfg.initial_position.x, cfg.initial_position.y, cfg.initial_position.z,
                     cfg.initial_velocity.x, cfg.initial_velocity.y, cfg.initial_velocity.z);
    }

    const auto groups = j.value("face_groups", json::array());
    if (!groups.is_array() || groups.empty()) {
        // 容错: 场景设计器产物可能没有 face_groups —— 生成默认单组。
        const uint32_t last = cfg.face_count > 0 ? cfg.face_count - 1 : 0;
        FaceGroup fg;
        fg.group_id    = "default";
        fg.face_start  = 0;
        fg.face_end    = last;
        fg.template_params = parseGroupParams(cfg.object_id, 0,
                                              j.value("vis_params", json::object()),
                                              j.value("lwir_params", json::object()),
                                              j.value("radar_x_params", json::object()),
                                              j.value("sar_c_params", json::object()));
        cfg.face_groups.push_back(std::move(fg));
        spdlog::warn("Layer0: object '{}' has no face_groups - generated default "
                     "group [0, {}]", cfg.object_id, last);
    } else {
        for (const auto& g : groups) {
            FaceGroup fg;
            fg.group_id   = g.at("group_id").get<std::string>();
            const auto& r = g.at("face_range");
            fg.face_start = r[0].get<uint32_t>();
            fg.face_end   = r[1].get<uint32_t>();
            fg.template_params = parseGroupParams(cfg.object_id, fg.face_start,
                                                  g.value("vis_params", json::object()),
                                                  g.value("lwir_params", json::object()),
                                                  g.value("radar_x_params", json::object()),
                                                  g.value("sar_c_params", json::object()));
            cfg.face_groups.push_back(std::move(fg));
        }
    }

    object_index_[cfg.object_id] = object_configs_.size();
    object_configs_.push_back(std::move(cfg));
    spdlog::info("Layer0: loaded config '{}' ({} faces, {} groups)",
                 object_configs_.back().object_id,
                 object_configs_.back().face_count,
                 object_configs_.back().face_groups.size());
}

std::size_t PhysParamManager::validateAll() {
    std::size_t corrected = 0;
    auto clamp = [](float& v, float lo, float hi) {
        if (v < lo) { v = lo; return true; }
        if (v > hi) { v = hi; return true; }
        return false;
    };
    for (auto& cfg : object_configs_) {
        for (auto& g : cfg.face_groups) {
            auto& p = g.template_params;
            corrected += clamp(p.vis_albedo, 0.0f, 1.0f);
            corrected += clamp(p.laser_scatter, 0.0f, 1.0f);
            corrected += clamp(p.laser_absorb,  0.0f, 1.0f);
            corrected += clamp(p.ir_emissivity[0], 0.0f, 1.0f);
            corrected += clamp(p.ir_emissivity[1], 0.0f, 1.0f);
            if (p.ir_base_temp < 100.0f || p.ir_base_temp > 1000.0f) {
                p.ir_base_temp = 293.15f; ++corrected;
            }
            corrected += clamp(p.radar_epsilon_real, 1.0f, 100.0f);
            corrected += clamp(p.radar_epsilon_imag, 0.0f, 100.0f);
            if (p.rcs_modify_factor <= 0.0f) { p.rcs_modify_factor = 1.0f; ++corrected; }
        }
    }
    spdlog::info("Layer0: validation completed ({} corrections)", corrected);
    return corrected;
}

void PhysParamManager::buildGeoUnitArray() {
    ++revision_;
    geo_units_.clear();
    geo_index_.clear();
    object_faces_.clear();
    for (auto& cfg : object_configs_) {
        auto& faces=object_faces_[cfg.object_id];
        for (auto& g : cfg.face_groups) {
            if (g.face_end<g.face_start || (cfg.face_count && g.face_end>=cfg.face_count))
                throw std::invalid_argument("material face range exceeds the object's face count");
            for (uint64_t face = g.face_start; face <= g.face_end; ++face) {
                const uint32_t f=uint32_t(face);
                if (faces.count(f)) throw std::invalid_argument("overlapping material face ranges");
                if (geo_units_.size()>=std::numeric_limits<uint32_t>::max()) throw std::overflow_error("geometry-unit index overflow");
                GeoUnitPhysParams p = g.template_params;
                p.geo_unit_id = uint32_t(geo_units_.size());
                p.rcs_modify_factor=cfg.rcs_modify_factor;
                faces[f]=geo_units_.size();
                geo_units_.push_back(p);
                geo_index_[p.geo_unit_id] = geo_units_.size() - 1;
            }
        }
    }
    spdlog::info("Layer0: built geometry-unit array ({} entries)", geo_units_.size());
}

void PhysParamManager::buildBandParamMap() {
    band_param_map_.clear();
    band_param_map_.reserve(geo_units_.size());
    for (std::size_t i = 0; i < geo_units_.size(); ++i) {
        const auto& p = geo_units_[i];
        BandParamMap m{p.geo_unit_id, p.band_mask, static_cast<uint32_t>(i), 0u};
        band_param_map_.push_back(m);
    }
    spdlog::info("Layer0: built band-parameter map ({} entries)", band_param_map_.size());
}

void PhysParamManager::updateDynamicParams(const std::string& object_id,
                                          float rcs_modify_factor,
                                          float temp_offset_K) {
    auto it = object_index_.find(object_id);
    if (it == object_index_.end()) {
        spdlog::warn("Layer0: updateDynamicParams: unknown object '{}'", object_id);
        return;
    }
    auto& cfg = object_configs_[it->second];
    const bool changed=cfg.rcs_modify_factor!=rcs_modify_factor ||
        std::any_of(cfg.face_groups.begin(),cfg.face_groups.end(),[&](const auto& group){return group.template_params.temp_modify_offset!=temp_offset_K;});
    if(!changed) return;
    ++revision_;
    cfg.rcs_modify_factor = rcs_modify_factor;
    for (auto& g : cfg.face_groups) {
        g.template_params.rcs_modify_factor = rcs_modify_factor;
        g.template_params.temp_modify_offset = temp_offset_K;
    }
    // Local face zero of another object must never receive this update.
    for (const auto& [face,index]:object_faces_[object_id]) {
        geo_units_[index].rcs_modify_factor=rcs_modify_factor;
        geo_units_[index].temp_modify_offset=temp_offset_K;
    }
    spdlog::debug("Layer0: dynamic params updated for '{}' (rcs={}, temp_off={:.2f}K)",
                 object_id, rcs_modify_factor, temp_offset_K);
}

void PhysParamManager::overrideGeoUnit(uint32_t geo_unit_id,
                                       const GeoUnitPhysParams& new_params) {
    auto it = geo_index_.find(geo_unit_id);
    if (it == geo_index_.end()) {
        spdlog::warn("Layer0: overrideGeoUnit: unknown geo_unit_id {}", geo_unit_id);
        return;
    }
    const auto object=geo_units_[it->second].object_id;
    ++revision_;
    geo_units_[it->second] = new_params;
    geo_units_[it->second].object_id=object;
    geo_units_[it->second].geo_unit_id = geo_unit_id;
}

const ObjectPhysConfig* PhysParamManager::findObject(const std::string& id) const {
    auto it = object_index_.find(id);
    if (it == object_index_.end()) return nullptr;
    return &object_configs_[it->second];
}
const GeoUnitPhysParams* PhysParamManager::findFace(const std::string& object,uint32_t face) const {
    const auto it=object_faces_.find(object);
    if(it==object_faces_.end()) return nullptr;
    auto value=it->second.find(face);
    const auto* cfg=findObject(object);
    // Legacy scenes with an unspecified count describe one uniform material.
    if(value==it->second.end() && cfg && cfg->face_count==0 && cfg->face_groups.size()==1 &&
       cfg->face_groups.front().group_id=="default") value=it->second.find(0);
    return value==it->second.end() ? nullptr : &geo_units_[value->second];
}

} // namespace vsgraysim
