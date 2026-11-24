#pragma once
/// @file RadarStages.h —— B6: 雷达段处理 (设计文档 §7.1 GPU 职责 3, §14.2)。
///
/// 回波合成 (GPU, RadarChain) 之后的段处理链, radar::IStageProcessor 实现
/// (接口冻结, 段可插拔):
///   Echo(GPU 产出) → RangeComp(脉压, 频域匹配滤波) → RdMap(慢时间多普勒 FFT)
///   → Product(RD 幅度图)。ComplexImg 尚未实现, 明确报错而不直通伪装。
/// CPU 实现定位: B6 验收 (改载频/带宽/CPI 看 RD 图变) + 显控侧装配基准;
/// cuFFT/compute 化按 P5 路线, 段接口不变。

#include <rocky_env/SensorApi.h>      // ProductFrame, radar::IStageProcessor

#include <complex>
#include <string>
#include <vector>

namespace rockyenv {
namespace sensor {

/// 回波链元数据 (RadarChainParams 投影; ProductFrame 元数据填充源)。
struct RadarEchoMeta {
    double lambda = 0.03;            ///< 波长 (m)
    double prf = 4000.0;             ///< PRF (Hz)
    double pulse_width_s = 1e-6;     ///< 脉宽 (s)
    double bandwidth_hz = 30e6;      ///< 带宽 (Hz)
    double range_min_m = 0.0;        ///< 距离门起始 (m)
    double range_max_m = 8000.0;     ///< 距离门结束 (m)
    uint32_t cpi_pulses = 128;       ///< 慢时间采样数 (2 的幂)
    uint32_t n_fast = 512;           ///< 快时间采样数 (2 的幂)
    double sample_rate_hz = 0.0;     ///< 0 = bandwidth_hz (same clock as GPU echo)
};

// ---- 段实现 (radar::IStageProcessor) ----

/// 脉冲压缩: 频域匹配滤波 (LFM chirp 参考谱共轭), 带宽↑ → 压缩峰变窄。
class RangeCompStage : public radar::IStageProcessor {
public:
    explicit RangeCompStage(const RadarEchoMeta& m) : meta_(m) {}
    radar::Stage stage() const override { return radar::Stage::RangeComp; }
    void process(const ProductFrame& in, ProductFrame& out) override;
private:
    RadarEchoMeta meta_;
};

/// 相干积累: 慢时间 (脉冲维) FFT → RD 图; CPI↑ → 多普勒分辨率↑。
class RdMapStage : public radar::IStageProcessor {
public:
    explicit RdMapStage(const RadarEchoMeta& m) : meta_(m) {}
    radar::Stage stage() const override { return radar::Stage::RdMap; }
    void process(const ProductFrame& in, ProductFrame& out) override;
private:
    RadarEchoMeta meta_;
};

/// Unsupported focusing interface: process throws std::logic_error until a
/// genuine SAR/ISAR focusing backend is installed.
class ComplexImgStage : public radar::IStageProcessor {
public:
    radar::Stage stage() const override { return radar::Stage::ComplexImg; }
    void process(const ProductFrame& in, ProductFrame& out) override;
};

/// 幅度图/检测入口: |z| 归一化 → pixels (显控消费)。
class ProductStageImpl : public radar::IStageProcessor {
public:
    radar::Stage stage() const override { return radar::Stage::Product; }
    void process(const ProductFrame& in, ProductFrame& out) override;
};

// ---- 装配 ----

/// 回波 → 按 want (radar::Stage 值 0..4, 升序去重) 增量处理, 返回对应
/// ProductFrame (雷达段值域与 ProductStage 0..4 对齐)。sim_time = CPI
/// 结束时刻 (§7.1 帧时统闭合)。
std::vector<ProductFrame> runRadarChain(const std::vector<std::complex<float>>& echo,
                                        const RadarEchoMeta& meta,
                                        uint64_t frame_id, double sim_time,
                                        uint32_t sensor_id,
                                        const std::vector<uint32_t>& want);

/// RD 图统计 (验收指标): 峰值距离/多普勒 bin、40dB 占用 bin 数、
/// 多普勒质心 (功率加权, 改载频/平台速度 → 质心平移)。
struct RadarMetrics {
    uint32_t peak_range_bin = 0;
    uint32_t peak_doppler_bin = 0;
    uint32_t occupied_bins = 0;      // P > P_peak/1e4 (40dB)
    double max_power = 0.0;
    double mean_power = 0.0;
    double doppler_centroid_bins = 0.0;
};
RadarMetrics rdMetrics(const ProductFrame& rd);

/// 幅度图 → 灰度 PPM (iq 取模 / pixels 直取, 峰值归一化; 验收目检)。
bool writeMagnitudePPM(const ProductFrame& f, const char* path);

} // namespace sensor
} // namespace rockyenv
