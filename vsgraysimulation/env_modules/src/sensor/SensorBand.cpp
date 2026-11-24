/// @file SensorBand.cpp —— BandSpec/普朗克波段 LUT/晴空大气 (蓝方物理, CPU 预计算)。
#include "rocky_env/SensorApi.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace rockyenv {
namespace sensor {

namespace {
// 物理常量 (SI)
constexpr double H_PLANCK = 6.62607015e-34;
constexpr double C_LIGHT  = 2.99792458e8;
constexpr double K_BOLTZ  = 1.380649e-23;
constexpr double C1 = 2.0 * M_PI * H_PLANCK * C_LIGHT * C_LIGHT;   // 3.7418e-16 W·m²
constexpr double C2 = H_PLANCK * C_LIGHT / K_BOLTZ;                // 1.4388e-2 m·K

/// 普朗克光谱出射度 M_λ(T) (W/m³) → 换 μm 时 ×1e-6
double planckSpectral(double lambda_m, double T) {
    return C1 / (std::pow(lambda_m, 5) * (std::exp(C2 / (lambda_m * T)) - 1.0));
}
} // namespace

/// 波段积分出射度 M_band(T) = ∫ M_λ dλ (W/m²), Simpson 128 点。
/// 批次2导出: SkyModule::bandSky 复用 (热天空柱发射 / 太阳 5778K 波段积分)。
double bandExitance(double lambda_min, double lambda_max, double T) {
    constexpr int N = 128;   // 偶数
    const double h = (lambda_max - lambda_min) / N;
    double sum = planckSpectral(lambda_min, T) + planckSpectral(lambda_max, T);
    for (int i = 1; i < N; ++i)
        sum += planckSpectral(lambda_min + i * h, T) * ((i % 2) ? 4.0 : 2.0);
    return sum * h / 3.0;
}

// ---------------------------------------------------------------------------
// 第九轮 IR 逐谱 LUT: 传感器响应 S(λ) + 波段内吸收带 G(λ) + S̃ 加权积分。
// ---------------------------------------------------------------------------
bool irSpectralOn() {
    static const bool on = [] {
        const char* e = std::getenv("VSIM_IR_SPECTRAL");
        return !(e && e[0] == '0');
    }();
    return on;
}

double sensorResponse(BandType t, double lambda_um) {
    // 逻辑斯蒂边沿 (σ 宽 w): L(x)=1/(1+exp(-4x)) — x=±1 处 ~0.88/0.12
    auto edge = [](double x) { return 1.0 / (1.0 + std::exp(-4.0 * x)); };
    switch (t) {
        case BandType::MWIR:   // InSb 光伏: 3.6..4.8 μm (典型制冷热像标称), 边沿 0.15
            return edge((lambda_um - 3.60) / 0.15) * edge((4.80 - lambda_um) / 0.15);
        case BandType::LWIR:   // HgCdTe/微测辐射热计: 8.5..13.0 μm (典型标称), 边沿 0.40
            return edge((lambda_um - 8.50) / 0.40) * edge((13.0 - lambda_um) / 0.40);
        default:               // TV/SWIR: 平坦 (不改动)
            return 1.0;
    }
}

double bandAbsorptionShape(BandType t, double lambda_um) {
    auto gauss = [](double l, double c, double s) {
        const double d = (l - c) / s;
        return std::exp(-0.5 * d * d);
    };
    switch (t) {
        case BandType::MWIR:   // CO2 4.30μm q 支 + H2O 6.3μm 翼 (v 形爬升)
            return gauss(lambda_um, 4.30, 0.12) + 0.45 * gauss(lambda_um, 5.6, 0.8);
        case BandType::LWIR:   // O3 9.6μm + H2O 转动带翼 (向 14μm 爬升)
            return gauss(lambda_um, 9.6, 0.30) + 0.50 * gauss(lambda_um, 15.2, 1.1);
        default:
            return 0.0;        // TV/SWIR 散射波段: 无热吸收带结构
    }
}

namespace {
/// 吸收带峰值光学深度 τ_a (波段常数): 吸收带内天顶即近饱和、窗口相对透明
/// 的量级 (MODTRAN 典型量级; 精确值被 τ_w 基线标定吸收, 只影响带内再分配)
constexpr double kTauAbsMwir = 2.2;
constexpr double kTauAbsLwir = 1.6;

/// S̃(λ) = S(λ)·Δλ/∫S dλ (均值保持: ∫S̃ = Δλ) — ∫S 每波段缓存一次。
/// 返回 false = 该波段无逐谱结构 (TV/SWIR, S̃≡1)。
bool sTilde(BandType t, double lambda_min_m, double lambda_max_m,
            double lambda_m, double& out) {
    if (t != BandType::MWIR && t != BandType::LWIR) return false;
    static double normMwir = -1.0, normLwir = -1.0;
    double& norm = (t == BandType::MWIR) ? normMwir : normLwir;
    if (norm < 0.0) {
        constexpr int N = 128;
        const double h = (lambda_max_m - lambda_min_m) / N;
        auto s = [&](double lm) {
            return sensorResponse(t, lm * 1e6);
        };
        double sum = s(lambda_min_m) + s(lambda_max_m);
        for (int i = 1; i < N; ++i)
            sum += s(lambda_min_m + i * h) * ((i % 2) ? 4.0 : 2.0);
        norm = sum * h / 3.0;                      // ∫S dλ (m)
        if (norm <= 0.0) norm = lambda_max_m - lambda_min_m;   // 退化保护
    }
    const double dLm = lambda_max_m - lambda_min_m;
    out = sensorResponse(t, lambda_m * 1e6) * dLm / norm;
    return true;
}
} // namespace

double bandExitanceSR(const BandSpec& band, double T) {
    constexpr int N = 128;
    const double h = (band.lambda_max - band.lambda_min) / N;
    auto integrand = [&](double lm) {
        double st = 1.0;
        if (!sTilde(band.type, band.lambda_min, band.lambda_max, lm, st)) st = 1.0;
        return st * planckSpectral(lm, T);
    };
    double sum = integrand(band.lambda_min) + integrand(band.lambda_max);
    for (int i = 1; i < N; ++i)
        sum += integrand(band.lambda_min + i * h) * ((i % 2) ? 4.0 : 2.0);
    return sum * h / 3.0;
}

const SpectralSkyLut& spectralSkyLut(BandType t, double tAtmK) {
    static SpectralSkyLut cache[4];
    SpectralSkyLut& c = cache[uint32_t(t)];
    if (c.ready || (t != BandType::MWIR && t != BandType::LWIR)) return c;
    const BandSpec band = BandSpec::make(t);
    c.tauA = (t == BandType::MWIR) ? kTauAbsMwir : kTauAbsLwir;
    constexpr int N = 128;
    const double h = (band.lambda_max - band.lambda_min) / N;
    for (int i = 0; i <= N; ++i) {
        const double lm = band.lambda_min + i * h;
        const double sw = (i == 0 || i == N) ? 1.0 : ((i % 2) ? 4.0 : 2.0);
        double st = 1.0;
        if (!sTilde(t, band.lambda_min, band.lambda_max, lm, st)) st = 1.0;
        c.emitW[i] = st * planckSpectral(lm, tAtmK) * sw;
        c.sunW[i]  = st * planckSpectral(lm, 5778.0) * sw;
        c.shape[i] = bandAbsorptionShape(t, lm * 1e6);
    }
    const double scale = h / 3.0;
    double emitS = 0.0;
    for (int i = 0; i <= N; ++i) {
        c.emitW[i] *= scale;
        c.sunW[i]  *= scale;
        emitS += c.emitW[i];
    }
    c.emitS = emitS;
    // τ_w 天顶基线标定由 spectralTauW 在消费端 (bandSky) 以 eps0 调用。
    c.ready = true;
    return c;
}

/// τ_w 二分标定 (按 BandType 缓存, eps0 为波段常数): 天顶带平均发射率 = eps0
/// → 解窗口光学深度。基线自洽锚点: 天顶 L_sky = eps0·M_S/π = 晴空基线。
double spectralTauW(BandType t, double eps0) {
    static double cached[4] = {-1.0, -1.0, -1.0, -1.0};
    double& cv = cached[uint32_t(t)];
    if (cv >= 0.0) return cv;
    const SpectralSkyLut& c = spectralSkyLut(t, (t == BandType::MWIR) ? 250.0 : 263.0);
    const double target = eps0 * c.emitS;
    auto f = [&](double tw) {
        double s = 0.0;
        for (int i = 0; i <= 128; ++i)
            s += c.emitW[i] * (1.0 - std::exp(-(tw + c.tauA * c.shape[i])));
        return s;
    };
    double lo = 0.0, hi = 30.0;                    // f 单调递增, f(30)>emitS
    for (int it = 0; it < 60; ++it) {
        const double mid = 0.5 * (lo + hi);
        if (f(mid) < target) lo = mid; else hi = mid;
    }
    cv = 0.5 * (lo + hi);
    return cv;
}

BandSpec BandSpec::make(BandType t) {
    BandSpec b;
    b.type = t;
    switch (t) {
        case BandType::TV:   b.lambda_min = 0.38e-6; b.lambda_max = 0.78e-6; break;
        case BandType::SWIR: b.lambda_min = 1.0e-6;  b.lambda_max = 3.0e-6;  break;
        case BandType::MWIR: b.lambda_min = 3.0e-6;  b.lambda_max = 5.0e-6;  break;
        case BandType::LWIR: b.lambda_min = 8.0e-6;  b.lambda_max = 14.0e-6; break;
    }
    return b;
}

const char* BandSpec::name() const {
    switch (type) {
        case BandType::TV:   return "TV";
        case BandType::SWIR: return "SWIR";
        case BandType::MWIR: return "MWIR";
        case BandType::LWIR: return "LWIR";
    }
    return "?";
}

bool BandRadianceLut::build(const BandSpec& band) {
    band_ = band;
    constexpr uint32_t N = 512;
    table_.assign(N, 0.0f);
    // 第九轮: MWIR/LWIR 逐谱响应加权 (VSIM_IR_SPECTRAL=0 或 TV/SWIR → 旧平坦
    // 路径逐位回归)。GPU lutTex / cloudEmitLut / cloudContribution / nedl 均
    // 经此统一升级, 消费端接口不变。
    const bool sr = irSpectralOn()
                 && (band.type == BandType::MWIR || band.type == BandType::LWIR);
    for (uint32_t i = 0; i < N; ++i) {
        const double T = T_MIN_ + (T_MAX_ - T_MIN_) * double(i) / (N - 1);
        table_[i] = float(sr ? bandExitanceSR(band, T)
                             : bandExitance(band.lambda_min, band.lambda_max, T));
    }
    return true;
}

float BandRadianceLut::exitance(float T) const {
    if (table_.empty()) return 0.0f;
    const double t = std::clamp(double(T), double(T_MIN_), double(T_MAX_));
    const double u = (t - T_MIN_) / (T_MAX_ - T_MIN_) * (table_.size() - 1);
    const size_t i0 = size_t(u);
    const size_t i1 = std::min(i0 + 1, table_.size() - 1);
    const double f = u - double(i0);
    return float(table_[i0] * (1.0 - f) + table_[i1] * f);
}

double EnvAnswer::transmittance(double sigma_per_km, double range_m) {
    return std::exp(-sigma_per_km * range_m / 1000.0);
}

EnvAnswer clearSkyEnv(const BandSpec& band, bool night) {
    EnvAnswer e;
    BandRadianceLut lut;
    lut.build(band);
    auto sky = [&](double T, double eps) { return eps * lut.exitance(float(T)) / M_PI; };
    switch (band.type) {
        case BandType::TV:
            e.sigma_per_km = 0.05;
            e.sky_radiance = night ? 1e-4 : 2.5;       // 夜间近黑/白天天空亮度
            e.sun_irradiance = night ? 0.0 : 700.0;
            break;
        case BandType::SWIR:
            e.sigma_per_km = 0.08;
            // SWIR 白天天空 = 太阳散射 (TV 同机制; Rayleigh λ^-4 更弱 →
            // sky/Esun 比 TV 低: 0.25/180 ≈ 1.4e-3, TV 2.5/700 ≈ 3.6e-3)。
            // 修复 (9.6-②): 旧式 0.5×sky(250K) 为热发射公式, 250K 普朗克
            // 在 1-3μm ≈ 0 → 白天 SWIR 天空/云系统性近黑 (夜间 5e-4 硬值
            // 反而比白天高, AGC 拉伸后昼夜观感反转)。
            e.sky_radiance = night ? 5e-4 : 0.25;
            e.sun_irradiance = night ? 0.0 : 180.0;
            break;
        case BandType::MWIR:
            e.sigma_per_km = 0.12;
            e.sky_radiance = 0.35 * sky(250.0, 0.85);
            e.sun_irradiance = 0.0;
            break;
        case BandType::LWIR:
            e.sigma_per_km = 0.30;
            e.sky_radiance = 0.90 * sky(263.0, 0.92);
            e.sun_irradiance = 0.0;
            break;
    }
    return e;
}

double nedlFromNetd(const BandRadianceLut& lut, double emissivity,
                    double t_ref_k, double netd_k) {
    // dL/dT ≈ ε·dM/dT/π (LUT 中心差分)
    const float dT = 1.0f;
    const double dMdT = (lut.exitance(float(t_ref_k) + dT) - lut.exitance(float(t_ref_k) - dT)) / (2.0 * dT);
    return emissivity * dMdT / M_PI * netd_k;
}

const char* ProductFrame::unit() const {
    // VIS/IR 按产品分级: P0–P2 辐亮度 / P3 电子数 / P4 DN; RADAR 按处理段。
    if (type == SensorFrameType::RADAR) {
        switch (stage) {
            case ProductStage::Scene: return "iq";              // echo
            case ProductStage::Pupil: return "iq";              // range_comp
            case ProductStage::Focal: return "iq";              // rd_map
            case ProductStage::Raw:   return "iq";              // complex_image
            case ProductStage::Image: return "amp/dn";          // product 幅度图
        }
    }
    switch (stage) {
        case ProductStage::Scene:
        case ProductStage::Pupil:
        case ProductStage::Focal: return "W/m^2/sr";
        case ProductStage::Raw:   return "full_well_fraction";
        case ProductStage::Image: return "normalized_display";
    }
    return "?";
}

} // namespace sensor
} // namespace rockyenv
