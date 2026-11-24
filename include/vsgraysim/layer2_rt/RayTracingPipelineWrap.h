#pragma once
/// @file RayTracingPipelineWrap.h
/// @brief Layer 2: Vulkan RT pipeline wrapper with full electromagnetic
///        ray-tracing dispatch (vkCmdTraceRaysKHR).
///
/// Owns:
///   - the TLAS / BLAS acceleration structures built from the VSG scene;
///   - the ray-tracing pipeline (raygen / miss / closesthit shader stages);
///   - the Vulkan SSBOs that mirror the Layer-0 CPU buffers;
///   - the shader binding table (SBT) for vkCmdTraceRaysKHR.
///
/// Also implements the Vulkan<->CUDA shared memory interop: the scatter-
/// point SSBO is exported as a Vulkan external memory handle and imported
/// into a CUDA context for the signal-processing kernels (Layer 3).

#include <memory>
#include <vector>
#include <string>
#include <vsg/core/ref_ptr.h>
#include <vsg/vk/Device.h>
#include "vsgraysim/core/RayPayload.h"
#include "vsgraysim/core/GeoUnitPhysParams.h"
#include "vsgraysim/core/AtmLayerParams.h"
#include "vsgraysim/core/SensorTypes.h"
#include "vsgraysim/layer0_phys/CrossInductionRules.h"

namespace vsg {
    class TopLevelAccelerationStructure;
    class BottomLevelAccelerationStructure;
    class RayTracingPipeline;
    class Buffer;
    class Image;
    class DescriptorSet;
    class CommandBuffer;
    class Node;
}

namespace vsgraysim {

struct RTPipelineDescriptor {
    VkDevice          device = VK_NULL_HANDLE;
    VkPhysicalDevice  physical_device = VK_NULL_HANDLE;
    VkQueue           queue = VK_NULL_HANDLE;
    uint32_t          queue_family = 0;
    VkCommandPool     command_pool = VK_NULL_HANDLE;
    uint32_t          width = 0;
    uint32_t          height = 0;
    std::string       shader_dir = "shaders";  ///< Directory containing .spv files
    vsg::ref_ptr<vsg::Device> vsg_device;      ///< VSG device for AS builds
    vsg::ref_ptr<vsg::Node> scene_node;        ///< VSG scene for TLAS
};

class RayTracingPipelineWrap {
public:
    RayTracingPipelineWrap();
    ~RayTracingPipelineWrap();

    /// Initialize device, descriptor set, pipeline and acceleration structures.
    bool initialise(const RTPipelineDescriptor& desc,
                    vsg::ref_ptr<vsg::Node> scene);

    /// Upload (or partially update) the geo-phys SSBO.
    void uploadGeoPhys(const std::vector<GeoUnitPhysParams>& params,
                       bool partial = false);

    /// Upload the atmosphere layers SSBO.
    void uploadAtmosphere(const std::vector<AtmLayerParams>& layers);

    /// Upload the band-parameter map SSBO.
    void uploadBandMap(const std::vector<BandParamMap>& map);

    /// Upload the sensor aperture SSBO (from SceneBuilder).
    struct ApertureDescGPU {
        uint32_t sensor_id;
        float    pos[3];
        float    dir[3];
        float    half_angle;
        float    aperture_area;
        uint32_t pad0, pad1;
    };
    void uploadApertures(const std::vector<ApertureDescGPU>& aps);

    /// Upload the crosstalk routes SSBO (from CrossInductionRules).
    struct RouteDescGPU {
        uint32_t tx_sensor_id;
        uint32_t rx_sensor_id;
        int32_t  tx_type;
        int32_t  rx_type;
        int32_t  cross_type;
        int32_t  priority;
        float    coupling_factor;
        float    band_match;
        int32_t  is_process;
        uint32_t pad;
    };
    void uploadRoutes(const std::vector<RouteDescGPU>& routes);

    /// Push constants for a single sensor trace dispatch.
    struct TracePush {
        uint32_t sensor_idx;      ///< Index into sensor_ids[]
        uint32_t band_channel;    ///< BandChannel (0=VIS,1=IR,2=RADAR,3=SAR)
        float    tx_power;        ///< Transmit power (W)
        float    beam_width;      ///< Beam width (rad)
        float    frequency;       ///< Carrier frequency (Hz)
        float    wavelength;      ///< Wavelength (m) = c/f
        float    origin[3];       ///< pulse aperture position; flags bit 0 enables it, including zero
        uint32_t pulse_seq;       ///< 帧内脉冲序号 (0-based)
        float    pulse_slow_time; ///< 帧内慢时间 (s, fire_time)
        uint32_t flags;           ///< bit 0: explicit origin; bit 1: explicit boresight
        float direction[3];       ///< normalized world-frame boresight
        uint32_t pad;
    };
    static_assert(sizeof(TracePush)==64 && offsetof(TracePush,origin)==24 && offsetof(TracePush,direction)==48,
                  "TracePush must match the scalar fields in raygen.rgen");

    /// Run a single trace pass for one sensor (kept for compatibility).
    /// Prefer traceBatch() to amortise fence/command-buffer overhead.
    void trace(const TracePush& push);

    /// Dispatch a batch of sensor traces in a single command buffer/submit.
    /// This removes the per-sensor vkWaitForFences and launch overhead.
    void traceBatch(const std::vector<TracePush>& pushes);
    /// Legacy native RT does not yet bind versioned scene/sensor resources.
    /// Production orchestration must use a backend with historical truth support.
    bool supportsMeasurementSnapshots() const { return false; }

    /// Wait for the most recent GPU trace submission to finish.
    /// Returns false if the fence wait failed or no submission is in flight.
    bool waitForScatterPoints(uint64_t timeout_ns = UINT64_MAX);

    /// Read back the scatter-point SSBO into a CPU vector.
    std::vector<ScatterPoint> readbackScatterPoints();

    /// Write a host-side scatter-point array back into the GPU SSBO.
    /// Useful when the CPU has modified scatter points (e.g. cross-induction)
    /// and wants CUDA to read the updated buffer through zero-copy interop.
    void writeScatterPoints(const std::vector<ScatterPoint>& sps);

    /// Export the scatter-point SSBO as a Vulkan external-memory handle.
    int64_t exportScatterSharedHandle();

    /// Total scatter-point buffer capacity (entries).
    std::size_t scatterCapacity() const;

    /// Whether the Vulkan RT pipeline is actually available (not stub mode).
    bool isRTAvailable() const;

    /// Build a bottom-level acceleration structure from a VSG node.
    bool buildBLAS(vsg::ref_ptr<vsg::Node> node);

    /// Build the top-level acceleration structure.
    bool buildTLAS();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

using RayTracingPipelineWrapPtr = std::shared_ptr<RayTracingPipelineWrap>;

} // namespace vsgraysim
