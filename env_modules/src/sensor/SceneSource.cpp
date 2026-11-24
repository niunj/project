/// @file SceneSource.cpp —— 场景数据源: JSON 解析 (nlohmann 单头) + WGS84/ENU 换算
///            (GeoFrame 纯数学) + 热场景图构建 (binding0 pos + binding1 props)。
///
/// schema 对齐 docs/design_3d_sensor_rendering.md §14.3:
///   objects[]{object_id, mesh_path, face_count, type, faction,
///             platform{faction/type/position_geo},
///             physics.initial_position (ENU 直读, 既有字段),
///             face_groups[]{group_id, face_range, lwir_params{base_temp[K], emissivity}},
///             thermal{base_temp_k, parts[]{temp_k, emissivity, face_group}}}
///   geo_anchor{lat, lon, alt, heading_deg} (顶层, ENU 原点)
#include "rocky_env/SceneSource.h"
#include "rocky_env/SensorApi.h"
#include <simcore/GeoFrame.h>
#include <simcore/Orientation.h>
#include "SceneSchema.h"        // P1: 场景 JSON 字段校验 (轻量, 随 nlohmann)

#include <nlohmann/json.hpp>

#include <vsg/all.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <utility>

namespace rockyenv {
namespace sensor {

// ===========================================================================
// 工具
// ===========================================================================
namespace {

/// JSON 多候选键取值 (base_temp_k / base_temp 等新旧字段兼容)
template <typename T>
T pick(const nlohmann::json& o, std::initializer_list<const char*> keys, T def) {
    if (!o.is_object()) return def;
    for (auto* k : keys)
        if (o.contains(k) && !o.at(k).is_null()) {
            try { return o.at(k).get<T>(); } catch (...) { return def; }
        }
    return def;
}

/// mesh_path 解析: 原样(CWD) / 场景文件目录 / 其上级 / 上两级。
/// 命中即返回; 未命中原样交回 (loader 报告失败 → 程序化盒体回退)。
std::string resolveMeshPath(const std::string& scenario_path, const std::string& ref) {
    namespace fs = std::filesystem;
    if (ref.empty()) return ref;
    const fs::path p(ref);
    std::error_code ec;
    if (p.is_absolute()) return ref;
    const fs::path dir = fs::path(scenario_path).parent_path();
    const fs::path cands[] = {fs::path(ref), dir / ref, dir / ".." / ref, dir / ".." / ".." / ref};
    for (const auto& c : cands) {
        if (fs::exists(c, ec)) {
            auto canon = fs::weakly_canonical(c, ec);
            return ec ? c.string() : canon.string();
        }
    }
    return ref;
}

/// WGS84 大地坐标 → 锚点 ENU 局部坐标 (米; X=East, Y=Up, Z=-North, GeoFrame 约定)。
/// 纯解析变换, 不依赖 rocky 运行时。
vsg::dvec3 geoToEnu(const GeoAnchorSpec& a, double lat_deg, double lon_deg, double alt_m) {
    using namespace simcore::geo;
    GeoAnchor anchor;
    anchor.lat_deg = a.lat_deg; anchor.lon_deg = a.lon_deg;
    anchor.alt_m = a.alt_m; anchor.heading_deg = a.heading_deg;
    const auto world = geodeticToECEF(lat_deg, lon_deg, alt_m);
    return ecefToLocal(anchor, world);
}

// ---------------------------------------------------------------------------
// 网格 → 三角形汤 (VSG 图遍历, 应用 MatrixTransform 链)
// ---------------------------------------------------------------------------
struct TriFlatten : public vsg::Inherit<vsg::Visitor, TriFlatten> {
    vsg::dmat4 xform;                       // 模型 → 世界 (祖先 MatrixTransform 积累)
    std::vector<vsg::vec3>* out = nullptr;
    std::vector<vsg::vec2>* out_uv = nullptr;    // 可选: 与 out 同序 (缺 uv → (0,0))
    std::vector<uint32_t>* out_slot = nullptr;   // 可选: 逐三角形槽号 (VID 序)
    uint32_t vidCounter = 0;                     // VID 序 → 材质槽号 (objloader 桶序)
    uint32_t skipped = 0;                   // 无位置数组/索引的 draw 数 (日志)

    void apply(vsg::Object& obj) override { obj.traverse(*this); }

    void apply(vsg::MatrixTransform& mt) override {
        vsg::dmat4 prev = xform;
        xform = xform * mt.matrix;
        mt.traverse(*this);
        xform = prev;
    }

    static const vsg::vec3Array* positions(const vsg::BufferInfoList& arrays) {
        for (auto& bi : arrays)
            if (bi && bi->data)
                if (auto* a = bi->data->cast<vsg::vec3Array>()) return a;
        return nullptr;
    }

    static const vsg::vec2Array* uvCoords(const vsg::BufferInfoList& arrays) {
        for (auto& bi : arrays)
            if (bi && bi->data)
                if (auto* a = bi->data->cast<vsg::vec2Array>()) return a;
        return nullptr;
    }

    void emit(uint32_t i0, uint32_t i1, uint32_t i2, const vsg::vec3Array* pos,
              const vsg::vec2Array* uv, uint32_t slot) {
        if (!pos || i0 >= pos->size() || i1 >= pos->size() || i2 >= pos->size()) return;
        for (uint32_t i : {i0, i1, i2}) {
            const vsg::dvec4 w = xform * vsg::dvec4(vsg::dvec3((*pos)[i]), 1.0);
            out->push_back(vsg::vec3(float(w.x), float(w.y), float(w.z)));
            if (out_uv)
                out_uv->push_back((uv && i < uv->size()) ? (*uv)[i]
                                                         : vsg::vec2(0.0f, 0.0f));
        }
        if (out_slot) out_slot->push_back(slot);
    }

    void apply(vsg::VertexIndexDraw& vid) override {
        auto* pos = positions(vid.arrays);
        auto* uv = uvCoords(vid.arrays);
        // 本 vsg 版本: indices 为 ref_ptr<BufferInfo>, 实际索引数据在 ->data
        const vsg::Data* idxData = vid.indices ? vid.indices->data.get() : nullptr;
        const uint32_t slot = vidCounter++;
        if (!pos || !idxData || vid.indexCount < 3) { ++skipped; return; }
        if (auto* u32 = idxData->cast<vsg::uintArray>()) {
            for (uint32_t t = 0; t + 2 < vid.indexCount; t += 3)
                emit((*u32)[t], (*u32)[t + 1], (*u32)[t + 2], pos, uv, slot);
        } else if (auto* u16 = idxData->cast<vsg::ushortArray>()) {
            for (uint32_t t = 0; t + 2 < vid.indexCount; t += 3)
                emit((*u16)[t], (*u16)[t + 1], (*u16)[t + 2], pos, uv, slot);
        } else {
            ++skipped;
        }
    }

    void apply(vsg::VertexDraw& vd) override {
        auto* pos = positions(vd.arrays);
        auto* uv = uvCoords(vd.arrays);
        const uint32_t slot = vidCounter++;
        if (!pos || vd.vertexCount < 3) { ++skipped; return; }
        for (uint32_t t = 0; t + 2 < vd.vertexCount; t += 3)
            emit(t, t + 1, t + 2, pos, uv, slot);
    }
};

/// 程序化盒体半尺寸 (m): type → 形体 (mesh 回退用)
vsg::dvec3 proceduralHalfExtent(const std::string& type) {
    if (type == "missile") return {0.2, 0.2, 1.2};
    if (type == "uav" || type == "aircraft") return {1.0, 0.25, 1.0};
    if (type == "tank") return {3.0, 1.2, 1.7};
    return {1.0, 0.75, 1.0};   // ground_station / 未知类型
}

// ---------------------------------------------------------------------------
// sensor_list 解析辅助 (§14.3; 与场景设计器 A1 推导规则一致)
// ---------------------------------------------------------------------------
/// frame_kind 推导: radar/sar→RADAR, vis→VIS, ir 按 band (缺省 MWIR), laser 未映射
std::string deriveFrameKind(const std::string& type, const std::string& band) {
    if (type == "radar" || type == "sar") return "RADAR";
    if (type == "vis") return "VIS";
    if (type == "ir") {
        if (band == "SWIR" || band == "LWIR" || band == "VIS") return band;
        return "MWIR";
    }
    return {};
}

/// λ 范围推导: RADAR=载波单点; 成像=BandSpec 规范范围 (SensorApi 单一真值源)
void deriveLambda(const std::string& kind, double wavelength, double& lo, double& hi) {
    if (lo > 0.0 && hi > 0.0) return;
    if (kind == "RADAR") {
        if (wavelength > 0.0) lo = hi = wavelength;
        return;
    }
    BandType bt = BandType::MWIR;
    if (kind == "VIS")       bt = BandType::TV;
    else if (kind == "SWIR") bt = BandType::SWIR;
    else if (kind == "LWIR") bt = BandType::LWIR;
    const BandSpec b = BandSpec::make(bt);
    lo = b.lambda_min;
    hi = b.lambda_max;
}

/// 机体系 az/el (°) → 单位指向 (场景设计器写回同公式: X=East 前向约定)
vsg::dvec3 azElToDir(double az_deg, double el_deg) {
    const double azr = az_deg * M_PI / 180.0, elr = el_deg * M_PI / 180.0;
    return vsg::dvec3(std::sin(azr) * std::cos(elr), std::sin(elr), std::cos(azr) * std::cos(elr));
}

/// sensors.sensor_list[] → SensorDesc (新旧 schema 双兼容, 新字段优先)
void parseSensorList(SceneData& data, const nlohmann::json& j) {
    if (!j.contains("sensors") || !j.at("sensors").is_object()) return;
    const auto& sensors = j.at("sensors");
    if (!sensors.contains("sensor_list") || !sensors.at("sensor_list").is_array()) return;

    for (const auto& sj : sensors.at("sensor_list")) {
        SensorDesc s;
        s.sensor_id = pick<int>(sj, {"sensor_id"}, int(data.sensors.size() + 1));
        s.type = sj.value("type", std::string());
        s.band = sj.value("band", std::string());
        s.role = sj.value("role", std::string("search"));
        s.platform = pick<std::string>(sj, {"platform_object", "platform"}, std::string());
        if (const SceneObjectDesc* plat = data.findObject(s.platform))
            s.faction = plat->faction;

        // tx_params (雷达/载波参数, B6)
        if (sj.contains("tx_params")) {
            const auto& tx = sj.at("tx_params");
            s.power      = pick<double>(tx, {"power"}, s.power);
            s.beam_width = pick<double>(tx, {"beam_width"}, s.beam_width);
            s.wavelength = pick<double>(tx, {"wavelength", "wavelength_m"}, s.wavelength);
            s.pulse_rate = pick<double>(tx, {"pulse_rate", "prf_hz"}, s.pulse_rate);
            s.duty_cycle = pick<double>(tx, {"duty_cycle"}, s.duty_cycle);
            s.cpi_pulses = uint32_t(std::max(1, pick<int>(tx, {"cpi_pulses", "cpi"}, int(s.cpi_pulses))));
        }

        // aperture: position_offset (新) / position (旧) + direction / az+el
        if (sj.contains("aperture")) {
            const auto& ap = sj.at("aperture");
            const nlohmann::json* off = nullptr;
            if (ap.contains("position_offset")) off = &ap.at("position_offset");
            else if (ap.contains("position"))   off = &ap.at("position");
            if (off && off->is_array() && off->size() >= 3) {
                try {
                    s.aperture_offset = vsg::dvec3(off->at(0).get<double>(),
                                                   off->at(1).get<double>(),
                                                   off->at(2).get<double>());
                } catch (...) {}
            }
            s.fov_h   = pick<double>(ap, {"fov_horizontal"}, 0.0);
            s.fov_v   = pick<double>(ap, {"fov_vertical"}, 0.0);
            s.az_deg  = pick<double>(ap, {"azimuth_deg"}, 0.0);
            s.el_deg  = pick<double>(ap, {"elevation_deg"}, 0.0);
            s.range_m = pick<double>(ap, {"range_m"}, s.range_m);
            bool has_dir = false;
            if (ap.contains("direction") && ap.at("direction").is_array() &&
                ap.at("direction").size() >= 3) {
                try {
                    s.direction = vsg::dvec3(ap.at("direction").at(0).get<double>(),
                                             ap.at("direction").at(1).get<double>(),
                                             ap.at("direction").at(2).get<double>());
                    has_dir = true;
                } catch (...) {}
            }
            if (!has_dir && (s.az_deg != 0.0 || s.el_deg != 0.0))
                s.direction = azElToDir(s.az_deg, s.el_deg);
        }

        // 旧 signal_params: integration_time (s) → 积分毫秒 (rate_hz 推导兜底)
        double old_integration_ms = 0.0;
        if (sj.contains("signal_params")) {
            old_integration_ms =
                pick<double>(sj.at("signal_params"), {"integration_time", "integration_time_s"}, 0.0)
                * 1000.0;
            if(sj.at("signal_params").contains("integration_time") || sj.at("signal_params").contains("integration_time_s"))
                s.integration_ms=old_integration_ms;
            s.netd_mk=pick<double>(sj.at("signal_params"),{"netd"},s.netd_mk*.001)*1000.;
        }

        // frame (新 schema; 缺省按 kind 推导)
        if (sj.contains("frame")) {
            const auto& fr = sj.at("frame");
            s.frame_kind = pick<std::string>(fr, {"kind"}, std::string());
            s.lambda_min = pick<double>(fr, {"lambda_min"}, 0.0);
            s.lambda_max = pick<double>(fr, {"lambda_max"}, 0.0);
            if (fr.contains("resolution") && fr.at("resolution").is_object()) {
                s.res_w = uint32_t(pick<int>(fr.at("resolution"), {"width"}, int(s.res_w)));
                s.res_h = uint32_t(pick<int>(fr.at("resolution"), {"height"}, int(s.res_h)));
            }
            s.fov_deg = pick<double>(fr, {"fov_deg"}, 0.0);
            s.rate_hz = pick<double>(fr, {"rate_hz"}, 0.0);
            s.cpi_pulses = pick<uint32_t>(fr, {"cpi_pulses"}, s.cpi_pulses);
        }
        if (s.frame_kind.empty())
            s.frame_kind = deriveFrameKind(s.type, s.band);
        deriveLambda(s.frame_kind, s.wavelength, s.lambda_min, s.lambda_max);
        if (s.fov_deg <= 0.0) s.fov_deg = s.fov_h;
        if (s.rate_hz <= 0.0) {
            if (s.frame_kind == "RADAR") s.rate_hz = s.pulse_rate;
            else if (old_integration_ms > 0.0) s.rate_hz = 1000.0 / old_integration_ms;
        }

        // profile 三组 (同名映射 SensorApi.h; 缺省保留 SensorApi 默认值)
        if (sj.contains("profile")) {
            const auto& pj = sj.at("profile");
            if (pj.contains("optics")) {
                const auto& o = pj.at("optics");
                s.f_number     = pick<double>(o, {"f_number"}, s.f_number);
                s.transmission = pick<double>(o, {"transmission"}, s.transmission);
                s.psf_sigma_px = pick<double>(o, {"psf_sigma_px"}, s.psf_sigma_px);
                s.vignetting   = pick<double>(o, {"vignetting"}, s.vignetting);
            }
            if (pj.contains("detector")) {
                const auto& o = pj.at("detector");
                s.pixel_pitch_um = pick<double>(o, {"pixel_pitch_um"}, s.pixel_pitch_um);
                s.qe             = pick<double>(o, {"qe"}, s.qe);
                s.integration_ms = pick<double>(o, {"integration_ms"}, s.integration_ms);
                s.netd_mk        = pick<double>(o, {"netd_mk"}, s.netd_mk);
                s.well_ke        = pick<double>(o, {"well_ke"}, s.well_ke);
                s.read_noise_e   = pick<double>(o, {"read_noise_e"}, s.read_noise_e);
                s.bits           = uint32_t(pick<int>(o, {"bits"}, int(s.bits)));
            }
            if (pj.contains("electronics")) {
                const auto& o = pj.at("electronics");
                s.agc_low_pct  = pick<double>(o, {"agc_low_pct"}, s.agc_low_pct);
                s.agc_high_pct = pick<double>(o, {"agc_high_pct"}, s.agc_high_pct);
                s.gamma        = pick<double>(o, {"gamma"}, s.gamma);
            }
        }

        // 发布级: RADAR→radar_stages_publish, 其余→stages (互为兜底)
        auto readStages = [&](const char* key, std::vector<std::string>& out) {
            if (sj.contains(key) && sj.at(key).is_array())
                for (const auto& st : sj.at(key))
                    try { out.push_back(st.get<std::string>()); } catch (...) {}
        };
        std::vector<std::string> img_st, rdr_st;
        readStages("stages", img_st);
        readStages("radar_stages_publish", rdr_st);
        s.stages = (s.frame_kind == "RADAR") ? (rdr_st.empty() ? img_st : rdr_st)
                                             : (img_st.empty() ? rdr_st : img_st);

        // scan (§7.2 指向时序; B7 GimbalModel 消费)
        if (sj.contains("scan")) {
            const auto& sc = sj.at("scan");
            s.scan.mode       = pick<std::string>(sc, {"mode"}, std::string("fixed"));
            s.scan.az_center  = pick<double>(sc, {"az_center"}, s.az_deg);
            s.scan.el_center  = pick<double>(sc, {"el_center"}, s.el_deg);
            s.scan.az_extent  = pick<double>(sc, {"az_extent"}, 0.0);
            s.scan.el_extent  = pick<double>(sc, {"el_extent"}, 0.0);
            s.scan.az_rate    = pick<double>(sc, {"az_rate"}, 0.0);
            s.scan.el_rate    = pick<double>(sc, {"el_rate"}, 0.0);
            s.scan.target_ref = pick<std::string>(sc, {"target_ref"}, std::string());
        }

        if (s.name.empty())
            s.name = "sensor_" + std::to_string(s.sensor_id > 0 ? s.sensor_id
                                                                : int(data.sensors.size()) + 1);
        data.sensors.push_back(std::move(s));
    }
}

} // namespace

std::vector<SensorDesc> parseSensorDescriptions(const std::string& text) {
    SceneData data;
    parseSensorList(data,nlohmann::json::parse(text));
    return data.sensors;
}

// ===========================================================================
// SceneData
// ===========================================================================
const SceneObjectDesc* SceneData::primaryTarget(const std::string& terrain_ref) const {
    for (const auto& o : objects)
        if (!o.isTerrain(terrain_ref)) return &o;
    return nullptr;
}

const SceneObjectDesc* SceneData::findObject(const std::string& object_id) const {
    for (const auto& o : objects)
        if (o.object_id == object_id) return &o;
    return nullptr;
}

// ===========================================================================
// JsonSceneDataSource
// ===========================================================================
JsonSceneDataSource::JsonSceneDataSource(std::string path) : path_(std::move(path)) {}

SceneDataSource::Result JsonSceneDataSource::load() {
    data_ = SceneData{};
    std::ifstream in(path_);
    if (!in) return {false, "无法打开场景文件: " + path_};

    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        return {false, "JSON 解析失败: " + std::string(e.what())};
    }
    // P1 工程化: schema 校验 (字段存在性/类型/物理范围), 发现问题仅警告不回退
    scene_schema::validateScenario(j, path_.c_str());

    data_.scene_name = j.value("scenario_name", std::string());
    data_.terrain_ref = j.value("terrain_ref", std::string());
    // terrain_ref 与 objects[].mesh_path 用同一解析规则 (绝对路径) ——
    // isTerrain() 依赖两者一致比较, 地面温度取 terrain 对象首面组。
    if (!data_.terrain_ref.empty())
        data_.terrain_ref = resolveMeshPath(path_, data_.terrain_ref);

    // ---- geo_anchor (ENU 原点; 缺省时回退首个 position_geo) ----
    if (j.contains("geo_anchor")) {
        const auto& a = j.at("geo_anchor");
        data_.anchor.lat_deg = pick<double>(a, {"lat", "lat_deg"}, 0.0);
        data_.anchor.lon_deg = pick<double>(a, {"lon", "lon_deg"}, 0.0);
        data_.anchor.alt_m = pick<double>(a, {"alt", "alt_m", "elev"}, 0.0);
        data_.anchor.heading_deg = pick<double>(a, {"heading_deg"}, 0.0);
        data_.anchor.valid = true;
    }

    // ---- ocean (批次3: 海面场景声明; OceanProvider/海面真值消费) ----
    if (j.contains("ocean") && j.at("ocean").is_object()) {
        const auto& oc = j.at("ocean");
        data_.ocean.enabled = oc.value("enabled", false);
        data_.ocean.sea_temp_k = float(pick<double>(oc, {"sea_temp_k", "temp_k"}, 291.0));
        data_.ocean.sea_level = float(pick<double>(oc, {"sea_level"}, 0.0));
        data_.ocean.extent = float(pick<double>(oc, {"extent", "size"}, 0.0));
        if (oc.contains("fft_patch_size")) {
            if (!oc.at("fft_patch_size").is_number())
                return {false, "ocean.fft_patch_size must be a finite positive number of metres"};
            data_.ocean.fft_patch_size = oc.at("fft_patch_size").get<float>();
            if (!std::isfinite(data_.ocean.fft_patch_size) || data_.ocean.fft_patch_size <= 0.0f)
                return {false, "ocean.fft_patch_size must be a finite positive number of metres"};
        }
    }

    // ---- objects[] ----
    if (j.contains("objects") && j.at("objects").is_array()) {
        for (const auto& oj : j.at("objects")) {
            SceneObjectDesc o;
            o.object_id = oj.value("object_id", std::string());
            o.mesh_path = oj.value("mesh_path", std::string());
            o.face_count = pick<uint32_t>(oj, {"face_count"}, 0u);
            o.type = oj.value("type", std::string());
            o.faction = oj.value("faction", std::string());
            o.scale=oj.value("scale",1.0);
            if (!std::isfinite(o.scale) || o.scale<=0) throw std::invalid_argument("invalid scene object scale");
            if (oj.contains("physics") && oj["physics"].contains("initial_rotation_deg")) {
                const auto& r=oj["physics"]["initial_rotation_deg"];
                if (!r.is_array() || r.size()!=3) throw std::invalid_argument("invalid scene object rotation");
                o.rotation_deg={r[0].get<double>(),r[1].get<double>(),r[2].get<double>()};
                simcore::headingRotation(o.rotation_deg.y,o.rotation_deg.x,o.rotation_deg.z);
            }

            // platform (§14.3 增补): 归属/类型优先, position_geo → ENU
            if (oj.contains("platform")) {
                const auto& pj = oj.at("platform");
                o.faction = pj.value("faction", o.faction);
                o.type = pj.value("type", o.type);
                if (pj.contains("position_geo")) {
                    const auto& g = pj.at("position_geo");
                    const double lat = pick<double>(g, {"lat", "lat_deg"}, 0.0);
                    const double lon = pick<double>(g, {"lon", "lon_deg"}, 0.0);
                    const double alt = pick<double>(g, {"elev", "alt", "alt_m"}, 0.0);
                    if (data_.anchor.valid) {
                        o.position_enu = geoToEnu(data_.anchor, lat, lon, alt);
                        o.from_geo = true;
                        ++data_.geo_count;
                    }
                }
            }

            // 位置兜底: physics.initial_position (ENU 直读, 既有 schema)
            if (!o.from_geo && oj.contains("physics") && oj.at("physics").contains("initial_position")) {
                const auto& p = oj.at("physics").at("initial_position");
                if (p.is_array() && p.size() >= 3)
                    o.position_enu = vsg::dvec3(p.at(0).get<double>(), p.at(1).get<double>(),
                                                p.at(2).get<double>());
            }

            // 热属性: face_groups (既有) + thermal.parts (§14.3 覆盖)
            if (oj.contains("face_groups") && oj.at("face_groups").is_array()) {
                for (const auto& fg : oj.at("face_groups")) {
                    ThermalGroup g;
                    g.group_id = fg.value("group_id", std::string());
                    if (fg.contains("face_range") && fg.at("face_range").is_array() &&
                        fg.at("face_range").size() >= 2) {
                        g.face_begin = fg.at("face_range").at(0).get<uint32_t>();
                        g.face_end = fg.at("face_range").at(1).get<uint32_t>();
                    }
                    if (fg.contains("lwir_params")) {
                        const auto& lw = fg.at("lwir_params");
                        g.temp_k = float(pick<double>(lw, {"base_temp_k", "base_temp"}, double(g.temp_k)));
                        g.emissivity = float(pick<double>(lw, {"emissivity"}, double(g.emissivity)));
                        // 谱批次: 分波段发射率 (缺省 0 → 打包时回退单值 emissivity)
                        g.radar.eps_mwir = float(pick<double>(lw, {"emissivity_mwir"}, double(g.radar.eps_mwir)));
                        g.radar.eps_lwir = float(pick<double>(lw, {"emissivity_lwir"}, double(g.radar.eps_lwir)));
                    }
                    // 谱批次: 反照率 reflectance{tv,swir} + 镜面 specular{strength,roughness}
                    // (缺省 → Kirchhoff ρ=1-ε / 朗伯, 旧场景 JSON 零配置兼容)
                    if (fg.contains("reflectance") && fg.at("reflectance").is_object()) {
                        const auto& rf = fg.at("reflectance");
                        g.radar.rho_tv = float(pick<double>(rf, {"tv"}, double(g.radar.rho_tv)));
                        g.radar.rho_swir = float(pick<double>(rf, {"swir"}, double(g.radar.rho_swir)));
                    }
                    if (fg.contains("specular") && fg.at("specular").is_object()) {
                        const auto& sp = fg.at("specular");
                        g.radar.spec_strength = float(pick<double>(sp, {"strength"}, double(g.radar.spec_strength)));
                        g.radar.spec_roughness = float(pick<double>(sp, {"roughness"}, double(g.radar.spec_roughness)));
                    }
                    // radar_x_params (B2): rcs_lut_id / 复介电常数 / 地形 sigma0
                    if (fg.contains("radar_x_params") && fg.at("radar_x_params").is_object()) {
                        const auto& rx = fg.at("radar_x_params");
                        g.radar.rcs_lut_id = pick<int>(rx, {"rcs_lut_id"}, g.radar.rcs_lut_id);
                        g.radar.dielectric_real = float(pick<double>(rx, {"dielectric_real"}, double(g.radar.dielectric_real)));
                        g.radar.dielectric_imag = float(pick<double>(rx, {"dielectric_imag"}, double(g.radar.dielectric_imag)));
                        g.radar.sigma0_db = float(pick<double>(rx, {"sigma0_db"}, double(g.radar.sigma0_db)));
                        g.radar.surface_rough = float(pick<double>(rx, {"surface_rough"}, double(g.radar.surface_rough)));
                        g.radar.dielectric_model = pick<int>(rx, {"dielectric_model"}, g.radar.dielectric_model);   // 批次4: 0/1/2/3
                    }
                    o.thermal_groups.push_back(std::move(g));
                }
            }
            if (oj.contains("thermal")) {
                const auto& tj = oj.at("thermal");
                o.fallback_temp_k = float(pick<double>(tj, {"base_temp_k", "base_temp"},
                                                       double(o.fallback_temp_k)));
                if (tj.contains("parts") && tj.at("parts").is_array()) {
                    for (const auto& part : tj.at("parts")) {
                        const std::string name = part.value("face_group", std::string());
                        for (auto& g : o.thermal_groups) {
                            if (g.group_id != name) continue;
                            g.temp_k = float(pick<double>(part, {"temp_k", "base_temp_k"}, double(g.temp_k)));
                            g.emissivity = float(pick<double>(part, {"emissivity"}, double(g.emissivity)));
                            g.part_override = true;   // P4: part 命中标记
                        }
                    }
                }
                // P4 工况倍率表 (设计器 A2 写盘格式: JSON 对象 {"名": 系数})
                if (tj.contains("states") && tj.at("states").is_object()) {
                    for (auto it = tj.at("states").begin(); it != tj.at("states").end(); ++it) {
                        if (it.value().is_number())
                            o.thermal_states[it.key()] = float(it.value().get<double>());
                    }
                }
            }
            // 谱批次: 单值 ε 镜像进材质 (parts 覆盖后同步 —— Kirchhoff 推导基准)
            for (auto& g : o.thermal_groups) g.radar.emissivity = g.emissivity;
            o.fallback_radar.emissivity = o.fallback_emissivity;

            if (!o.mesh_path.empty()) {
                o.mesh_path = resolveMeshPath(path_, o.mesh_path);
                ++data_.mesh_count;
            }
            data_.objects.push_back(std::move(o));
        }
    }

    // geo_anchor 缺省但对象带 position_geo → 首个作为锚点 (无第二次坐标换算:
    // 其余对象仍用 ENU 直读位置; 锚点仅用于地理标注一致性)
    if (!data_.anchor.valid && !data_.objects.empty()) {
        // 已在对象循环中因 anchor 无效而未换算; 此处保留 valid=false,
        // 场景以 ENU 直读坐标运行 (既有场景 JSON 的工作方式)。
    }

    // ---- sensors.sensor_list (§14.3, B1 场景驱动设备注册) ----
    parseSensorList(data_, j);

    // ---- P4: 顶层缺省工况名 (CLI --thermal-state 优先于该值, sensor_viewer 决定) ----
    if (j.contains("thermal_state") && j.at("thermal_state").is_string())
        data_.default_thermal_state = j.at("thermal_state").get<std::string>();

    // ---- 地面材质: 顶层 ground{temp_k,emissivity} 为缺省, terrain 对象
    //      首面组命中时覆盖 (JSON 同名映射; 范围随对象分布自适应) ----
    if (j.contains("ground") && j.at("ground").is_object()) {
        const auto& gj = j.at("ground");
        data_.ground.temp_k = float(pick<double>(gj, {"temp_k", "base_temp_k"}, double(data_.ground.temp_k)));
        data_.ground.emissivity = float(pick<double>(gj, {"emissivity"}, double(data_.ground.emissivity)));
    }
    for (const auto& o : data_.objects) {
        if (o.isTerrain(data_.terrain_ref) && !o.thermal_groups.empty()) {
            data_.ground.temp_k = o.thermal_groups.front().temp_k;
            data_.ground.emissivity = o.thermal_groups.front().emissivity;
            data_.ground.radar = o.thermal_groups.front().radar;   // sigma0_db/surface_rough (B2)
            data_.ground.radar.emissivity = data_.ground.emissivity;   // 谱批次: ε 镜像同步
            break;
        }
    }

    return {true, {}};
}

// ===========================================================================
// buildThermalSceneGraph
// ===========================================================================
bool flattenMeshToTriangles(vsg::ref_ptr<vsg::Node> node, std::vector<vsg::vec3>& out,
                            std::vector<vsg::vec2>* out_uv, std::vector<uint32_t>* out_slot) {
    if (!node) return false;
    TriFlatten fl;
    fl.out = &out;
    fl.out_uv = out_uv;
    fl.out_slot = out_slot;
    node->accept(fl);
    if (fl.skipped > 0)
        std::fprintf(stderr, "[SceneSource] 警告: %u 个 draw 缺位置数组/索引, 已跳过\n", fl.skipped);
    return !out.empty();
}

ThermalScene buildThermalSceneGraph(const SceneData& data, MeshLoader loader,
                                    TerrainProvider terrainProvider,
                                    const std::string& thermalState,
                                    OceanProvider oceanProvider,
                                    TextureLuminance texLumFn,
                                    float solarHeatK,
                                    GeoElevationQuery elevationQuery) {
    // ---- 累积器 (uint 索引, 大网格安全; 画家序: 地面 → 对象 JSON 序) ----
    std::vector<vsg::vec3> pos;
    std::vector<vsg::vec2> props;
    std::vector<uint32_t> idx;
    std::vector<uint32_t> matIdx;                 // B2: 顶点材质索引 (RT 材质表)
    std::vector<vsg::vec2> uvs;                   // TV 彩色: 逐顶点 uv (binding3)
    std::vector<uint32_t> slotIdx;                // TV 彩色: 逐顶点贴图槽号 (binding4)
    std::vector<RadarMaterial> materials;         // B2: RT 材质表 ([0]=地形)
    std::vector<MeshSlotInfo> texSlots;           // TV 彩色: 全局贴图槽表 ([0]=缺省)
    std::map<std::string, uint32_t> texSlotByKey; // map_kd → 槽号 (跨网格去重)
    std::map<const void*, uint32_t> matSlot;      // 面组/兜底指针 → 材质索引 (去重)
    vsg::dvec3 bmin(1e30, 1e30, 1e30), bmax(-1e30, -1e30, -1e30);
    uint32_t tris = 0;

    auto materialFor = [&](const void* key, const RadarMaterial& m) -> uint32_t {
        auto it = matSlot.find(key);
        if (it != matSlot.end()) return it->second;
        const uint32_t id = uint32_t(materials.size());
        materials.push_back(m);
        matSlot.emplace(key, id);
        return id;
    };
    materials.push_back(data.ground.radar);       // [0] = 地形材质
    texSlots.push_back(MeshSlotInfo{});           // [0] = 无贴图缺省槽

    auto addVertex = [&](const vsg::dvec3& p, float T, float eps, uint32_t mat,
                         vsg::vec2 uv = vsg::vec2(0.0f, 0.0f), uint32_t slot = 0) {
        pos.push_back(vsg::vec3(float(p.x), float(p.y), float(p.z)));
        props.push_back(vsg::vec2(T, eps));
        matIdx.push_back(mat);
        uvs.push_back(uv);
        slotIdx.push_back(slot);
        bmin = {std::min(bmin.x, p.x), std::min(bmin.y, p.y), std::min(bmin.z, p.z)};
        bmax = {std::max(bmax.x, p.x), std::max(bmax.y, p.y), std::max(bmax.z, p.z)};
    };
    auto addTri = [&](const vsg::dvec3& a, const vsg::dvec3& b, const vsg::dvec3& c,
                      float T, float eps, uint32_t mat,
                      vsg::vec2 uvA = vsg::vec2(0.0f, 0.0f),
                      vsg::vec2 uvB = vsg::vec2(0.0f, 0.0f),
                      vsg::vec2 uvC = vsg::vec2(0.0f, 0.0f),
                      uint32_t slot = 0) {
        const uint32_t base = uint32_t(pos.size());
        addVertex(a, T, eps, mat, uvA, slot);
        addVertex(b, T, eps, mat, uvB, slot);
        addVertex(c, T, eps, mat, uvC, slot);
        idx.push_back(base); idx.push_back(base + 1); idx.push_back(base + 2);
        ++tris;
    };
    auto addQuad = [&](const vsg::dvec3& a, const vsg::dvec3& b, const vsg::dvec3& c,
                       const vsg::dvec3& d, float T, float eps, uint32_t mat) {
        addTri(a, b, c, T, eps, mat);
        addTri(a, c, d, T, eps, mat);
    };
    auto addBox = [&](const vsg::dvec3& c, const vsg::dvec3& h, float T, float eps, uint32_t mat) {
        const double x0 = c.x - h.x, x1 = c.x + h.x;
        const double y0 = c.y - h.y, y1 = c.y + h.y;
        const double z0 = c.z - h.z, z1 = c.z + h.z;
        addQuad({x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1}, T, eps, mat);   // 底
        addQuad({x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1}, T, eps, mat);   // 顶
        addQuad({x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0}, T, eps, mat);   // -z
        addQuad({x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}, T, eps, mat);   // +z
        addQuad({x0,y0,z0},{x0,y0,z1},{x0,y1,z1},{x0,y1,z0}, T, eps, mat);   // -x
        addQuad({x1,y0,z0},{x1,y0,z1},{x1,y1,z1},{x1,y1,z0}, T, eps, mat);   // +x
    };

    // ---- 地面三级链: ① TerrainProvider (.vsgterrain 高度场) →
    //      ② rocky 地表高程网格 (GeoElevationQuery 桥 + geo_anchor) →
    //      ③ 常高平面兜底 (无锚点/无查询, 近似) ----
    {
        // 平面/网格范围: 随对象分布自适应 (原 GroundDesc.half_extent 语义内联化)
        double maxR = 0.0;
        for (const auto& o : data.objects)
            maxR = std::max(maxR, std::sqrt(o.position_enu.x * o.position_enu.x +
                                            o.position_enu.z * o.position_enu.z));
        const double he = std::clamp(maxR * 1.5 + 100.0, 500.0, 20000.0);
        bool groundFromProvider = false;
        if (terrainProvider) {
            TerrainMeshData tm;
            if (terrainProvider(tm) && tm.vertices.size() >= 3) {
                const float T = tm.temp_k > 0.0f ? tm.temp_k : data.ground.temp_k;
                const float E = tm.emissivity > 0.0f ? tm.emissivity : data.ground.emissivity;
                const size_t nTri = tm.vertices.size() / 3;
                for (size_t t = 0; t < nTri; ++t) {
                    // 批次3: 逐顶点热属性命中 → 覆盖整网格缺省 (空数组 = 旧行为)
                    for (int k = 0; k < 3; ++k) {
                        const size_t v = 3 * t + size_t(k);
                        const float vT = (v < tm.vertex_temp_k.size() && tm.vertex_temp_k[v] > 0.0f)
                                             ? tm.vertex_temp_k[v] : T;
                        const float vE = (v < tm.vertex_emissivity.size() && tm.vertex_emissivity[v] > 0.0f)
                                             ? tm.vertex_emissivity[v] : E;
                        addVertex(vsg::dvec3(tm.vertices[v]), vT, vE, 0u);
                    }
                    const uint32_t base = uint32_t(pos.size()) - 3;
                    idx.push_back(base); idx.push_back(base + 1); idx.push_back(base + 2);
                    ++tris;
                }
                groundFromProvider = true;
                std::printf("[SceneSource] 地面: TerrainProvider 地形网格 %zu 三角 "
                            "(T=%.1fK ε=%.2f, 真实高程进 RT)\n", nTri, T, E);
            } else {
                std::printf("[SceneSource] 地面: TerrainProvider 未提供网格 → 高程链下一级\n");
            }
        }
        // ② rocky 地表高程网格: 锚点 ENU 平面 + 逐顶点真实椭球高 (遮挡/地平
        //    语义随真实地形; 查询失败点回落锚点高)。
        if (!groundFromProvider && elevationQuery && data.anchor.valid) {
            using namespace simcore::geo;
            GeoAnchor anchor;
            anchor.lat_deg = data.anchor.lat_deg; anchor.lon_deg = data.anchor.lon_deg;
            anchor.alt_m = data.anchor.alt_m; anchor.heading_deg = data.anchor.heading_deg;
            const auto localToWorld = makeEnuToWorldMatrix(anchor);
            const vsg::dvec3 E(localToWorld[0].x, localToWorld[0].y, localToWorld[0].z);
            const vsg::dvec3 U(localToWorld[1].x, localToWorld[1].y, localToWorld[1].z);
            const vsg::dvec3 Z(localToWorld[2].x, localToWorld[2].y, localToWorld[2].z);
            const vsg::dvec3 P0(localToWorld[3].x, localToWorld[3].y, localToWorld[3].z);
            constexpr int kN = 64;                              // 64×64 → 8k 三角
            const double cell = (2.0 * he) / kN;
            size_t miss = 0;
            auto elevAt = [&](int i, int j) -> double {
                const double x = -he + i * cell, z = -he + j * cell;
                const vsg::dvec3 flat = P0 + E * x + Z * z;     // 平面点 y=0 → ECEF
                const vsg::dvec3 g = ecefToGeodetic(flat.x, flat.y, flat.z);
                double elev = 0.0;
                if (elevationQuery(g.x, g.y, elev)) {
                    const auto surface = geodeticToECEF(g.x, g.y, elev);
                    return vsg::dot(surface - P0, U); // includes earth curvature in the local frame
                }
                ++miss;
                return 0.0;                                     // 查询失败 → 锚点高
            };
            std::vector<double> hgt(size_t(kN + 1) * (kN + 1));
            for (int j = 0; j <= kN; ++j)
                for (int i = 0; i <= kN; ++i)
                    hgt[size_t(j) * (kN + 1) + i] = elevAt(i, j);
            for (int j = 0; j < kN; ++j) {
                for (int i = 0; i < kN; ++i) {
                    const double x0 = -he + i * cell, x1 = x0 + cell;
                    const double z0 = -he + j * cell, z1 = z0 + cell;
                    const double h00 = hgt[size_t(j) * (kN + 1) + i];
                    const double h10 = hgt[size_t(j) * (kN + 1) + i + 1];
                    const double h01 = hgt[size_t(j + 1) * (kN + 1) + i];
                    const double h11 = hgt[size_t(j + 1) * (kN + 1) + i + 1];
                    addQuad({x0,h00,z0},{x1,h10,z0},{x1,h11,z1},{x0,h01,z1},
                            data.ground.temp_k, data.ground.emissivity, 0u);
                }
            }
            groundFromProvider = true;
            std::printf("[SceneSource] 地面: rocky 高程网格 %dx%d (半宽 %.0fm, T=%.1fK ε=%.2f, "
                        "查询失败 %zu 点→锚点高)\n", kN, kN, he, data.ground.temp_k,
                        data.ground.emissivity, miss);
        }
        if (!groundFromProvider) {
            // ③ 常高平面兜底 = 无真实高程/遮挡, RT 结果为近似
            // 海面场景: 兜底板必须沉到海平面以下 (sea_level-5m) —— 板与波面共面
            // 会在近场 z-fighting (低风速波幅≈0 时板 ε=0.92 顶替海面) 并在
            // 板外缘形成假地平带; 沉板后板仅作波隙背板。
            double h = data.ground.height;
            if (data.ocean.enabled)
                h = std::min(h, double(data.ocean.sea_level) - 5.0);
            addQuad({-he,h,-he},{he,h,-he},{he,h,he},{-he,h,he},
                    data.ground.temp_k, data.ground.emissivity, 0u);
            std::printf("[SceneSource] 地面: 常高平面兜底 (%.0fm 半宽 @y=%.1f, T=%.1fK ε=%.2f) —— "
                        "无锚点/无高程查询, RT/成像为平面近似\n", he, h, data.ground.temp_k,
                        data.ground.emissivity);
        }
    }

    // ---- 海面 (批次3: OceanProvider 命中 → 波面三角形汤进 RT, 相位冻结 t0) ----
    if (oceanProvider) {
        OceanMeshData om;
        if (oceanProvider(om) && om.vertices.size() >= 3) {
            const uint32_t waterMat = materialFor(&om.water_material, om.water_material);
            // 白沫材质: 发射率/反照率/rcs 任一指定 → 独立材质槽, 否则复用水面
            const bool foamSpecified = om.foam_material.emissivity > 0.0f ||
                                       om.foam_material.rho_tv > 0.0f ||
                                       om.foam_material.rcs_lut_id >= 0;
            const uint32_t foamMat = foamSpecified
                                         ? materialFor(&om.foam_material, om.foam_material)
                                         : waterMat;
            const size_t nTri = om.vertices.size() / 3;
            size_t foamTris = 0;
            for (size_t t = 0; t < nTri; ++t) {
                const bool isFoam = t < om.face_flags.size() && om.face_flags[t] != 0;
                if (isFoam) ++foamTris;
                const uint32_t mat = isFoam ? foamMat : waterMat;
                for (int k = 0; k < 3; ++k) {
                    const size_t v = 3 * t + size_t(k);
                    const float vT = (v < om.vertex_temp_k.size() && om.vertex_temp_k[v] > 0.0f)
                                         ? om.vertex_temp_k[v] : om.sea_temp_k;
                    const float vE = (v < om.vertex_emissivity.size() && om.vertex_emissivity[v] > 0.0f)
                                         ? om.vertex_emissivity[v] : 0.98f;
                    addVertex(vsg::dvec3(om.vertices[v]), vT, vE, mat);
                }
                const uint32_t base = uint32_t(pos.size()) - 3;
                idx.push_back(base); idx.push_back(base + 1); idx.push_back(base + 2);
                ++tris;
            }
            std::printf("[SceneSource] 海面: OceanProvider 波面网格 %zu 三角 "
                        "(海温 %.1fK, 白沫面 %zu, 相位冻结 t0)\n",
                        nTri, om.sea_temp_k, foamTris);
        } else {
            std::printf("[SceneSource] 海面: OceanProvider 未提供网格 → 无海面\n");
        }
    }

    // ---- 对象 (mesh 三角形汤 / 程序化盒体回退; 网格按路径缓存共享) ----
    std::map<std::string, MeshLoadResult> meshCache;
    // TV 彩色: map_kd 路径 → 全局贴图槽号 (跨网格去重共享, [0]=无贴图)
    auto texSlotFor = [&](const std::string& mapKd, const vsg::vec4& kd) -> uint32_t {
        if (mapKd.empty()) return 0;
        auto it = texSlotByKey.find(mapKd);
        if (it != texSlotByKey.end()) return it->second;
        const uint32_t id = uint32_t(texSlots.size());
        texSlots.push_back({mapKd, kd});
        texSlotByKey.emplace(mapKd, id);
        return id;
    };
    for (const auto& o : data.objects) {
        const auto firstVertex=pos.size();
        // P4 工况倍率: thermalState 命中该对象 states 表 → 全温度 × 系数
        // (优先级 CLI > 场景 JSON 顶层 > base 由调用方解析后传入)
        float stateMul = 1.0f;
        if (!thermalState.empty()) {
            auto it = o.thermal_states.find(thermalState);
            if (it != o.thermal_states.end()) stateMul = it->second;
        }
        auto groupForFace = [&](uint32_t face) -> std::tuple<float, float, uint32_t> {
            for (const auto& g : o.thermal_groups)
                if (face >= g.face_begin && face <= g.face_end)
                    return {g.temp_k * stateMul, g.emissivity, materialFor(&g, g.radar)};
            return {o.fallback_temp_k * stateMul, o.fallback_emissivity,
                    materialFor(&o.fallback_radar, o.fallback_radar)};
        };
        if (stateMul != 1.0f)
            std::printf("[SceneSource] %s: 工况 '%s' 温度倍率 %.2f "
                        "(面组+兜底温度已乘)\n",
                        o.object_id.c_str(), thermalState.c_str(), stateMul);

        const MeshLoadResult* mesh = nullptr;
        if (loader && !o.mesh_path.empty()) {
            auto it = meshCache.find(o.mesh_path);
            if (it == meshCache.end()) {
                MeshLoadResult m;
                if (loader(o.mesh_path, m))
                    it = meshCache.emplace(o.mesh_path, std::move(m)).first;
                else
                    std::fprintf(stderr, "[SceneSource] 网格加载失败: %s → 程序化盒体回退\n",
                                 o.mesh_path.c_str());
            }
            if (it != meshCache.end()) mesh = &it->second;
        }

        if (mesh && mesh->vertices.size() >= 3) {
            // 本网格槽号 → 全局贴图槽号重映射 (跨网格按 map_kd 去重共享)
            std::vector<uint32_t> slotRemap(mesh->slots.size(), 0);
            for (size_t s = 0; s < mesh->slots.size(); ++s)
                slotRemap[s] = texSlotFor(mesh->slots[s].map_kd, mesh->slots[s].kd);
            // B1: 槽号 → 贴图亮度 (懒计算缓存; <0 = 未知/无贴图 → 不调制)
            std::map<uint32_t, float> slotLum;
            auto lumForSlot = [&](uint32_t slot) -> float {
                auto it = slotLum.find(slot);
                if (it != slotLum.end()) return it->second;
                float lum = -1.0f;
                if (texLumFn && slot != 0 && slot < texSlots.size())
                    lum = texLumFn(texSlots[slot].map_kd);
                slotLum.emplace(slot, lum);
                return lum;
            };
            const size_t nTri = mesh->vertices.size() / 3;
            const size_t nUv = mesh->uvs.size();
            for (size_t t = 0; t < nTri; ++t) {
                // face_range 归一化: 已加载三角形 t → 声称面索引 (比例近似:
                // assimp 不保证 OBJ 面序; 精确面组烘焙 = 场景设计侧职责)
                const uint32_t face = (o.face_count > 0)
                                          ? uint32_t((double(t) * double(o.face_count)) / double(nTri))
                                          : 0;
                const auto [T, eps, mat] = groupForFace(face);
                const uint32_t slot = (t < mesh->tri_slots.size() &&
                                       mesh->tri_slots[t] < slotRemap.size())
                                          ? slotRemap[mesh->tri_slots[t]] : 0;
                // B1: 贴图亮度升温 —— 暗面 (沥青/深色屋顶) 白天吸热更热, 亮面
                // (混凝土/白漆) 反射偏冷; 面组均温上按贴图内容分层 → 红外细节
                float Tb = T;
                if (solarHeatK > 0.0f) {
                    const float lum = lumForSlot(slot);
                    if (lum >= 0.0f)
                        Tb = T + (1.0f - lum) * solarHeatK;
                }
                const vsg::dvec3 a = vsg::dvec3(mesh->vertices[3 * t + 0]) + o.position_enu;
                const vsg::dvec3 b = vsg::dvec3(mesh->vertices[3 * t + 1]) + o.position_enu;
                const vsg::dvec3 c = vsg::dvec3(mesh->vertices[3 * t + 2]) + o.position_enu;
                const vsg::vec2 uva = (3 * t + 0 < nUv) ? mesh->uvs[3 * t + 0] : vsg::vec2(0.0f, 0.0f);
                const vsg::vec2 uvb = (3 * t + 1 < nUv) ? mesh->uvs[3 * t + 1] : vsg::vec2(0.0f, 0.0f);
                const vsg::vec2 uvc = (3 * t + 2 < nUv) ? mesh->uvs[3 * t + 2] : vsg::vec2(0.0f, 0.0f);
                addTri(a, b, c, Tb, eps, mat, uva, uvb, uvc, slot);
            }
            std::printf("[SceneSource] %s: mesh %zu 三角, %zu 材质槽 (贴图 %zu), %zu 面组 (T=%.1fK ε=%.2f)\n",
                        o.object_id.c_str(), nTri, mesh->slots.size(),
                        texSlots.size() - 1, o.thermal_groups.size(),
                        o.thermal_groups.empty() ? o.fallback_temp_k : o.thermal_groups.front().temp_k,
                        o.thermal_groups.empty() ? o.fallback_emissivity
                                                 : o.thermal_groups.front().emissivity);
        } else {
            // 程序化盒体: type → 尺寸; 首面组温度 (面组映射仅网格有意义)
            const vsg::dvec3 half = proceduralHalfExtent(o.type);
            const vsg::dvec3 c = o.position_enu + vsg::dvec3(0.0, half.y, 0.0);
            const auto [T, eps, mat] = groupForFace(0);
            addBox(c, half, T, eps, mat);
            std::printf("[SceneSource] %s: 程序化盒体 (%s), T=%.1fK ε=%.2f @ ENU(%.1f, %.1f, %.1f)\n",
                        o.object_id.c_str(), o.type.c_str(), T, eps,
                        o.position_enu.x, o.position_enu.y, o.position_enu.z);
        }
        const auto rotation=simcore::headingRotation(o.rotation_deg.y,o.rotation_deg.x,o.rotation_deg.z);
        for (size_t i=firstVertex;i<pos.size();++i) {
            const auto p=o.position_enu + rotation*((vsg::dvec3(pos[i])-o.position_enu)*o.scale);
            pos[i]=vsg::vec3(p);
        }
    }

    if (pos.empty()) return {};
    bmin=vsg::dvec3(1e30,1e30,1e30); bmax=vsg::dvec3(-1e30,-1e30,-1e30);
    for (const auto& vertex:pos) {
        const vsg::dvec3 p(vertex);
        bmin={std::min(bmin.x,p.x),std::min(bmin.y,p.y),std::min(bmin.z,p.z)};
        bmax={std::max(bmax.x,p.x),std::max(bmax.y,p.y),std::max(bmax.z,p.z)};
    }

    // ---- VSG 组装 (单 draw, index 序 = 画家序; 无深度附件设计, 见 SensorPipeline) ----
    auto posArr = vsg::vec3Array::create(pos.size());
    auto propsArr = vsg::vec2Array::create(props.size());
    auto matArr = vsg::uintArray::create(matIdx.size());
    auto uvArr = vsg::vec2Array::create(uvs.size());
    auto slotArr = vsg::uintArray::create(slotIdx.size());
    auto idxArr = vsg::uintArray::create(idx.size());
    std::memcpy(posArr->dataPointer(), pos.data(), pos.size() * sizeof(vsg::vec3));
    std::memcpy(propsArr->dataPointer(), props.data(), props.size() * sizeof(vsg::vec2));
    std::memcpy(matArr->dataPointer(), matIdx.data(), matIdx.size() * sizeof(uint32_t));
    std::memcpy(uvArr->dataPointer(), uvs.data(), uvs.size() * sizeof(vsg::vec2));
    std::memcpy(slotArr->dataPointer(), slotIdx.data(), slotIdx.size() * sizeof(uint32_t));
    std::memcpy(idxArr->dataPointer(), idx.data(), idx.size() * sizeof(uint32_t));

    auto draw = vsg::VertexIndexDraw::create();
    // binding0=pos, binding1=props, binding2=mat_idx(B2), binding3=uv, binding4=tex_slot
    draw->assignArrays({posArr, propsArr, matArr, uvArr, slotArr});
    draw->assignIndices(idxArr);                     // uint32 → VK_INDEX_TYPE_UINT32
    draw->indexCount = static_cast<uint32_t>(idx.size());
    draw->instanceCount = 1;

    if (texSlots.size() > 1)
        std::printf("[SceneSource] 贴图槽表: %zu 槽 (含缺省, 跨网格去重)\n", texSlots.size());

    ThermalScene ts;
    ts.node = draw;
    ts.materials = std::move(materials);
    ts.tex_slots = std::move(texSlots);
    ts.bound_min = bmin;
    ts.bound_max = bmax;
    ts.has_bounds = true;
    ts.triangle_count = tris;
    return ts;
}

} // namespace sensor
} // namespace rockyenv
