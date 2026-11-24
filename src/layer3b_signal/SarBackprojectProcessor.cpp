#include "vsgraysim/layer3b_signal/SensorSignalProcessor.h"

#include <cmath>
#include <algorithm>
#include <complex>
#include <spdlog/spdlog.h>

namespace vsgraysim {

namespace {

void fft_sar(std::vector<std::complex<float>>& x) {
    size_t N = x.size();
    if (N <= 1) return;
    for (size_t i = 1, j = 0; i < N; ++i) {
        size_t bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (size_t len = 2; len <= N; len <<= 1) {
        float ang = -2.0f * 3.14159265f / float(len);
        std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < N; i += len) {
            std::complex<float> w(1.0f);
            for (size_t j = 0; j < len / 2; ++j) {
                std::complex<float> u = x[i + j];
                std::complex<float> v = x[i + j + len / 2] * w;
                x[i + j] = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

void ifft_sar(std::vector<std::complex<float>>& x) {
    for (auto& v : x) v = std::conj(v);
    fft_sar(x);
    size_t N = x.size();
    for (auto& v : x) { v = std::conj(v); v /= float(N); }
}

} // anonymous namespace

// ====================================================================
// SarBackprojectProcessor
// ====================================================================

SignalFrame SarBackprojectProcessor::process(const std::vector<ScatterPoint>& sps,
                                               const SignalFormationConfig& cfg,
                                               uint32_t sensor_id) {
    if (sps.empty()) return SignalFrame{};

    uint32_t N_az = std::min(uint32_t(sps.size()), uint32_t(512));
    uint32_t N_rg = cfg.range_gates;
    float fs = cfg.fs;
    float B = cfg.chirp_bandwidth;
    float Tp = cfg.chirp_duration;
    float K = B / Tp;
    const float c = 3e8f;

    // Determine scene extent from scatter point positions
    float x_min = 1e9f, x_max = -1e9f;
    float y_min = 1e9f, y_max = -1e9f;
    float z_min = 1e9f, z_max = -1e9f;
    for (const auto& sp : sps) {
        x_min = std::min(x_min, sp.position.x);
        x_max = std::max(x_max, sp.position.x);
        y_min = std::min(y_min, sp.position.y);
        y_max = std::max(y_max, sp.position.y);
        z_min = std::min(z_min, sp.position.z);
        z_max = std::max(z_max, sp.position.z);
    }

    float scene_size_x = std::max(x_max - x_min, 1.0f);
    float scene_size_z = std::max(z_max - z_min, 1.0f);
    float dx = scene_size_x / float(N_az);
    float dz = scene_size_z / float(N_rg);

    // Simulate aperture positions along the sensor's y-axis (flight direction)
    float aperture_length = scene_size_x * 2.0f;
    float sensor_y = y_max + 10.0f;  // above scene
    float sensor_z = (z_min + z_max) / 2.0f;
    float sensor_x_start = (x_min + x_max) / 2.0f - aperture_length / 2.0f;

    // Build aperture positions
    struct AperturePos {
        float x, y, z;
        std::vector<const ScatterPoint*> hits;
    };
    std::vector<AperturePos> aperture(N_az);
    for (uint32_t ai = 0; ai < N_az; ++ai) {
        aperture[ai].x = sensor_x_start + float(ai) * aperture_length / float(N_az);
        aperture[ai].y = sensor_y;
        aperture[ai].z = sensor_z;
    }

    // Assign scatter points to nearest aperture position
    for (const auto& sp : sps) {
        int ai = int((sp.position.x - sensor_x_start) / aperture_length * float(N_az));
        ai = std::clamp(ai, 0, int(N_az) - 1);
        aperture[ai].hits.push_back(&sp);
    }

    // Generate range-compressed signal for each aperture position
    size_t M = 1;
    while (M < N_rg + size_t(fs * Tp)) M <<= 1;

    // LFM chirp reference
    std::vector<std::complex<float>> h_ref(M, 0.0f);
    int Tp_samples = int(fs * Tp);
    int h_offset = int(fs * Tp / 2.0f);
    for (int n = 0; n < Tp_samples; ++n) {
        float t = float(n - h_offset) / fs;
        float phase = 3.14159265f * K * t * t;
        int idx = n;
        if (idx >= 0 && idx < int(M))
            h_ref[idx] = std::complex<float>(std::cos(phase), -std::sin(phase));
    }
    fft_sar(h_ref);

    // Raw radar data: aperture × range
    SignalFrame frame;
    frame.resize(N_rg, N_az);
    frame.fs = fs;
    frame.carrier_hz = 5e9;
    frame.sensor_type = SensorType::SAR;
    frame.sensor_id = sensor_id;

    float max_range_val = 0.0f;
    for (uint32_t ai = 0; ai < N_az; ++ai) {
        std::vector<std::complex<float>> s(M, 0.0f);
        float ap_x = aperture[ai].x;
        float ap_y = aperture[ai].y;
        float ap_z = aperture[ai].z;

        for (const auto* sp : aperture[ai].hits) {
            float dx_a = sp->position.x - ap_x;
            float dy_a = sp->position.y - ap_y;
            float dz_a = sp->position.z - ap_z;
            float range = std::sqrt(dx_a * dx_a + dy_a * dy_a + dz_a * dz_a);
            float tau = 2.0f * range / c;
            int center_bin = int(tau * fs);
            std::complex<float> iq(sp->i_amplitude, sp->q_amplitude);

            for (int n = -Tp_samples / 2; n < Tp_samples / 2; ++n) {
                int idx = center_bin + n;
                if (idx < 0 || idx >= int(M)) continue;
                float t = float(n) / fs;
                float phase = 3.14159265f * K * t * t;
                s[idx] += iq * std::complex<float>(std::cos(phase), std::sin(phase));
            }
        }

        // Range compression
        fft_sar(s);
        for (size_t i = 0; i < M; ++i) s[i] *= h_ref[i];
        ifft_sar(s);

        for (uint32_t ri = 0; ri < N_rg; ++ri) {
            size_t src_idx = size_t(float(ri) / float(N_rg) * M);
            if (src_idx >= M) src_idx = M - 1;
            frame.samples[ai * N_rg + ri] = s[src_idx];
            float mag = std::abs(frame.samples[ai * N_rg + ri]);
            if (mag > max_range_val) max_range_val = mag;
        }
    }
    if (max_range_val > 1e-12f)
        for (auto& v : frame.samples) v /= max_range_val;

    // ---- Time-domain backprojection ----
    // For each output pixel, sum contributions from all aperture positions
    uint32_t img_w = 128;
    uint32_t img_h = 128;
    SignalFrame sar_image;
    sar_image.resize(img_w, img_h);
    sar_image.sensor_type = SensorType::SAR;
    sar_image.sensor_id = sensor_id;
    sar_image.fs = fs;

    float kc = 2.0f * 3.14159265f * frame.carrier_hz / c;

    for (uint32_t pi = 0; pi < img_h; ++pi) {
        for (uint32_t pj = 0; pj < img_w; ++pj) {
            float wx = x_min + float(pj) / float(img_w) * scene_size_x;
            float wz = z_min + float(pi) / float(img_h) * scene_size_z;
            float wy = (y_min + y_max) / 2.0f;

            std::complex<float> accum(0.0f);
            for (uint32_t ai = 0; ai < N_az; ++ai) {
                float dx_p = wx - aperture[ai].x;
                float dy_p = wy - aperture[ai].y;
                float dz_p = wz - aperture[ai].z;
                float R = std::sqrt(dx_p * dx_p + dy_p * dy_p + dz_p * dz_p);

                float range_bin = 2.0f * R / c * fs;
                int ri = int(range_bin);
                if (ri < 0 || ri >= int(N_rg)) continue;

                std::complex<float> phase_correction(std::cos(2.0f * kc * R), std::sin(2.0f * kc * R));

                // Linear interpolation
                float frac = range_bin - float(ri);
                int ri_next = std::min(ri + 1, int(N_rg) - 1);
                std::complex<float> v0 = frame.samples[ai * N_rg + ri];
                std::complex<float> v1 = frame.samples[ai * N_rg + ri_next];
                std::complex<float> interp = v0 + frac * (v1 - v0);

                accum += interp * phase_correction;
            }
            sar_image.samples[pi * img_w + pj] = accum;
        }
    }

    // Normalize
    float max_sar = 0.0f;
    for (auto& v : sar_image.samples) max_sar = std::max(max_sar, std::abs(v));
    if (max_sar > 1e-12f)
        for (auto& v : sar_image.samples) v /= max_sar;

    sar_image.range_axis.resize(img_w);
    for (uint32_t i = 0; i < img_w; ++i)
        sar_image.range_axis[i] = x_min + float(i) / float(img_w) * scene_size_x;

    sar_image.doppler_axis.resize(img_h);
    for (uint32_t i = 0; i < img_h; ++i)
        sar_image.doppler_axis[i] = z_min + float(i) / float(img_h) * scene_size_z;

    spdlog::info("SarBackprojectProcessor: {} aperture × {} range → {}×{} image, {} pts",
                 N_az, N_rg, img_w, img_h, sps.size());
    return sar_image;
}

void SarBackprojectProcessor::reset() {
}

} // namespace vsgraysim
