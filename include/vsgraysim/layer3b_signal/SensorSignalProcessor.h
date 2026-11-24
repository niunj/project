#pragma once

#include <memory>
#include <vector>
#include <complex>
#include <random>
#include <optional>
#include "vsgraysim/core/SensorTypes.h"
#include "vsgraysim/core/RayPayload.h"
#include "vsgraysim/layer3b_signal/SignalFrame.h"

namespace vsgraysim {

/// Pulse-major complex samples in, unshifted slow-time spectrum out.
std::vector<std::complex<float>> slowTimeSpectrum(const std::vector<std::complex<float>>& samples,
                                                 uint32_t rangeBins,uint32_t pulses);

struct SignalFormationConfig {
    float chirp_bandwidth  = 10e6f;
    float chirp_duration   = 10e-6f;
    uint32_t range_gates   = 1024;
    uint32_t doppler_bins  = 256;
    float fs               = 20e6f;
    float prf              = 1000.0f;
    double carrier_hz      = 1e9;

    Vec3  sensor_position{};
    Vec3  sensor_velocity{};   ///< platform velocity (m/s) for Doppler
    Vec3  sensor_direction{0,0,-1};
    Vec3  sensor_up{0,1,0};

    float focal_length     = 0.05f;
    float pixel_pitch      = 5e-6f;
    uint32_t image_width   = 640;
    uint32_t image_height  = 480;
    float f_number         = 2.0f;
    float integration_time = 0.01f;

    float netd             = 0.025f;
    float responsivity     = 1000.0f;
    float solar_irradiance = 1000.0f;

    // --- Real pulse-level timing (filled by SignalSystem) ----------------
    /// Simulation time at frame start (s).  Absolute slow time of this
    /// pulse is sim_time + pulse_slow_time.
    double   sim_time = 0.0;
    uint64_t frame_id = 0;
    /// Slow time of this pulse within the frame (s, fire_time).
    double   pulse_slow_time = 0.0;
    /// Pulse index within the frame (0-based).
    uint32_t pulse_seq = 0;
    /// Total number of pulses this sensor emitted this frame.
    /// 0 = legacy whole-frame mode (all scatter points in one call).
    uint32_t pulses_in_frame = 0;
};

class SensorSignalProcessor {
public:
    virtual ~SensorSignalProcessor() = default;
    virtual SignalFrame process(const std::vector<ScatterPoint>& sps,
                                const SignalFormationConfig& cfg,
                                uint32_t sensor_id) = 0;
    virtual void reset() {}
    static std::unique_ptr<SensorSignalProcessor> create(SensorType type);
};

// ---- Concrete implementations ----

class RadarPulseProcessor : public SensorSignalProcessor {
public:
    SignalFrame process(const std::vector<ScatterPoint>& sps,
                        const SignalFormationConfig& cfg,
                        uint32_t sensor_id) override;
    void reset() override;
private:
    /// Real-pulse CPI path: one call == one pulse.  Accumulates pulse
    /// profiles in a sliding slow-time window and emits the range-Doppler
    /// map when the last pulse of the frame arrives.
    SignalFrame processPulseCPI(const std::vector<ScatterPoint>& sps,
                                const SignalFormationConfig& cfg,
                                uint32_t sensor_id);
    /// Legacy whole-frame path: interleaves all scatter points into
    /// pseudo-pulses (only when pulses_in_frame == 0).
    SignalFrame legacyProcess(const std::vector<ScatterPoint>& sps,
                              const SignalFormationConfig& cfg,
                              uint32_t sensor_id);
    std::mt19937 rng_{42};
    std::vector<std::complex<float>> pulse_buf_;
    std::vector<std::complex<float>> ref_buf_;
    /// Cached LFM chirp samples (avoids recomputing cos/sin per target per
    /// sample — the dominant cost in the per-pulse matched-filter loop).
    std::vector<std::complex<float>> chirp_cache_;
    int chirp_cache_n_ = -1;
    /// 复用工作缓冲: 距离压缩谱域缓冲与慢时间 FFT 行缓冲 (避免每脉冲堆分配)。
    std::vector<std::complex<float>> work_buf_;
    std::vector<std::complex<float>> az_buf_;
    // --- Real-CPI state (sliding slow-time window) ---
    std::vector<std::vector<std::complex<float>>> cpi_profiles_;
    std::vector<double> cpi_slow_time_;
    std::optional<SignalFormationConfig> active_config_;
    static void fft(std::vector<std::complex<float>>& x);
    static void ifft(std::vector<std::complex<float>>& x);
};

class SarBackprojectProcessor : public SensorSignalProcessor {
public:
    SignalFrame process(const std::vector<ScatterPoint>& sps,
                        const SignalFormationConfig& cfg,
                        uint32_t sensor_id) override;
    void reset() override;
private:
    std::mt19937 rng_{42};
    void rangeCompress(const std::vector<ScatterPoint>& aperture,
                       float fs, float B, float Tp,
                       std::vector<std::complex<float>>& out);
    static void fft(std::vector<std::complex<float>>& x);
    static void ifft(std::vector<std::complex<float>>& x);
};

class VisCameraProcessor : public SensorSignalProcessor {
public:
    SignalFrame process(const std::vector<ScatterPoint>& sps,
                        const SignalFormationConfig& cfg,
                        uint32_t sensor_id) override;
    void reset() override;
private:
    std::mt19937 rng_{42};
    void applyGaussianPSF(std::vector<float>& image, uint32_t w, uint32_t h, float sigma);
};

class IrThermalProcessor : public SensorSignalProcessor {
public:
    SignalFrame process(const std::vector<ScatterPoint>& sps,
                        const SignalFormationConfig& cfg,
                        uint32_t sensor_id) override;
    void reset() override;
private:
    std::mt19937 rng_{42};
    float planckRadiance(float T, float lambda) const;
    void applyAGC(std::vector<float>& image, uint32_t w, uint32_t h);
};

} // namespace vsgraysim
