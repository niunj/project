#include "vsgraysim/layer3_cuda/SignalProcessor.h"

#include <map>
#include <spdlog/spdlog.h>
#include <dlfcn.h>
#include <cuda_runtime.h>

namespace vsgraysim {

// Function-pointer typedefs matching the extern "C" wrappers in signal_processing.cu
typedef int (*fn_apply_atm)(ScatterPoint*, std::size_t, float, float);
typedef int (*fn_process_cross)(ScatterPoint*, std::size_t, float*, uint32_t, uint32_t);
typedef int (*fn_detect_cross)(ScatterPoint*, std::size_t, const CrossRouteGPU*, uint32_t,
                               const ApertureGPU*, uint32_t, float);
typedef int (*fn_gen_image)(ScatterPoint*, std::size_t, float*, uint32_t, uint32_t, uint32_t, float);
typedef int (*fn_upload)(const ScatterPoint*, std::size_t, ScatterPoint**);
typedef int (*fn_free)(ScatterPoint*);
typedef int (*fn_scatter_stats)(const ScatterPoint*, std::size_t, float, uint32_t*);
typedef int (*fn_form_image)(const ScatterPoint*, std::size_t, const FrameFormGPU*,
                             float*, uint32_t*, float*, float*);
typedef int (*fn_form_rd)(const ScatterPoint*, std::size_t, const FrameFormGPU*,
                          float*, float2*);
typedef int (*fn_rd_to_db)(const float2*, float*, uint32_t);

/// Per-sensor GPU frame staging buffer (grow-only, reused across frames).
struct FrameStaging {
    void*        dev   = nullptr;
    std::size_t  bytes = 0;
};

struct SignalProcessor::Impl {
    void*           dl_handle   = nullptr;
    ScatterPoint*   d_sps       = nullptr;
    bool            d_sps_external = false;  ///< true if d_sps is imported, do not cudaFree it

    fn_apply_atm    apply_atm   = nullptr;
    fn_process_cross process_cross = nullptr;
    fn_detect_cross detect_cross = nullptr;
    fn_gen_image    gen_image   = nullptr;
    fn_upload       upload      = nullptr;
    fn_free         free       = nullptr;
    fn_scatter_stats scatter_stats = nullptr;
    fn_form_image   form_image  = nullptr;
    fn_form_rd      form_rd     = nullptr;
    fn_rd_to_db     rd_to_db    = nullptr;

    // Owned device buffers
    float*          d_streams   = nullptr;
    uint32_t        n_sensors   = 0;
    uint32_t        stream_len  = 0;
    float*          d_image     = nullptr;

    // Crosstalk routes / receiver apertures for GPU-side detection.
    CrossRouteGPU*  d_routes    = nullptr;
    uint32_t        n_routes    = 0;
    ApertureGPU*    d_apertures = nullptr;
    uint32_t        n_apertures = 0;

    // CUDA external memory handle for imported Vulkan memory.
    cudaExternalMemory_t ext_mem = nullptr;

    // ---- Layer 3b GPU framing state ----
    /// Per-sensor frame staging (keyed by sensor id).
    std::map<uint32_t, FrameStaging> frame_staging;
    /// Shared scratch for framing kernels.
    float*          d_scratch_energy = nullptr;   ///< w*h floats
    uint32_t*       d_scratch_counts = nullptr;   ///< w*h uints
    float*          d_scratch_scalar = nullptr;   ///< 1 float (max/peak)
    std::size_t     scratch_pixels   = 0;
};

SignalProcessor::SignalProcessor() : impl_(std::make_unique<Impl>()) {}

SignalProcessor::~SignalProcessor() {
    if (impl_->d_sps && impl_->free && !impl_->d_sps_external) {
        impl_->free(impl_->d_sps);
    }
    if (impl_->ext_mem) {
        cudaDestroyExternalMemory(impl_->ext_mem);
    }
    if (impl_->d_routes)    cudaFree(impl_->d_routes);
    if (impl_->d_apertures) cudaFree(impl_->d_apertures);
    if (impl_->d_streams) cudaFree(impl_->d_streams);
    if (impl_->d_image)   cudaFree(impl_->d_image);
    for (auto& [sid, fs] : impl_->frame_staging)
        if (fs.dev) cudaFree(fs.dev);
    if (impl_->d_scratch_energy) cudaFree(impl_->d_scratch_energy);
    if (impl_->d_scratch_counts) cudaFree(impl_->d_scratch_counts);
    if (impl_->d_scratch_scalar) cudaFree(impl_->d_scratch_scalar);
    if (impl_->dl_handle) dlclose(impl_->dl_handle);
}

bool SignalProcessor::init() {
    impl_->dl_handle = dlopen("libvsgraysim_cuda.so", RTLD_NOW | RTLD_GLOBAL);
    if (!impl_->dl_handle) {
        spdlog::error("Layer3: failed to load libvsgraysim_cuda.so: {}", dlerror());
        spdlog::error("Layer3: hint - ensure LD_LIBRARY_PATH includes the build dir & CUDA lib path");
        return false;
    }
    impl_->apply_atm    = (fn_apply_atm)   dlsym(impl_->dl_handle, "vsgraysim_apply_atmospheric_coupling");
    impl_->process_cross= (fn_process_cross) dlsym(impl_->dl_handle, "vsgraysim_process_cross_induction");
    impl_->detect_cross = (fn_detect_cross) dlsym(impl_->dl_handle, "vsgraysim_detect_cross_induction");
    impl_->gen_image    = (fn_gen_image)   dlsym(impl_->dl_handle, "vsgraysim_generate_sensor_image");
    impl_->upload       = (fn_upload)      dlsym(impl_->dl_handle, "vsgraysim_upload_scatter_points");
    impl_->free         = (fn_free)        dlsym(impl_->dl_handle, "vsgraysim_free_scatter_points");
    // Layer 3b GPU framing entry points (optional: older libs fall back
    // to the CPU framing path).
    impl_->scatter_stats = (fn_scatter_stats) dlsym(impl_->dl_handle, "vsgraysim_scatter_stats");
    impl_->form_image    = (fn_form_image)    dlsym(impl_->dl_handle, "vsgraysim_form_image");
    impl_->form_rd       = (fn_form_rd)       dlsym(impl_->dl_handle, "vsgraysim_form_rd");
    impl_->rd_to_db      = (fn_rd_to_db)      dlsym(impl_->dl_handle, "vsgraysim_rd_to_db");
    if (!impl_->form_image || !impl_->form_rd || !impl_->scatter_stats) {
        spdlog::warn("Layer3: GPU framing symbols missing - signal framing "
                     "stays on the CPU (scatter readback retained)");
    }
    if (!impl_->apply_atm || !impl_->process_cross || !impl_->gen_image ||
        !impl_->upload || !impl_->free) {
        spdlog::error("Layer3: missing CUDA symbols in libvsgraysim_cuda.so");
        return false;
    }
    if (!impl_->detect_cross) {
        // Older CUDA libs without the detection kernel: degrade to the
        // CPU cross-induction path instead of failing init.
        spdlog::warn("Layer3: vsgraysim_detect_cross_induction missing - "
                     "GPU cross-induction detection disabled");
    }

    // Probe that a real CUDA device is usable.  cudaErrorOperatingSystem (304)
    // or cudaErrorNoDevice (100) here means the runtime can talk to the driver
    // but no usable context exists.  We log a friendly hint but still return
    // true so the rest of the pipeline degrades gracefully (no scatter points
    // will be uploaded).
    int dev_count = 0;
    cudaError_t ce = cudaGetDeviceCount(&dev_count);
    if (ce != cudaSuccess) {
        spdlog::warn("Layer3: cudaGetDeviceCount failed (err={}): {}",
                    (int)ce, cudaGetErrorString(ce));
        spdlog::warn("Layer3: CUDA operations will be skipped.  Possible causes:");
        spdlog::warn("         * driver/runtime mismatch");
        spdlog::warn("         * LD_LIBRARY_PATH missing CUDA lib dir");
        spdlog::warn("         * no NVIDIA GPU accessible to this process");
    } else {
        if (dev_count == 0) {
            spdlog::warn("Layer3: 0 CUDA devices visible - CUDA processing will be skipped");
        } else {
            cudaDeviceProp prop{};
            cudaGetDeviceProperties(&prop, 0);
            spdlog::info("Layer3: CUDA signal processor initialised (device 0: {}, {} MiB)",
                        prop.name, prop.totalGlobalMem / (1024 * 1024));
        }
    }
    return true;
}

ScatterPoint* SignalProcessor::uploadScatterPoints(const std::vector<ScatterPoint>& sps) {
    if (!impl_->upload) return nullptr;
    if (impl_->d_sps && !impl_->d_sps_external) { impl_->free(impl_->d_sps); }
    impl_->d_sps = nullptr;
    impl_->d_sps_external = false;
    if (impl_->ext_mem) {
        cudaDestroyExternalMemory(impl_->ext_mem);
        impl_->ext_mem = nullptr;
    }
    int e = impl_->upload(sps.data(), sps.size(), &impl_->d_sps);
    if (e != 0) {
        spdlog::error("Layer3: uploadScatterPoints failed (err={})", e);
        return nullptr;
    }
    scatter_count_ = sps.size();
    return impl_->d_sps;
}

bool SignalProcessor::importExternalScatterBuffer(int fd, std::size_t count) {
    if (!impl_->dl_handle) return false;

    // The scatter buffer is a persistent Vulkan SSBO: once imported the
    // mapping stays valid across frames.  Re-importing is not only
    // wasteful but wrong - the OPAQUE fd's ownership transfers to CUDA on
    // import, so a cached fd cannot be consumed twice.
    if (impl_->d_sps_external && impl_->d_sps && scatter_count_ > 0) {
        return true;
    }

    if (fd < 0 || count == 0) return false;

    // Release any previous device buffer.
    if (impl_->d_sps && !impl_->d_sps_external) {
        impl_->free(impl_->d_sps);
    }
    if (impl_->ext_mem) {
        cudaDestroyExternalMemory(impl_->ext_mem);
    }
    impl_->d_sps = nullptr;
    impl_->d_sps_external = false;
    impl_->ext_mem = nullptr;

    cudaExternalMemoryHandleDesc handle_desc{};
    handle_desc.type = cudaExternalMemoryHandleTypeOpaqueFd;
    handle_desc.handle.fd = fd;
    handle_desc.size = count * sizeof(ScatterPoint);

    cudaError_t err = cudaImportExternalMemory(&impl_->ext_mem, &handle_desc);
    if (err != cudaSuccess) {
        spdlog::warn("Layer3: cudaImportExternalMemory failed ({})", cudaGetErrorString(err));
        return false;
    }

    cudaExternalMemoryBufferDesc buf_desc{};
    buf_desc.offset = 0;
    buf_desc.size = handle_desc.size;
    buf_desc.flags = 0;

    void* d_ptr = nullptr;
    err = cudaExternalMemoryGetMappedBuffer(&d_ptr, impl_->ext_mem, &buf_desc);
    if (err != cudaSuccess) {
        spdlog::warn("Layer3: cudaExternalMemoryGetMappedBuffer failed ({})", cudaGetErrorString(err));
        cudaDestroyExternalMemory(impl_->ext_mem);
        impl_->ext_mem = nullptr;
        return false;
    }

    impl_->d_sps = static_cast<ScatterPoint*>(d_ptr);
    impl_->d_sps_external = true;
    scatter_count_ = count;
    spdlog::info("Layer3: imported Vulkan scatter buffer as CUDA device pointer (count={})", count);
    return true;
}

ScatterPoint* SignalProcessor::getScatterDevicePtr() const {
    return impl_->d_sps;
}

bool SignalProcessor::hasExternalScatterBuffer() const {
    return impl_->d_sps_external && impl_->d_sps != nullptr;
}

bool SignalProcessor::applyAtmosphericCoupling(float sensitivity_w, float noise_floor_w) {
    if (!impl_->apply_atm || !impl_->d_sps) return false;
    int e = impl_->apply_atm(impl_->d_sps, scatter_count_, sensitivity_w, noise_floor_w);
    if (e != 0) spdlog::error("Layer3: applyAtmosphericCoupling failed (err={})", e);
    return e == 0;
}

bool SignalProcessor::uploadCrossRoutes(const std::vector<CrossRouteGPU>& routes,
                                        const std::vector<ApertureGPU>& apertures) {
    if (routes.empty() || apertures.empty()) {
        spdlog::warn("Layer3: uploadCrossRoutes got empty routes/apertures - "
                     "GPU cross-induction detection stays disabled");
        return false;
    }
    if (impl_->d_routes)    { cudaFree(impl_->d_routes);    impl_->d_routes = nullptr; }
    if (impl_->d_apertures) { cudaFree(impl_->d_apertures); impl_->d_apertures = nullptr; }
    impl_->n_routes = impl_->n_apertures = 0;

    const std::size_t rb = routes.size() * sizeof(CrossRouteGPU);
    cudaError_t e = cudaMalloc((void**)&impl_->d_routes, rb);
    if (e != cudaSuccess) {
        spdlog::warn("Layer3: cudaMalloc(routes) failed ({})", cudaGetErrorString(e));
        impl_->d_routes = nullptr;
        return false;
    }
    if (cudaMemcpy(impl_->d_routes, routes.data(), rb,
                   cudaMemcpyHostToDevice) != cudaSuccess) {
        spdlog::warn("Layer3: routes H2D copy failed");
        cudaFree(impl_->d_routes);
        impl_->d_routes = nullptr;
        return false;
    }

    const std::size_t ab = apertures.size() * sizeof(ApertureGPU);
    e = cudaMalloc((void**)&impl_->d_apertures, ab);
    if (e != cudaSuccess) {
        spdlog::warn("Layer3: cudaMalloc(apertures) failed ({})", cudaGetErrorString(e));
        cudaFree(impl_->d_routes);
        impl_->d_routes = nullptr;
        impl_->d_apertures = nullptr;
        return false;
    }
    if (cudaMemcpy(impl_->d_apertures, apertures.data(), ab,
                   cudaMemcpyHostToDevice) != cudaSuccess) {
        spdlog::warn("Layer3: apertures H2D copy failed");
        cudaFree(impl_->d_routes);
        cudaFree(impl_->d_apertures);
        impl_->d_routes = nullptr;
        impl_->d_apertures = nullptr;
        return false;
    }

    impl_->n_routes    = static_cast<uint32_t>(routes.size());
    impl_->n_apertures = static_cast<uint32_t>(apertures.size());
    spdlog::info("Layer3: {} crosstalk routes + {} apertures uploaded for GPU detection",
                 impl_->n_routes, impl_->n_apertures);
    return true;
}

bool SignalProcessor::crossDetectionReady() const {
    return impl_->detect_cross && impl_->d_routes && impl_->d_apertures &&
           impl_->n_routes > 0 && impl_->n_apertures > 0;
}

bool SignalProcessor::detectCrossInduction(float sensitivity_w) {
    if (!crossDetectionReady() || !impl_->d_sps) return false;
    int e = impl_->detect_cross(impl_->d_sps, scatter_count_,
                                impl_->d_routes, impl_->n_routes,
                                impl_->d_apertures, impl_->n_apertures,
                                sensitivity_w);
    if (e != 0) {
        spdlog::error("Layer3: detectCrossInduction failed (err={})", e);
        return false;
    }
    // The scatter buffer is shared with Vulkan/the CPU readback path: wait
    // for the kernel so later consumers observe the stamped cross fields.
    cudaDeviceSynchronize();
    return true;
}

bool SignalProcessor::processCrossInduction(uint32_t n_sensors, uint32_t stream_len) {
    if (!impl_->process_cross || !impl_->d_sps) return false;
    if (n_sensors != impl_->n_sensors || stream_len != impl_->stream_len) {
        if (impl_->d_streams) cudaFree(impl_->d_streams);
        std::size_t bytes = (std::size_t)n_sensors * stream_len * sizeof(float);
        cudaError_t e = cudaMalloc((void**)&impl_->d_streams, bytes);
        if (e != cudaSuccess) {
            spdlog::error("Layer3: failed to allocate sensor streams ({})", cudaGetErrorString(e));
            return false;
        }
        cudaMemset(impl_->d_streams, 0, bytes);
        impl_->n_sensors  = n_sensors;
        impl_->stream_len = stream_len;
    }
    int e = impl_->process_cross(impl_->d_sps, scatter_count_, impl_->d_streams,
                                  impl_->n_sensors, impl_->stream_len);
    if (e != 0) spdlog::error("Layer3: processCrossInduction failed (err={})", e);
    return e == 0;
}

bool SignalProcessor::generateSensorImage(uint32_t width, uint32_t height,
                                          uint32_t band_channel, float gain,
                                          std::vector<float>& out_image) {
    if (!impl_->gen_image || !impl_->d_sps) return false;
    std::size_t img_bytes = (std::size_t)width * height * 4 * sizeof(float);
    if (impl_->d_image) cudaFree(impl_->d_image);
    cudaError_t e = cudaMalloc((void**)&impl_->d_image, img_bytes);
    if (e != cudaSuccess) {
        spdlog::error("Layer3: failed to allocate image buffer ({})", cudaGetErrorString(e));
        return false;
    }
    cudaMemset(impl_->d_image, 0, img_bytes);
    int rc = impl_->gen_image(impl_->d_sps, scatter_count_, impl_->d_image,
                              width, height, band_channel, gain);
    if (rc != 0) {
        spdlog::error("Layer3: generateSensorImage failed (err={})", rc);
        return false;
    }
    out_image.resize((std::size_t)width * height * 4);
    cudaMemcpy(out_image.data(), impl_->d_image, img_bytes, cudaMemcpyDeviceToHost);
    return true;
}

void* SignalProcessor::formFrame(const FrameFormGPU& cfg, std::size_t* out_bytes) {
    if (out_bytes) *out_bytes = 0;
    if (!impl_->dl_handle || !impl_->d_sps || scatter_count_ == 0)
        return nullptr;
    const bool image_mode = (cfg.mode <= 1);
    if (image_mode && !impl_->form_image) return nullptr;
    if (!image_mode && !impl_->form_rd)   return nullptr;
    if (cfg.width == 0 || cfg.height == 0) return nullptr;

    const std::size_t pixels  = std::size_t(cfg.width) * cfg.height;
    const std::size_t bytes   = pixels * (image_mode ? sizeof(float)
                                                       : sizeof(float2));

    // Grow-only shared scratch (largest frame seen so far).
    if (pixels > impl_->scratch_pixels) {
        if (impl_->d_scratch_energy) cudaFree(impl_->d_scratch_energy);
        if (impl_->d_scratch_counts) cudaFree(impl_->d_scratch_counts);
        impl_->d_scratch_energy = nullptr;
        impl_->d_scratch_counts = nullptr;
        if (cudaMalloc((void**)&impl_->d_scratch_energy,
                       pixels * sizeof(float)) != cudaSuccess ||
            cudaMalloc((void**)&impl_->d_scratch_counts,
                       pixels * sizeof(uint32_t)) != cudaSuccess) {
            spdlog::error("Layer3: formFrame scratch alloc failed");
            return nullptr;
        }
        impl_->scratch_pixels = pixels;
    }
    if (!impl_->d_scratch_scalar) {
        if (cudaMalloc((void**)&impl_->d_scratch_scalar, sizeof(float))
            != cudaSuccess)
            return nullptr;
    }

    // Per-sensor staging buffer (persists so callers may D2D-copy it later).
    FrameStaging& st = impl_->frame_staging[cfg.sensor_id];
    if (bytes > st.bytes) {
        if (st.dev) cudaFree(st.dev);
        st.dev = nullptr;
        st.bytes = 0;
        if (cudaMalloc(&st.dev, bytes) != cudaSuccess) {
            spdlog::error("Layer3: formFrame staging alloc failed");
            return nullptr;
        }
        st.bytes = bytes;
    }

    int rc;
    if (image_mode) {
        rc = impl_->form_image(impl_->d_sps, scatter_count_, &cfg,
                               impl_->d_scratch_energy, impl_->d_scratch_counts,
                               impl_->d_scratch_scalar,
                               static_cast<float*>(st.dev));
    } else {
        rc = impl_->form_rd(impl_->d_sps, scatter_count_, &cfg,
                            impl_->d_scratch_scalar,
                            static_cast<float2*>(st.dev));
    }
    if (rc != 0) {
        spdlog::error("Layer3: formFrame(mode={}, sensor={}) failed (err={})",
                      cfg.mode, cfg.sensor_id, rc);
        return nullptr;
    }
    // Frame staging is consumed from the host/broker right after: wait so
    // the D2D/D2H copies observe the finished frame.
    cudaDeviceSynchronize();
    if (out_bytes) *out_bytes = bytes;
    return st.dev;
}

bool SignalProcessor::downloadFrame(uint32_t sensor_id,
                                    std::vector<float>& out) const {
    auto it = impl_->frame_staging.find(sensor_id);
    if (it == impl_->frame_staging.end() || !it->second.dev ||
        it->second.bytes == 0)
        return false;
    out.resize(it->second.bytes / sizeof(float));
    return cudaMemcpy(out.data(), it->second.dev, it->second.bytes,
                      cudaMemcpyDeviceToHost) == cudaSuccess;
}

bool SignalProcessor::scatterStats(uint32_t& valid, uint32_t& cross,
                                   float noise_floor) const {
    if (!impl_->scatter_stats || !impl_->d_sps || scatter_count_ == 0)
        return false;
    uint32_t h_out[2] = {0, 0};
    const int e = impl_->scatter_stats(impl_->d_sps, scatter_count_,
                                       noise_floor, h_out);
    if (e != 0) {
        spdlog::warn("Layer3: scatterStats failed (err={})", e);
        return false;
    }
    valid = h_out[0];
    cross = h_out[1];
    return true;
}

bool SignalProcessor::rdToDb(const void* dev_complex, uint32_t width,
                             uint32_t height, std::vector<float>& db_out) {
    if (!impl_->rd_to_db || !dev_complex || width == 0 || height == 0)
        return false;
    const uint32_t n = width * height;
    float* d_db = nullptr;
    if (cudaMalloc((void**)&d_db, std::size_t(n) * sizeof(float)) != cudaSuccess)
        return false;
    const int e = impl_->rd_to_db(static_cast<const float2*>(dev_complex),
                                  d_db, n);
    bool ok = (e == 0);
    if (ok) {
        db_out.resize(n);
        ok = cudaMemcpy(db_out.data(), d_db, std::size_t(n) * sizeof(float),
                        cudaMemcpyDeviceToHost) == cudaSuccess;
    } else {
        spdlog::warn("Layer3: rdToDb failed (err={})", e);
    }
    cudaFree(d_db);
    return ok;
}

void SignalProcessor::releaseScatterPoints() {
    if (impl_->d_sps && !impl_->d_sps_external && impl_->free) {
        impl_->free(impl_->d_sps);
    }
    if (impl_->ext_mem) {
        cudaDestroyExternalMemory(impl_->ext_mem);
        impl_->ext_mem = nullptr;
    }
    impl_->d_sps = nullptr;
    impl_->d_sps_external = false;
    scatter_count_ = 0;
}

} // namespace vsgraysim
