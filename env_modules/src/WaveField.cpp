/// @file WaveField.cpp —— JONSWAP 谱派生海况 + Gerstner 谐波表 + CPU 采样。
#include "WaveField.h"

#include <algorithm>
#include <numeric>

namespace rockyenv {

namespace {
constexpr float kG = 9.80665f;      // 重力加速度
constexpr float kPi = 3.14159265358979323846f;

/// 蒲福风级 (10m 风速区间中值上限, m/s): 0..12 级。
int beaufortOf(float u10) {
    static const float kUpper[13] = {
        0.3f, 1.6f, 3.4f, 5.5f, 8.0f, 10.8f, 13.9f, 17.2f, 20.8f,
        24.5f, 28.5f, 32.7f, 999.0f};
    for (int b = 0; b < 13; ++b)
        if (u10 <= kUpper[b]) return b;
    return 12;
}

/// JONSWAP 谱峰值周期经验式: Tp ≈ 0.87·(g X / U²)^0.33 · 2π U / g …
/// 简化采用 Hasselmann 拟合: fp = 3.5 (g/U10) (X g / U10²)^-0.33。
float jonswapPeakFreq(float u10, float fetch_m) {
    if (u10 < 0.5f) return 0.0f;
    const float X = std::max(fetch_m, 1000.0f);
    const float a = 3.5f * (kG / u10);
    const float b = std::pow((X * kG) / (u10 * u10), -0.33f);
    return a * b;  // rad/s 制谱峰角频率换算前 (Hz)
}

/// 有效波高 (风区/风时受限 + 完全发展海上限):
///   风区受限:  Hs = 0.0016·√(X_eff/g)·U10   (JONSWAP 拟合)
///   风时受限:  等效风区 X_dur ≈ 4e-4·g·t²/2π (duration-limited 近似)
///   完全发展海上限 (Pierson-Moskowitz): Hs_PM = 0.21·U10²/g
float jonswapHs(float u10, float fetch_m, float duration_s) {
    if (u10 < 0.5f) return 0.0f;
    const float X = std::max(fetch_m, 1000.0f);
    const float t = std::max(duration_s, 60.0f);
    const float xDur = 4.0e-4f * kG * t * t / (2.0f * kPi);
    const float xEff = std::min(X, xDur);
    const float hsFetch = 0.0016f * std::sqrt(xEff / kG) * u10;
    const float hsPM = 0.21f * u10 * u10 / kG;
    return std::min(hsFetch, hsPM);
}

// ---- 确定性 hash 工具 (与 band_shade.comp cHash/cNoise 同公式) ----
// CPU 白沫噪声 breakup 与 shader 逐位同源; hash01 与 shader wavePhase0 的
// index-hash 同族 (同 index 同值, UBO 不传初相/噪声真值)。
inline float wfFract(float x) { return x - std::floor(x); }
inline float wfHash01(float x) {
    return wfFract(std::sin(x * 12.9898f) * 43758.5453f);
}
float wfHash(float x, float y) {
    float px = wfFract(x * 123.34f);
    float py = wfFract(y * 456.21f);
    const float d = px * (px + 45.32f) + py * (py + 45.32f);  // dot(p, p+45.32)
    px += d; py += d;
    return wfFract(px * py);
}
float wfNoise(float x, float y) {
    const float ix = std::floor(x), iy = std::floor(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    const float a = wfHash(ix, iy),         b = wfHash(ix + 1.0f, iy);
    const float c = wfHash(ix, iy + 1.0f),  d = wfHash(ix + 1.0f, iy + 1.0f);
    const float ab = a + (b - a) * fx, cd = c + (d - c) * fx;
    return ab + (cd - ab) * fy;
}
/// 谐波初相 (index-hash, shader wavePhase0 同公式): 打破零初相波列原点对齐
inline float wavePhase0(int i) {
    return wfFract(std::sin((float(i) + 1.0f) * 12.9898f) * 43758.5453f) *
           2.0f * kPi;
}

// ---- P0: Mitsuyasu-Hasselmann 方向谱 (对标 Triton FFTWaterModel.cpp:1026-1041) ----
// 方向扩散随频率变化: 谱峰附近波列集中, 高频/低频发散 —— 替代 ±60° 线性展开。
//   s(ω) = 11.5·(g/(ωp·U10))^2.5·(ω/ωp)^μ,  μ = 5 (ω≤ωp) / -2.5 (ω>ωp)
//   D(ω,Δθ) = N(s)·cos^{2s}(Δθ/2),  N(s)=Γ(s+1)/Γ(s+½) (归一)
// gamma 比值用 Stirling 渐近 (s≥1 时相对误差 <1e-3; s<1 回退 tgamma 比值)。
inline double wfGammaRatio(double s) {
    // Γ(s+1)/Γ(s+½) = s·Γ(s)/Γ(s+½); 大 s 时 ≈ √(s+¼) (Stirling 展开首项+修正)
    if (s >= 8.0) {
        const double x = s + 0.25;   // √(s+¼) 的一阶修正足够
        return std::sqrt(x) * (1.0 - 1.0 / (64.0 * x * x));
    }
    return std::tgamma(s + 1.0) / std::tgamma(s + 0.5);
}

/// 给定谐波角频率 w (rad/s)、谱峰 wp、风速 U10、相对风向目标夹角 dTheta,
/// 返回该方向的 Mitsuyasu 权重 (未归一到 1, 仅相对比较用)。
inline double mitsuyasuWeight(double w, double wp, double u10, double dTheta) {
    if (wp <= 1e-6 || u10 <= 1e-3) return 1.0;
    const double mu = (w <= wp) ? 5.0 : -2.5;
    const double s = 11.5 * std::pow(kG / (wp * u10), 2.5) * std::pow(w / wp, mu);
    if (s <= 1e-4) return 1.0;                 // 全向
    if (s > 60.0) return (std::abs(dTheta) < 0.05) ? 1.0 : 0.0;  // 极窄→仅风向
    const double nrm = wfGammaRatio(s);
    // Δθ 包到 [-π,π]
    double dt = std::fmod(dTheta, 2.0 * kPi);
    if (dt > kPi) dt -= 2.0 * kPi;
    if (dt < -kPi) dt += 2.0 * kPi;
    const double c = std::cos(dt * 0.5);
    return nrm * std::pow(std::max(c, 0.0), 2.0 * s);
}
}  // namespace

void WaveField::generate(Mode mode, float wind_speed, float wind_dir_x, float wind_dir_z,
                         float fetch_m, float duration_s, int harmonics,
                         float hs_override, float peak_len_override, float depth_m) {
    mode_ = mode;
    wind_speed_ = wind_speed;
    swell_hs_ = 0.0f; wind_count_=0; swell_count_=0; // Rebuilding must not retain old swell indices.
    computeSeaState(wind_speed, fetch_m, duration_s);

    // P1 浅水色散: 代表水深 >0 时启用; MIN_DEPTH 下限防 tanh(kd)→0 奇异。
    // 深水 (<=0) 时 depthFactor=1, ω=√(gk), 与旧版逐位一致。
    const float depth = (depth_m > 0.5f) ? depth_m : -1.0f;

    if (mode == GerstnerLegacy) {
        // ---- Phase 1 兼容: 4 个写死谐波 (与旧 ocean.vert 逐参数一致) ----
        // 波长/振幅比例、方向展开角复刻旧 shader; 基准波长/波高由参数给出。
        const float L0 = peak_len_override > 0.0f ? peak_len_override
                          : (p_len_default_ = std::max(peak_len_, 26.0f));
        const float H0 = hs_override > 0.0f ? hs_override : 2.6f;  // 旧默认 Hs≈2.6
        waves_.clear();
        static const float Ls[4] = {1.0f, 0.60f, 0.34f, 0.20f};
        static const float As[4] = {0.50f, 0.27f, 0.15f, 0.08f};
        static const float Ang[4] = {0.0f, 1.15f, -0.85f, 2.05f};
        const float len = std::sqrt(wind_dir_x * wind_dir_x + wind_dir_z * wind_dir_z);
        const float wx = len > 1e-6f ? wind_dir_x / len : 1.0f;
        const float wz = len > 1e-6f ? wind_dir_z / len : 0.0f;
        for (int i = 0; i < 4; ++i) {
            WaveHarmonic h;
            h.wave_length = L0 * Ls[i];
            h.amplitude = H0 * 0.5f * As[i];   // 旧 shader 波高→单幅近似
            const float ang = Ang[i];
            h.angle_off = ang;
            h.dir_cos = wx * std::cos(ang) - wz * std::sin(ang);
            h.dir_sin = wx * std::sin(ang) + wz * std::cos(ang);
            h.k = 2.0f * kPi / h.wave_length;
            h.omega = std::sqrt(kG * h.k);
            waves_.push_back(h);
        }
        hs_ = H0;   // legacy 不做能量归一 (兼容优先)
        return;
    }

    // ---- GerstnerSpectral (S1+) ----
    int n = std::clamp(harmonics, 4, kMaxHarmonics);
    if (hs_override > 0.0f) hs_ = hs_override;
    if (peak_len_override > 0.0f) peak_len_ = peak_len_override;

    // ---- 谐波表: 波长对数均布 [0.16·Lp, 1.4·Lp] (长波到短波) ----
    waves_.assign(static_cast<size_t>(n), WaveHarmonic{});

    // JONSWAP 形状: 能量按频率加权。用相对频率 f/fp 的 -5 次幂近似分配
    // (谱主峰在 fp; 短波侧衰减)。对数波长网格 → 频率比 → 能量权重。
    // 去规则化: bin 内 ±0.4 确定性 hash 抖动 —— 等距波长+交错方向+零初相
    // 会渲染出规则平行波列 (修 IR/TV 海况条纹感); clamp 保持包络端点。
    const float lp = std::max(peak_len_, 4.0f);
    std::vector<float> wl(static_cast<size_t>(n));
    std::vector<float> w(static_cast<size_t>(n), 0.0f);
    float wSum = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float jit = wfHash01(float(i) + 1.0f) - 0.5f;
        const float f = std::clamp((float(i) + 0.8f * jit) / float(n - 1),
                                   0.0f, 1.0f);
        wl[i] = lp * std::pow(1.4f / 0.16f, -(f - 0.0f));
        // 能量权重: S(f) ∝ (fp/f)^5·exp(-1.25(fp/f)^4) 的离散近似,
        // 波长域 S ∝ (L/Lp)^2.5·exp(-1.25(Lp/L)^2)
        const float r = lp / wl[i];  // fp/f ∝ Lp/L 的倒数域
        w[i] = std::pow(wl[i] / lp, 2.5f) * std::exp(-1.25f * (lp / wl[i]) * (lp / wl[i]));
        wSum += w[i];
    }
    // 归一化能量到目标 Hs: 波列 σ = √(Σ A²/2), Hs = 2√2 σ ⇒ ΣA² = Hs²/4。
    const float a2Target = hs_ * hs_ / 4.0f;
    float a2Sum = 0.0f;
    for (int i = 0; i < n; ++i) {
        waves_[i].wave_length = wl[i];
        waves_[i].amplitude = std::sqrt(a2Target * (wSum > 1e-9f ? w[i] / wSum : 1.0f / n));
        a2Sum += waves_[i].amplitude * waves_[i].amplitude;
    }
    // 修正数值误差 (能量分配截断): 整体缩放对齐 ΣA²。
    if (a2Sum > 1e-9f) {
        const float s = std::sqrt(a2Target / a2Sum);
        for (auto& h : waves_) h.amplitude *= s;
    }

    // ---- P0 方向分配: Mitsuyasu 频率相关方向谱 (替代 ±60° 线性展开) ----
    // 目标: 每个谐波按其频率/谱峰/风速自动获得物理方向, 且方向分布的
    // 能量权重 = cos^{2s}(Δθ/2) (s 随 ω/ωp 变化)。实现: 以风向为轴对称采样
    // 2K+1 个候选方向, 按 Mitsuyasu 权重确定该谐波的方向角; 振幅不再依赖
    // 方向 (方向权重只决定摆放, 能量归一仍由 Hs 守恒保证)。
    const float len = std::sqrt(wind_dir_x * wind_dir_x + wind_dir_z * wind_dir_z);
    const float wx = len > 1e-6f ? wind_dir_x / len : 1.0f;
    const float wz = len > 1e-6f ? wind_dir_z / len : 0.0f;
    // 谱峰角频率 ωp = 2π/Tp (Tp 已由 computeSeaState 求出); 风速取生成参数。
    const float wp = (tp_ > 1e-4f) ? (2.0f * kPi / tp_) : 1.0f;
    const float u10 = std::max(wind_speed, 0.5f);
    for (int i = 0; i < n; ++i) {
        // 谐波角频率
        const float w = std::sqrt(kG * (2.0f * kPi / std::max(waves_[i].wave_length, 1e-3f)));
        // 候选方向: 风向 ±120° 内 7 个 (短波方向更散由 s 自动放宽, 不必加宽采样)
        // 确定性轮换起点, 避免方向系统性偏置。
        const float baseJit = (wfHash01(float(i) + 5.13f) - 0.5f) * 0.35f;
        float bestAng = 0.0f, bestW = -1.0f;
        // 采样 9 个候选方向取权重最大者 (确定性, 覆盖 ±120°)
        for (int c = 0; c < 9; ++c) {
            const float f = (float(c) - 4.0f) / 4.0f;         // -1..1
            const float ang = baseJit + f * (120.0f * kPi / 180.0f);
            const double wt = mitsuyasuWeight(w, wp, u10, double(ang));
            // 轻微确定性扰动打散同权平局
            const double score = wt * (0.9 + 0.2 * wfHash01(float(i * 31 + c) + 0.7f));
            if (score > bestW) { bestW = score; bestAng = ang; }
        }
        waves_[i].angle_off = bestAng;
        const float c = std::cos(bestAng), s = std::sin(bestAng);
        waves_[i].dir_cos = wx * c - wz * s;
        waves_[i].dir_sin = wx * s + wz * c;
        waves_[i].k = 2.0f * kPi / waves_[i].wave_length;
        // P1 浅水色散: ω=√(g·k·tanh(k·d)); 深水 (depth<0) tanh→1 恒等。
        if (depth > 0.0f) {
            const float kd = waves_[i].k * depth;
            waves_[i].omega = std::sqrt(kG * waves_[i].k * std::tanh(kd));
        } else {
            waves_[i].omega = std::sqrt(kG * waves_[i].k);
        }
        waves_[i].phase = wavePhase0(i);
    }

    // Hs 与 Tp 回填 (override 时 Tp 仍取谱峰)。
    tp_ = peak_len_ > 0.0f ? std::sqrt(2.0f * kPi * peak_len_ / kG) : tp_;
}

void WaveField::addSwell(const Swell& sw, float base_dir_x, float base_dir_z) {
    if (!sw.on()) {
        // 关闭: 若此前叠加过涌浪谐波, 截断表 (涌浪总在尾部)。
        if (swell_count_ > 0) waves_.resize(static_cast<size_t>(wind_count_));
        swell_count_ = 0;
        swell_hs_ = 0.0f;
        return;
    }
    // 涌浪: 3 个长波谐波, 窄方向 (±8°), 低频。振幅按 Hs/2√2 分布
    // (单列近似: A1≈0.45·Hs, A2≈0.30·Hs, A3≈0.15·Hs, 能量平方和≈(Hs/2√2)²)。
    if (swell_count_ > 0) waves_.resize(static_cast<size_t>(wind_count_));  // 先截断旧涌浪
    wind_count_ = static_cast<int>(waves_.size());

    const float len = std::sqrt(base_dir_x * base_dir_x + base_dir_z * base_dir_z);
    const float wx = len > 1e-6f ? base_dir_x / len : 1.0f;
    const float wz = len > 1e-6f ? base_dir_z / len : 0.0f;
    const float off = sw.dir_off_deg * kPi / 180.0f;
    const float cx = wx * std::cos(off) - wz * std::sin(off);
    const float cz = wx * std::sin(off) + wz * std::cos(off);

    static const float Lr[3] = {1.00f, 0.72f, 1.35f};
    static const float Ar[3] = {0.45f, 0.30f, 0.15f};
    static const float Aoff[3] = {0.0f, 0.10f, -0.14f};
    for (int i = 0; i < 3; ++i) {
        if (static_cast<int>(waves_.size()) >= kMaxHarmonics) break;
        WaveHarmonic h;
        h.wave_length = sw.length * Lr[i];
        h.amplitude = sw.height * Ar[i];
        const float ang = Aoff[i];
        h.angle_off = off + ang;
        h.dir_cos = cx * std::cos(ang) - cz * std::sin(ang);
        h.dir_sin = cx * std::sin(ang) + cz * std::cos(ang);
        h.k = 2.0f * kPi / h.wave_length;
        h.omega = std::sqrt(kG * h.k);
        h.phase = wavePhase0(static_cast<int>(waves_.size()));  // 表序 index-hash
        waves_.push_back(h);
    }
    swell_count_ = static_cast<int>(waves_.size()) - wind_count_;
    swell_hs_ = sw.height;
}

void WaveField::computeSeaState(float wind_speed, float fetch_m, float duration_s) {
    beaufort_ = beaufortOf(wind_speed);
    hs_ = jonswapHs(wind_speed, fetch_m, duration_s);
    const float fp = jonswapPeakFreq(wind_speed, fetch_m);  // Hz
    tp_ = fp > 1e-6f ? 1.0f / fp : 4.0f;
    // 深水色散: Lp = g Tp² / 2π
    peak_len_ = kG * tp_ * tp_ / (2.0f * kPi);
}

// ---- CPU 采样 (与 ocean.vert 逐公式一致) ----

float WaveField::waveHeight(double ex, double ez, double t) const {
    float y = 0.0f;
    for (const auto& h : waves_) {
        const float ph = h.k * (h.dir_cos * static_cast<float>(ex) +
                                h.dir_sin * static_cast<float>(ez)) -
                         h.omega * static_cast<float>(t) + h.phase;
        y += h.amplitude * std::sin(ph);
    }
    return y;
}

void WaveField::waveNormal(double ex, double ez, double t,
                           float& nx, float& ny, float& nz) const {
    nx = 0.0f; ny = 1.0f; nz = 0.0f;
    float sx = 0.0f, sz = 0.0f;
    for (const auto& h : waves_) {
        const float ph = h.k * (h.dir_cos * static_cast<float>(ex) +
                                h.dir_sin * static_cast<float>(ez)) -
                         h.omega * static_cast<float>(t) + h.phase;
        const float c = std::cos(ph);
        sx += h.k * h.amplitude * h.dir_cos * c;
        sz += h.k * h.amplitude * h.dir_sin * c;
    }
    const float inv = 1.0f / std::sqrt(sx * sx + 1.0f + sz * sz);
    nx = -sx * inv; ny = inv; nz = -sz * inv;
}

void WaveField::orbitalVelocity(double ex, double ez, double t,
                                float& vx, float& vy, float& vz) const {
    vx = 0.0f; vy = 0.0f; vz = 0.0f;
    for (const auto& h : waves_) {
        const float ph = h.k * (h.dir_cos * static_cast<float>(ex) +
                                h.dir_sin * static_cast<float>(ez)) -
                         h.omega * static_cast<float>(t) + h.phase;
        const float c = std::cos(ph), s = std::sin(ph);
        const float aw = h.amplitude * h.omega;
        vx += aw * h.dir_cos * c;
        vz += aw * h.dir_sin * c;
        vy += aw * s;
    }
}

float WaveField::jacobian(double ex, double ez, double t) const {
    // P2 完整 2×2 雅可比行列式 (对标 Triton FFTWaterModel.cu:89-97):
    //   Gerstner 水平位移 (Q·A·cosφ 沿 d) 的梯度矩阵
    //     Jxx = 1 - Σ Q·k·A·dx·dx·sinφ   Jxy = -Σ Q·k·A·dx·dz·sinφ
    //     Jyx = -Σ Q·k·A·dz·dx·sinφ      Jyy = 1 - Σ Q·k·A·dz·dz·sinφ
    //   J = Jxx·Jyy - Jxy·Jyx; 含剪切项后波面折叠判据不再只认主波向压缩,
    //   交叉波/斜向破碎也能捕获 (旧对角近似 J=1-ΣQkA 只对单波向准确)。
    float sxx = 0.0f, szz = 0.0f, sxz = 0.0f;
    for (const auto& h : waves_) {
        const float ph = h.k * (h.dir_cos * static_cast<float>(ex) +
                                h.dir_sin * static_cast<float>(ez)) -
                         h.omega * static_cast<float>(t) + h.phase;
        const float qkas = choppy_ * h.k * h.amplitude * std::sin(ph);
        sxx += qkas * h.dir_cos * h.dir_cos;
        szz += qkas * h.dir_sin * h.dir_sin;
        sxz += qkas * h.dir_cos * h.dir_sin;
    }
    const float jxx = 1.0f - sxx, jyy = 1.0f - szz;
    const float jxy = -sxz,         jyx = -sxz;
    return jxx * jyy - jxy * jyx;
}

float WaveField::foamCoverage(double ex, double ez, double t) const {
    // 波陡掩码 (P2 完整 2×2 雅可比): J<0.62 → 完全破碎; 0.62..0.82 线性过渡
    // (与 ocean.frag / band_shade.comp 同标定)。
    const float j = jacobian(ex, ez, t);
    float foam = 1.0f - std::clamp((j - 0.62f) / (0.82f - 0.62f), 0.0f, 1.0f);
    // 破碎项噪声 breakup (cNoise 43m 尺度, 与 band_shade.comp seaShade 同公式):
    // J 破碎随波相锁定 → 规则白沫条带; 值噪声门控 0.75..1.25 去规则化。
    foam *= 0.75f + 0.5f * wfNoise(float(ex) / 43.0f, float(ez) / 43.0f);
    // 风成覆盖率 (白帽经验式, Monahan): U10 > 6.5 m/s 后随风速增长。
    if (wind_speed_ > 6.5f) {
        const float windFoam = 0.0035f * (wind_speed_ - 6.5f);
        foam = std::clamp(foam + windFoam, 0.0f, 1.0f);
    }
    return foam;
}

}  // namespace rockyenv
