#include "vsgraysim/layer2_rt/RayTracingPipelineWrap.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <type_traits>
#include <vector>

#include <vulkan/vulkan.h>

#include <vsg/raytracing/BuildAccelerationStructureTraversal.h>
#include <vsg/vk/Context.h>

namespace vsgraysim {

// ============================================================================
// Helper: load SPIR-V binary from file
// ============================================================================
static std::vector<char> loadSpirV(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        spdlog::error("Layer2: cannot open SPIR-V file '{}'", path);
        return {};
    }
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<char> data(static_cast<std::size_t>(sz));
    f.read(data.data(), sz);
    return data;
}

// ============================================================================
// Helper: ray-tracing function pointers via vkGetDeviceProcAddr
// ============================================================================
struct RtFuncs {
    PFN_vkCreateRayTracingPipelinesKHR  vkCreateRayTracingPipelinesKHR = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR = nullptr;
    PFN_vkCmdTraceRaysKHR              vkCmdTraceRaysKHR = nullptr;
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkGetBufferDeviceAddressKHR    vkGetBufferDeviceAddressKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkGetMemoryFdKHR               vkGetMemoryFdKHR = nullptr;

    bool load(VkDevice device) {
        auto get = [&](auto& f, const char* name) {
            using FuncType = std::remove_reference_t<decltype(f)>;
            f = reinterpret_cast<FuncType>(vkGetDeviceProcAddr(device, name));
            return f != nullptr;
        };
        bool ok = true;
        ok = ok && get(vkCreateRayTracingPipelinesKHR, "vkCreateRayTracingPipelinesKHR");
        ok = ok && get(vkGetRayTracingShaderGroupHandlesKHR, "vkGetRayTracingShaderGroupHandlesKHR");
        ok = ok && get(vkCmdTraceRaysKHR, "vkCmdTraceRaysKHR");
        ok = ok && get(vkCreateAccelerationStructureKHR, "vkCreateAccelerationStructureKHR");
        ok = ok && get(vkDestroyAccelerationStructureKHR, "vkDestroyAccelerationStructureKHR");
        ok = ok && get(vkGetAccelerationStructureBuildSizesKHR, "vkGetAccelerationStructureBuildSizesKHR");
        ok = ok && get(vkCmdBuildAccelerationStructuresKHR, "vkCmdBuildAccelerationStructuresKHR");
        ok = ok && get(vkGetBufferDeviceAddressKHR, "vkGetBufferDeviceAddressKHR");
        ok = ok && get(vkGetAccelerationStructureDeviceAddressKHR, "vkGetAccelerationStructureDeviceAddressKHR");
        // vkGetMemoryFdKHR is only required for CUDA interop; don't fail RT if it's missing.
        get(vkGetMemoryFdKHR, "vkGetMemoryFdKHR");
        if (!ok) {
            spdlog::error("Layer2: missing ray-tracing extension function(s)");
        }
        return ok;
    }
};

// ============================================================================
// Internal buffer allocator
// ============================================================================
struct SSBO {
    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize   size   = 0;
};

static bool allocateSSBO(VkDevice device, VkPhysicalDevice physDev,
                          SSBO& s, VkDeviceSize size, VkBufferUsageFlags usage,
                          bool exportable = false) {
    if (!device || size <= 0) return false;
    if (s.buffer) { vkDestroyBuffer(device, s.buffer, nullptr); s.buffer = VK_NULL_HANDLE; }
    if (s.memory) { vkFreeMemory(device, s.memory, nullptr); s.memory = VK_NULL_HANDLE; }
    s.size = 0;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bci, nullptr, &s.buffer) != VK_SUCCESS) {
        spdlog::error("Layer2: vkCreateBuffer failed");
        return false;
    }
    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(device, s.buffer, &mr);
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(physDev, &mp);
    uint32_t type_index = 0;
    bool found = false;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            type_index = i;
            found = true;
            break;
        }
    }
    if (!found) {
        spdlog::error("Layer2: no host-visible memory type found");
        vkDestroyBuffer(device, s.buffer, nullptr);
        s.buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = type_index;

    VkExportMemoryAllocateInfo export_info{};
    export_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    if (exportable) {
        mai.pNext = &export_info;
    }

    if (vkAllocateMemory(device, &mai, nullptr, &s.memory) != VK_SUCCESS) {
        spdlog::error("Layer2: vkAllocateMemory failed");
        vkDestroyBuffer(device, s.buffer, nullptr);
        s.buffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(device, s.buffer, s.memory, 0);
    s.size = mr.size;
    return true;
}

static bool writeSSBO(VkDevice device, SSBO& s, const void* data, VkDeviceSize size) {
    if (!s.buffer || size > s.size) return false;
    void* mapped = nullptr;
    if (vkMapMemory(device, s.memory, 0, size, 0, &mapped) != VK_SUCCESS) return false;
    std::memcpy(mapped, data, static_cast<std::size_t>(size));
    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = s.memory;
    range.offset = 0;
    range.size   = size;
    vkFlushMappedMemoryRanges(device, 1, &range);
    vkUnmapMemory(device, s.memory);
    return true;
}

static void destroySSBO(VkDevice device, SSBO& s) {
    if (s.buffer) { vkDestroyBuffer(device, s.buffer, nullptr); s.buffer = VK_NULL_HANDLE; }
    if (s.memory) { vkFreeMemory(device, s.memory, nullptr); s.memory = VK_NULL_HANDLE; }
    s.size = 0;
}

// ============================================================================
// Internal structure for GPU buffer memory (device-local for AS)
// ============================================================================
struct GpuBuffer {
    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize   size   = 0;
};

static bool createGpuBuffer(VkDevice device, VkPhysicalDevice physDev,
                             GpuBuffer& b, VkDeviceSize size,
                             VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags props) {
    if (b.buffer) { vkDestroyBuffer(device, b.buffer, nullptr); b.buffer = VK_NULL_HANDLE; }
    if (b.memory) { vkFreeMemory(device, b.memory, nullptr); b.memory = VK_NULL_HANDLE; }
    b.size = 0;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bci, nullptr, &b.buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(device, b.buffer, &mr);
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(physDev, &mp);
    uint32_t type_index = 0;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((mr.memoryTypeBits & (1u << i)) &&
            ((mp.memoryTypes[i].propertyFlags & props) == props)) {
            type_index = i; break;
        }
    }
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = type_index;
    if (vkAllocateMemory(device, &mai, nullptr, &b.memory) != VK_SUCCESS) {
        vkDestroyBuffer(device, b.buffer, nullptr);
        b.buffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(device, b.buffer, b.memory, 0);
    b.size = mr.size;
    return true;
}

static void destroyGpuBuffer(VkDevice device, GpuBuffer& b) {
    if (b.buffer) { vkDestroyBuffer(device, b.buffer, nullptr); b.buffer = VK_NULL_HANDLE; }
    if (b.memory) { vkFreeMemory(device, b.memory, nullptr); b.memory = VK_NULL_HANDLE; }
    b.size = 0;
}

// ============================================================================
// RayTracingPipelineWrap::Impl
// ============================================================================
struct RayTracingPipelineWrap::Impl {
    // Device handles
    VkDevice         device         = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkQueue          queue          = VK_NULL_HANDLE;
    uint32_t         queue_family   = 0;
    VkCommandPool    command_pool   = VK_NULL_HANDLE;
    VkCommandBuffer  cmd_buffer     = VK_NULL_HANDLE;
    VkFence          fence          = VK_NULL_HANDLE;
    uint32_t         width          = 0;
    uint32_t         height         = 0;
    std::string      shader_dir     = "shaders";
    vsg::ref_ptr<vsg::Device> vsg_device;
    vsg::ref_ptr<vsg::Node>   scene_node;

    // Function pointers
    RtFuncs          funcs;

    // Cached external-memory handle for the scatter buffer.
    // -1 means not yet exported; once exported the fd remains valid until the
    // underlying VkDeviceMemory is destroyed.
    int              scatter_fd_ = -1;

    // SSBOs
    SSBO geo_buf;
    SSBO atm_buf;
    SSBO band_map_buf;
    SSBO scatter_buf;
    SSBO aperture_buf;
    SSBO route_buf;
    SSBO sensor_buf;
    std::size_t      scatter_capacity = 0;

    // Ray-tracing pipeline
    VkPipeline       rt_pipeline      = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout  = VK_NULL_HANDLE;
    VkDescriptorSetLayout ds_layout   = VK_NULL_HANDLE;
    VkDescriptorSet  descriptor_set   = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool        = VK_NULL_HANDLE;

    // Shader binding table
    GpuBuffer        sbt_buffer;
    VkStridedDeviceAddressRegionKHR raygen_sbt{};
    VkStridedDeviceAddressRegionKHR miss_sbt{};
    VkStridedDeviceAddressRegionKHR hit_sbt{};
    VkStridedDeviceAddressRegionKHR call_sbt{};

    // Acceleration structures (placeholder - real AS built from scene)
    VkAccelerationStructureKHR tlas   = VK_NULL_HANDLE;
    GpuBuffer                  tlas_buffer;
    GpuBuffer                  scratch_buffer;
    bool                       as_built = false;

    // ------------------------------------------------------------------
    // Initialise Vulkan handles, create pipeline and SBT
    // ------------------------------------------------------------------
    bool init(const RTPipelineDescriptor& desc) {
        device          = desc.device;
        physical_device = desc.physical_device;
        queue           = desc.queue;
        queue_family    = desc.queue_family;
        command_pool    = desc.command_pool;
        width           = desc.width;
        height          = desc.height;
        shader_dir      = desc.shader_dir;
        vsg_device      = desc.vsg_device;
        scene_node      = desc.scene_node;

        if (!device || !physical_device) {
            spdlog::warn("Layer2: no VkDevice; running CPU-stub mode");
            scatter_capacity = static_cast<std::size_t>(std::max<uint32_t>(width, 1)) *
                               std::max<uint32_t>(height, 1) * 8;
            return true;
        }

        scatter_capacity = static_cast<std::size_t>(width) * height * 8;

        // Load function pointers
        if (!funcs.load(device)) {
            spdlog::warn("Layer2: RT extensions unavailable; falling back to CPU-stub mode");
            return true; // non-fatal
        }

        // Allocate scatter SSBO (exportable so CUDA can import it for zero-copy)
        allocateSSBO(device, physical_device, scatter_buf,
                     static_cast<VkDeviceSize>(scatter_capacity * sizeof(ScatterPoint)),
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     /*exportable=*/ true);

        // Create command buffer + fence for one-shot submits
        VkCommandBufferAllocateInfo cbai{};
        cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool        = command_pool;
        cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device, &cbai, &cmd_buffer) != VK_SUCCESS) {
            spdlog::error("Layer2: failed to allocate command buffer");
            return false;
        }

        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(device, &fci, nullptr, &fence) != VK_SUCCESS) {
            spdlog::error("Layer2: failed to create fence");
            return false;
        }

        // Create descriptor set layout
        createDescriptorSetLayout();
        createDescriptorSet();

        // Create ray tracing pipeline and SBT
        if (!createRayTracingPipeline()) {
            spdlog::error("Layer2: failed to create ray tracing pipeline");
            return false;
        }
        if (!createShaderBindingTable()) {
            spdlog::error("Layer2: failed to create shader binding table");
            return false;
        }

        spdlog::info("Layer2: RT pipeline initialised ({}x{}, {} SSBO, pipeline={})",
                     width, height,
                     scatter_capacity * sizeof(ScatterPoint) / (1024*1024), "VK");
        return true;
    }

    // ------------------------------------------------------------------
    void createDescriptorSetLayout() {
        // Bindings:
        // 0: CameraParams (uniform)
        // 1: AtmParamsBuf (SSBO)
        // 2: GeoPhysBuf (SSBO)
        // 3: BandParamMapBuf (SSBO)
        // 4: SensorList (SSBO)
        // 5: ScatterPoints (SSBO)
        // 6: ApertureBuf (SSBO)
        // 7: RouteBuf (SSBO)
        // 8: output_image (storage image)
        // 9: TLAS (acceleration structure)
        std::vector<VkDescriptorSetLayoutBinding> bindings(10);

        auto setupBind = [&](uint32_t i, VkDescriptorType t, VkShaderStageFlags s) {
            bindings[i].binding         = i;
            bindings[i].descriptorCount = 1;
            bindings[i].descriptorType  = t;
            bindings[i].stageFlags      = s;
        };

        VkShaderStageFlags rtStages = VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                                      VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                                      VK_SHADER_STAGE_MISS_BIT_KHR;

        setupBind(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, rtStages);
        setupBind(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, rtStages);
        setupBind(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, rtStages);
        setupBind(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, rtStages);
        setupBind(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
        setupBind(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
        setupBind(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
        setupBind(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
        setupBind(8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_SHADER_STAGE_RAYGEN_BIT_KHR);
        setupBind(9, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR);

        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = static_cast<uint32_t>(bindings.size());
        dslci.pBindings    = bindings.data();
        vkCreateDescriptorSetLayout(device, &dslci, nullptr, &ds_layout);

        // Pipeline layout with push constants
        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        pcr.offset     = 0;
        pcr.size       = sizeof(TracePush); // scalar layout shared with raygen.rgen

        VkPipelineLayoutCreateInfo plci{};
        plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount         = 1;
        plci.pSetLayouts            = &ds_layout;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges    = &pcr;
        vkCreatePipelineLayout(device, &plci, nullptr, &pipeline_layout);
    }

    // ------------------------------------------------------------------
    void createDescriptorSet() {
        VkDescriptorPoolSize pool_sizes[4] = {};
        pool_sizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount = 1;
        pool_sizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes[1].descriptorCount = 7;
        pool_sizes[2].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_sizes[2].descriptorCount = 1;
        pool_sizes[3].type            = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        pool_sizes[3].descriptorCount = 1;

        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets       = 1;
        dpci.poolSizeCount = 4;
        dpci.pPoolSizes    = pool_sizes;
        vkCreateDescriptorPool(device, &dpci, nullptr, &desc_pool);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool     = desc_pool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts        = &ds_layout;
        vkAllocateDescriptorSets(device, &dsai, &descriptor_set);
    }

    // ------------------------------------------------------------------
    // Build a stub TLAS with a single AABB so traceRayEXT can execute.
    // In production this would walk the VSG scene graph.
    void buildStubTLAS() {
        if (tlas) return;
        spdlog::info("Layer2: building stub TLAS (single AABB)");

        // Create a single AABB geometry
        VkAccelerationStructureGeometryKHR as_geom{};
        as_geom.sType       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        as_geom.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
        as_geom.flags       = VK_GEOMETRY_OPAQUE_BIT_KHR;
        as_geom.geometry.aabbs.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
        // No actual AABB data - stub
        as_geom.geometry.aabbs.data.deviceAddress = 0;
        as_geom.geometry.aabbs.stride = sizeof(VkAabbPositionsKHR);

        VkAccelerationStructureBuildGeometryInfoKHR build_info{};
        build_info.sType       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        build_info.type        = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        build_info.flags       = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        build_info.geometryCount = 0; // empty TLAS
        build_info.pGeometries  = &as_geom;
        build_info.mode         = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

        VkAccelerationStructureBuildSizesInfoKHR sizes{};
        sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        funcs.vkGetAccelerationStructureBuildSizesKHR(
            device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &build_info, nullptr, &sizes);

        // Allocate TLAS buffer
        createGpuBuffer(device, physical_device, tlas_buffer, sizes.accelerationStructureSize,
                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkAccelerationStructureCreateInfoKHR asci{};
        asci.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        asci.buffer = tlas_buffer.buffer;
        asci.size   = sizes.accelerationStructureSize;
        asci.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        funcs.vkCreateAccelerationStructureKHR(device, &asci, nullptr, &tlas);

        // Allocate scratch buffer if needed
        if (sizes.buildScratchSize > 0) {
            if (scratch_buffer.size < sizes.buildScratchSize) {
                destroyGpuBuffer(device, scratch_buffer);
                createGpuBuffer(device, physical_device, scratch_buffer,
                                sizes.buildScratchSize,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            }
        }
        spdlog::info("Layer2: stub TLAS created (empty)");
    }

    // ------------------------------------------------------------------
    // Update descriptor set to point to the actual SSBOs.
    // Called before each trace dispatch.
    void updateDescriptorSet() {
        if (!descriptor_set) return;

        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkDescriptorBufferInfo> buf_infos(8);
        std::vector<VkDescriptorImageInfo> img_infos(1);

        // Helper: add a buffer descriptor write
        auto addBuf = [&](uint32_t binding, SSBO& ssbo, VkDescriptorType type) {
            uint32_t idx = static_cast<uint32_t>(writes.size());
            buf_infos[idx].buffer = ssbo.buffer;
            buf_infos[idx].offset = 0;
            buf_infos[idx].range  = ssbo.size;

            writes.emplace_back();
            writes.back().sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes.back().dstSet           = descriptor_set;
            writes.back().dstBinding       = binding;
            writes.back().descriptorCount  = 1;
            writes.back().descriptorType   = type;
            writes.back().pBufferInfo      = &buf_infos[idx];
        };

        // Binding 0: CameraParams (uniform) - borrow geo_buf as dummy
        addBuf(0, geo_buf, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        addBuf(1, atm_buf, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        addBuf(2, geo_buf, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        addBuf(3, band_map_buf, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        addBuf(4, sensor_buf, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        addBuf(5, scatter_buf, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        addBuf(6, aperture_buf, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        addBuf(7, route_buf, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        // Binding 8: output_image (storage image) - stub
        img_infos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        img_infos[0].imageView   = VK_NULL_HANDLE;
        writes.emplace_back();
        writes.back().sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes.back().dstSet           = descriptor_set;
        writes.back().dstBinding       = 8;
        writes.back().descriptorCount  = 1;
        writes.back().descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes.back().pImageInfo      = &img_infos[0];

        // Binding 9: TLAS (acceleration structure)
        VkWriteDescriptorSetAccelerationStructureKHR as_write{};
        as_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        as_write.accelerationStructureCount = 1;
        as_write.pAccelerationStructures    = &tlas;
        writes.emplace_back();
        writes.back().sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes.back().pNext           = &as_write;
        writes.back().dstSet          = descriptor_set;
        writes.back().dstBinding      = 9;
        writes.back().descriptorCount = 1;
        writes.back().descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

        vkUpdateDescriptorSets(device,
                               static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }

    // ------------------------------------------------------------------
    bool createRayTracingPipeline() {
        // Load SPIR-V files
        auto raygen_code  = loadSpirV(shader_dir + "/raygen.rgen.spv");
        auto miss_code    = loadSpirV(shader_dir + "/miss.rmiss.spv");
        auto closest_code = loadSpirV(shader_dir + "/closesthit.rchit.spv");

        if (raygen_code.empty() || miss_code.empty() || closest_code.empty()) {
            spdlog::error("Layer2: missing SPIR-V shaders in '{}'", shader_dir);
            return false;
        }

        // Create shader modules
        auto createModule = [&](const std::vector<char>& code) {
            VkShaderModule mod;
            VkShaderModuleCreateInfo smci{};
            smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            smci.codeSize = code.size();
            smci.pCode    = reinterpret_cast<const uint32_t*>(code.data());
            vkCreateShaderModule(device, &smci, nullptr, &mod);
            return mod;
        };
        VkShaderModule raygen_sm  = createModule(raygen_code);
        VkShaderModule miss_sm    = createModule(miss_code);
        VkShaderModule closest_sm = createModule(closest_code);

        // Shader stages
        VkPipelineShaderStageCreateInfo stages[3] = {};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        stages[0].module = raygen_sm;
        stages[0].pName  = "main";

        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
        stages[1].module = miss_sm;
        stages[1].pName  = "main";

        stages[2].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[2].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        stages[2].module = closest_sm;
        stages[2].pName  = "main";

        // Shader groups: [0]=raygen, [1]=miss, [2]=closesthit
        VkRayTracingShaderGroupCreateInfoKHR groups[3] = {};
        for (int i = 0; i < 3; ++i) {
            groups[i].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            groups[i].type  = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR; // default
        }
        groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[0].generalShader      = 0;
        groups[0].closestHitShader   = VK_SHADER_UNUSED_KHR;
        groups[0].anyHitShader       = VK_SHADER_UNUSED_KHR;
        groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

        groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[1].generalShader      = 1;
        groups[1].closestHitShader   = VK_SHADER_UNUSED_KHR;

        groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        groups[2].generalShader      = VK_SHADER_UNUSED_KHR;
        groups[2].closestHitShader   = 2;

        VkRayTracingPipelineCreateInfoKHR rtpci{};
        rtpci.sType                        = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        rtpci.stageCount                   = 3;
        rtpci.pStages                      = stages;
        rtpci.groupCount                   = 3;
        rtpci.pGroups                      = groups;
        rtpci.maxPipelineRayRecursionDepth = 1;
        rtpci.layout                       = pipeline_layout;

        VkResult res = funcs.vkCreateRayTracingPipelinesKHR(
            device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rtpci, nullptr, &rt_pipeline);

        // Clean up shader modules
        vkDestroyShaderModule(device, raygen_sm, nullptr);
        vkDestroyShaderModule(device, miss_sm, nullptr);
        vkDestroyShaderModule(device, closest_sm, nullptr);

        if (res != VK_SUCCESS) {
            spdlog::error("Layer2: failed to create ray tracing pipeline (res={})", static_cast<int>(res));
            return false;
        }
        spdlog::info("Layer2: ray tracing pipeline created");
        return true;
    }

    // ------------------------------------------------------------------
    bool createShaderBindingTable() {
        // Get shader group handles
        const uint32_t handle_size = 32; // VK_SHADER_INDEX_UNUSED_KHR alignment
        uint32_t handle_size_aligned = 32; // typically 32 bytes, aligned to 32

        // Query handle size from device properties
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props{};
        rt_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &rt_props;
        vkGetPhysicalDeviceProperties2(physical_device, &props2);

        handle_size_aligned = rt_props.shaderGroupHandleSize;
        uint32_t handle_alignment = rt_props.shaderGroupHandleAlignment;
        uint32_t sbt_base_alignment = rt_props.shaderGroupBaseAlignment;

        std::vector<uint8_t> handles(handle_size_aligned * 3);
        if (funcs.vkGetRayTracingShaderGroupHandlesKHR(
                device, rt_pipeline, 0, 3, handles.size(), handles.data()) != VK_SUCCESS) {
            spdlog::error("Layer2: failed to get shader group handles");
            return false;
        }

        // SBT layout: each table uses handle_size_aligned stride.
        // Ensure raygen start is aligned to sbt_base_alignment.
        VkDeviceSize raygen_offset = 0;
        VkDeviceSize raygen_stride = handle_size_aligned;
        VkDeviceSize raygen_size   = raygen_stride; // 1 entry
        // Align miss table start
        VkDeviceSize miss_offset = ((raygen_offset + raygen_size + sbt_base_alignment - 1) /
                                    sbt_base_alignment) * sbt_base_alignment;
        VkDeviceSize miss_stride = handle_size_aligned;
        VkDeviceSize miss_size   = miss_stride;
        // Align hit table start
        VkDeviceSize hit_offset = ((miss_offset + miss_size + sbt_base_alignment - 1) /
                                   sbt_base_alignment) * sbt_base_alignment;
        VkDeviceSize hit_stride = handle_size_aligned;
        VkDeviceSize hit_size   = hit_stride;
        VkDeviceSize sbt_total  = hit_offset + hit_size;

        if (!createGpuBuffer(device, physical_device, sbt_buffer, sbt_total,
                             VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            return false;
        }

        void* mapped = nullptr;
        vkMapMemory(device, sbt_buffer.memory, 0, sbt_total, 0, &mapped);
        std::memset(mapped, 0, static_cast<std::size_t>(sbt_total));

        // Copy raygen handle at offset 0
        std::memcpy(mapped, handles.data(), handle_size_aligned);
        // Copy miss handle at miss_offset
        std::memcpy(static_cast<uint8_t*>(mapped) + miss_offset,
                    handles.data() + handle_size_aligned, handle_size_aligned);
        // Copy hit handle at hit_offset
        std::memcpy(static_cast<uint8_t*>(mapped) + hit_offset,
                    handles.data() + 2 * handle_size_aligned, handle_size_aligned);
        vkUnmapMemory(device, sbt_buffer.memory);

        VkBufferDeviceAddressInfo bdai{};
        bdai.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        bdai.buffer = sbt_buffer.buffer;
        VkDeviceAddress sbt_addr = funcs.vkGetBufferDeviceAddressKHR(device, &bdai);

        raygen_sbt = { sbt_addr + raygen_offset, raygen_stride, raygen_size };
        miss_sbt   = { sbt_addr + miss_offset,   miss_stride,   miss_size };
        hit_sbt    = { sbt_addr + hit_offset,    hit_stride,    hit_size };

        spdlog::info("Layer2: SBT created ({} bytes, {} groups)", sbt_total, 3);
        return true;
    }

    // ------------------------------------------------------------------
    void destroy() {
        destroySSBO(device, geo_buf);
        destroySSBO(device, atm_buf);
        destroySSBO(device, band_map_buf);
        destroySSBO(device, scatter_buf);
        destroySSBO(device, aperture_buf);
        destroySSBO(device, route_buf);
        destroySSBO(device, sensor_buf);

        destroyGpuBuffer(device, sbt_buffer);
        destroyGpuBuffer(device, tlas_buffer);
        destroyGpuBuffer(device, scratch_buffer);

        if (tlas) { funcs.vkDestroyAccelerationStructureKHR(device, tlas, nullptr); tlas = VK_NULL_HANDLE; }
        if (rt_pipeline) { vkDestroyPipeline(device, rt_pipeline, nullptr); rt_pipeline = VK_NULL_HANDLE; }
        if (pipeline_layout) { vkDestroyPipelineLayout(device, pipeline_layout, nullptr); pipeline_layout = VK_NULL_HANDLE; }
        if (ds_layout) { vkDestroyDescriptorSetLayout(device, ds_layout, nullptr); ds_layout = VK_NULL_HANDLE; }
        if (desc_pool) { vkDestroyDescriptorPool(device, desc_pool, nullptr); desc_pool = VK_NULL_HANDLE; }
        if (fence) { vkDestroyFence(device, fence, nullptr); fence = VK_NULL_HANDLE; }
        if (cmd_buffer) {
            vkFreeCommandBuffers(device, command_pool, 1, &cmd_buffer);
            cmd_buffer = VK_NULL_HANDLE;
        }
    }

    ~Impl() {
        destroy();
    }
};

// ============================================================================
// Public API
// ============================================================================

RayTracingPipelineWrap::RayTracingPipelineWrap()
    : impl_(std::make_unique<Impl>()) {}

RayTracingPipelineWrap::~RayTracingPipelineWrap() {
    if (impl_ && impl_->device) vkDeviceWaitIdle(impl_->device);
}

bool RayTracingPipelineWrap::initialise(const RTPipelineDescriptor& desc,
                                        vsg::ref_ptr<vsg::Node> scene) {
    impl_->shader_dir = desc.shader_dir;
    if (impl_->shader_dir.empty()) impl_->shader_dir = "shaders";
    // Copy descriptor and attach the scene node so buildTLAS() can use it.
    RTPipelineDescriptor desc_with_scene = desc;
    if (!desc_with_scene.scene_node && scene) {
        desc_with_scene.scene_node = scene;
    }
    return impl_->init(desc_with_scene);
}

void RayTracingPipelineWrap::uploadGeoPhys(const std::vector<GeoUnitPhysParams>& params, bool) {
    if (!impl_->device || params.empty()) return;
    VkDeviceSize sz = params.size() * sizeof(GeoUnitPhysParams);
    if (impl_->geo_buf.size < sz)
        allocateSSBO(impl_->device, impl_->physical_device, impl_->geo_buf, sz,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    writeSSBO(impl_->device, impl_->geo_buf, params.data(), sz);
}

void RayTracingPipelineWrap::uploadAtmosphere(const std::vector<AtmLayerParams>& layers) {
    if (!impl_->device || layers.empty()) return;
    VkDeviceSize sz = layers.size() * sizeof(AtmLayerParams);
    if (impl_->atm_buf.size < sz)
        allocateSSBO(impl_->device, impl_->physical_device, impl_->atm_buf, sz,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    writeSSBO(impl_->device, impl_->atm_buf, layers.data(), sz);
}

void RayTracingPipelineWrap::uploadBandMap(const std::vector<BandParamMap>& map) {
    if (!impl_->device || map.empty()) return;
    VkDeviceSize sz = map.size() * sizeof(BandParamMap);
    if (impl_->band_map_buf.size < sz)
        allocateSSBO(impl_->device, impl_->physical_device, impl_->band_map_buf, sz,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    writeSSBO(impl_->device, impl_->band_map_buf, map.data(), sz);
}

void RayTracingPipelineWrap::uploadApertures(const std::vector<ApertureDescGPU>& aps) {
    if (!impl_->device || aps.empty()) return;
    VkDeviceSize sz = aps.size() * sizeof(ApertureDescGPU);
    if (impl_->aperture_buf.size < sz)
        allocateSSBO(impl_->device, impl_->physical_device, impl_->aperture_buf, sz,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    writeSSBO(impl_->device, impl_->aperture_buf, aps.data(), sz);
}

void RayTracingPipelineWrap::uploadRoutes(const std::vector<RouteDescGPU>& routes) {
    if (!impl_->device || routes.empty()) return;
    VkDeviceSize sz = routes.size() * sizeof(RouteDescGPU);
    if (impl_->route_buf.size < sz)
        allocateSSBO(impl_->device, impl_->physical_device, impl_->route_buf, sz,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    writeSSBO(impl_->device, impl_->route_buf, routes.data(), sz);
}

void RayTracingPipelineWrap::trace(const TracePush& push) {
    traceBatch({push});
}

void RayTracingPipelineWrap::traceBatch(const std::vector<TracePush>& pushes) {
    if (!impl_->device || !impl_->rt_pipeline) {
        spdlog::debug("Layer2: traceBatch() no-op (CPU stub mode)");
        return;
    }
    if (pushes.empty()) return;

    // Ensure descriptor set points to current buffers
    impl_->updateDescriptorSet();

    // Wait for previous submission before re-recording the one-shot CB.
    vkWaitForFences(impl_->device, 1, &impl_->fence, VK_TRUE, UINT64_MAX);
    vkResetFences(impl_->device, 1, &impl_->fence);

    VkCommandBufferBeginInfo cbbi{};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(impl_->cmd_buffer, &cbbi);

    // Build TLAS if not yet built (real scene TLAS or stub fallback)
    if (!impl_->tlas) {
        buildTLAS();
    }

    // Bind ray tracing pipeline
    vkCmdBindPipeline(impl_->cmd_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                      impl_->rt_pipeline);

    // Bind descriptor set
    vkCmdBindDescriptorSets(impl_->cmd_buffer,
                            VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            impl_->pipeline_layout, 0, 1,
                            &impl_->descriptor_set, 0, nullptr);

    // Batch all sensors into a single command buffer/submit.
    for (const auto& push : pushes) {
        // Push constants (frequency, wavelength, beam params)
        vkCmdPushConstants(impl_->cmd_buffer, impl_->pipeline_layout,
                           VK_SHADER_STAGE_RAYGEN_BIT_KHR,
                           0, sizeof(TracePush), &push);

        // Dispatch rays for this sensor
        impl_->funcs.vkCmdTraceRaysKHR(
            impl_->cmd_buffer,
            &impl_->raygen_sbt,
            &impl_->miss_sbt,
            &impl_->hit_sbt,
            &impl_->call_sbt,
            impl_->width, impl_->height, 1);
    }

    vkEndCommandBuffer(impl_->cmd_buffer);

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &impl_->cmd_buffer;
    vkQueueSubmit(impl_->queue, 1, &si, impl_->fence);

    // CPU no longer stalls here; caller can overlap CPU work before calling
    // waitForScatterPoints() / readbackScatterPoints().
}

bool RayTracingPipelineWrap::waitForScatterPoints(uint64_t timeout_ns) {
    if (!impl_->device || !impl_->fence) return false;
    return vkWaitForFences(impl_->device, 1, &impl_->fence, VK_TRUE, timeout_ns) == VK_SUCCESS;
}

std::vector<ScatterPoint> RayTracingPipelineWrap::readbackScatterPoints() {
    if (!impl_->device || !impl_->scatter_buf.buffer) return {};

    // Ensure the GPU has finished writing before we read back.
    waitForScatterPoints();

    std::vector<ScatterPoint> out(impl_->scatter_capacity);
    void* mapped = nullptr;
    if (vkMapMemory(impl_->device, impl_->scatter_buf.memory, 0,
                    impl_->scatter_buf.size, 0, &mapped) != VK_SUCCESS) {
        return {};
    }
    std::memcpy(out.data(), mapped,
                std::min(out.size() * sizeof(ScatterPoint),
                         static_cast<std::size_t>(impl_->scatter_buf.size)));
    vkUnmapMemory(impl_->device, impl_->scatter_buf.memory);
    return out;
}

void RayTracingPipelineWrap::writeScatterPoints(const std::vector<ScatterPoint>& sps) {
    if (!impl_->device || !impl_->scatter_buf.buffer) return;
    if (sps.empty()) return;

    waitForScatterPoints();

    void* mapped = nullptr;
    if (vkMapMemory(impl_->device, impl_->scatter_buf.memory, 0,
                    impl_->scatter_buf.size, 0, &mapped) != VK_SUCCESS) {
        return;
    }
    std::memcpy(mapped, sps.data(),
                std::min(sps.size() * sizeof(ScatterPoint),
                         static_cast<std::size_t>(impl_->scatter_buf.size)));
    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = impl_->scatter_buf.memory;
    range.offset = 0;
    range.size   = sps.size() * sizeof(ScatterPoint);
    vkFlushMappedMemoryRanges(impl_->device, 1, &range);
    vkUnmapMemory(impl_->device, impl_->scatter_buf.memory);
}

int64_t RayTracingPipelineWrap::exportScatterSharedHandle() {
    if (!impl_->device || !impl_->scatter_buf.memory) return -1;

    if (!impl_->funcs.vkGetMemoryFdKHR) {
        spdlog::warn("Layer2: vkGetMemoryFdKHR unavailable - cannot export scatter buffer");
        return -1;
    }

    // Each call returns a fresh fd; ownership transfers to the importer
    // (CUDA).  Never cache: a consumed fd cannot be re-imported, and a
    // cached value would make a failed import unrecoverable.
    VkMemoryGetFdInfoKHR get_fd_info{};
    get_fd_info.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    get_fd_info.memory       = impl_->scatter_buf.memory;
    get_fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    int fd = -1;
    VkResult res = impl_->funcs.vkGetMemoryFdKHR(impl_->device, &get_fd_info, &fd);
    if (res != VK_SUCCESS || fd < 0) {
        spdlog::warn("Layer2: vkGetMemoryFdKHR failed (res={})", static_cast<int>(res));
        return -1;
    }
    impl_->scatter_fd_ = fd;  // last exported fd (diagnostics only)
    return static_cast<int64_t>(fd);
}

std::size_t RayTracingPipelineWrap::scatterCapacity() const {
    return impl_->scatter_capacity;
}

bool RayTracingPipelineWrap::isRTAvailable() const {
    return impl_->device && impl_->rt_pipeline != VK_NULL_HANDLE;
}

bool RayTracingPipelineWrap::buildBLAS(vsg::ref_ptr<vsg::Node> node) {
    if (!impl_->device) return false;
    if (!node) return false;
    spdlog::info("Layer2: BLAS build requested ({})", node.valid() ? "node" : "null");
    // VSG creates bottom-level acceleration structures during the
    // BuildAccelerationStructureTraversal used by buildTLAS().
    return true;
}

bool RayTracingPipelineWrap::buildTLAS() {
    if (!impl_->device) return false;

    if (impl_->tlas) return true;  // already built

    // Prefer a real scene TLAS if a VSG device/scene are available.
    if (impl_->vsg_device && impl_->scene_node) {
        try {
            vsg::BuildAccelerationStructureTraversal basTraversal(impl_->vsg_device);
            impl_->scene_node->accept(basTraversal);

            if (!basTraversal.tlas) {
                spdlog::warn("Layer2: BuildAccelerationStructureTraversal produced no TLAS");
            } else {
                auto vsg_queue = impl_->vsg_device->getQueue(impl_->queue_family, 0);
                if (!vsg_queue) {
                    spdlog::warn("Layer2: VSG queue family {} unavailable for TLAS build", impl_->queue_family);
                } else {
                    vsg::Context context(impl_->vsg_device);
                    context.commandPool = vsg::CommandPool::create(impl_->vsg_device, impl_->queue_family);
                    context.graphicsQueue = vsg_queue;

                    basTraversal.tlas->compile(context);

                    if (!context.record()) {
                        spdlog::warn("Layer2: failed to record TLAS build commands");
                    } else {
                        context.waitForCompletion();
                        impl_->tlas = *basTraversal.tlas;
                        spdlog::info("Layer2: scene TLAS built from VSG scene graph");
                        impl_->as_built = true;
                        return true;
                    }
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("Layer2: exception building scene TLAS: {}", e.what());
        } catch (...) {
            spdlog::warn("Layer2: unknown exception building scene TLAS");
        }
    }

    // Fallback to a minimal stub TLAS so the pipeline can still run.
    spdlog::info("Layer2: falling back to stub TLAS");
    impl_->buildStubTLAS();
    impl_->as_built = true;
    return true;
}

} // namespace vsgraysim
