/// @file SensorInstance.cpp —— 传感器实例模型: SensorDesc → SensorProfile 解析
///            + 场景驱动多实例注册 (设计文档 §12c B1)。
#include "rocky_env/SensorInstance.h"

#include <cstdio>
#include <cstdlib>
#include <simcore/Orientation.h>

namespace rockyenv {
namespace sensor {

SensorProfile SensorRegistry::profileFromDesc(const SensorDesc& d) {
    SensorProfile p;
    p.name = d.name.empty() ? "sensor_" + std::to_string(d.sensor_id) : d.name;
    p.width = d.res_w;
    p.height = d.res_h;
    p.fov_deg = (d.fov_deg > 0.0) ? d.fov_deg : ((d.fov_h > 0.0) ? d.fov_h : 20.0);

    // kind → BandSpec (规范范围), 显式 λ (frame.lambda_min/max) 覆盖
    if (d.frame_kind == "VIS")       p.band = BandSpec::make(BandType::TV);
    else if (d.frame_kind == "SWIR") p.band = BandSpec::make(BandType::SWIR);
    else if (d.frame_kind == "LWIR") p.band = BandSpec::make(BandType::LWIR);
    else                             p.band = BandSpec::make(BandType::MWIR);
    if (d.lambda_min > 0.0) p.band.lambda_min = d.lambda_min;
    if (d.lambda_max > 0.0) p.band.lambda_max = d.lambda_max;

    p.optics = SensorOptics{d.f_number, d.transmission, d.psf_sigma_px, d.vignetting};
    p.detector = SensorDetector{d.pixel_pitch_um, d.qe, d.integration_ms,
                                d.netd_mk, d.well_ke, d.read_noise_e, d.bits};
    p.electronics = SensorElectronics{d.agc_low_pct, d.agc_high_pct, d.gamma};
    return p;
}

std::vector<SensorInstance> SensorRegistry::createFromScene(const SceneData& data) {
    std::vector<SensorInstance> out;
    out.reserve(data.sensors.size());
    for (const auto& d : data.sensors) {
        SensorInstance inst;
        inst.desc = &d;
        inst.profile = profileFromDesc(d);
        vsg::dquat rotation{0,0,0,1};

        if (const SceneObjectDesc* plat = data.findObject(d.platform)) {
            rotation=simcore::headingRotation(plat->rotation_deg.y,plat->rotation_deg.x,plat->rotation_deg.z);
            inst.position_enu = plat->position_enu + rotation*d.aperture_offset;
        } else {
            inst.position_enu = d.aperture_offset;   // 未绑定平台: ENU 原点安装
            std::fprintf(stderr,
                         "[registry] 传感器 %s 平台未绑定: '%s' (ENU 原点安装)\n",
                         d.name.c_str(), d.platform.c_str());
        }

        const double len = vsg::length(d.direction);
        inst.los_dir = rotation*((len > 1e-9) ? vsg::dvec3(d.direction / len) : vsg::dvec3(0.0, 0.0, -1.0));
        inst.renderable = (d.frame_kind == "VIS" || d.frame_kind == "SWIR" ||
                           d.frame_kind == "MWIR" || d.frame_kind == "LWIR");
        out.push_back(std::move(inst));
    }
    return out;
}

const SensorInstance* SensorRegistry::select(const std::vector<SensorInstance>& registry,
                                             const std::string& key) {
    if (registry.empty()) return nullptr;
    if (key.empty()) {
        // 缺省: 首个可渲染实例, 否则首个
        for (const auto& inst : registry)
            if (inst.renderable) return &inst;
        return &registry.front();
    }
    // 数字: 先按 sensor_id 精确匹配, 再按序号 (1 基)
    char* end = nullptr;
    const long num = std::strtol(key.c_str(), &end, 10);
    if (end && *end == '\0' && key.find_first_not_of("-0123456789") == std::string::npos) {
        for (const auto& inst : registry)
            if (inst.desc && inst.desc->sensor_id == int(num)) return &inst;
        if (num >= 1 && num <= long(registry.size())) return &registry[size_t(num) - 1];
        return nullptr;
    }
    // 名称匹配 (name / platform)
    for (const auto& inst : registry) {
        if (!inst.desc) continue;
        if (inst.desc->name == key || inst.desc->platform == key) return &inst;
    }
    return nullptr;
}

} // namespace sensor
} // namespace rockyenv
