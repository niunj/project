#pragma once
/// @file SignalProcessor.h
/// @brief Layer 3: C++ host wrapper for the CUDA signal-processing kernels.
///
/// Loads the `vsgraysim_cuda` shared library at runtime and exposes the
/// three operations defined in `signal_processing.cu` as plain C++ methods
/// on top of the Layer-2 `ScatterPoint` buffer:
///   - applyAtmosphericCoupling
///   - processCrossInduction
///   - generateSensorImage

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include "vsgraysim/core/RayPayload.h"

namespace vsgraysim {

class SignalProcessor {
public:
    SignalProcessor();
    ~SignalProcessor();

    /// Load the CUDA shared library.  Returns false on failure.
    bool init();

    /// Upload a host-side scatter-point array to the device.
    /// Returns the device pointer (owned by this object).
    ScatterPoint* uploadScatterPoints(const std::vector<ScatterPoint>& sps);

    /// Import a Vulkan-exported memory handle (file descriptor) as the
    /// device-side scatter-point buffer.  This avoids the PCIe upload of
    /// scatter points from the Vulkan RT pipeline to CUDA.
    /// On failure the existing device buffer is left untouched.
    bool importExternalScatterBuffer(int fd, std::size_t count);

    /// Whether the device scatter buffer currently aliases an imported
    /// Vulkan external buffer (i.e. zero-copy mapping is active).
    bool hasExternalScatterBuffer() const;

    /// Return the CUDA-accessible device pointer for the current scatter
    /// points.  Valid after uploadScatterPoints() or importExternalScatterBuffer().
    ScatterPoint* getScatterDevicePtr() const;

    /// Apply atmospheric coupling (section 3.1.3).
    bool applyAtmosphericCoupling(float sensitivity_w, float noise_floor_w);

    /// Upload crosstalk routes + receiver apertures to the device so that
    /// cross-induction detection can run fully on the GPU.
    bool uploadCrossRoutes(const std::vector<CrossRouteGPU>& routes,
                           const std::vector<ApertureGPU>& apertures);

    /// Detect cross-induction events on the device (GPU port of
    /// CrossInductionDetector::detect).  Stamps has_cross / cross_power /
    /// cross_rx_sensor_id on the current device scatter buffer.
    /// Requires a prior uploadCrossRoutes() and an imported/uploaded
    /// scatter buffer.
    bool detectCrossInduction(float sensitivity_w = 1e-9f);

    /// Whether detectCrossInduction() is available (CUDA lib exposes the
    /// detection kernel and cross data has been uploaded).
    bool crossDetectionReady() const;

    /// Process cross-induction events into per-sensor signal streams
    /// (section 3.2.2 (5)).  The streams buffer is allocated internally
    /// with `n_sensors * stream_len` floats.
    bool processCrossInduction(uint32_t n_sensors, uint32_t stream_len);

    /// Generate a per-sensor image for the given band channel.
    /// `out_image` is resized to width*height*4 floats (RGBA per pixel).
    bool generateSensorImage(uint32_t width, uint32_t height,
                             uint32_t band_channel, float gain,
                             std::vector<float>& out_image);

    // -------- Layer 3b on device: signal framing (GPU 成帧) --------
    /// Form one sensor's signal frame (VIS/IR image or RADAR/SAR range-
    /// Doppler map) directly from the device-resident scatter buffer.
    /// Returns the per-sensor staging device pointer (valid until the next
    /// formFrame() call for the same sensor), or nullptr on failure.
    /// `out_bytes` receives the frame payload size.
    void* formFrame(const FrameFormGPU& cfg, std::size_t* out_bytes);

    /// Download the last formed frame of a sensor as raw floats
    /// (VIS/IR: w*h real samples; RADAR/SAR: w*h*2 interleaved I/Q).
    /// This is the small final-frame readback used only by visualization /
    /// recording consumers - the scatter buffer itself stays on the GPU.
    bool downloadFrame(uint32_t sensor_id, std::vector<float>& out) const;

    /// Scatter statistics without a full-buffer readback:
    /// `valid` = points above the noise floor, `cross` = crosstalk points.
    bool scatterStats(uint32_t& valid, uint32_t& cross, float noise_floor) const;

    /// Convert a device-resident complex RD map (e.g. a broker channel
    /// buffer reached via GpuBufferHandle) into a dB magnitude map and
    /// return it to the host - display-side GPU signal processing.
    bool rdToDb(const void* dev_complex, uint32_t width, uint32_t height,
                std::vector<float>& db_out);

    /// Free the device-side scatter-point buffer.
    void releaseScatterPoints();

    /// Total number of scatter points currently on the device.
    std::size_t scatterCount() const { return scatter_count_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::size_t           scatter_count_ = 0;
};

using SignalProcessorPtr = std::shared_ptr<SignalProcessor>;

} // namespace vsgraysim
