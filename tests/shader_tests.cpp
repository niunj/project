#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
void vkcheck(VkResult result, const char* what) { require(result == VK_SUCCESS, what); }

struct Binding {
    std::vector<uint8_t> data;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
};
template<class T> Binding bytes(const std::vector<T>& value, VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
    Binding b; b.type = type; b.data.resize(value.size() * sizeof(T));
    std::memcpy(b.data.data(), value.data(), b.data.size()); return b;
}

class ComputeDevice {
public:
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    bool atomicFloat = false;

    ~ComputeDevice() {
        if (device) vkDestroyDevice(device, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
    }
    bool init() {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_2;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ci.pApplicationInfo = &app;
        if (vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS) return false;
        uint32_t count = 0; vkEnumeratePhysicalDevices(instance, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count); vkEnumeratePhysicalDevices(instance, &count, devices.data());
        for (auto candidate : devices) {
            uint32_t n = 0; vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, nullptr);
            std::vector<VkQueueFamilyProperties> families(n); vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, families.data());
            for (uint32_t i = 0; i < n; ++i) if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                physical = candidate; family = i; break;
            }
            if (physical) break;
        }
        if (!physical) return false;
        VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(physical, &props);
        std::cout << "Compute device: " << props.deviceName << '\n';
        VkPhysicalDeviceShaderAtomicFloatFeaturesEXT af{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; features.pNext = &af;
        vkGetPhysicalDeviceFeatures2(physical, &features);
        uint32_t extCount = 0; vkEnumerateDeviceExtensionProperties(physical, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(physical, nullptr, &extCount, exts.data());
        const bool hasExt = std::any_of(exts.begin(), exts.end(), [](const auto& e) {
            return std::strcmp(e.extensionName, VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME) == 0;
        });
        atomicFloat = hasExt && af.shaderSharedFloat32AtomicAdd;
        VkPhysicalDeviceShaderAtomicFloatFeaturesEXT enabled{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT};
        enabled.shaderSharedFloat32AtomicAdd = atomicFloat;
        const char* extension = VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME;
        float priority = 1;
        VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qc.queueFamilyIndex = family; qc.queueCount = 1; qc.pQueuePriorities = &priority;
        VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dc.queueCreateInfoCount = 1; dc.pQueueCreateInfos = &qc;
        if (atomicFloat) { dc.pNext = &enabled; dc.enabledExtensionCount = 1; dc.ppEnabledExtensionNames = &extension; }
        vkcheck(vkCreateDevice(physical, &dc, nullptr, &device), "create compute device");
        vkGetDeviceQueue(device, family, 0, &queue);
        return true;
    }

    std::vector<Binding> run(const char* filename, std::vector<Binding> bindings,
                             uint32_t x, uint32_t y = 1, uint32_t z = 1,
                             const std::array<uint32_t, 4>* push = nullptr) {
        struct Resources {
            VkDevice device;
            std::vector<VkBuffer> buffers;
            std::vector<VkDeviceMemory> memories;
            VkShaderModule module = VK_NULL_HANDLE;
            VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
            VkPipelineLayout layout = VK_NULL_HANDLE;
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkDescriptorPool descriptors = VK_NULL_HANDLE;
            VkCommandPool commands = VK_NULL_HANDLE;
            VkFence fence = VK_NULL_HANDLE;
            ~Resources() {
                if (fence) vkDestroyFence(device, fence, nullptr);
                if (commands) vkDestroyCommandPool(device, commands, nullptr);
                if (descriptors) vkDestroyDescriptorPool(device, descriptors, nullptr);
                if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
                if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
                if (setLayout) vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
                if (module) vkDestroyShaderModule(device, module, nullptr);
                for (auto b : buffers) vkDestroyBuffer(device, b, nullptr);
                for (auto m : memories) vkFreeMemory(device, m, nullptr);
            }
        } r{device};
        std::vector<VkDescriptorBufferInfo> infos;
        std::vector<VkDescriptorSetLayoutBinding> layouts;
        VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(physical, &mp);
        for (size_t i = 0; i < bindings.size(); ++i) {
            auto& b = bindings[i];
            VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bc.size = b.data.size();
            bc.usage = b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            VkBuffer buffer; vkcheck(vkCreateBuffer(device, &bc, nullptr, &buffer), "create buffer"); r.buffers.push_back(buffer);
            VkMemoryRequirements req; vkGetBufferMemoryRequirements(device, buffer, &req);
            uint32_t index = mp.memoryTypeCount;
            const auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            for (uint32_t k = 0; k < mp.memoryTypeCount; ++k)
                if ((req.memoryTypeBits & (1u << k)) && (mp.memoryTypes[k].propertyFlags & flags) == flags) { index = k; break; }
            require(index < mp.memoryTypeCount, "coherent compute test memory unavailable");
            VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ma.allocationSize = req.size; ma.memoryTypeIndex = index;
            VkDeviceMemory memory; vkcheck(vkAllocateMemory(device, &ma, nullptr, &memory), "allocate test memory"); r.memories.push_back(memory);
            vkcheck(vkBindBufferMemory(device, buffer, memory, 0), "bind buffer");
            void* mapped; vkcheck(vkMapMemory(device, memory, 0, b.data.size(), 0, &mapped), "map input");
            std::memcpy(mapped, b.data.data(), b.data.size()); vkUnmapMemory(device, memory);
            infos.push_back({buffer, 0, b.data.size()});
            layouts.push_back({uint32_t(i), b.type, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
        }
        VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; sl.bindingCount = layouts.size(); sl.pBindings = layouts.data();
        vkcheck(vkCreateDescriptorSetLayout(device, &sl, nullptr, &r.setLayout), "descriptor layout");
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pl.setLayoutCount = 1; pl.pSetLayouts = &r.setLayout;
        if (push) { pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &range; }
        vkcheck(vkCreatePipelineLayout(device, &pl, nullptr, &r.layout), "pipeline layout");
        std::ifstream input(std::string(SIM_TEST_BINARY_DIR) + "/" + filename + ".spv", std::ios::binary | std::ios::ate);
        require(bool(input), "missing test SPIR-V"); const auto size = size_t(input.tellg());
        std::vector<uint32_t> code(size / 4); input.seekg(0); input.read(reinterpret_cast<char*>(code.data()), size);
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; sm.codeSize = size; sm.pCode = code.data();
        vkcheck(vkCreateShaderModule(device, &sm, nullptr, &r.module), "shader module");
        VkComputePipelineCreateInfo pc{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; pc.layout = r.layout;
        pc.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, r.module, "main", nullptr};
        vkcheck(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pc, nullptr, &r.pipeline), "compute pipeline");
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, uint32_t(bindings.size())}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uint32_t(bindings.size())}};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dp.maxSets = 1; dp.poolSizeCount = 2; dp.pPoolSizes = sizes;
        vkcheck(vkCreateDescriptorPool(device, &dp, nullptr, &r.descriptors), "descriptor pool");
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; da.descriptorPool = r.descriptors; da.descriptorSetCount = 1; da.pSetLayouts = &r.setLayout;
        VkDescriptorSet set; vkcheck(vkAllocateDescriptorSets(device, &da, &set), "descriptor set");
        std::vector<VkWriteDescriptorSet> writes;
        for (size_t i = 0; i < bindings.size(); ++i) {
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = set; w.dstBinding = i;
            w.descriptorCount = 1; w.descriptorType = bindings[i].type; w.pBufferInfo = &infos[i]; writes.push_back(w);
        }
        vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cp.queueFamilyIndex = family;
        vkcheck(vkCreateCommandPool(device, &cp, nullptr, &r.commands), "command pool");
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ca.commandPool = r.commands; ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount = 1;
        VkCommandBuffer cb; vkcheck(vkAllocateCommandBuffers(device, &ca, &cb), "command buffer");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkcheck(vkBeginCommandBuffer(cb, &begin), "begin compute");
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, r.layout, 0, 1, &set, 0, nullptr);
        if (push) vkCmdPushConstants(cb, r.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, push->data());
        vkCmdDispatch(cb, x, y, z);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        vkcheck(vkEndCommandBuffer(cb), "end compute");
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; vkcheck(vkCreateFence(device, &fc, nullptr, &r.fence), "fence");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &cb;
        vkcheck(vkQueueSubmit(queue, 1, &submit, r.fence), "submit compute");
        const auto waited = vkWaitForFences(device, 1, &r.fence, VK_TRUE, 30000000000ull);
        if (waited != VK_SUCCESS) { vkDeviceWaitIdle(device); vkcheck(waited, "compute fence timeout"); }
        for (size_t i = 0; i < bindings.size(); ++i) {
            void* mapped; vkcheck(vkMapMemory(device, r.memories[i], 0, bindings[i].data.size(), 0, &mapped), "map result");
            std::memcpy(bindings[i].data.data(), mapped, bindings[i].data.size()); vkUnmapMemory(device, r.memories[i]);
        }
        return bindings;
    }
};

void cloudPaths(ComputeDevice& gpu) {
    auto output = gpu.run("cloud_path_test.comp", {bytes(std::vector<float>(32))}, 1);
    std::array<float, 32> values; std::memcpy(values.data(), output[0].data.data(), sizeof(values));
    const double lengths[] = {0, 50, 100, 50};
    for (int scenario = 0; scenario < 4; ++scenario)
        for (int count = 0; count < 4; ++count)
            require(std::abs(values[2 * (scenario * 4 + count)] - std::exp(-lengths[scenario] / 1000.0)) < 2e-5,
                    "cloud transmission differs from analytic finite path");
    std::cout << "PASS: production cloud integration, finite rays and 10/12/16/24 steps\n";
}

void radarSegments(ComputeDevice& gpu) {
    constexpr uint32_t nf = 256, np = 2;
    std::vector<std::array<float, 4>> positions, signals;
    for (const auto [x, count] : std::vector<std::pair<float, int>>{{.25f, 8}, {1.25f, 8}, {127.5f, 6}, {128.5f, 9}, {250.1f, 5}})
        for (int i = 0; i < count; ++i) { positions.push_back({x, 0, 0, 1}); signals.push_back({1.f + i * .1f, .2f, x, 0}); }
    std::vector<uint32_t> offsets(nf + 1), meta;
    for (auto p : positions) { const auto b = uint32_t(p[0]); ++offsets[b + 1]; meta.push_back(b | (b << 12)); }
    for (size_t i = 1; i < offsets.size(); ++i) offsets[i] += offsets[i - 1];
    std::vector<float> params(256 * 8 + 16);
    params[4] = .1f; // antenna displacement in pulse 1
    params[3] = params[7] = 250.05f; // excluded in pulse 0, included in pulse 1
    const size_t m = 256 * 8;
    params[m] = 10; params[m + 1] = 2; params[m + 3] = 1;
    params[m + 4] = np; params[m + 5] = nf; params[m + 6] = 1; params[m + 7] = positions.size();
    std::vector<std::complex<double>> expected(nf * np);
    for (uint32_t pulse = 0; pulse < np; ++pulse)
        for (size_t i = 0; i < positions.size(); ++i) {
            const double r = positions[i][0] - double(params[pulse * 4]);
            if(r>params[pulse*4+3])continue;
            const auto value = std::complex<double>(signals[i][0], signals[i][1]) * std::polar(1.0, -4 * std::acos(-1.0) * r / 10);
            const uint32_t bin = uint32_t(r); const double fraction = r - bin;
            expected[pulse * nf + bin] += value * (1 - fraction);
            expected[pulse * nf + bin + 1] += value * fraction;
        }
    for (uint32_t segments : {1u, 2u, 8u, 31u}) {
        auto partial = gpu.run("radar_echo.comp", {bytes(positions), bytes(signals),
            bytes(std::vector<float>(segments * np * nf * 2)), bytes(params, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
            bytes(meta), bytes(offsets), bytes(std::vector<uint32_t>{0})}, nf / 128, np, segments);
        const std::array<uint32_t, 4> push{nf * np, np, nf, segments};
        auto reduced = gpu.run("radar_echo_reduce.comp", {partial[2], bytes(std::vector<float>(np * nf * 2)),
            bytes(params,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)}, (np * nf + 255) / 256, 1, 1, &push);
        std::vector<float> actual(np * nf * 2); std::memcpy(actual.data(), reduced[1].data.data(), reduced[1].data.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            if (std::abs(std::complex<double>(actual[2*i], actual[2*i+1]) - expected[i]) >= 2e-3) {
                std::cerr << "segments=" << segments << " cell=" << i
                    << " actual=" << actual[2*i] << "," << actual[2*i+1]
                    << " expected=" << expected[i] << '\n';
                throw std::runtime_error("segmented radar echo disagrees with direct coherent sum");
            }
        }
    }
    std::cout << "PASS: production radar shaders vs direct sum, multiple buckets and segment counts\n";
}

void radarNoise(ComputeDevice& gpu) {
    constexpr uint32_t cells=8192;
    std::vector<float> zero(cells*2),params(256*8+16);
    constexpr size_t receiver=256*8+12;
    params[receiver]=.04f;
    const std::array<uint32_t,4> push{cells,4u,cells/4,1u};
    auto receive=[&](float seed){
        params[receiver+1]=seed;
        auto output=gpu.run("radar_echo_reduce.comp",{bytes(zero),bytes(zero),bytes(params,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)},cells/256,1,1,&push);
        std::vector<float> samples(zero.size());
        std::memcpy(samples.data(),output[1].data.data(),output[1].data.size());
        return samples;
    };
    const auto first=receive(412);
    require(first==receive(412)&&first!=receive(413),"receiver noise did not follow its frame seed");
    double power=0,iMean=0,qMean=0,iq=0;
    for(size_t n=0;n<first.size();n+=2){
        const double i=first[n],q=first[n+1];
        iMean+=i;qMean+=q;power+=i*i+q*q;iq+=i*q;
    }
    require(std::abs(std::sqrt(power/cells)-.04)<.0015&&std::abs(iMean/cells)<.001&&
            std::abs(qMean/cells)<.001&&std::abs(iq/cells)<.00005,"complex receiver noise moments differ from configuration");
    std::cout<<"PASS: deterministic Vulkan receiver noise, RMS and I/Q independence\n";
}
} // namespace

int main() {
    try {
        ComputeDevice gpu;
        if (!gpu.init()) { std::cout << "SKIP: no Vulkan compute device\n"; return 77; }
        cloudPaths(gpu);
        if (!gpu.atomicFloat) { std::cout << "SKIP: radar requires shared float atomics\n"; return 77; }
        radarSegments(gpu);
        radarNoise(gpu);
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
