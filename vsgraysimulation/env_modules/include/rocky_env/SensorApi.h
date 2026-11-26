#pragma once
/// @file SensorApi.h —— 多传感器仿真渲染: 数据层契约 (设计文档 §3-§6)。
///
/// 分层 (docs/design_3d_sensor_rendering.md):
///   共享资产层(外部) → BandRenderer(蓝方物理辐射) → SensorChain(红方信号级) → ProductFrame
/// 数据形态分型:
///   红外(SWIR/MWIR/LWIR) = 波段辐射亮度图 (W/m²/sr, 线性物理量)
///   可见光(TV)           = 辐亮度图 → DN 图
///   产品分级 P0..P4 = scene/pupil/focal/raw/image, 每级可停靠输出。
#ifndef ROCKY_ENV_SENSOR_API_H
#define ROCKY_ENV_SENSOR_API_H

#include <algorithm>
#include <complex>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace rockyenv {
namespace sensor {

// ===========================================================================
// BandSpec —— 波段抽象 (设计文档 §4.2)。波段是渲染器/材质/大气统一寻址键。
// ===========================================================================
enum class BandType : uint32_t {
    TV   = 0,   ///< 可见光电视 0.38–0.78 μm (反射+太阳辐照)
    SWIR = 1,   ///< 短波红外 1–3 μm    (太阳反射为主+夜辉)
    MWIR = 2,   ///< 中波红外 3–5 μm    (热辐射↑+反射混合, 普朗克峰 ~580K)
    LWIR = 3,   ///< 长波红外 8–14 μm   (纯热辐射, 普朗克峰 ~290K)
};

struct BandSpec {
    BandType type = BandType::MWIR;
    double lambda_min = 0.0;   ///< 下限 (m)
    double lambda_max = 0.0;   ///< 上限 (m)

    static BandSpec make(BandType t);
    /// 显示名 (MWIR/LWIR/...)
    const char* name() const;
};

// ===========================================================================
// 物理常量与普朗克波段积分 (蓝方物理, CPU 预计算 LUT)
// ===========================================================================
/// 普朗克波段出射度 LUT: T(kK 网格) → M_band (W/m², 半球出射度)。
/// 辐亮度 L = ε·M_band(T)/π。每波段一张, GPU 以 1D 纹理采样 (sensor_rad.frag)。
class BandRadianceLut {
public:
    bool build(const BandSpec& band);
    /// CPU 查询 (调试/基准): T 开尔文 → 波段出射度 W/m²
    float exitance(float T) const;
    const BandSpec& band() const { return band_; }
    /// GPU 1D 数据: vec4(M_band, dM/dT, 0, 0), N 点均匀网格
    const std::vector<float>& table() const { return table_; }
    uint32_t samples() const { return uint32_t(table_.size()); }
    float tMin() const { return T_MIN_; }   ///< K
    float tMax() const { return T_MAX_; }   ///< K

private:
    BandSpec band_{};
    std::vector<float> table_;   // N 点, 线性网格 [T_MIN_, T_MAX_]
    float T_MIN_ = 150.0f;
    float T_MAX_ = 1500.0f;
};

/// 大气参数 (设计文档 §6 环境契约: EnvRequest/EnvAnswer)。
/// B3 起 EnvPhysicsService 从 rocky_env 环境栈 (sky/cloud, AtmosphereState +
/// ICloudQuery) 映射产出; 无环境栈时回退 clearSkyEnv 晴空模型。
struct EnvRequest {
    BandSpec band{};               ///< 请求波段
    double fov_h_deg = 0.0;        ///< 水平视场 (元数据)
    double fov_v_deg = 0.0;        ///< 垂直视场 (元数据)
    double los_az_deg = 0.0;       ///< 视线方位 (°, 机体系; B7 GimbalModel 接入后驱动)
    double los_el_deg = 0.0;       ///< 视线俯仰 (°)
    double lat = 25.078;           ///< 观测点纬度 (°)
    double lon = 121.772;          ///< 观测点经度 (°)
    double alt = 0.0;              ///< 观测点高程 (m)
    double sim_time = 0.0;         ///< 仿真时刻 (s, 帧对齐)
    double hours_utc = 4.0;        ///< UTC 时刻 (h, 驱动昼夜)
};

struct EnvAnswer {
    double sigma_per_km = 0.0;     ///< 波段消光系数 (1/km)
    double sky_radiance = 0.0;     ///< 天空背景辐亮度 (W/m²/sr)
    double sun_irradiance = 0.0;   ///< 直射辐照度 (W/m²) — TV 用
    // ---- 太阳方位/俯仰 (°, 局部系; 真北=0，顺时针向东，+Z=-North)。
    //      缺省值与旧管线 sunDirEnu_ 硬编码 (0.3,0.8,0.2) 归一化方向一致,
    //      GPU UBO 打包时经 dirFromAzEl 还原为 ENU 单位向量 ----
    double sun_az_deg = 123.69;
    double sun_el_deg = 65.75;
    // ---- B3 扩展 (EnvPhysicsService 填充; clearSkyEnv 回退时为缺省) ----
    double path_radiance = 0.0;    ///< 路径辐射基准 (W/m²/sr; shader 内 (1-τ)·sky 同源)
    double cloud_coverage = 0.0;   ///< 总云量 0..1 (与云模块画面同源)
    double visibility_m = 10000.0; ///< 能见度 (m)
    double is_day = 1.0;           ///< 昼因子 1..0 (AtmosphereState 同源)
    int    weather_type = 0;       ///< 0 晴 / 1 少云 / 2 多云 / 3 阴 / 4 降水
    // ---- 批次3 (海洋真值): 与海面渲染同源 (IOceanSampler); 无海洋时 0/-1 ----
    double sea_surface_temp_k = 0.0;   ///< 海表温度 (K); 0 = 场景无海洋
    double foam_fraction = 0.0;        ///< 代表性白沫覆盖率 0..1 (原点周边均值)
    int    beaufort = -1;              ///< 蒲福风级 0..12; -1 = 无海洋
    double thermal_local_hour = 12.0;
    double thermal_air_delta_k = 0.0;
    double thermal_wetness = 0.0;
    double thermal_scene_seconds = 0.0;
    /// 沿路径透过率 τ(R) = exp(-σ·R_km)
    static double transmittance(double sigma_per_km, double range_m);
};

/// 方位/俯仰 (°) → ENU 单位指向 (X=East, Y=Up, Z=-North; 与场景设计器 azElToDir 同约定)。
/// 批次1: EnvAnswer 太阳角 → GPU env UBO env2 段打包的单一真值换算。
inline void dirFromAzEl(double az_deg, double el_deg, double out[3]) {
    const double azr = az_deg * M_PI / 180.0;
    const double elr = el_deg * M_PI / 180.0;
    const double ce = std::cos(elr);
    out[0] = std::sin(azr) * ce;
    out[1] = std::sin(elr);
    out[2] = -std::cos(azr) * ce;
}

/// 晴空大气模型: 波段消光系数/天空辐亮度 (clearSkyEnv 回退基线; 有环境栈时由
/// EnvPhysicsService 在此基线上做云/昼夜/能见度调制)
EnvAnswer clearSkyEnv(const BandSpec& band, bool night);

/// 波段积分出射度 M_band(T) = ∫ M_λ dλ (W/m², Simpson 128 点)。
/// 批次2导出: SkyModule::bandSky 复用 (大气柱热发射 / 太阳 5778K 波段积分)。
double bandExitance(double lambda_min, double lambda_max, double T);

/// 气溶胶消光 (Ångström 幂律): λc μm 处, 基准 λ0=0.55μm Koschmieder。
/// 批次2导出: SkyModule::bandSky 与 EnvPhysicsService 共用同一实现 (单一真值)。
inline double aerosolSigma(double visibility_m, double lambdaC_um) {
    const double visKm = std::max(0.5, visibility_m * 1e-3);
    const double sigma55 = 3.912 / visKm;                 // Koschmieder (0.55μm)
    const double alpha = 1.3;                              // 大陆型气溶胶
    return sigma55 * std::pow(0.55 / std::max(0.1, lambdaC_um), alpha);
}

// ===========================================================================
// 第九轮 IR 逐谱 LUT —— 波段平均标量 → 传感器响应 × 波段内逐谱积分。
//   IR 波段内大气窗口/吸收带结构强 (MWIR 3-5μm 含 CO2 4.3μm 强吸收带,
//   LWIR 8-14μm 含 O3 9.6μm 带 + 14μm 水汽翼), 平坦波段平均把吸收带与
//   窗口混为灰体; 逐谱化后: M_S(T) 随响应窗口收缩、天空柱发射随气团在
//   吸收带先饱和 (地平趋黑体/天顶保窗口透过) —— 水平/垂直对比结构更真。
//   回归锚: VSIM_IR_SPECTRAL=0 走旧平坦路径 (逐位回归)。
// ===========================================================================
/// 逐谱开关 (默认开; VSIM_IR_SPECTRAL=0 关闭 → 全链旧平坦标量, 回归锚)
bool irSpectralOn();

/// 传感器参考响应 S(λ) (0..1, λ 单位 μm): 超高斯(逻辑斯蒂)边沿窗口 ——
///   MWIR (InSb): 3.6..4.8 μm, 边沿宽 0.15 μm
///   LWIR (HgCdTe/热敏): 8.5..13.0 μm, 边沿宽 0.40 μm
///   TV/SWIR: 平坦 1.0 (不改动, 其天空基线为标定常量)
double sensorResponse(BandType t, double lambda_um);

/// 波段内吸收带形状 G(λ) (峰值 1, λ 单位 μm):
///   MWIR: CO2 4.30μm (σ=0.12) + H2O 6.3μm 翼 (向 5μm 边沿爬升)
///   LWIR: O3 9.6μm (σ=0.30) + H2O 转动带翼 (向 14μm 边沿爬升)
double bandAbsorptionShape(BandType t, double lambda_um);

/// 响应加权波段出射度 M_S(T) = ∫ S̃(λ)·M_λ(T) dλ (W/m², Simpson 128 点)。
/// S̃ = S·Δλ/∫S (均值保持归一化: ∫S̃dλ = Δλ) —— 平坦响应时与 bandExitance
/// 逐位一致 (能量守恒); M_S 即"传感器测得的波段出射度", 消费端语义不变。
double bandExitanceSR(const BandSpec& band, double T);

/// 逐谱天空 LUT (每波段一份, 首次构建后缓存): 预积分权值表把 bandSky 的
/// 逐谱积分降为 128 点加权和 + 1 次 exp —— 天图烘焙 (256×128×每帧) 可负担。
///   emitW[i]: S̃(λi)·M_λ(T_atm)·Simpson 权 (Σ = M_S(T_atm))
///   sunW[i]:  S̃(λi)·M_λ(5778K)·Simpson 权 (Σ = M_S(5778K), esun 逐谱)
///   shape[i]: G(λi) 吸收形状
///   tauW:     窗口光学深度 —— 由天顶带平均发射率 eps0 基线反解 (二分标定,
///             基线自洽: 天顶 L_sky = eps0·M_S/π = 晴空基线 sky_radiance)
struct SpectralSkyLut {
    bool   ready = false;
    double tauA = 0.0;        ///< 吸收带峰值光学深度 (波段常数)
    double tauW = 0.0;        ///< 窗口光学深度 (天顶基线标定)
    double emitW[129] = {};   ///< Simpson 128 区间 → 129 节点
    double emitS = 0.0;
    double sunW[129]  = {};
    double shape[129] = {};
};
/// 构建/查询 (tAtmK: 该波段大气柱有效温度 MWIR=250 / LWIR=263)
const SpectralSkyLut& spectralSkyLut(BandType t, double tAtmK);
/// 窗口光学深度 τ_w 天顶基线标定 (按 BandType 缓存; eps0 = 天顶带平均发射率)
double spectralTauW(BandType t, double eps0);

/// λ 中心波长归类标准波段 (bandSky 以任意 λ 区间为契约; 实际调用方均为
/// BandSpec::make 的标准波段, 边界取相邻波段中心中点)。
inline BandSpec bandSpecFromLambda(double lmin_m, double lmax_m) {
    const double lc = 0.5 * (lmin_m + lmax_m);
    BandType t = BandType::LWIR;
    if (lc < 0.9e-6)       t = BandType::TV;     // TV 中心 0.58 / SWIR 中心 2.0
    else if (lc < 3.0e-6)  t = BandType::SWIR;
    else if (lc < 6.5e-6)  t = BandType::MWIR;   // MWIR 中心 4.0 / LWIR 中心 11.0
    return BandSpec::make(t);
}

// ===========================================================================
// SensorProfile —— 传感器参数 (红方信号级链, JSON 可配, 分组同属性表)
// ===========================================================================
struct SensorOptics {           // 光学 (P1→P2)
    double f_number = 4.0;      ///< F# (口径/焦距比) — 与 well_ke 标定一致:
                                ///< F/4 下 LWIR 常温地面 raw≈0.55 / MWIR 尾焰饱和, 动态合理
    double transmission = 0.85; ///< 光学透过率 [0,1]
    double psf_sigma_px = 0.9;  ///< 高斯 PSF σ (像素) — MTF 近似
    double vignetting = 0.15;   ///< 渐晕强度 (cos⁴ 近似系数 [0,1])
};
struct SensorDetector {         // 探测器 (P2→P3)
    double pixel_pitch_um = 15.0;   ///< 像元间距 (μm)
    double qe = 0.7;            ///< 量子效率 [0,1]
    double integration_ms = 8.0;///< 积分时间 (ms)
    double netd_mk = 50.0;      ///< NETD (mK) — 噪声等效温差
    double well_ke = 20000.0;   ///< 满阱 (ke-) — 制冷 MWIR 典型 2e7 e-,
                                ///< 保证 F/4+15μm+8ms 下常温天空 (L~0.065) 不饱和
    double read_noise_e = 80.0; ///< 读出噪声 (e-)
    uint32_t bits = 14;         ///< ADC 位数
};
struct SensorElectronics {      // 电子学 (P3→P4)
    double agc_low_pct = 0.002;   ///< AGC 下限分位 — 只裁极端离群 (目标占比<2% 时
                                  ///< 0.98 分位会把 x_hi 拉到地面亮端, 淹没目标)
    double agc_high_pct = 0.998;  ///< AGC 上限分位 — 覆盖引擎/尾焰高值, 主体保留动态
    double gamma = 1.0;         ///< Gamma (1.0=线性)
};

struct SensorProfile {
    BandSpec band{};
    uint32_t width = 640;
    uint32_t height = 512;
    double fov_deg = 20.0;      ///< 水平视场
    SensorOptics optics{};
    SensorDetector detector{};
    SensorElectronics electronics{};
    std::string name = "ir01";
};

/// 探测器噪声等效: NETD(mK) → 目标温差噪声 → 辐亮度噪声 (W/m²/sr)
/// NEDL ≈ dL/dT · NETD ; dL/dT = ε·dM/dT/π (LUT 梯度)
double nedlFromNetd(const BandRadianceLut& lut, double emissivity,
                    double t_ref_k, double netd_k);

// ===========================================================================
// SensorFrameType —— 产品帧类型 (设计文档 §5; 产品决策 2026-09 固化)。
//   VIS/IR: 3D 模块完成光学物理仿真+成像处理, 输出成品图像数据 (全链在本侧);
//   RADAR : 仅预留接口与处理段骨架, 仿真/显控处理分界待定 (设计文档 §14)。
// ===========================================================================
enum class SensorFrameType : uint32_t {
    VIS   = 0,   ///< 可见光 (TV 波段) 成品图像
    SWIR  = 1,   ///< 短波红外
    MWIR  = 2,   ///< 中波红外
    LWIR  = 3,   ///< 长波红外
    RADAR = 4,   ///< 雷达 (IQ/RD 图/复图像, 预留)
};

inline const char* sensorFrameTypeName(SensorFrameType t) {
    switch (t) {
        case SensorFrameType::VIS:   return "VIS";
        case SensorFrameType::SWIR:  return "SWIR";
        case SensorFrameType::MWIR:  return "MWIR";
        case SensorFrameType::LWIR:  return "LWIR";
        case SensorFrameType::RADAR: return "RADAR";
    }
    return "?";
}

/// BandType → 帧类型映射 (TV→VIS, 红外同名, 雷达另用 RADAR)
inline SensorFrameType sensorFrameTypeFromBand(BandType band) {
    switch (band) {
        case BandType::TV:   return SensorFrameType::VIS;
        case BandType::SWIR: return SensorFrameType::SWIR;
        case BandType::MWIR: return SensorFrameType::MWIR;
        case BandType::LWIR: return SensorFrameType::LWIR;
    }
    return SensorFrameType::MWIR;
}

// ===========================================================================
// ProductStage —— 产品分级 P0..P4 (设计文档 §5)。
//   通道按级发布: eq/<id>/<band>/<stage>, stage ∈ scene|pupil|focal|raw|image;
//   不订阅不计算。
// ===========================================================================
enum class ProductStage : uint32_t {
    Scene = 0,   ///< P0 场景辐射亮度 (蓝方图, 传感器无关)
    Pupil = 1,   ///< P1 入瞳前 (+投影几何)
    Focal = 2,   ///< P2 焦平面 (+光学)
    Raw   = 3,   ///< P3 full-well fraction (+detector)
    Image = 4,   ///< P4 图像 (+电子学, 显控使用)
};

inline const char* productStageName(ProductStage s) {
    switch (s) {
        case ProductStage::Scene: return "scene";
        case ProductStage::Pupil: return "pupil";
        case ProductStage::Focal: return "focal";
        case ProductStage::Raw:   return "raw";
        case ProductStage::Image: return "image";
    }
    return "?";
}

// ===========================================================================
// ProductFrame —— 分级产品帧 (设计文档 §5: `ProductFrame { BandSpec, stage,
//   float[]/iq[], 单位, 光学/时序元数据 }`)。渲染链出口与 eq 通道的统一载荷。
//   注: 与 vsgraysim/broker/SensorFrames.h 的传输层帧 (IrFrame/VisFrame/
//   RadarFrame/SarFrame) 分工 —— ProductFrame 是渲染链产品帧, 发布时映射到
//   对应传输层帧。
// ===========================================================================
struct ProductFrame {
    // === 类型与分级 ===
    SensorFrameType type  = SensorFrameType::MWIR;
    ProductStage    stage = ProductStage::Image;

    // === 帧标识 (与传输层帧对齐) ===
    uint64_t frame_id     = 0;
    double   sim_time     = 0.0;          ///< 帧结束时刻 (s)
    uint64_t timestamp_ns = 0;            ///< 仿真时统 (sim_time × 1e9)
    uint32_t sensor_id    = 0;
    uint32_t platform_id  = 0;

    // === 形态 ===
    uint32_t width  = 0;
    uint32_t height = 0;

    // === 载荷 (按类型二选一) ===
    /// VIS/IR: P0–P2 波段辐射亮度 (W/m²/sr, 线性物理量) → P3 full-well fraction → P4 normalized display
    std::vector<float> pixels;
    /// RADAR: IQ (回波/RD 图/复图像), 形态随 radar 段 (见 radar::Stage)
    std::vector<std::complex<float>> iq;
    /// Diagnostic residency flag only. Host ProductDispatcher rejects this:
    /// publishing GPU data requires typed resource ownership and synchronization.
    bool gpu_resident = false;

    // === 光学/时序元数据 ===
    double integration_s = 0.0;           ///< 积分/驻留时间 (s)
    double fov_h_deg = 0.0, fov_v_deg = 0.0;
    double range_min_m = 0.0;             ///< 雷达距离门 (RADAR 用)
    double range_max_m = 0.0;
    double pulse_width_s = 0.0;           ///< 雷达脉宽 (RADAR 用, 0=未设→映射缺省)
    double bandwidth_hz = 0.0;            ///< 雷达带宽 (RADAR 用, 0=未设→映射缺省)
    /// B7 扫描机构波束指向 (°, ENU 数学角: 0=East 绕 +Y 向 +Z):
    /// 雷达 = CPI 中心时刻 LOS(t_cpi_mid) (§7.2 标称波束指向), 成像 = 曝光时刻 LOS(t_exp)。
    double beam_az_deg = 0.0;
    double beam_el_deg = 0.0;
    double sample_rate_hz = 0.0; ///< physical fast-time sampling, independent of FFT padding

    /// 数据单位标签 (按类型+分级)
    const char* unit() const;
};

// ===========================================================================
// 雷达预留接口 (产品决策 2026-09: 处理分界仿真/显控待定, 仅固化段骨架)。
//   段间载荷统一走 ProductFrame(type=RADAR), 每段可插拔 → 分界落定后只需
//   把对应段装配到仿真端或显控端, 接口不变。
// ===========================================================================
namespace radar {

/// 处理段 (设计文档 §5 P0→P4 的雷达对应形态)
enum class Stage : uint32_t {
    Echo       = 0,   ///< 原始 IQ 回波 (快时间×慢时间)
    RangeComp  = 1,   ///< 脉冲压缩/距离压缩
    RdMap      = 2,   ///< 相干积累 → RD 图 (MTI/MTD 入口)
    ComplexImg = 3,   ///< 方位压缩复图像 (SAR RD/BP, ISAR 转角积累)
    Product    = 4,   ///< 幅度图/检测入口 (显控)
};

inline const char* stageName(Stage s) {
    switch (s) {
        case Stage::Echo:       return "echo";
        case Stage::RangeComp:  return "range_comp";
        case Stage::RdMap:      return "rd_map";
        case Stage::ComplexImg: return "complex_image";
        case Stage::Product:    return "product";
    }
    return "?";
}

/// 雷达参数 (场景 JSON sensor.tx_params 同名映射)
struct RadarParams {
    double   carrier_hz   = 9.4e9;   ///< 载频 (Hz, X 波段典型)
    double   prf_hz       = 4000.0;  ///< 脉冲重复频率 (Hz)
    double   pulse_width_s = 1e-6;   ///< 脉冲宽度 (s)
    double   bandwidth_hz = 30e6;    ///< 带宽 (Hz)
    uint32_t cpi_pulses   = 128;     ///< 一帧 CPI 脉冲数
    double   range_min_m  = 0.0;     ///< 距离门起始 (m)
    double   range_max_m  = 8000.0;  ///< 距离门结束 (m)
};

/// 处理段接口: 每段吃一帧产一帧 (GPU/CPU 实现按 P5 路线填充, 接口先冻结)
class IStageProcessor {
public:
    virtual ~IStageProcessor() = default;
    virtual Stage stage() const = 0;
    /// 输入帧 → 输出帧 (形态由 stage 决定; in.type 必须为 RADAR)
    virtual void process(const ProductFrame& in, ProductFrame& out) = 0;
};

} // namespace radar

} // namespace sensor
} // namespace rockyenv

#endif // ROCKY_ENV_SENSOR_API_H
