#include "vsgraysim/layer3b_signal/SensorSignalProcessor.h"

#include <cmath>
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace vsgraysim {

namespace {

// ====================================================================
// 缓存式 radix-2 FFT: 位反转置换与旋转因子由每个处理线程按尺寸复用,
// 消除每次 FFT 的逐蝶形复数旋转乘法链 (信号成帧热路径主开销之一)。
// ====================================================================
struct FftPlan {
    std::vector<size_t> rev;                          ///< 位反转置换表 (i < rev[i] 时交换)
    std::vector<std::vector<std::complex<float>>> tw; ///< 逐级旋转因子 (stage s: 半长 2^s)
};

const FftPlan& fft_plan(size_t N) {
    static thread_local std::unordered_map<size_t, FftPlan> plans;
    auto it = plans.find(N);
    if (it != plans.end()) return it->second;

    FftPlan p;
    int bits = 0;
    while ((size_t(1) << bits) < N) ++bits;
    p.rev.resize(N);
    for (size_t i = 0; i < N; ++i) {
        size_t r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (size_t(1) << b)) r |= size_t(1) << (bits - 1 - b);
        p.rev[i] = r;
    }
    p.tw.resize(size_t(bits));
    for (int s = 0; s < bits; ++s) {
        const size_t half = size_t(1) << s;           // len/2
        const float ang = -3.14159265f / float(half); // -2π/len = -π/half
        p.tw[size_t(s)].resize(half);
        for (size_t j = 0; j < half; ++j)
            p.tw[size_t(s)][j] = std::complex<float>(std::cos(ang * float(j)),
                                                     std::sin(ang * float(j)));
    }
    return plans.emplace(N, std::move(p)).first->second;
}

void fft_radar(std::vector<std::complex<float>>& x) {
    const size_t N = x.size();
    if (N <= 1) return;
    const FftPlan& p = fft_plan(N);
    for (size_t i = 0; i < N; ++i) {
        const size_t j = p.rev[i];
        if (i < j) std::swap(x[i], x[j]);
    }
    for (size_t s = 0, len = 2; len <= N; ++s, len <<= 1) {
        const auto& tw = p.tw[s];
        const size_t half = len >> 1;
        for (size_t i = 0; i < N; i += len) {
            for (size_t j = 0; j < half; ++j) {
                const std::complex<float> u = x[i + j];
                const std::complex<float> v = x[i + j + half] * tw[j];
                x[i + j] = u + v;
                x[i + j + half] = u - v;
            }
        }
    }
}

void ifft_radar(std::vector<std::complex<float>>& x) {
    for (auto& v : x) v = std::conj(v);
    fft_radar(x);
    size_t N = x.size();
    for (auto& v : x) { v = std::conj(v); v /= float(N); }
}

/// Hamming 窗按尺寸缓存 (窗只与长度有关, 此前每脉冲重算+重分配)。
const std::vector<std::complex<float>>& hamming_cached(size_t N) {
    static std::unordered_map<size_t, std::vector<std::complex<float>>> cache;
    auto it = cache.find(N);
    if (it != cache.end()) return it->second;
    std::vector<std::complex<float>> w(N);
    for (size_t i = 0; i < N; ++i)
        w[i] = std::complex<float>(
            0.54f - 0.46f * std::cos(2.0f * 3.14159265f * float(i) / float(N - 1)), 0.0f);
    return cache.emplace(N, std::move(w)).first->second;
}

} // anonymous namespace

std::vector<std::complex<float>> slowTimeSpectrum(const std::vector<std::complex<float>>& samples,
                                                 uint32_t rangeBins,uint32_t pulses) {
    if(!rangeBins||rangeBins>8192||pulses<2||pulses>256||(pulses&(pulses-1))||
       samples.size()!=size_t(rangeBins)*pulses)
        throw std::invalid_argument("invalid shape for a slow-time spectrum");
    std::vector<std::complex<float>> spectrum(samples.size()),column(pulses);
    for(uint32_t bin=0;bin<rangeBins;++bin) {
        for(uint32_t pulse=0;pulse<pulses;++pulse)column[pulse]=samples[size_t(pulse)*rangeBins+bin];
        fft_radar(column);
        for(uint32_t row=0;row<pulses;++row)spectrum[size_t(row)*rangeBins+bin]=column[row];
    }
    return spectrum;
}

// ====================================================================
// RadarPulseProcessor
//   Generates a range-Doppler map via:
//   1. Round-robin pulse grouping (simulates CPI with N pulses)
//   2. LFM chirp generation + matched filter pulse compression
//   3. Coherent propagation phase from each real pulse
//   4. Slow-time FFT → RD map
//   When real pulse timing is supplied (SignalFormationConfig
//   pulses_in_frame > 0) each process() call is one real pulse and the
//   RD map is emitted by a sliding-window slow-time FFT at the last
//   pulse of the frame.
// ====================================================================

SignalFrame RadarPulseProcessor::process(const std::vector<ScatterPoint>& sps,
                                          const SignalFormationConfig& cfg,
                                          uint32_t sensor_id) {
    const auto powerOfTwo=[](uint32_t n) { return n>=2 && !(n&(n-1)); };
    if (!std::isfinite(cfg.prf) || cfg.prf<=0 || !std::isfinite(cfg.fs) || cfg.fs<=0 ||
        !std::isfinite(cfg.carrier_hz) || cfg.carrier_hz<=0 ||
        !std::isfinite(cfg.chirp_duration) || cfg.chirp_duration<=0 ||
        !std::isfinite(cfg.chirp_bandwidth) || cfg.chirp_bandwidth<=0 ||
        !powerOfTwo(cfg.range_gates) || !powerOfTwo(cfg.doppler_bins) ||
        cfg.range_gates>(1u<<20) || cfg.doppler_bins>(1u<<16) ||
        double(cfg.fs)*cfg.chirp_duration<1 || double(cfg.fs)*cfg.chirp_duration>(1u<<20) ||
        uint64_t(cfg.range_gates)*cfg.doppler_bins>(1u<<26) ||
        !std::isfinite(cfg.sim_time) || !std::isfinite(cfg.pulse_slow_time) ||
        (cfg.pulses_in_frame && cfg.pulse_seq>=cfg.pulses_in_frame))
        throw std::invalid_argument("unsupported radar sampling, waveform, pulse index or FFT dimensions");
    if (active_config_) {
        const auto& old=*active_config_;
        if (old.fs!=cfg.fs || old.prf!=cfg.prf || old.carrier_hz!=cfg.carrier_hz ||
            old.chirp_duration!=cfg.chirp_duration || old.chirp_bandwidth!=cfg.chirp_bandwidth ||
            old.range_gates!=cfg.range_gates || old.doppler_bins!=cfg.doppler_bins ||
            bool(old.pulses_in_frame)!=bool(cfg.pulses_in_frame)) reset();
    }
    active_config_=cfg;
    if (cfg.pulses_in_frame > 0) return processPulseCPI(sps, cfg, sensor_id);
    if (sps.empty()) return SignalFrame{};
    return legacyProcess(sps, cfg, sensor_id);
}

// ====================================================================
// Real-pulse CPI path: one call == one pulse.  Each pulse is range
// compressed and appended to a sliding slow-time window; the
// range-Doppler map is emitted when the last pulse of the frame
// arrives.  Doppler phase uses the absolute slow time
// (sim_time + fire_time) so coherence is preserved across frames.
// ====================================================================
SignalFrame RadarPulseProcessor::processPulseCPI(
    const std::vector<ScatterPoint>& sps,
    const SignalFormationConfig& cfg,
    uint32_t sensor_id) {
    const float c = 299792458.0f;
    float K = cfg.chirp_bandwidth / cfg.chirp_duration;
    float Tp = cfg.chirp_duration;
    float fs = cfg.fs;
    uint32_t R = cfg.range_gates;
    uint32_t M = std::max(cfg.doppler_bins, 4u);

    struct TargetInfo {
        const ScatterPoint* sp;
        float range;
        float range_bin;
    };
    std::vector<TargetInfo> targets;
    targets.reserve(sps.size());
    for (const auto& sp : sps) {
        float dx = sp.position.x - cfg.sensor_position.x;
        float dy = sp.position.y - cfg.sensor_position.y;
        float dz = sp.position.z - cfg.sensor_position.z;
        float range = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (range < 1.0f) continue;

        targets.push_back({&sp, range, 2.0f * range / c * fs});
    }

    // ---- Range compression of this pulse ----
    size_t N = 1;
    while (N < R + size_t(fs * Tp)) N <<= 1;
    // Cached matched-filter reference (recomputed only when size changes)
    if (ref_buf_.size() != N) {
        ref_buf_.assign(N, std::complex<float>(0.0f, 0.0f));
        int Tp_samples = int(fs * Tp);
        int h_center = Tp_samples / 2;
        for (int n = 0; n < Tp_samples && n < int(N); ++n) {
            float t = float(n - h_center) / fs;
            float phase = 3.14159265f * K * t * t;
            ref_buf_[n] = std::complex<float>(std::cos(phase), -std::sin(phase));
        }
        fft_radar(ref_buf_);
    }

    // 复用工作缓冲 (避免每脉冲堆分配; assign 置零与原构造等价)
    work_buf_.assign(N, std::complex<float>(0.0f, 0.0f));
    auto& s = work_buf_;
    int Tp_samples = int(fs * Tp);
    // Precomputed chirp table shared by all targets of this pulse
    // (cos/sin per sample would otherwise dominate the frame budget).
    if (chirp_cache_n_ != Tp_samples) {
        chirp_cache_.resize(size_t(Tp_samples));
        for (int n = 0; n < Tp_samples; ++n) {
            float tn = float(n - Tp_samples / 2) / fs;
            float chirp_phase = 3.14159265f * K * tn * tn;
            chirp_cache_[size_t(n)] = {
                std::cos(chirp_phase), std::sin(chirp_phase)};
        }
        chirp_cache_n_ = Tp_samples;
    }
    const double slow_time = cfg.sim_time + cfg.pulse_slow_time;
    for (const auto& t : targets) {
        // The real-pulse IQ already carries the propagation phase evaluated
        // at this pulse's pose. A second velocity phase would count motion twice.
        const std::complex<float> phase_rot(1,0);
        std::complex<float> iq(t.sp->i_amplitude, t.sp->q_amplitude);
        int center_bin = int(t.range_bin);
        int h = Tp_samples / 2;
        for (int n = -h; n < Tp_samples - h; ++n) {
            int idx = center_bin + n;
            if (idx < 0 || idx >= int(N)) continue;
            s[idx] += iq * chirp_cache_[size_t(n + h)] * phase_rot;
        }
    }
    fft_radar(s);
    for (size_t i = 0; i < N; ++i) s[i] *= ref_buf_[i];
    ifft_radar(s);

    std::vector<std::complex<float>> profile(R, std::complex<float>(0.0f, 0.0f));
    const auto& hamming_r = hamming_cached(R);
    for (uint32_t ri = 0; ri < R; ++ri) {
        size_t src = size_t(float(ri) / float(R) * N);
        if (src >= N) src = N - 1;
        profile[ri] = s[src] * hamming_r[ri];
    }

    // ---- Sliding slow-time window (max doppler_bins pulses) ----
    if (cpi_profiles_.size() >= M) {
        cpi_profiles_.erase(cpi_profiles_.begin());
        cpi_slow_time_.erase(cpi_slow_time_.begin());
    }
    cpi_profiles_.push_back(std::move(profile));
    cpi_slow_time_.push_back(slow_time);

    // Only the last pulse of the frame emits the RD map.
    if (cfg.pulse_seq + 1 < cfg.pulses_in_frame) return SignalFrame{};

    // ---- Slow-time FFT over the window → range-Doppler map ----
    size_t W = cpi_profiles_.size();
    SignalFrame frame;
    frame.resize(R, M);
    frame.fs = fs * float(R) / float(N); // effective rate after the explicit N -> R sampling
    frame.prf = cfg.prf;
    frame.carrier_hz = cfg.carrier_hz;
    frame.sensor_type = SensorType::RADAR;
    frame.sensor_id = sensor_id;
    frame.sim_time = slow_time;
    frame.frame_id = cfg.frame_id;
    frame.coherent_pulses = uint32_t(W);
    frame.range_min = 0.0f;
    frame.range_max = float(R-1)*299792458.0f/(2*frame.fs);

    const auto& hamming_d = hamming_cached(M);
    az_buf_.assign(M, std::complex<float>(0.0f, 0.0f));
    for (uint32_t ri = 0; ri < R; ++ri) {
        for (uint32_t pi = 0; pi < M; ++pi) az_buf_[pi] = 0.0f;
        for (size_t pi = 0; pi < W; ++pi)
            az_buf_[pi] = cpi_profiles_[pi][ri] * hamming_d[pi];
        fft_radar(az_buf_);
        for (uint32_t pi = 0; pi < M; ++pi)
            frame.samples[size_t(pi) * R + ri] = az_buf_[pi];
    }

    {
        float peak = 0.0f;
        for (auto& v : frame.samples) peak = std::max(peak, std::abs(v));
        if (peak > 1e-12f)
            for (auto& v : frame.samples) v /= peak;
    }

    frame.range_axis.resize(R);
    for (uint32_t ri = 0; ri < R; ++ri)
        frame.range_axis[ri] = float(ri)*299792458.0f/(2*frame.fs);

    float fd_max = cfg.prf / 2.0f;
    frame.doppler_axis.resize(M);
    for (uint32_t pi = 0; pi < M; ++pi) {
        float fd = float(pi) / float(M) * cfg.prf;
        if (fd > fd_max) fd -= cfg.prf;
        frame.doppler_axis[pi] = fd;
    }

    spdlog::info("RadarPulseProcessor(CPI): frame={} sim={:.3f}s pulse={}/{} "
                 "window={} pulses × {} gates, {} targets",
                 cfg.frame_id, cfg.sim_time, cfg.pulse_seq + 1, cfg.pulses_in_frame,
                 W, R, targets.size());
    return frame;
}

// ====================================================================
// Legacy whole-frame path: interleaves all scatter points into
// pseudo-pulses (unchanged historical behavior).
// ====================================================================
SignalFrame RadarPulseProcessor::legacyProcess(const std::vector<ScatterPoint>& sps,
                                               const SignalFormationConfig& cfg,
                                               uint32_t sensor_id) {
    if (sps.empty() || cfg.prf <= 0.0f) return SignalFrame{};

    const float c = 3e8f;
    float PRI = 1.0f / cfg.prf;
    float lambda = c / cfg.carrier_hz;
    float K = cfg.chirp_bandwidth / cfg.chirp_duration;
    float Tp = cfg.chirp_duration;
    float fs = cfg.fs;
    uint32_t R = cfg.range_gates;
    uint32_t D = std::max(4u,cfg.doppler_bins);

    // ---- Step 1: Round-robin pulse grouping ----
    // Scatter points are interleaved into D pseudo-pulses.
    // Each pulse represents one PRI's measurement of the scene.
    std::vector<std::vector<const ScatterPoint*>> pulse_groups(D);
    float max_range = 0.0f;
    for (size_t i = 0; i < sps.size(); ++i) {
        pulse_groups[i % D].push_back(&sps[i]);
        float range = sps[i].time_delay * c / 2.0f;
        if (range > max_range) max_range = range;
    }

    float range_max = std::max(max_range * 1.2f, 100.0f);

    // ---- Step 2: Matched filter reference (LFM chirp) ----
    size_t M = 1;
    while (M < R + size_t(fs * Tp)) M <<= 1;

    std::vector<std::complex<float>> h_ref(M, 0.0f);
    int Tp_samples = int(fs * Tp);
    int h_center = Tp_samples / 2;
    for (int n = 0; n < Tp_samples; ++n) {
        float t = float(n - h_center) / fs;
        float phase = 3.14159265f * K * t * t;
        if (n >= 0 && n < int(M))
            h_ref[n] = std::complex<float>(std::cos(phase), -std::sin(phase));
    }
    fft_radar(h_ref);

    // ---- Step 3: Compute coherent Doppler parameters per target ----
    // Doppler comes from two physical sources:
    //   1. Platform motion:  fd = 2 * v_los / lambda, where v_los is the
    //      sensor velocity projected onto the line of sight (radial).
    //   2. Beam scanning:    fd = 2 * v_eff * cross_range / (lambda * range)
    //      (simulates a scanning/rotating radar as a fallback when the
    //      platform is stationary).
    const float v_sensor = std::sqrt(cfg.sensor_velocity.x * cfg.sensor_velocity.x +
                                     cfg.sensor_velocity.y * cfg.sensor_velocity.y +
                                     cfg.sensor_velocity.z * cfg.sensor_velocity.z);
    const float v_eff = 0.0f;
    spdlog::info("RadarPulseProcessor: platform velocity ({:.2f},{:.2f},{:.2f}) m/s "
                  "(v_sensor={:.2f}, v_eff={:.2f})",
                  cfg.sensor_velocity.x, cfg.sensor_velocity.y, cfg.sensor_velocity.z,
                  v_sensor, v_eff);

    struct TargetInfo {
        const ScatterPoint* sp;
        float range;          // one-way slant range (m)
        float cross_range;    // azimuth offset from boresight (m)
        float doppler_hz;     // Doppler frequency (Hz)
        float range_bin;      // range gate index (float)
    };

    std::vector<TargetInfo> targets;
    targets.reserve(sps.size());

    for (const auto& sp : sps) {
        float dx = sp.position.x - cfg.sensor_position.x;
        float dy = sp.position.y - cfg.sensor_position.y;
        float dz = sp.position.z - cfg.sensor_position.z;
        float range = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (range < 1.0f) continue;

        // Cross-range: horizontal component perpendicular to boresight (0,0,-1)
        float cross_range = dx;

        // Radial velocity along the line of sight (approaching => +).
        float v_los = (cfg.sensor_velocity.x * dx +
                       cfg.sensor_velocity.y * dy +
                       cfg.sensor_velocity.z * dz) / range;

        float fd = 2.0f * (v_los + v_eff * cross_range / std::max(range, 1.0f)) / lambda;
        float tau = 2.0f * range / c;
        float range_bin = tau * fs;

        targets.push_back({&sp, range, cross_range, fd, range_bin});
    }

    // ---- Step 4: Generate fast-time signal + pulse compression per pulse ----
    SignalFrame frame;
    frame.resize(R, D);
    frame.fs = fs;
    frame.prf = cfg.prf;
    frame.carrier_hz = cfg.carrier_hz;
    frame.sensor_type = SensorType::RADAR;
    frame.sensor_id = sensor_id;
    frame.sim_time = cfg.sim_time;
    frame.frame_id = cfg.frame_id;
    frame.coherent_pulses = D;
    frame.range_min = 0.0f;
    frame.range_max = range_max;

    auto hamming_r = hamming_cached(R);

    for (uint32_t pi = 0; pi < D; ++pi) {
        work_buf_.assign(M, std::complex<float>(0.0f, 0.0f));
        auto& s = work_buf_;

        for (const auto& t : targets) {
            float slow_time = pi * PRI;
            float doppler_phase = 2.0f * 3.14159265f * t.doppler_hz * slow_time;
            std::complex<float> phase_rot(std::cos(doppler_phase), std::sin(doppler_phase));

            std::complex<float> iq(t.sp->i_amplitude, t.sp->q_amplitude);
            int center_bin = int(t.range_bin);

            for (int n = -Tp_samples / 2; n < Tp_samples / 2; ++n) {
                int idx = center_bin + n;
                if (idx < 0 || idx >= int(M)) continue;
                float tn = float(n) / fs;
                float chirp_phase = 3.14159265f * K * tn * tn;
                std::complex<float> chirp(std::cos(chirp_phase), std::sin(chirp_phase));
                s[idx] += iq * chirp * phase_rot;
            }
        }

        // Matched filter pulse compression
        fft_radar(s);
        for (size_t i = 0; i < M; ++i) s[i] *= h_ref[i];
        ifft_radar(s);

        // Decimate to range gates + window
        for (uint32_t ri = 0; ri < R; ++ri) {
            size_t src = size_t(float(ri) / float(R) * M);
            if (src >= M) src = M - 1;
            frame.samples[pi * R + ri] = s[src] * hamming_r[ri];
        }
    }

    // ---- Step 5: Normalize range profiles ----
    {
        float peak = 0.0f;
        for (auto& v : frame.samples) peak = std::max(peak, std::abs(v));
        if (peak > 1e-12f)
            for (auto& v : frame.samples) v /= peak;
    }

    // ---- Step 6: Doppler FFT (slow-time FFT per range gate) ----
    // Transpose (pulse, range) → (range, pulse)
    std::vector<std::complex<float>> rd_buf(R * D);
    for (uint32_t ri = 0; ri < R; ++ri)
        for (uint32_t pi = 0; pi < D; ++pi)
            rd_buf[ri * D + pi] = frame.samples[pi * R + ri];

    const auto& hamming_d = hamming_cached(D);
    az_buf_.assign(D, std::complex<float>(0.0f, 0.0f));
    for (uint32_t ri = 0; ri < R; ++ri) {
        for (uint32_t pi = 0; pi < D; ++pi) az_buf_[pi] = 0.0f;
        for (uint32_t pi = 0; pi < D; ++pi)
            az_buf_[pi] = rd_buf[ri * D + pi] * hamming_d[pi];
        fft_radar(az_buf_);
        for (uint32_t pi = 0; pi < D; ++pi)
            rd_buf[ri * D + pi] = az_buf_[pi];
    }

    // Store final RD map as (range, Doppler) — ri major
    for (uint32_t ri = 0; ri < R; ++ri)
        for (uint32_t pi = 0; pi < D; ++pi)
            frame.samples[ri * D + pi] = rd_buf[ri * D + pi];

    // Normalize to [0,1]
    {
        float peak = 0.0f;
        for (auto& v : frame.samples) peak = std::max(peak, std::abs(v));
        if (peak > 1e-12f)
            for (auto& v : frame.samples) v /= peak;
    }

    // Diagnostic: distribution of the final RD-map complex samples.
    {
        float smin = 1e30f, smax = 0.0f, smean = 0.0f;
        std::size_t nz = 0;
        for (const auto& v : frame.samples) {
            const float m = std::abs(v);
            smin = std::min(smin, m);
            smax = std::max(smax, m);
            smean += m;
            if (m > 1e-4f) ++nz;
        }
        smean /= float(frame.samples.size());
        spdlog::debug("RadarPulseProcessor: RD samples n={} min={:.3e} max={:.3f} mean={:.3e} nonzero(>1e-4)={}",
                      frame.samples.size(), smin, smax, smean, nz);
    }

    // Build axes
    frame.range_axis.resize(R);
    for (uint32_t ri = 0; ri < R; ++ri)
        frame.range_axis[ri] = float(ri) / float(R) * range_max;

    float fd_max = cfg.prf / 2.0f;
    frame.doppler_axis.resize(D);
    for (uint32_t pi = 0; pi < D; ++pi) {
        float fd = float(pi) / float(D) * cfg.prf;
        if (fd > fd_max) fd -= cfg.prf;
        frame.doppler_axis[pi] = fd;
    }

    spdlog::info("RadarPulseProcessor: {} pulses × {} range gates, {} targets, PRF={:.1f}Hz",
                 D, R, targets.size(), cfg.prf);

    // Debug: export the raw RD-map complex samples (I/Q) + axes for offline
    // inspection, and print IQ statistics.  Written once per frame (last
    // sensor wins) to /tmp/vsgraysim_rdmap_iq.bin.
    if (spdlog::should_log(spdlog::level::debug)) {
        {
            std::ofstream ofs("/tmp/vsgraysim_rdmap_iq.bin", std::ios::binary);
            if (ofs) {
                uint32_t w = R, h = D;
                float hdr[5] = { fs, cfg.prf, static_cast<float>(cfg.carrier_hz), range_max, 0.0f };
                ofs.write(reinterpret_cast<const char*>(&w), 4);
                ofs.write(reinterpret_cast<const char*>(&h), 4);
                ofs.write(reinterpret_cast<const char*>(hdr), 5 * 4);
                ofs.write(reinterpret_cast<const char*>(frame.samples.data()),
                           std::streamsize(frame.samples.size() * sizeof(std::complex<float>)));
            }
        }

        double sum_i = 0.0, sum_q = 0.0, sum_ph = 0.0;
        float imin = 1e30f, imax = -1e30f, qmin = 1e30f, qmax = -1e30f;
        std::size_t nz = 0;
        for (const auto& z : frame.samples) {
            if (std::abs(z) > 1e-6f) {
                ++nz;
                sum_i += z.real(); sum_q += z.imag();
                sum_ph += std::atan2(z.imag(), z.real());
                imin = std::min(imin, z.real()); imax = std::max(imax, z.real());
                qmin = std::min(qmin, z.imag()); qmax = std::max(qmax, z.imag());
            }
        }
        float peak_mag = 0.0f; uint32_t pk_ri = 0, pk_pi = 0;
        for (uint32_t ri = 0; ri < R; ++ri)
            for (uint32_t pi = 0; pi < D; ++pi)
                if (std::abs(frame.samples[ri * D + pi]) > peak_mag) {
                    peak_mag = std::abs(frame.samples[ri * D + pi]);
                    pk_ri = ri; pk_pi = pi;
                }
        spdlog::debug("RadarPulseProcessor IQ: nz={}/{} I=[{:.3e},{:.3e}] mean={:.3e} "
                      "Q=[{:.3e},{:.3e}] mean={:.3e} phase_mean={:.3f} rad "
                      "peak|z|={:.3f} at (range_bin {}, doppler_bin {}) => range={:.1f}m fd={:.1f}Hz",
                      nz, frame.samples.size(), imin, imax, nz ? sum_i / nz : 0.0,
                      qmin, qmax, nz ? sum_q / nz : 0.0,
                      nz ? sum_ph / nz : 0.0,
                      peak_mag, pk_ri, pk_pi,
                      frame.range_axis[pk_ri], frame.doppler_axis[pk_pi]);
    }

    return frame;
}

void RadarPulseProcessor::reset() {
    pulse_buf_.clear();
    ref_buf_.clear();
    chirp_cache_.clear();
    chirp_cache_n_=-1;
    cpi_profiles_.clear();
    cpi_slow_time_.clear();
    active_config_.reset();
}

} // namespace vsgraysim
