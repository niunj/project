/// @file ProductDispatch.cpp —— ProductFrame→传输帧映射 + 门控分发 (B5)。
///
/// 映射原则: 载荷直传 (pixels/iq 直映射, 无附加处理 —— 工程约定); 参数按
/// SensorDesc 同名映射; 传输帧缺省参数 (雷达脉宽/带宽) 与 RadarParams
/// 缺省一致, B6 场景 schema 扩展后随 SensorDesc 下发。
///
/// Publishing requires a complete host payload until a typed GPU-resource sink
/// is implemented. A residency flag alone cannot transfer ownership or data.

#include "product_dispatch/ProductDispatch.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace product_dispatch {

using rockyenv::sensor::ProductFrame;
using rockyenv::sensor::ProductStage;
using rockyenv::sensor::SensorDesc;

namespace {

constexpr double kC = 2.99792458e8;   // 光速 (m/s)
constexpr double kDeg2Rad = M_PI / 180.0;

/// 级名 → ProductStage (成像级名 + 雷达五段名双词表, 值域对齐 0..4)。
bool stageFromName(const std::string& s, ProductStage& out) {
    if (s == "scene" || s == "echo")            { out = ProductStage::Scene;  return true; }
    if (s == "pupil" || s == "range_comp")      { out = ProductStage::Pupil;  return true; }
    if (s == "focal" || s == "rd_map")          { out = ProductStage::Focal;  return true; }
    if (s == "raw"  || s == "complex_image")    { out = ProductStage::Raw;    return true; }
    if (s == "image" || s == "product")         { out = ProductStage::Image;  return true; }
    return false;
}

void setStages(ProductDispatcher& d, const std::vector<std::string>& names) {
    // 通过 csv 复用解析路径 (空 → 缺省 {image})
    std::string csv;
    for (const auto& n : names) {
        if (!csv.empty()) csv += ',';
        csv += n;
    }
    d.setStagesFromCsv(csv);
}

} // namespace

// ===========================================================================
// 帧型选择与公共段
// ===========================================================================

SinkKind sinkKindFor(const SensorDesc& desc) {
    const std::string& k = desc.frame_kind;
    if (k == "VIS")                       return SinkKind::Vis;
    if (k == "SWIR" || k == "MWIR" || k == "LWIR") return SinkKind::Ir;
    if (k == "SAR") return SinkKind::Sar;
    if (k == "RADAR") return (desc.type == "sar") ? SinkKind::Sar : SinkKind::Radar;
    return SinkKind::None;
}

void fillCommon(ProductFrame& pf, uint32_t sensor_id, uint32_t platform_id) {
    pf.sensor_id = sensor_id;
    pf.platform_id = platform_id;
}

// ===========================================================================
// 映射实现
// ===========================================================================

void mapToIr(const ProductFrame& pf, const SensorDesc& desc, vsgraysim::IrFrame& out) {
    out = vsgraysim::IrFrame{};
    out.frame_id = pf.frame_id;
    out.sim_time = pf.sim_time;
    out.timestamp_ns = pf.timestamp_ns;
    out.sensor_id = pf.sensor_id;
    out.platform_id = pf.platform_id;
    // λ (m → μm); desc 显式值优先, 否则按 kind 规范范围 (SensorApi.h BandSpec 同源)
    double lmin = desc.lambda_min, lmax = desc.lambda_max;
    if (lmin <= 0.0 || lmax <= 0.0) {
        if      (desc.frame_kind == "SWIR") { lmin = 1e-6; lmax = 3e-6; }
        else if (desc.frame_kind == "LWIR") { lmin = 8e-6; lmax = 14e-6; }
        else                                 { lmin = 3e-6; lmax = 5e-6; }
    }
    out.wavelength_min = float(lmin * 1e6);
    out.wavelength_max = float(lmax * 1e6);
    out.fov_horizontal = float(pf.fov_h_deg * kDeg2Rad);
    out.fov_vertical   = float(pf.fov_v_deg * kDeg2Rad);
    out.integration_time = float(pf.integration_s);
    out.netd = float(desc.netd_mk * 1e-3);   // mK → K (字段注释单位)
    out.width = pf.width;
    out.height = pf.height;
    out.radiance = pf.pixels;                // stage units: P0-P2 radiance, P3 full-well fraction, P4 normalized
    out.stage = uint8_t(pf.stage);
}

void mapToVis(const ProductFrame& pf, const SensorDesc& desc, vsgraysim::VisFrame& out) {
    out = vsgraysim::VisFrame{};
    out.frame_id = pf.frame_id;
    out.sim_time = pf.sim_time;
    out.timestamp_ns = pf.timestamp_ns;
    out.sensor_id = pf.sensor_id;
    out.platform_id = pf.platform_id;
    const double lmin = (desc.lambda_min > 0.0) ? desc.lambda_min : 0.38e-6;
    const double lmax = (desc.lambda_max > 0.0) ? desc.lambda_max : 0.78e-6;
    out.wavelength = float((lmin + lmax) * 0.5 * 1e9);   // 中心波长 (nm)
    out.fov = float(pf.fov_h_deg * kDeg2Rad);
    out.exposure_time = float(pf.integration_s);
    out.gain = 1.0f;
    out.width = pf.width;
    out.height = pf.height;
    out.intensity = pf.pixels;               // 直映射
    out.stage = uint8_t(pf.stage);
}

void mapToRadar(const ProductFrame& pf, const SensorDesc& desc, vsgraysim::RadarFrame& out) {
    out = vsgraysim::RadarFrame{};
    out.frame_id = pf.frame_id;
    out.sim_time = pf.sim_time;
    out.timestamp_ns = pf.timestamp_ns;
    out.sensor_id = pf.sensor_id;
    out.platform_id = pf.platform_id;
    out.carrier_hz = float((desc.wavelength > 0.0) ? kC / desc.wavelength : 9.4e9);
    out.prf = float((desc.pulse_rate > 0.0) ? desc.pulse_rate : desc.rate_hz);
    out.pulse_width = (pf.pulse_width_s > 0.0) ? float(pf.pulse_width_s) : 1e-6f;
    out.bandwidth = (pf.bandwidth_hz > 0.0) ? float(pf.bandwidth_hz) : 30e6f;
    out.sample_rate_hz = pf.sample_rate_hz > 0.0 ? pf.sample_rate_hz : out.bandwidth;
    out.coherent_pulses = desc.cpi_pulses;
    out.pol = vsgraysim::Polarization::VV;
    if (pf.range_max_m > 0.0) {
        out.range_min = float(pf.range_min_m);
        out.range_max = float(pf.range_max_m);
    } else {
        out.range_min = 0.0f;
        out.range_max = float(desc.range_m);
    }
    out.range_resolution = float(kC / (2.0 * out.bandwidth));
    const double t_cpi = (out.prf > 0.0) ? double(out.coherent_pulses) / out.prf : 0.0;
    out.velocity_resolution = float((t_cpi > 0.0) ? (kC / out.carrier_hz) / (2.0 * t_cpi) : 0.0);
    out.range_bins = pf.width;
    out.doppler_bins = pf.height;
    out.rd_map = pf.iq;                      // 直映射 (B6: 回波/RD 图形态随 stage)
    // B7 §7.2: CPI 中心波束方位 ENU 数学角 (0=East) → 罗盘 bearing (0=北顺时针):
    // bearing = deg(atan2(cos(az), −sin(az))) —— PPI 扫描线帧驱动数据源。
    out.beam_az_deg = float(std::fmod(
        std::atan2(std::cos(pf.beam_az_deg * kDeg2Rad),
                   -std::sin(pf.beam_az_deg * kDeg2Rad)) * (180.0 / M_PI) + 360.0,
        360.0));
    if (pf.iq.empty() && !pf.pixels.empty()) {
        // Product 幅度图 → 复数模 (传输帧统一 iq 形态, 虚部 0)
        out.rd_map.reserve(pf.pixels.size());
        for (float v : pf.pixels) out.rd_map.emplace_back(v, 0.0f);
    }
    out.stage = uint8_t(pf.stage);
}

void mapToSar(const ProductFrame& pf, const SensorDesc& desc, vsgraysim::SarFrame& out) {
    out = vsgraysim::SarFrame{};
    out.frame_id = pf.frame_id;
    out.sim_time = pf.sim_time;
    out.timestamp_ns = pf.timestamp_ns;
    out.sensor_id = pf.sensor_id;
    out.platform_id = pf.platform_id;
    out.carrier_hz = float((desc.wavelength > 0.0) ? kC / desc.wavelength : 9.4e9);
    out.prf = float((desc.pulse_rate > 0.0) ? desc.pulse_rate : desc.rate_hz);
    out.looks = 1;
    out.range_samples = pf.width;
    out.azimuth_samples = pf.height;
    out.echo_data = pf.iq;                   // 直映射 (复回波/复图像)
    out.sample_rate_hz=float(pf.sample_rate_hz);
    out.range_min=float(pf.range_min_m);
    out.stage = uint8_t(pf.stage);
}

// ===========================================================================
// ProductDispatcher
// ===========================================================================

void ProductDispatcher::setStagesFromCsv(const std::string& csv) {
    stages_.clear();
    stageSet_.clear();
    size_t pos = 0;
    while (pos <= csv.size()) {
        size_t comma = csv.find(',', pos);
        if (comma == std::string::npos) comma = csv.size();
        if (comma > pos) {
            std::string name = csv.substr(pos, comma - pos);
            ProductStage stg;
            if (stageFromName(name, stg) && !stageSet_.count(uint32_t(stg))) {
                stageSet_.insert(uint32_t(stg));
                stages_.push_back(stg);
            } else if (!name.empty()) {
                std::fprintf(stderr, "[ProductDispatch] 未知发布级 '%s' (忽略)\n", name.c_str());
            }
        }
        pos = comma + 1;
    }
    std::sort(stages_.begin(), stages_.end());
    // 空缺省: P4 image (显控使用)
    if (stages_.empty()) {
        stageSet_.insert(uint32_t(ProductStage::Image));
        stages_.push_back(ProductStage::Image);
    }
}

void ProductDispatcher::configure(const SensorDesc& desc) {
    desc_ = desc;
    kind_ = sinkKindFor(desc);
    setStages(*this, desc.stages);
}

bool ProductDispatcher::stageEnabled(ProductStage s) const {
    return stageSet_.count(uint32_t(s)) > 0;
}

bool ProductDispatcher::dispatch(const ProductFrame& pf) {
    if (!stageSet_.count(uint32_t(pf.stage))) { ++stats_.dropped_stage; return false; }
    if (!linked_)    { ++stats_.dropped_unlinked;  return false; }   // 链接门控: 未订阅不发布
    if (!powered_)   { ++stats_.dropped_unpowered; return false; }   // 供电门控: 断电停帧
    const size_t cells = size_t(pf.width) * size_t(pf.height);
    const bool image = kind_ == SinkKind::Ir || kind_ == SinkKind::Vis;
    const bool scalarRadar = kind_ == SinkKind::Radar && pf.stage == ProductStage::Image;
    if (pf.width == 0 || pf.height == 0 || pf.gpu_resident ||
        (image ? pf.pixels.size() != cells :
         (scalarRadar && !pf.pixels.empty() ? pf.pixels.size() != cells : pf.iq.size() != cells))) {
        ++stats_.dropped_invalid;
        return false;
    }
    bool accepted = false;
    switch (kind_) {
    case SinkKind::Ir:
        if (!onIr) return false;
        { vsgraysim::IrFrame f; mapToIr(pf, desc_, f); accepted = onIr(f); }
        break;
    case SinkKind::Vis:
        if (!onVis) return false;
        { vsgraysim::VisFrame f; mapToVis(pf, desc_, f); accepted = onVis(f); }
        break;
    case SinkKind::Radar:
        if (!onRadar) return false;
        { vsgraysim::RadarFrame f; mapToRadar(pf, desc_, f); accepted = onRadar(f); }
        break;
    case SinkKind::Sar:
        if (!onSar) return false;
        { vsgraysim::SarFrame f; mapToSar(pf, desc_, f); accepted = onSar(f); }
        break;
    case SinkKind::None:
        return false;
    }
    if (!accepted) { ++stats_.sink_rejected; return false; }
    ++stats_.published;
    return true;
}

} // namespace product_dispatch
