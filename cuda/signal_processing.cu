/**
 * signal_processing.cu
 *
 * Layer 3: CUDA signal-processing kernels.
 *
 *   - applyAtmosphericCoupling(): mixes atmospheric attenuation / scatter
 *     into the per-sensor raw signals (per section 3.1.3).
 *   - processCrossInduction(): separates / modulates / re-injects crosstalk
 *     signals into the receiving sensor's stream (per section 3.2.2 (5)).
 *   - generateSensorImage(): simulates the detector sampling, noise and
 *     optical distortion for the requested sensor type.
 *
 * Host wrappers expose plain C ABI so they can be called from the C++ side
 * without nvcc bleeding into the rest of the build.
 */

#include <cuda_runtime.h>
#include <cstdio>
#include <cstdint>
#include <cmath>

#include "vsgraysim/core/RayPayload.h"
#include "vsgraysim/core/BandTypes.h"

namespace vsgraysim {

// ---------------------------------------------------------------------------
// Device-side helpers
// ---------------------------------------------------------------------------

__device__ __forceinline__
float band_channel_to_band_mask(int ch) {
    switch (ch) {
        case 0: return 0x1u;  // VIS
        case 1: return 0x2u;  // IR
        case 2: return 0x4u;  // RADAR
        case 3: return 0x8u;  // SAR
    }
    return 0;
}

__device__ __forceinline__
int band_mask_to_channel(uint32_t mask) {
    if (mask & 0x2u) return 1;
    if (mask & 0x4u) return 2;
    if (mask & 0x8u) return 3;
    return 0;
}

// ---------------------------------------------------------------------------
// Kernel: apply atmospheric coupling to a scatter-point list
// ---------------------------------------------------------------------------
__global__
void applyAtmosphericCoupling_kernel(ScatterPoint* sps,
                                    std::size_t    n,
                                    float          sensor_sensitivity,
                                    float          noise_floor) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= (int)n) return;

    ScatterPoint sp = sps[idx];

    // Atmospheric attenuation: amplitude *= L_atm
    float amp = sp.amplitude * sp.atm_atten;
    // Atmospheric scatter noise (Poisson-like)
    float scatter_noise = sp.atm_scatter * 1e-3f;

    // Below sensitivity threshold -> mark as no-detection
    if (amp < sensor_sensitivity) {
        sp.amplitude = 0.0f;
        sps[idx] = sp;
        return;
    }

    // Add Gaussian-ish noise (cheap LCG; not cryptographic)
    uint32_t s = (uint32_t)(idx * 2654435761u) ^ 0x9E3779B9u;
    s ^= s >> 13; s *= 0x85ebca6b; s ^= s >> 16;
    float u1 = (float)(s & 0xFFFF) / 65535.0f;
    float u2 = (float)((s >> 16) & 0xFFFF) / 65535.0f;
    float gauss = sqrtf(-2.0f * logf(u1 + 1e-9f)) * cosf(2.0f * 3.14159265358979f * u2);

    amp += gauss * (noise_floor + scatter_noise);

    sp.amplitude = amp;
    sps[idx] = sp;
}

// ---------------------------------------------------------------------------
// Kernel: cross-induction processor (separate, modulate, inject)
// ---------------------------------------------------------------------------
__global__
void processCrossInduction_kernel(ScatterPoint* sps,
                                 std::size_t    n,
                                 float*         sensor_streams, // [n_sensors * stream_len]
                                 uint32_t       n_sensors,
                                 uint32_t       stream_len) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= (int)n) return;

    ScatterPoint sp = sps[idx];
    if (!sp.has_cross) return;

    uint32_t rx = sp.cross_rx_sensor_id;
    if (rx >= n_sensors) return;

    // 1. Separate: subtract the cross contribution from the main stream
    //    (here we use a simple delta on the time-delay bin)
    uint32_t delay_bin = (uint32_t)(sp.time_delay * 1e6f) % stream_len;
    float old = sensor_streams[rx * stream_len + delay_bin];

    // 2. Modulate & inject: crosstalk amplitude already includes band_match
    //    (it is baked into cross_power at detection time, see
    //    CrossInductionDetector::computeCrossPower).
    float cross_amp = sp.cross_power;

    // 3. Inject: add crosstalk to the normal stream
    sensor_streams[rx * stream_len + delay_bin] = old + cross_amp;
}

// ---------------------------------------------------------------------------
// Kernel: cross-induction DETECTION (GPU port of CrossInductionDetector::detect)
//
// Per scatter point: walk the routes emitted by its tx sensor, pick the
// highest-priority (lowest priority value) route whose receiver aperture
// passes all judgement criteria (FoV, distance, sensitivity), then stamp
// has_cross / cross_power / cross_rx_sensor_id directly on the point.
// This keeps the detection entirely on the device so no CPU round-trip of
// the scatter buffer is required.
// ---------------------------------------------------------------------------
__global__
void detectCrossInduction_kernel(ScatterPoint*       sps,
                                 std::size_t         n,
                                 const CrossRouteGPU* routes,
                                 uint32_t            n_routes,
                                 const ApertureGPU*  apertures,
                                 uint32_t            n_apertures,
                                 float               sensitivity_w) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= (int)n) return;

    ScatterPoint sp = sps[idx];
    sp.has_cross = 0;

    // Empty / noise-floor points carry no crosstalk.
    if (sp.amplitude <= 0.0f) { sps[idx] = sp; return; }

    // Distance travelled mirrors the CPU path: |position| from the
    // transmitter snapshot (dir fixed to (0,0,-1) toward the receivers).
    const float distance = sqrtf(sp.position.x * sp.position.x +
                                 sp.position.y * sp.position.y +
                                 sp.position.z * sp.position.z);
    if (distance <= 0.0f) { sps[idx] = sp; return; }

    int   best_priority = 0x7fffffff;
    float best_power    = 0.0f;
    uint32_t best_rx    = 0;
    bool  found         = false;

    for (uint32_t ri = 0; ri < n_routes; ++ri) {
        const CrossRouteGPU rt = routes[ri];
        if (rt.tx_sensor_id != sp.tx_sensor_id) continue;

        // Locate the receiver aperture.
        const ApertureGPU* ap = nullptr;
        for (uint32_t ai = 0; ai < n_apertures; ++ai) {
            if (apertures[ai].sensor_id == rt.rx_sensor_id) {
                ap = &apertures[ai];
                break;
            }
        }
        if (!ap) continue;

        // FoV check: angle between the back-propagation direction (0,0,+1)
        // and the aperture boresight must be <= half_angle.
        // neg_dir = -(0,0,-1) = (0,0,1)
        const float dot = ap->dir[2];
        const float cos_angle = fminf(fmaxf(dot, -1.0f), 1.0f);
        if (acosf(cos_angle) > ap->half_angle) continue;

        // Received crosstalk power: P_cross = P_tx * L_atm * eta * S * band_match
        const float cross_power = sp.amplitude * sp.atm_atten *
                                  rt.coupling_factor * ap->aperture_area *
                                  rt.band_match;
        if (cross_power < sensitivity_w) continue;

        if (rt.priority < best_priority) {
            best_priority = rt.priority;
            best_power    = cross_power;
            best_rx       = rt.rx_sensor_id;
            found         = true;
        }
    }

    if (found) {
        sp.has_cross           = 1;
        sp.cross_power         = best_power;
        sp.cross_rx_sensor_id  = best_rx;
    }
    sps[idx] = sp;
}

// ---------------------------------------------------------------------------
// Kernel: per-sensor image generation (VIS/IR/RADAR/SAR)
// ---------------------------------------------------------------------------
__global__
void generateSensorImage_kernel(ScatterPoint* sps,
                                std::size_t    n,
                                float*         out_image,  // [w*h*bands]
                                uint32_t       width,
                                uint32_t       height,
                                uint32_t       band_channel,
                                float          gain) {
    int px = blockIdx.x * blockDim.x + threadIdx.x;
    int py = blockIdx.y * blockDim.y + threadIdx.y;
    if (px >= (int)width || py >= (int)height) return;

    // Simple accumulation: splat each scatter point onto a 2D grid using
    // its position.  This is intentionally simple - the real per-sensor
    // processing lives in dedicated device functions per band.
    float acc = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        ScatterPoint sp = sps[i];
        int ch = band_mask_to_channel(sp.band_type);
        if (ch != (int)band_channel) continue;
        // Project sp.position to image coordinates (orthographic, top-down).
        int x = (int)(sp.position.x * 0.01f) + (int)(width / 2);
        int y = (int)(sp.position.z * 0.01f) + (int)(height / 2);
        if (x == px && y == py) {
            acc += sp.amplitude * gain;
        }
    }
    out_image[(py * width + px) * 4 + band_channel] = acc;
}

// ---------------------------------------------------------------------------
// Layer 3b on device: signal framing kernels.
//
// These form the per-sensor signal frames (VIS/IR image, RADAR/SAR
// range-Doppler map) directly from the device-resident scatter buffer so
// the scatter points never round-trip through the host.
// ---------------------------------------------------------------------------

/// Cheap per-thread Gaussian sample (Box-Muller over a hash LCG).
__device__ __forceinline__
float gauss_noise(uint32_t seed, uint32_t idx) {
    uint32_t s = seed ^ (idx * 2654435761u) ^ 0x9E3779B9u;
    s ^= s >> 13; s *= 0x85ebca6b; s ^= s >> 16;
    float u1 = (float)(s & 0xFFFF) / 65535.0f;
    float u2 = (float)((s >> 16) & 0xFFFF) / 65535.0f;
    return sqrtf(-2.0f * logf(u1 + 1e-9f)) * cosf(2.0f * 3.14159265358979f * u2);
}

/// atomicMax for non-negative floats (bit trick on the signed int view).
__device__ __forceinline__
void atomicMaxPosFloat(float* addr, float v) {
    if (v <= 0.0f) return;
    atomicMax(reinterpret_cast<int*>(addr), __float_as_int(v));
}

// --- Kernel: scatter statistics (valid / crosstalk counts) -----------------
// Replaces the full-buffer host readback for logging & gating: only two
// 32-bit counters cross the PCIe boundary.
__global__
void scatterStats_kernel(const ScatterPoint* sps, std::size_t n,
                         float noise_floor, uint32_t* out /*[2]*/) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= (int)n) return;
    const ScatterPoint sp = sps[idx];
    if (sp.amplitude > noise_floor)
        atomicAdd(&out[0], 1u);
    if (sp.has_cross)
        atomicAdd(&out[1], 1u);
}

// --- Kernel: VIS/IR image splat (one thread per scatter point) -------------
__global__
void formImageSplat_kernel(const ScatterPoint* sps, std::size_t n,
                           FrameFormGPU cfg,
                           float* energy, uint32_t* counts) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= (int)n) return;
    const ScatterPoint sp = sps[idx];
    if (sp.tx_sensor_id != cfg.sensor_id) return;
    if (sp.amplitude <= cfg.noise_floor) return;

    const float X = sp.position.x;
    const float Y = sp.position.y;
    const float Z = sp.position.z;
    if (Z >= -0.1f) return;                 // behind camera
    const float depth = -Z;

    const float cx = float(cfg.width)  * 0.5f;
    const float cy = float(cfg.height) * 0.5f;
    const float x_img = cfg.focal_length / cfg.pixel_pitch * X / depth + cx;
    const float y_img = cfg.focal_length / cfg.pixel_pitch * Y / depth + cy;
    const int px_i = int(__float2int_rn(x_img));
    const int py_i = int(__float2int_rn(y_img));
    if (px_i < 0 || px_i >= (int)cfg.width ||
        py_i < 0 || py_i >= (int)cfg.height) return;

    const size_t pix = size_t(py_i) * cfg.width + px_i;
    if (cfg.mode == 0) {
        // VIS: diffuse radiance L = albedo * E_sun * NdotL / pi (CPU parity)
        float lx = 0.0f, ly = 0.5f, lz = -1.0f;
        const float ll = sqrtf(lx * lx + ly * ly + lz * lz);
        lx /= ll; ly /= ll; lz /= ll;
        float ndotl = sp.normal.x * lx + sp.normal.y * ly + sp.normal.z * lz;
        ndotl = fmaxf(0.0f, ndotl);
        const float radiance = sp.amplitude * cfg.solar_irradiance * ndotl / 3.14159265f;
        atomicAdd(&energy[pix], radiance);
        atomicAdd(&counts[pix], 1u);
    } else {
        // IR: T ~ 300 K + amplitude scaling -> Planck radiance at 10 um
        const float h = 6.62607015e-34f, c = 2.99792458e8f, k = 1.380649e-23f;
        const float lambda = 10.0e-6f;
        const float T = 300.0f + sp.amplitude * 50.0f;
        const float c1 = 2.0f * h * c * c;
        const float c2 = h * c / k;
        float L = 0.0f;
        const float ex = c2 / (lambda * T);
        if (ex < 100.0f) {
            const float l5 = lambda * lambda * lambda * lambda * lambda;
            L = (c1 / l5) / (expf(ex) - 1.0f);
        }
        const float L_sensor = 0.95f * L * 0.9f * 0.8f;  // emissivity * tau * atm
        atomicAdd(&energy[pix], L_sensor * 1.1f);          // +10% path radiance
        atomicAdd(&counts[pix], 1u);
    }
}

// --- Kernel: VIS/IR image finalize (one thread per pixel) ------------------
__global__
void formImageFinalize_kernel(float* energy, const uint32_t* counts,
                              FrameFormGPU cfg, float* out, float* max_out) {
    int pix = blockIdx.x * blockDim.x + threadIdx.x;
    const int npix = int(cfg.width * cfg.height);
    if (pix >= npix) return;

    if (cfg.mode == 0) {
        // VIS: average, photon -> electrons, shot/read noise, well clamp
        float e = energy[pix];
        if (counts[pix] > 1) e /= float(counts[pix]);
        const float lens_area = 3.14159265f * cfg.focal_length * cfg.focal_length /
                                (4.0f * cfg.f_number * cfg.f_number);
        const float photon_energy = 6.626e-34f * 3e8f / 5.5e-7f;
        float electrons = e * lens_area * cfg.integration_time / photon_energy * 0.7f;
        electrons += gauss_noise(cfg.seed, uint32_t(pix)) *
                     sqrtf(fmaxf(0.0f, electrons));           // shot noise
        electrons += gauss_noise(cfg.seed ^ 0x5A5A5A5Au, uint32_t(pix)) * 5.0f;
        electrons = fminf(fmaxf(electrons, 0.0f), 30000.0f);  // full well
        const float v = electrons / 30000.0f;                 // normalize to well
        out[pix] = v;
        atomicMaxPosFloat(max_out, v);
    } else {
        // IR: background fill, radiance -> voltage, NUC + NETD noise
        float v;
        if (counts[pix] == 0) {
            v = 0.5f;   // uniform 300 K background plateau (post-AGC domain)
        } else {
            float L = energy[pix] / float(counts[pix]);
            const float px_rad = cfg.pixel_pitch / cfg.focal_length;
            const float ifov = px_rad * px_rad;
            const float lens_area = 3.14159265f * cfg.focal_length *
                                    cfg.focal_length /
                                    (4.0f * cfg.f_number * cfg.f_number);
            const float power = L * lens_area * ifov * cfg.integration_time;
            v = power * cfg.responsivity;
            v *= 1.0f + 0.02f * gauss_noise(cfg.seed, uint32_t(pix));
            v += 0.005f * gauss_noise(cfg.seed ^ 0xA5A5A5A5u, uint32_t(pix));
            const float noise_std = cfg.netd * cfg.responsivity * power / 300.0f;
            v += noise_std * gauss_noise(cfg.seed ^ 0x3C3C3C3Cu, uint32_t(pix));
            v = fmaxf(0.0f, v);
        }
        out[pix] = v;
        atomicMaxPosFloat(max_out, v);
    }
}

// --- Kernel: normalize an image by its max (AGC surrogate) -----------------
__global__
void normalizeImage_kernel(float* img, uint32_t n, const float* max_v) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= (int)n) return;
    const float m = *max_v;
    if (m > 1e-12f) img[idx] /= m;
}

// --- Kernel: RADAR/SAR range-Doppler splat (one thread per scatter point) ---
// GPU framing uses direct geometry -> RD bin splatting (no per-pulse chirp
// matched filter): range bin from two-way delay, Doppler bin from radial +
// cross-range velocity, complex I/Q accumulated with atomic adds.
__global__
void formRdSplat_kernel(const ScatterPoint* sps, std::size_t n,
                        FrameFormGPU cfg, float2* rd) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= (int)n) return;
    const ScatterPoint sp = sps[idx];
    if (sp.tx_sensor_id != cfg.sensor_id) return;
    if (sp.amplitude <= cfg.noise_floor) return;

    const float dx = sp.position.x - cfg.sensor_pos[0];
    const float dy = sp.position.y - cfg.sensor_pos[1];
    const float dz = sp.position.z - cfg.sensor_pos[2];
    const float range = sqrtf(dx * dx + dy * dy + dz * dz);
    if (range < 1.0f) return;

    // Range bin mirrors the CPU path: bin = 2*range/c*fs
    const float rbin = 2.0f * range / 3e8f * cfg.fs;
    if (rbin >= float(cfg.width)) return;

    // Doppler (CPU parity): fd = 2*(v_los + v_eff*cross/range)/lambda
    const float v_sensor = sqrtf(cfg.sensor_vel[0] * cfg.sensor_vel[0] +
                                 cfg.sensor_vel[1] * cfg.sensor_vel[1] +
                                 cfg.sensor_vel[2] * cfg.sensor_vel[2]);
    const float v_eff = 0.0f; // no synthetic cross-range velocity
    const float v_los = (cfg.sensor_vel[0] * dx + cfg.sensor_vel[1] * dy +
                         cfg.sensor_vel[2] * dz) / range;
    const float lambda = 3e8f / fmaxf(cfg.carrier_hz, 1.0f);
    const float fd = 2.0f * (v_los + v_eff * dx / fmaxf(range, 1.0f)) / lambda;
    if (cfg.prf <= 0.0f) return;

    int dbin = int(__float2int_rn(fd / cfg.prf * float(cfg.height)));
    dbin %= int(cfg.height);
    if (dbin < 0) dbin += int(cfg.height);

    const size_t cell = size_t(dbin) * cfg.width + size_t(int(rbin));
    atomicAdd(&rd[cell].x, sp.i_amplitude);
    atomicAdd(&rd[cell].y, sp.q_amplitude);
}

// --- Kernel: RD magnitude reduction (peak for normalization) ---------------
__global__
void rdPeak_kernel(const float2* rd, uint32_t n, float* peak) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= (int)n) return;
    const float2 z = rd[idx];
    atomicMaxPosFloat(peak, sqrtf(z.x * z.x + z.y * z.y));
}

// --- Kernel: normalize RD map by peak --------------------------------------
__global__
void rdNormalize_kernel(float2* rd, uint32_t n, const float* peak) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= (int)n) return;
    const float p = *peak;
    if (p > 1e-12f) { rd[idx].x /= p; rd[idx].y /= p; }
}

// --- Kernel: complex RD -> dB map (display-side GPU processing) -------------
// Runs on the broker channel device buffer (via GpuBufferHandle) so the
// display console converts the raw RD map without downloading the I/Q data.
__global__
void rdToDb_kernel(const float2* rd, float* db, uint32_t n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= (int)n) return;
    const float2 z = rd[idx];
    db[idx] = 20.0f * log10f(sqrtf(z.x * z.x + z.y * z.y) + 1e-9f);
}

// ---------------------------------------------------------------------------
// Host wrappers (extern "C" so they can be called from C++ without nvcc)
// ---------------------------------------------------------------------------

} // namespace vsgraysim

extern "C" {

using namespace vsgraysim;

/// Apply atmospheric coupling to a scatter-point buffer.
/// Returns the CUDA error code (cudaSuccess = 0).
int vsgraysim_apply_atmospheric_coupling(ScatterPoint* d_sps,
                                         std::size_t n,
                                         float sensitivity,
                                         float noise_floor) {
    if (!d_sps || n == 0) return cudaSuccess;
    int block = 256;
    int grid = (int)((n + block - 1) / block);
    vsgraysim::applyAtmosphericCoupling_kernel<<<grid, block>>>(
        d_sps, n, sensitivity, noise_floor);
    return cudaGetLastError();
}

/// Process cross-induction events into per-sensor signal streams.
int vsgraysim_process_cross_induction(ScatterPoint* d_sps,
                                      std::size_t n,
                                      float* d_streams,
                                      uint32_t n_sensors,
                                      uint32_t stream_len) {
    if (!d_sps || n == 0) return cudaSuccess;
    int block = 256;
    int grid = (int)((n + block - 1) / block);
    vsgraysim::processCrossInduction_kernel<<<grid, block>>>(
        d_sps, n, d_streams, n_sensors, stream_len);
    return cudaGetLastError();
}

/// Detect cross-induction events on the device (GPU port of
/// CrossInductionDetector::detect).  Stamps has_cross/cross_power/
/// cross_rx_sensor_id on each qualifying scatter point.
int vsgraysim_detect_cross_induction(ScatterPoint* d_sps,
                                     std::size_t n,
                                     const CrossRouteGPU* d_routes,
                                     uint32_t n_routes,
                                     const ApertureGPU* d_apertures,
                                     uint32_t n_apertures,
                                     float sensitivity_w) {
    if (!d_sps || n == 0) return cudaSuccess;
    if (!d_routes || n_routes == 0 || !d_apertures || n_apertures == 0)
        return cudaSuccess;  // nothing to detect against
    int block = 256;
    int grid = (int)((n + block - 1) / block);
    vsgraysim::detectCrossInduction_kernel<<<grid, block>>>(
        d_sps, n, d_routes, n_routes, d_apertures, n_apertures, sensitivity_w);
    return cudaGetLastError();
}

/// Generate a sensor image for a given band channel.
int vsgraysim_generate_sensor_image(ScatterPoint* d_sps,
                                   std::size_t n,
                                   float* d_image,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t band_channel,
                                   float gain) {
    if (!d_image) return cudaSuccess;
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x,
              (height + block.y - 1) / block.y);
    vsgraysim::generateSensorImage_kernel<<<grid, block>>>(
        d_sps, n, d_image, width, height, band_channel, gain);
    return cudaGetLastError();
}

/// Allocate device memory and copy host data.
int vsgraysim_upload_scatter_points(const ScatterPoint* h_sps,
                                    std::size_t n,
                                    ScatterPoint** d_sps_out) {
    if (!d_sps_out) return cudaErrorInvalidValue;
    cudaError_t e = cudaMalloc((void**)d_sps_out, n * sizeof(ScatterPoint));
    if (e != cudaSuccess) return e;
    if (h_sps && n > 0) {
        e = cudaMemcpy(*d_sps_out, h_sps, n * sizeof(ScatterPoint),
                       cudaMemcpyHostToDevice);
    }
    return e;
}

int vsgraysim_free_scatter_points(ScatterPoint* d_sps) {
    return cudaFree(d_sps);
}

/// Scatter statistics (valid / crosstalk counts).  `h_out` receives
/// [valid_count, cross_count]; only two counters cross the PCIe bus.
int vsgraysim_scatter_stats(const ScatterPoint* d_sps,
                            std::size_t n,
                            float noise_floor,
                            uint32_t* h_out) {
    if (!d_sps || !h_out) return cudaErrorInvalidValue;
    uint32_t* d_out = nullptr;
    cudaError_t e = cudaMalloc((void**)&d_out, 2 * sizeof(uint32_t));
    if (e != cudaSuccess) return e;
    e = cudaMemset(d_out, 0, 2 * sizeof(uint32_t));
    if (e == cudaSuccess && n > 0) {
        int block = 256;
        int grid = (int)((n + block - 1) / block);
        scatterStats_kernel<<<grid, block>>>(d_sps, n, noise_floor, d_out);
        e = cudaGetLastError();
        if (e == cudaSuccess)
            e = cudaMemcpy(h_out, d_out, 2 * sizeof(uint32_t),
                           cudaMemcpyDeviceToHost);
    }
    cudaFree(d_out);
    return e;
}

/// Form a VIS (mode 0) or IR (mode 1) image on the device.
/// `d_energy`/`d_counts` are scratch buffers of width*height elements;
/// `d_max` is a single-float scratch.  Result is `width*height` floats in
/// `d_out`.
int vsgraysim_form_image(const ScatterPoint* d_sps,
                         std::size_t n,
                         const FrameFormGPU* h_cfg,
                         float* d_energy,
                         uint32_t* d_counts,
                         float* d_max,
                         float* d_out) {
    if (!d_sps || !h_cfg || !d_energy || !d_counts || !d_max || !d_out)
        return cudaErrorInvalidValue;
    const FrameFormGPU cfg = *h_cfg;
    const uint32_t npix = cfg.width * cfg.height;
    if (npix == 0) return cudaErrorInvalidValue;

    cudaError_t e = cudaMemset(d_energy, 0, size_t(npix) * sizeof(float));
    if (e != cudaSuccess) return e;
    e = cudaMemset(d_counts, 0, size_t(npix) * sizeof(uint32_t));
    if (e != cudaSuccess) return e;
    e = cudaMemset(d_max, 0, sizeof(float));
    if (e != cudaSuccess) return e;

    if (n > 0) {
        int block = 256;
        int grid = (int)((n + block - 1) / block);
        formImageSplat_kernel<<<grid, block>>>(d_sps, n, cfg, d_energy, d_counts);
        e = cudaGetLastError();
        if (e != cudaSuccess) return e;
    }
    {
        int block = 256;
        int grid = (int)((npix + block - 1) / block);
        formImageFinalize_kernel<<<grid, block>>>(d_energy, d_counts, cfg,
                                                  d_out, d_max);
        e = cudaGetLastError();
        if (e != cudaSuccess) return e;
        normalizeImage_kernel<<<grid, block>>>(d_out, npix, d_max);
        e = cudaGetLastError();
    }
    return e;
}

/// Form a RADAR (mode 2) or SAR (mode 3) range-Doppler map on the device.
/// `d_peak` is a single-float scratch.  Result is `width*height` interleaved
/// complex floats in `d_out`.
int vsgraysim_form_rd(const ScatterPoint* d_sps,
                      std::size_t n,
                      const FrameFormGPU* h_cfg,
                      float* d_peak,
                      float2* d_out) {
    if (!d_sps || !h_cfg || !d_peak || !d_out) return cudaErrorInvalidValue;
    const FrameFormGPU cfg = *h_cfg;
    const uint32_t ncell = cfg.width * cfg.height;
    if (ncell == 0) return cudaErrorInvalidValue;

    cudaError_t e = cudaMemset(d_out, 0, size_t(ncell) * sizeof(float2));
    if (e != cudaSuccess) return e;
    e = cudaMemset(d_peak, 0, sizeof(float));
    if (e != cudaSuccess) return e;

    if (n > 0) {
        int block = 256;
        int grid = (int)((n + block - 1) / block);
        formRdSplat_kernel<<<grid, block>>>(d_sps, n, cfg, d_out);
        e = cudaGetLastError();
        if (e != cudaSuccess) return e;
    }
    {
        int block = 256;
        int grid = (int)((ncell + block - 1) / block);
        rdPeak_kernel<<<grid, block>>>(d_out, ncell, d_peak);
        e = cudaGetLastError();
        if (e != cudaSuccess) return e;
        rdNormalize_kernel<<<grid, block>>>(d_out, ncell, d_peak);
        e = cudaGetLastError();
    }
    return e;
}

/// Convert a device-resident complex RD map to a dB magnitude map
/// (display-side GPU processing over a GpuBufferHandle device pointer).
int vsgraysim_rd_to_db(const float2* d_rd, float* d_db, uint32_t n) {
    if (!d_rd || !d_db || n == 0) return cudaErrorInvalidValue;
    int block = 256;
    int grid = (int)((n + block - 1) / block);
    rdToDb_kernel<<<grid, block>>>(d_rd, d_db, n);
    return cudaGetLastError();
}

} // extern "C"
