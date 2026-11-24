#pragma once
/// @file WaveField.h
/// @brief 海浪波场真值源 (S1): JONSWAP 谱派生海况 → Gerstner 谐波表 →
///        CPU 采样求值 (与 ocean.vert 逐公式一致)。
///
/// 纯 C++ / 零 VSG 依赖, 便于单测。OceanModule 每帧把谐波表写入 UBO
/// (offset 92..123), shader 与本类用同一份参数与同一套 Gerstner 公式,
/// 保证"所见即所采" (渲染与仿真闭环的 S1 验收标准)。
#ifndef ROCKY_ENV_WAVE_FIELD_H
#define ROCKY_ENV_WAVE_FIELD_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace rockyenv {

/// Gerstner 谐波 (CPU 与 shader 共享的最小参数集)。
struct WaveHarmonic {
    float wave_length = 0.0f;  ///< 波长 L (m)
    float amplitude   = 0.0f;  ///< 振幅 A (m)
    float dir_cos     = 0.0f;  ///< 传播方向 cos(θ) (绕风向展开后)
    float dir_sin     = 0.0f;  ///< 传播方向 sin(θ)
    float k           = 0.0f;  ///< 波数 2π/L (缓存)
    float omega       = 0.0f;  ///< 角频率 √(gk) (缓存)
    float angle_off   = 0.0f;  ///< 相对风向的展开角 (rad, 调试/UBO 用)
    float phase       = 0.0f;  ///< 初相 (rad): 确定性生成，与 omega 一并经
                                 ///< WaveDynamics UBO 下发给 ocean.vert。
};

/// 波场生成与采样。
class WaveField {
public:
    static constexpr int kMaxHarmonics = 16;

    /// 波形模式。
    enum Mode {
        GerstnerLegacy = 0,    ///< 4 个写死谐波 (Phase 1 兼容)
        GerstnerSpectral = 1,  ///< 谱化谐波表 (S1+, 默认)
    };

    /// 涌浪配置 (S2): 独立长周期波列。
    struct Swell {
        float height = 0.0f;    ///< 涌浪有效波高 (m); 0=关闭
        float length = 180.0f;  ///< 主波长 (m)
        float dir_off_deg = 35.0f;  ///< 相对风浪主向偏角 (度)
        bool  on() const { return height > 0.01f; }
    };

    /// 生成波场 (谐波表 + 海况)。
    /// @param mode          波形模式 (legacy=4 写死谐波; spectral=谱化表)
    /// @param wind_speed    10m 风速 (m/s)
    /// @param wind_dir_x    风向单位向量 x (ENU, 水平)
    /// @param wind_dir_z    风向单位向量 z
    /// @param fetch_m       风区 (m)
    /// @param duration_s    风时 (s)
    /// @param harmonics     谐波数 (4..kMaxHarmonics; legacy 固定 4)
    /// @param hs_override   >0 时锁定有效波高 (手动想定), 仅用谱形状
    /// @param peak_len_override >0 时锁定谱峰波长
    /// @param depth_m       P1 浅水色散: 代表水深 (m); <=0 视为深水 (ω=√gk,
    ///                      行为与旧版逐位一致)。>0 时 ω=√(g·k·tanh(k·d)),
    ///                      近岸波速减慢/波长压缩/波高增大 (shoaling)。
    void generate(Mode mode, float wind_speed, float wind_dir_x, float wind_dir_z,
                  float fetch_m, float duration_s, int harmonics,
                  float hs_override = 0.0f, float peak_len_override = 0.0f,
                  float depth_m = -1.0f);

    /// 涌浪叠加 (在 generate 之后调用; 追加谐波到表尾)。
    /// @param base_dir_x/z  风浪主向 (涌浪从其偏转 dir_off_deg)
    void addSwell(const Swell& sw, float base_dir_x, float base_dir_z);

    /// ---- 状态查询 ----
    Mode   mode() const { return mode_; }
    float  significantWaveHeight() const { return hs_; }     ///< 风浪 Hs (m, 不含涌浪)
    float  swellWaveHeight() const { return swell_hs_; }     ///< 涌浪 Hs (m)
    float  totalWaveHeight() const {                    ///< 总 Hs = √(风浪²+涌浪²)
        return std::sqrt(hs_ * hs_ + swell_hs_ * swell_hs_); }
    float  peakPeriod() const { return tp_; }                ///< Tp (s)
    float  peakWaveLength() const { return peak_len_; }      ///< 谱峰波长 (m)
    int    beaufort() const { return beaufort_; }            ///< 蒲福风级
    float  choppy() const { return choppy_; }                ///< Gerstner Q
    /// 设置 Gerstner 水平收缩 Q (0..1)。generate() 后调用 (模块 UBO 下发前)。
    void   setChoppy(float q) { choppy_ = std::clamp(q, 0.0f, 1.0f); }
    int    harmonicCount() const { return static_cast<int>(waves_.size()); }
    int    windHarmonicCount() const { return int(waves_.size())-swell_count_; }
    const std::vector<WaveHarmonic>& harmonics() const { return waves_; }
    bool   valid() const { return !waves_.empty(); }

    /// ---- CPU 采样 (与 ocean.vert 同公式) ----
    /// 波面高度 (含 choppy 垂向位移, 相对静水面)。
    float waveHeight(double ex, double ez, double t) const;

    /// 波面法线 (ENU, 归一化)。
    void waveNormal(double ex, double ez, double t,
                    float& nx, float& ny, float& nz) const;

    /// 水质点轨道速度 (m/s): x/z=水平传播分量, y=垂向。
    void orbitalVelocity(double ex, double ez, double t,
                         float& vx, float& vy, float& vz) const;

    /// 白沫覆盖率 0..1: 雅可比波陡掩码 + 风成覆盖率。
    float foamCoverage(double ex, double ez, double t) const;

    /// 波陡雅可比 (调试/单测; <1 收敛, 阈值附近破碎)。
    float jacobian(double ex, double ez, double t) const;

private:
    void computeSeaState(float wind_speed, float fetch_m, float duration_s);

    std::vector<WaveHarmonic> waves_;
    Mode  mode_ = GerstnerSpectral;
    float hs_ = 0.0f;          ///< 风浪有效波高 (m)
    float swell_hs_ = 0.0f;    ///< 涌浪有效波高 (m)
    float tp_ = 0.0f;          ///< 谱峰周期 (s)
    float peak_len_ = 0.0f;    ///< 谱峰波长 (m)
    int   beaufort_ = 0;       ///< 蒲福风级
    float choppy_ = 0.65f;     ///< Gerstner Q
    float wind_speed_ = 0.0f;  ///< 采样时风成白沫用
    float p_len_default_ = 26.0f;  ///< legacy 模式默认波长
    int   wind_count_ = 0;     ///< 风浪谐波数 (涌浪叠加前)
    int   swell_count_ = 0;    ///< 涌浪谐波数
};

}  // namespace rockyenv

#endif  // ROCKY_ENV_WAVE_FIELD_H
