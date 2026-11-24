#include "vsgraysim/layer3b_signal/SensorSignalProcessor.h"
#include "vsgraysim/layer3b_signal/CameraProjection.h"

#include <cmath>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace vsgraysim {

// ====================================================================
// VisCameraProcessor
// ====================================================================

SignalFrame VisCameraProcessor::process(const std::vector<ScatterPoint>& sps,
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
    float E_sun = cfg.solar_irradiance;
    const float h = 6.626e-34f;
    const float c = 3e8f;
    float lambda = 5.5e-7f;  // VIS center wavelength

    if (W == 0 || H == 0) { W = 640; H = 480; }

    // Accumulate per-pixel irradiance
    std::vector<float> pixel_energy(W * H, 0.0f);
    std::vector<uint32_t> pixel_count(W * H, 0);
    float cx = float(W) / 2.0f;
    float cy = float(H) / 2.0f;

    float max_depth = 0.0f;

    const CameraProjection camera(cfg);
    for (const auto& sp : sps) {
        double X,Y,depth;
        if(!camera.point(sp.position,X,Y,depth)) continue;
        max_depth = std::max(max_depth, float(depth));

        float x_img = f / px * X / depth + cx;
        float y_img = f / px * Y / depth + cy;

        int px_i = int(std::round(x_img));
        int py_i = int(std::round(y_img));
        if (px_i < 0 || px_i >= int(W) || py_i < 0 || py_i >= int(H)) continue;

        // Simple diffuse radiance model: L = albedo * E_sun * NdotL / pi
        Vec3 normal = sp.normal;
        Vec3 light_dir = {0.0f, 0.5f, -1.0f};
        float len_l = std::sqrt(light_dir.x * light_dir.x + light_dir.y * light_dir.y + light_dir.z * light_dir.z);
        light_dir = {light_dir.x / len_l, light_dir.y / len_l, light_dir.z / len_l};
        float NdotL = normal.x * light_dir.x + normal.y * light_dir.y + normal.z * light_dir.z;
        NdotL = std::max(0.0f, NdotL);

        // Use amplitude as proxy for BRDF response
        float radiance = sp.amplitude * E_sun * NdotL / 3.14159265f;

        size_t idx = size_t(py_i) * W + px_i;
        pixel_energy[idx] += radiance;
        pixel_count[idx]++;
    }

    // Average overlapping points
    for (size_t i = 0; i < pixel_energy.size(); ++i) {
        if (pixel_count[i] > 1)
            pixel_energy[i] /= float(pixel_count[i]);
    }

    // Apply Gaussian PSF (3×3, sigma = 0.8 px)
    applyGaussianPSF(pixel_energy, W, H, 0.8f);

    // Sensor noise model: shot noise + read noise + quantization
    float QE = 0.7f;       // quantum efficiency
    float read_noise_e = 5.0f;  // read noise (e-)
    float well_capacity = 30000.0f;  // full well (e-)
    float gain = 1.0f;     // e-/DN

    float lens_area = 3.14159265f * f * f / (4.0f * f_stop * f_stop);
    float photon_energy = h * c / lambda;

    std::vector<float> output(W * H);
    float max_out = 0.0f;

    for (size_t i = 0; i < pixel_energy.size(); ++i) {
        // Convert irradiance to electrons
        float photons = pixel_energy[i] * lens_area * tint / photon_energy;
        float electrons = photons * QE;

        // Shot noise (Poisson approximated by Gaussian)
        float shot_noise = std::sqrt(std::max(0.0f, electrons));
        if(shot_noise>0) electrons += std::normal_distribution<float>(0.0f, shot_noise)(rng_);

        // Read noise
        electrons += std::normal_distribution<float>(0.0f, read_noise_e)(rng_);

        // ADC saturation and quantization
        electrons = std::clamp(electrons, 0.0f, well_capacity);
        float DN = electrons / gain;
        DN = std::round(DN);

        output[i] = DN;
        if (DN > max_out) max_out = DN;
    }

    // Normalize to [0, 1] for display
    if (max_out > 1.0f)
        for (auto& v : output) v /= max_out;

    // Build SignalFrame
    SignalFrame frame;
    frame.sim_time=cfg.sim_time; frame.frame_id=cfg.frame_id;
    frame.resize(W, H);
    frame.fs = 0;
    frame.prf = 0;
    frame.carrier_hz = c / lambda;
    frame.sensor_type = SensorType::VIS;
    frame.sensor_id = sensor_id;

    for (size_t i = 0; i < W * H; ++i)
        frame.samples[i] = std::complex<float>(output[i], 0.0f);

    spdlog::info("VisCameraProcessor: {}×{} image from {} scatter points", W, H, sps.size());
    return frame;
}

void VisCameraProcessor::applyGaussianPSF(std::vector<float>& image,
                                            uint32_t w, uint32_t h, float sigma) {
    if (sigma <= 0.0f) return;

    // Separable Gaussian blur (3-sigma kernel)
    int radius = int(std::ceil(3.0f * sigma));
    std::vector<float> kernel(2 * radius + 1);
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        float v = std::exp(-float(i * i) / (2.0f * sigma * sigma));
        kernel[i + radius] = v;
        sum += v;
    }
    for (auto& v : kernel) v /= sum;

    std::vector<float> tmp(w * h);

    // Horizontal
    for (uint32_t py = 0; py < h; ++py) {
        for (uint32_t px = 0; px < w; ++px) {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                int sx = int(px) + k;
                if (sx < 0 || sx >= int(w)) continue;
                acc += image[py * w + sx] * kernel[k + radius];
            }
            tmp[py * w + px] = acc;
        }
    }

    // Vertical
    for (uint32_t py = 0; py < h; ++py) {
        for (uint32_t px = 0; px < w; ++px) {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                int sy = int(py) + k;
                if (sy < 0 || sy >= int(h)) continue;
                acc += tmp[sy * w + px] * kernel[k + radius];
            }
            image[py * w + px] = acc;
        }
    }
}

void VisCameraProcessor::reset() {}

} // namespace vsgraysim
