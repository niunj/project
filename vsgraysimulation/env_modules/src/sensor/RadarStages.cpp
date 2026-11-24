/// @file RadarStages.cpp —— B6: 雷达段处理实现 (CPU FFT; 段接口冻结 §14.2)。
#include "rocky_env/RadarStages.h"
#include "rocky_env/RadarSampling.h"
#include <simcore/Time.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace rockyenv {
namespace sensor {

namespace {

constexpr double kC = 2.99792458e8;

void requireMatrix(const ProductFrame& f, uint32_t width, uint32_t height) {
    if (width < 2 || height < 2 || (width & (width - 1)) || (height & (height - 1)) ||
        f.width != width || f.height != height || f.iq.size() != uint64_t(width) * height)
        throw std::invalid_argument("radar FFT requires a complete power-of-two IQ matrix");
}

// 就地 radix-2 FFT (n 为 2 的幂; inverse=true 共轭旋转因子)。
void fft(std::vector<std::complex<double>>& a, bool inverse) {
    const size_t n = a.size();
    if (n < 2) return;
    // 位反转重排
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = (inverse ? 2.0 : -2.0) * M_PI / double(len);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse) {
        for (auto& x : a) x /= double(n);
    }
}

// 帧 → 行列复矩阵视图 (行=慢时间脉冲, 列=快时间距离; iq 交错存储)
inline std::complex<float> at(const ProductFrame& f, uint32_t pulse, uint32_t fast) {
    return f.iq[size_t(pulse) * f.width + fast];
}
inline std::complex<float>& at(ProductFrame& f, uint32_t pulse, uint32_t fast) {
    return f.iq[size_t(pulse) * f.width + fast];
}

ProductFrame radarBase(const RadarEchoMeta& meta, uint64_t frame_id, double sim_time,
                       uint32_t sensor_id) {
    ProductFrame pf;
    pf.type = SensorFrameType::RADAR;
    pf.frame_id = frame_id;
    pf.sim_time = sim_time;
    pf.timestamp_ns = uint64_t(simcore::fromSeconds(sim_time));
    pf.sensor_id = sensor_id;
    pf.width = meta.n_fast;
    pf.height = meta.cpi_pulses;
    pf.range_min_m = meta.range_min_m;
    pf.range_max_m = meta.range_max_m;
    pf.pulse_width_s = meta.pulse_width_s;
    pf.bandwidth_hz = meta.bandwidth_hz;
    pf.sample_rate_hz = RadarSampling::make(meta.bandwidth_hz, meta.sample_rate_hz).rate_hz;
    pf.integration_s = double(meta.cpi_pulses) / meta.prf;   // CPI 时长
    return pf;
}

} // namespace

// ===========================================================================
// RangeComp —— 频域匹配滤波 (LFM)
// ===========================================================================
void RangeCompStage::process(const ProductFrame& in, ProductFrame& out) {
    requireMatrix(in, meta_.n_fast, meta_.cpi_pulses);
    const uint32_t nf = meta_.n_fast;
    const double fs = RadarSampling::make(meta_.bandwidth_hz, meta_.sample_rate_hz).rate_hz;
    if (!std::isfinite(meta_.pulse_width_s) || meta_.pulse_width_s <= 0.0 ||
        meta_.pulse_width_s * fs < 1 || meta_.pulse_width_s * fs > nf)
        throw std::invalid_argument("chirp duration does not fit the fast-time sampling window");
    out = in;
    out.sample_rate_hz = fs;
    const double K = meta_.bandwidth_hz / std::max(meta_.pulse_width_s, 1e-12);
    const uint32_t nChirp = std::min<uint32_t>(nf, uint32_t(std::round(meta_.pulse_width_s * fs)));

    // chirp 参考谱 (补零至 n_fast)
    std::vector<std::complex<double>> ref(nf);
    for (uint32_t n = 0; n < nChirp; ++n) {
        const double t = double(n) / fs;
        ref[n] = std::polar(1.0, M_PI * K * t * t);
    }
    fft(ref, false);

    std::vector<std::complex<double>> row(nf);
    for (uint32_t p = 0; p < meta_.cpi_pulses; ++p) {
        for (uint32_t i = 0; i < nf; ++i) row[i] = at(in, p, i);
        fft(row, false);
        for (uint32_t i = 0; i < nf; ++i) row[i] *= std::conj(ref[i]);
        fft(row, true);
        for (uint32_t i = 0; i < nf; ++i) at(out, p, i) = std::complex<float>(row[i]);
    }
    out.stage = ProductStage::Pupil;   // 雷达段值域对齐: RangeComp=1
}

// ===========================================================================
// RdMap —— 慢时间多普勒 FFT
// ===========================================================================
void RdMapStage::process(const ProductFrame& in, ProductFrame& out) {
    requireMatrix(in, meta_.n_fast, meta_.cpi_pulses);
    out = in;
    const uint32_t np = meta_.cpi_pulses;
    std::vector<std::complex<double>> col(np);
    for (uint32_t b = 0; b < meta_.n_fast; ++b) {
        for (uint32_t p = 0; p < np; ++p) col[p] = at(in, p, b);
        fft(col, false);
        for (uint32_t p = 0; p < np; ++p) at(out, p, b) = std::complex<float>(col[p]);
    }
    out.stage = ProductStage::Focal;   // RdMap=2
}

// ===========================================================================
// ComplexImg —— unsupported until a calibrated focusing backend exists
// ===========================================================================
void ComplexImgStage::process(const ProductFrame& in, ProductFrame& out) {
    throw std::logic_error("SAR/ISAR focusing is not implemented; an RD map is not a focused image");
}

// ===========================================================================
// Product —— 幅度图 (|z| 峰值归一化)
// ===========================================================================
void ProductStageImpl::process(const ProductFrame& in, ProductFrame& out) {
    requireMatrix(in, in.width, in.height);
    if (&in == &out) {
        const auto copy = in;
        process(copy, out);
        return;
    }
    out = in;
    out.iq.clear();
    out.pixels.assign(size_t(in.width) * in.height, 0.0f);
    float m = 0.0f;
    for (const auto& z : in.iq) m = std::max(m, std::abs(z));
    if (m > 0.0f) {
        for (size_t i = 0; i < in.iq.size(); ++i)
            out.pixels[i] = std::abs(in.iq[i]) / m;
    }
    out.stage = ProductStage::Image;   // Product=4
}

// ===========================================================================
// 装配
// ===========================================================================
std::vector<ProductFrame> runRadarChain(const std::vector<std::complex<float>>& echo,
                                        const RadarEchoMeta& meta,
                                        uint64_t frame_id, double sim_time,
                                        uint32_t sensor_id,
                                        const std::vector<uint32_t>& want) {
    std::vector<ProductFrame> out;
    std::vector<uint32_t> stages = want;
    std::sort(stages.begin(), stages.end());
    stages.erase(std::unique(stages.begin(), stages.end()), stages.end());
    if (stages.empty()) stages = {uint32_t(radar::Stage::Product)};
    for (const auto s : stages) {
        if (s > uint32_t(radar::Stage::Product))
            throw std::invalid_argument("unknown radar processing stage");
        if (s == uint32_t(radar::Stage::ComplexImg))
            throw std::logic_error("SAR/ISAR focusing is not implemented");
    }
    if (!std::isfinite(meta.prf) || meta.prf <= 0.0)
        throw std::invalid_argument("radar PRF must be finite and positive");

    // Supported chain: Echo → RangeComp → RdMap → normalized RD magnitude.
    RangeCompStage rc(meta);
    RdMapStage rm(meta);
    ProductStageImpl ps;
    ProductFrame f0 = radarBase(meta, frame_id, sim_time, sensor_id);
    f0.stage = ProductStage::Scene;
    f0.iq = echo;
    requireMatrix(f0, meta.n_fast, meta.cpi_pulses);
    ProductFrame f1, f2, f3, f4;
    bool have[5] = {true, false, false, false, false};
    ProductFrame* frames[5] = {&f0, &f1, &f2, &f3, &f4};
    uint32_t maxStage = *std::max_element(stages.begin(), stages.end());
    // 指标消费 RD 图 → 链路至少推进到 RdMap
    maxStage = std::max(maxStage, uint32_t(radar::Stage::RdMap));
    if (maxStage >= 1 && !have[1]) { rc.process(*frames[0], *frames[1]); have[1] = true; }
    if (maxStage >= 2 && !have[2]) { rm.process(*frames[1], *frames[2]); have[2] = true; }
    if (maxStage >= 4 && !have[4]) { ps.process(*frames[2], *frames[4]); have[4] = true; }
    for (uint32_t s : stages)
        if (have[s]) out.push_back(*frames[s]);
    return out;
}

// ===========================================================================
// 指标 + PPM
// ===========================================================================
RadarMetrics rdMetrics(const ProductFrame& rd) {
    RadarMetrics m;
    if (rd.width == 0 || rd.height == 0 || rd.iq.size() != uint64_t(rd.width) * rd.height) return m;
    std::vector<double> power(rd.iq.size());
    double peak = 0.0;
    double sum = 0.0;
    uint32_t pb = 0, pd = 0;
    for (size_t i = 0; i < rd.iq.size(); ++i) {
        const double p = std::norm(rd.iq[i]);   // |z|²
        power[i] = p;
        sum += p;
        if (p > peak) {
            peak = p;
            pb = uint32_t(i % rd.width);
            pd = uint32_t(i / rd.width);
        }
    }
    m.peak_range_bin = pb;
    m.peak_doppler_bin = pd;
    m.max_power = peak;
    m.mean_power = sum / double(power.size());
    double wsum = 0.0, psum = 0.0;
    for (uint32_t i = 0; i < power.size(); ++i) {
        if (power[i] > peak * 1e-4) ++m.occupied_bins;
        const uint32_t dop = uint32_t(i / rd.width);
        wsum += power[i] * double(dop);
        psum += power[i];
    }
    m.doppler_centroid_bins = (psum > 0.0) ? wsum / psum : 0.0;
    return m;
}

bool writeMagnitudePPM(const ProductFrame& f, const char* path) {
    const size_t n = size_t(f.width) * f.height;
    if (!path || n == 0 || (f.iq.empty() ? f.pixels.size() != n : f.iq.size() != n)) return false;
    std::vector<float> mag(n, 0.0f);
    if (!f.iq.empty()) {
        float m = 0.0f;
        for (size_t i = 0; i < n && i < f.iq.size(); ++i) {
            mag[i] = std::abs(f.iq[i]);
            m = std::max(m, mag[i]);
        }
        if (m > 0.0f) for (auto& v : mag) v /= m;
    } else {
        float m = 0.0f;
        for (size_t i = 0; i < n && i < f.pixels.size(); ++i) m = std::max(m, f.pixels[i]);
        if (m > 0.0f) for (size_t i = 0; i < n; ++i) mag[i] = f.pixels[i] / m;
    }
    FILE* fp = std::fopen(path, "wb");
    if (!fp) return false;
    std::fprintf(fp, "P6\n%u %u\n255\n", f.width, f.height);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t g = uint8_t(std::min(1.0f, mag[i]) * 255.0f);
        const uint8_t rgb[3] = {g, g, g};
        std::fwrite(rgb, 1, 3, fp);
    }
    std::fclose(fp);
    return true;
}

} // namespace sensor
} // namespace rockyenv
