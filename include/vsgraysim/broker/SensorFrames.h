#pragma once
/// @file SensorFrames.h
/// @brief 各传感器类型的独立帧结构定义。
///
/// 仿真软件根据时序调度器按时生成各装备的帧数据，每帧包含：
/// - 帧标识（frame_id, sim_time, timestamp_ns）
/// - 传感器标识（sensor_id, platform_id）
/// - 传感器参数（载波频率、PRF、带宽等）
/// - 平台状态（位置、姿态、速度）
/// - 数据载荷（复数回波/辐射亮度/强度图像）
/// - 质量标志（信噪比、状态标志）

#include <complex>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#include "vsgraysim/core/SensorTypes.h"

namespace vsgraysim {

// =============================================================================
// 极化方式
// =============================================================================
enum class Polarization : uint8_t {
    HH = 0,   // 水平发射-水平接收
    VV = 1,   // 垂直发射-垂直接收
    HV = 2,   // 水平发射-垂直接收
    VH = 3,   // 垂直发射-水平接收
};

// =============================================================================
// SAR 工作模式
// =============================================================================
enum class SarMode : uint8_t {
    STRIPMAP = 0,   // 条带模式
    SPOTLIGHT = 1,  // 聚束模式
};

/// Image-pixel coordinates supplied by a focused ISAR producer.
struct IsarImageAxes {
    double range_origin_m=0;
    double range_spacing_m=0;
    double cross_range_origin_m=0;
    double cross_range_spacing_m=0;
    bool illustrative=false;

    bool valid() const {
        return std::isfinite(range_origin_m)&&std::isfinite(range_spacing_m)&&range_spacing_m>0&&
               std::isfinite(cross_range_origin_m)&&std::isfinite(cross_range_spacing_m)&&cross_range_spacing_m!=0;
    }
};

// =============================================================================
// 雷达帧（按 PRF 生成，每帧对应一个 CPI）
// =============================================================================
struct RadarFrame {
    struct GroundSample {
        float bearing_deg=0,slant_range_m=0,power_relative=0;
        bool valid()const{
            return std::isfinite(bearing_deg)&&bearing_deg>=0.f&&bearing_deg<360.f&&
                std::isfinite(slant_range_m)&&slant_range_m>0.f&&std::isfinite(power_relative)&&power_relative>0.f;
        }
    };
    static constexpr uint32_t maxGroundSamples=4096;
    struct Receiver {
        double first_transmit_s=0;
        double noise_iq_rms=0;
        bool valid() const {
            return std::isfinite(first_transmit_s)&&first_transmit_s>=0&&
                   std::isfinite(noise_iq_rms)&&noise_iq_rms>=0;
        }
    };
    // === 帧标识 ===
    uint64_t    surface_revision = 0;
    uint64_t    frame_id = 0;           // 雷达帧序号
    double      sim_time = 0.0;         // 仿真时间：该 CPI 结束时刻 (s)
    uint64_t    timestamp_ns = 0;       // 仿真时统 (sim_time × 1e9, 供跨装备对齐)

    // === 传感器标识 ===
    uint32_t    sensor_id = 0;
    uint32_t    platform_id = 0;

    // === 雷达参数 ===
    float       carrier_hz = 0.0f;      // 载波频率 (Hz)
    float       prf = 0.0f;             // 脉冲重复频率 (Hz)
    float       pulse_width = 0.0f;     // 脉冲宽度 (s)
    float       bandwidth = 0.0f;       // 带宽 (Hz)
    uint32_t    coherent_pulses = 0;    // 本帧 CPI 包含的脉冲数
    Polarization pol = Polarization::VV;

    // === 探测参数 ===
    float       range_min = 0.0f;       // 距离门起始 (m)
    float       range_max = 0.0f;       // 距离门结束 (m)
    float       range_resolution = 0.0f;    // 距离分辨率 (m)
    float       velocity_resolution = 0.0f; // 速度分辨率 (m/s)
    float       beam_az_deg = 0.0f;     // B7: CPI 中心波束方位 (°, 罗盘角 0=北顺时针;
                                        //  显控 PPI 扫描线驱动, §7.2; 由 ENU 数学角映射)

    // === 平台状态 ===
    double      latitude = 0.0;         // 平台纬度 (deg)
    double      longitude = 0.0;        // 平台经度 (deg)
    double      altitude = 0.0;         // 平台高度 (m)
    float       roll = 0.0f;            // 横滚 (rad)
    float       pitch = 0.0f;           // 俯仰 (rad)
    float       yaw = 0.0f;             // 偏航 (rad)
    float       velocity_n = 0.0f;      // 北向速度 (m/s)
    float       velocity_e = 0.0f;      // 东向速度 (m/s)
    float       velocity_d = 0.0f;      // 地向速度 (m/s)

    // === 数据载荷 ===
    uint32_t    range_bins = 0;         // 距离单元数
    uint32_t    doppler_bins = 0;       // 多普勒单元数
    std::vector<std::complex<float>> rd_map;  // P0 为脉冲主序 I/Q，P2 为距离–多普勒图

    // === 质量标志 ===
    float       snr_db = 0.0f;
    uint32_t    quality_flags = 0;      // 0=正常, 1=数据截断, 2=干扰
    uint8_t     stage = 4;              // 产品分级 P0..P4 (0=scene 1=pupil 2=focal
                                        //  3=raw 4=image; 雷达对应 echo..product, §5)

    double sample_rate_hz = 0.0;       // physical fast-time clock; 0 = legacy metadata
    std::optional<Receiver> receiver;  // explicit CPI start and impulse-equivalent receiver model metadata
    // Present but empty means no ground-scattering hits during this observation.
    std::optional<std::vector<GroundSample>> ground;
    double rangeAtBin(uint32_t bin) const {
        const double spacing = sample_rate_hz > 0.0 ? 299792458.0 / (2.0 * sample_rate_hz)
            : (range_bins > 1 ? (range_max - range_min) / double(range_bins - 1) : 0.0);
        return range_min + double(bin) * spacing;
    }
    bool empty() const { return range_bins == 0 || doppler_bins == 0; }
    void clear() {
        range_bins = doppler_bins = 0;
        rd_map.clear();
    }
    void resize(uint32_t r, uint32_t d) {
        range_bins = r;
        doppler_bins = d;
        rd_map.resize(size_t(r) * d);
    }
};

// =============================================================================
// SAR 帧（按合成孔径时间生成，每帧对应一个测绘带）
// =============================================================================
struct SarFrame {
    // === 帧标识 ===
    uint64_t    surface_revision = 0;
    uint64_t    frame_id = 0;
    double      sim_time = 0.0;
    uint64_t    timestamp_ns = 0;       // 仿真时统 (sim_time × 1e9, 供跨装备对齐)

    // === 传感器标识 ===
    uint32_t    sensor_id = 0;
    uint32_t    platform_id = 0;

    // === SAR 参数 ===
    float       carrier_hz = 0.0f;
    float       prf = 0.0f;
    float       velocity = 0.0f;        // 平台速度 (m/s)
    float       altitude = 0.0f;        // 平台高度 (m)
    float       incidence_angle = 0.0f; // 入射角 (rad)
    float       swath_width = 0.0f;     // 测绘带宽度 (m)
    SarMode     mode = SarMode::STRIPMAP;
    uint32_t    looks = 1;              // 多视数

    // === 分辨率 ===
    float       range_resolution = 0.0f;
    float       azimuth_resolution = 0.0f;

    // === 平台状态 ===
    double      latitude = 0.0;
    double      longitude = 0.0;
    double      altitude_m = 0.0;
    float       roll = 0.0f;
    float       pitch = 0.0f;
    float       yaw = 0.0f;
    float       velocity_n = 0.0f;
    float       velocity_e = 0.0f;
    float       velocity_d = 0.0f;

    // === 数据载荷 ===
    uint32_t    range_samples = 0;
    uint32_t    azimuth_samples = 0;
    std::vector<std::complex<float>> echo_data;  // 复数回波

    // === 质量标志 ===
    float       snr_db = 0.0f;
    uint32_t    quality_flags = 0;
    uint8_t     stage = 4;              // 产品分级 P0..P4 (同 RadarFrame)
    float       sample_rate_hz = 0.0f; // physical fast-time clock, independent of FFT padding
    float       range_min = 0.0f;      // fast-time gate origin (m)
    std::optional<IsarImageAxes> isar_axes; // Present only with a focused image, never with a spectrum.

    bool hasFocusedIsar() const { return (stage==3||stage==4)&&isar_axes&&isar_axes->valid(); }
    bool empty() const { return range_samples == 0 || azimuth_samples == 0; }
    void clear() {
        range_samples = azimuth_samples = 0;
        echo_data.clear();
        isar_axes.reset();
    }
    void resize(uint32_t r, uint32_t a) {
        range_samples = r;
        azimuth_samples = a;
        echo_data.resize(size_t(r) * a);
    }
};

// =============================================================================
// 红外帧（按探测器积分时间生成，每帧对应一帧热像）
// =============================================================================
struct IrFrame {
    // === 帧标识 ===
    uint64_t    surface_revision = 0;
    uint64_t    frame_id = 0;
    double      sim_time = 0.0;
    uint64_t    timestamp_ns = 0;       // 仿真时统 (sim_time × 1e9, 供跨装备对齐)

    // === 传感器标识 ===
    uint32_t    sensor_id = 0;
    uint32_t    platform_id = 0;

    // === 红外参数 ===
    float       wavelength_min = 0.0f;  // 工作波段下限 (μm)
    float       wavelength_max = 0.0f;  // 工作波段上限 (μm)
    float       fov_horizontal = 0.0f;  // 水平视场 (rad)
    float       fov_vertical = 0.0f;    // 垂直视场 (rad)
    float       integration_time = 0.0f; // 积分时间 (s)
    float       netd = 0.0f;            // 噪声等效温差 (K)
    float       emissivity = 1.0f;      // 目标发射率 [0,1]
    float       atmospheric_trans = 1.0f; // 大气透过率 [0,1]

    // === 平台状态 ===
    double      latitude = 0.0;
    double      longitude = 0.0;
    double      altitude = 0.0;
    float       roll = 0.0f;
    float       pitch = 0.0f;
    float       yaw = 0.0f;

    // === 数据载荷 ===
    uint32_t    width = 0;
    uint32_t    height = 0;
    std::vector<float> radiance;        // 辐射亮度 (W/m²/sr)
    std::vector<float> temperature;     // 表观温度 (K)，可选

    // === 质量标志 ===
    float       snr_db = 0.0f;
    uint32_t    quality_flags = 0;      // 0=正常, 1=饱和, 2=NUC异常
    uint8_t     stage = 4;              // 产品分级 P0..P4 (载荷单位随级:
                                        //  shader producer: P0-P2 W/m²/sr,
                                        //  P3 full-well fraction, P4 normalized display)

    bool empty() const { return width == 0 || height == 0; }
    void clear() {
        width = height = 0;
        radiance.clear();
        temperature.clear();
    }
    void resize(uint32_t w, uint32_t h) {
        width = w;
        height = h;
        radiance.resize(size_t(w) * h);
    }
};

// =============================================================================
// 可见光帧（按曝光时间生成）
// =============================================================================
struct VisFrame {
    // === 帧标识 ===
    uint64_t    surface_revision = 0;
    uint64_t    frame_id = 0;
    double      sim_time = 0.0;
    uint64_t    timestamp_ns = 0;       // 仿真时统 (sim_time × 1e9, 供跨装备对齐)

    // === 传感器标识 ===
    uint32_t    sensor_id = 0;
    uint32_t    platform_id = 0;

    // === 可见光参数 ===
    float       wavelength = 0.0f;      // 中心波长 (nm)
    float       fov = 0.0f;             // 视场 (rad)
    float       exposure_time = 0.0f;   // 曝光时间 (s)
    float       gain = 1.0f;            // 增益

    // === 平台状态 ===
    double      latitude = 0.0;
    double      longitude = 0.0;
    double      altitude = 0.0;
    float       roll = 0.0f;
    float       pitch = 0.0f;
    float       yaw = 0.0f;

    // === 数据载荷 ===
    uint32_t    width = 0;
    uint32_t    height = 0;
    std::vector<float> intensity;       // 强度 (0-1 归一化)

    // === 质量标志 ===
    uint32_t    quality_flags = 0;
    uint8_t     stage = 4;              // 产品分级 P0..P4 (同 IrFrame)

    bool empty() const { return width == 0 || height == 0; }
    void clear() {
        width = height = 0;
        intensity.clear();
    }
    void resize(uint32_t w, uint32_t h) {
        width = w;
        height = h;
        intensity.resize(size_t(w) * h);
    }
};

} // namespace vsgraysim
