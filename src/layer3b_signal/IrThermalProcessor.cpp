#include "vsgraysim/layer3b_signal/SensorSignalProcessor.h"
#include "vsgraysim/layer3b_signal/CameraProjection.h"

#include <cmath>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace vsgraysim {

// ====================================================================
// IrThermalProcessor
// ====================================================================

float IrThermalProcessor::planckRadiance(float T, float lambda) const {
    // Planck's law: L(λ,T) = (2hc²/λ⁵) / (exp(hc/λkT) - 1)
    const float h = 6.62607015e-34f;
    const float c = 2.99792458e8f;
    const float k = 1.380649e-23f;
    float c1 = 2.0f * h * c * c;
    float c2 = h * c / k;
    float lambda5 = lambda * lambda * lambda * lambda * lambda;
    float exponent = c2 / (lambda * T);
    if (exponent > 100.0f) return 0.0f;  // avoid overflow
    return (c1 / lambda5) / (std::exp(exponent) - 1.0f);
}

SignalFrame IrThermalProcessor::process(const std::vector<ScatterPoint>& sps,
                                          const SignalFormationConfig& cfg,
                                          uint32_t sensor_id) {
    std::seed_seq seed{42u,sensor_id,uint32_t(cfg.frame_id),uint32_t(cfg.frame_id>>32)};
    rng_.seed(seed);
    uint32_t W = cfg.image_width;
    uint32_t H = cfg.image_height;
    float f = cfg.focal_length;
    float px = cfg.pixel_pitch;
    float f_stop = cfg.f_number;
    float tint = cfg.integration_time;
    float lambda = 10.0e-6f;  // LWIR 10 μm
    float NETD = cfg.netd;
    float R = cfg.responsivity;

    if (W == 0 || H == 0) { W = 640; H = 480; }

    // Accumulate per-pixel radiance
    std::vector<float> pixel_radiance(W * H, 0.0f);
    std::vector<uint32_t> pixel_count(W * H, 0);
    float cx = float(W) / 2.0f;
    float cy = float(H) / 2.0f;

    // Optical parameters
    float lens_area = 3.14159265f * f * f / (4.0f * f_stop * f_stop);
    float tau_optics = 0.9f;
    float atm_transmission = 0.8f;
    float IFOV_sr = (px / f) * (px / f);  // instantaneous FOV solid angle

    const CameraProjection camera(cfg);
    for (const auto& sp : sps) {
        double X,Y,depth;
        if(!camera.point(sp.position,X,Y,depth)) continue;

        // Perspective projection
        float x_img = f / px * X / depth + cx;
        float y_img = f / px * Y / depth + cy;
        int px_i = int(std::round(x_img));
        int py_i = int(std::round(y_img));
        if (px_i < 0 || px_i >= int(W) || py_i < 0 || py_i >= int(H)) continue;

        // Target temperature from scatter point amplitude (model: T ~ 300K + amplitude scaling)
        float T_target = 300.0f + sp.amplitude * 50.0f;
        float emissivity = 0.95f;

        // Spectral radiance at sensor (after atmosphere)
        float L_target = emissivity * planckRadiance(T_target, lambda);
        float L_at_sensor = L_target * tau_optics * atm_transmission;

        // Add path radiance (simplified: 10% of target radiance)
        float L_path = 0.1f * L_at_sensor;

        size_t idx = size_t(py_i) * W + px_i;
        pixel_radiance[idx] += L_at_sensor + L_path;
        pixel_count[idx]++;
    }

    // Average overlapping points
    for (size_t i = 0; i < pixel_radiance.size(); ++i) {
        if (pixel_count[i] > 1)
            pixel_radiance[i] /= float(pixel_count[i]);
    }

    // Add background radiance for empty pixels (300K background)
    float L_bg = 0.95f * planckRadiance(300.0f, lambda) * tau_optics * atm_transmission;
    for (size_t i = 0; i < pixel_radiance.size(); ++i) {
        if (pixel_count[i] == 0)
            pixel_radiance[i] = L_bg;
    }

    // Convert radiance to voltage/DN
    std::vector<float> raw(W * H);
    for (size_t i = 0; i < pixel_radiance.size(); ++i) {
        float power_on_detector = pixel_radiance[i] * lens_area * IFOV_sr * tint;
        float voltage = power_on_detector * R;

        // Non-uniformity correction (simplified: gain + offset per pixel)
        float gain_nuc = 1.0f + std::normal_distribution<float>(0.0f, 0.02f)(rng_);
        float offset_nuc = std::normal_distribution<float>(0.0f, 0.005f)(rng_);
        voltage = voltage * gain_nuc + offset_nuc;

        // NETD noise
        float noise_std = NETD * R * power_on_detector / 300.0f;
        if(noise_std>0) voltage += std::normal_distribution<float>(0.0f, noise_std)(rng_);

        raw[i] = std::max(0.0f, voltage);
    }

    // Apply AGC for display
    applyAGC(raw, W, H);

    // Build SignalFrame
    SignalFrame frame;
    frame.sim_time=cfg.sim_time; frame.frame_id=cfg.frame_id;
    frame.resize(W, H);
    frame.carrier_hz = 3e8f / lambda;
    frame.sensor_type = SensorType::IR;
    frame.sensor_id = sensor_id;

    for (size_t i = 0; i < W * H; ++i)
        frame.samples[i] = std::complex<float>(raw[i], 0.0f);

    spdlog::info("IrThermalProcessor: {}×{} thermal image, {} scatter points", W, H, sps.size());
    return frame;
}

void IrThermalProcessor::applyAGC(std::vector<float>& image, uint32_t w, uint32_t h) {
    // Histogram-based auto gain control
    size_t N = size_t(w) * h;
    if (N == 0) return;

    // Find percentiles
    std::vector<float> sorted = image;
    std::sort(sorted.begin(), sorted.end());
    float p_low = sorted[size_t(N * 0.02f)];
    float p_high = sorted[size_t(N * 0.98f)];
    float range = p_high - p_low;
    if (range < 1e-12f) range = 1.0f;

    for (auto& v : image) {
        v = (v - p_low) / range;
        v = std::clamp(v, 0.0f, 1.0f);
    }
}

void IrThermalProcessor::reset() {}

} // namespace vsgraysim
