#include <rocky_env/GpuContext.h>
#include <vsg/all.h>
#include <stdexcept>
#include <cstring>
#include <map>
#include <algorithm>

namespace rockyenv::sensor {
namespace {
void check(VkResult result,const char* operation) {
    if(result!=VK_SUCCESS) throw std::runtime_error(std::string(operation)+": Vulkan error "+std::to_string(result));
}
}
struct GpuContext::Impl {
    vsg::ref_ptr<vsg::Instance> instance;
    vsg::ref_ptr<vsg::Device> device;
    bool rt=false,radar=false;
    bool submissionPending=false;
    uint64_t submissionTimeoutNs=60000000000ull;
    std::string name;
    struct Task { vsg::ref_ptr<vsg::RecordAndSubmitTask> submit; vsg::ref_ptr<vsg::TransferTask> transfer; };
    std::map<vsg::ref_ptr<vsg::CommandGraph>,Task> tasks;
    uint64_t executionSubmissions=0;
    vsg::ref_ptr<vsg::Buffer> host;
    vsg::ref_ptr<vsg::DeviceMemory> hostMemory;
    vsg::ref_ptr<vsg::CommandPool> readPool;
    vsg::ref_ptr<vsg::CommandBuffer> readCommand;
    vsg::ref_ptr<vsg::Fence> readFence;
    VkDeviceSize hostCapacity=0;
    void* mapped=nullptr;
    ~Impl(){if(mapped&&hostMemory)hostMemory->unmap();}
};
GpuContext::GpuContext():impl_(std::make_unique<Impl>()) {}
GpuContext::~GpuContext() { try { clearGraphs(); } catch(...) {} }
void GpuContext::initialize(bool validation,vsg::Device* shared) try {
    if(impl_->device) throw std::logic_error("GPU context already initialized");
    if(shared) {
        if(validation) throw std::invalid_argument("shared GPU validation must be enabled when the host device is created");
        impl_->device=shared; impl_->instance=shared->getInstance();
    } else {
    vsg::Names layers;
    if(validation) {
        layers=vsg::validateInstanceLayerNames({"VK_LAYER_KHRONOS_validation"});
        if(layers.empty()) throw std::runtime_error("requested Vulkan validation layer is unavailable");
    }
    impl_->instance=vsg::Instance::create(vsg::Names{},layers,VK_API_VERSION_1_2);
    auto physical=impl_->instance->getPhysicalDevice(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT);
    if(!physical) throw std::runtime_error("no Vulkan graphics+compute device");
    auto enabled=vsg::DeviceFeatures::create(); vsg::Names enabledExtensions;
    configureDevice(*physical,enabledExtensions,enabled);
    const auto family=physical->getQueueFamily(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT);
    impl_->device=vsg::Device::create(physical,vsg::QueueSettings{{family,{1.0f}}},vsg::Names{},enabledExtensions,enabled);
    }
    auto has=[&](const char* name) { for(const auto* ext:impl_->device->enabledExtensions) if(std::strcmp(ext,name)==0) return true; return false; };
    impl_->rt=has(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) && has(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    if(impl_->rt && !impl_->device->getExtensions()->vkGetBufferDeviceAddressKHR)
        throw std::runtime_error("RT device is missing the VSG buffer device address entry point");
    impl_->radar=has(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
    VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(impl_->device->getPhysicalDevice()->vk(),&properties);
    impl_->name=properties.deviceName;
    // CPU Vulkan can compile/execute shaders slowly under build/CI load.
    if(properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU) impl_->submissionTimeoutNs=300000000000ull;
} catch(const vsg::Exception& error) { throw std::runtime_error(error.message+" ("+std::to_string(error.result)+")"); }

void GpuContext::configureDevice(vsg::PhysicalDevice& physicalArg,std::vector<const char*>& enabledExtensions,vsg::ref_ptr<vsg::DeviceFeatures>& enabled) {
    auto* physical=&physicalArg;
    uint32_t count=0;
    check(vkEnumerateDeviceExtensionProperties(physical->vk(),nullptr,&count,nullptr),"enumerate device extensions");
    std::vector<VkExtensionProperties> extensions(count);
    check(vkEnumerateDeviceExtensionProperties(physical->vk(),nullptr,&count,extensions.data()),"enumerate device extensions");
    auto has=[&](const char* name) { for(const auto& extension:extensions) if(std::strcmp(extension.extensionName,name)==0) return true; return false; };
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceBufferDeviceAddressFeatures address{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomic{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext=&rt; rt.pNext=&acceleration; acceleration.pNext=&address; address.pNext=&atomic;
    vkGetPhysicalDeviceFeatures2(physical->vk(),&features);
    const bool supportsRT=has(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) && has(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
        has(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) && has(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) && rt.rayTracingPipeline && acceleration.accelerationStructure && address.bufferDeviceAddress;
    const bool supportsRadar=has(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME) && atomic.shaderSharedFloat32AtomicAdd;
    if(!enabled) enabled=vsg::DeviceFeatures::create();
    auto extension=[&](const char* name) { for(const auto* item:enabledExtensions) if(std::strcmp(item,name)==0) return; enabledExtensions.push_back(name); };
    if(supportsRT) {
        // VSG builds BLAS with the KHR entry point, including on Vulkan 1.2 devices.
        extension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
        extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME); extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME); extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        enabled->get<VkPhysicalDeviceRayTracingPipelineFeaturesKHR,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR>().rayTracingPipeline=VK_TRUE;
        enabled->get<VkPhysicalDeviceAccelerationStructureFeaturesKHR,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR>().accelerationStructure=VK_TRUE;
        enabled->get<VkPhysicalDeviceBufferDeviceAddressFeatures,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES>().bufferDeviceAddress=VK_TRUE;
    }
    if(supportsRadar) {
        extension(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
        auto& af=enabled->get<VkPhysicalDeviceShaderAtomicFloatFeaturesEXT,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT>();
        af.shaderSharedFloat32Atomics=atomic.shaderSharedFloat32Atomics;
        af.shaderSharedFloat32AtomicAdd=VK_TRUE;
    }
}
vsg::Device* GpuContext::device() const { return impl_->device; }
bool GpuContext::rayTracing() const { return impl_->rt; }
bool GpuContext::radarSupported() const { return impl_->radar; }
const std::string& GpuContext::deviceName() const { return impl_->name; }
void GpuContext::waitIdle() { if(impl_->device) for(const auto& queue:impl_->device->getQueues()) check(queue->waitIdle(),"wait GPU queue idle"); }
void GpuContext::clearGraphs() {
    // execute() waits its own fence. A failed submission is the only path
    // that can leave work outstanding when this owner releases its graphs.
    if(impl_->submissionPending) waitIdle();
    impl_->submissionPending=false;
    impl_->tasks.clear();
}
void GpuContext::compile(vsg::ref_ptr<vsg::CommandGraph> graph) {
    if(!impl_->device || !graph || graph->window) throw std::invalid_argument("offscreen command graph requires a device and no window");
    auto requirements=vsg::CollectResourceRequirements::create(); graph->accept(*requirements);
    graph->maxSlots=requirements->requirements.maxSlots;
    auto transfer=vsg::TransferTask::create(impl_->device,1);
    const auto family=uint32_t(graph->queueFamily);
    transfer->transferQueue=impl_->device->getQueue(family);
    auto compiler=vsg::CompileTraversal::create(impl_->device,requirements->requirements);
    for(auto& context:compiler->contexts) {
        context->transferTask=transfer;
        context->graphicsQueue=impl_->device->getQueue(family);
        context->commandPool=vsg::CommandPool::create(impl_->device,family);
    }
    graph->accept(*compiler);
    if(compiler->record()) compiler->waitForCompletion();
    transfer->assign(requirements->requirements.dynamicData);
    auto task=vsg::RecordAndSubmitTask::create(impl_->device,1);
    task->queue=impl_->device->getQueue(family); task->commandGraphs={graph}; task->transferTask=transfer;
    impl_->tasks[graph]={task,transfer};
}
void GpuContext::execute(vsg::ref_ptr<vsg::CommandGraph> graph) {
    const auto found=impl_->tasks.find(graph);
    if(found==impl_->tasks.end()) throw std::logic_error("compile offscreen graph before executing");
    auto& task=found->second.submit;
    task->advance();
    impl_->submissionPending=true;
    check(task->submit(),"submit offscreen measurement");
    ++impl_->executionSubmissions;
    check(task->fence()->wait(impl_->submissionTimeoutNs),"wait offscreen measurement");
    impl_->submissionPending=false;
}
uint64_t GpuContext::executionSubmissions()const{return impl_->executionSubmissions;}
void GpuContext::executeBatch(const std::vector<vsg::ref_ptr<vsg::CommandGraph>>& graphs){
    if(graphs.empty())return;
    std::vector<vsg::ref_ptr<vsg::RecordAndSubmitTask>> work;
    for(const auto& graph:graphs){
        const auto found=impl_->tasks.find(graph);
        if(found==impl_->tasks.end())throw std::logic_error("compile every equipment graph before batch execution");
        if(std::find(work.begin(),work.end(),found->second.submit)!=work.end())throw std::invalid_argument("duplicate mutable graph in equipment batch");
        work.push_back(found->second.submit);
    }
    // Keep the original compiled traversal and dynamic-data transfer domain of
    // each equipment. Queue the complete compatible group before waiting; no
    // host pose/UBO/output may change until every completion fence is signaled.
    for(const auto& task:work){task->advance();impl_->submissionPending=true;check(task->submit(),"enqueue offscreen equipment batch");++impl_->executionSubmissions;}
    for(const auto& task:work)check(task->fence()->wait(impl_->submissionTimeoutNs),"wait offscreen equipment batch");
    impl_->submissionPending=false;
}
void GpuContext::readbackBatch(const std::vector<Readback>& requests){
    if(requests.empty())return;
    auto& p=*impl_;std::vector<VkDeviceSize> offsets;VkDeviceSize total=0;
    for(const auto& r:requests){
        if((bool(r.image)==bool(r.buffer))||!r.destination||!r.bytes||r.bytes%4)throw std::invalid_argument("invalid batch readback");
        offsets.push_back(total);total+=(r.bytes+15)&~VkDeviceSize(15);
        if(total>256ull*1024*1024)throw std::invalid_argument("readback batch exceeds 256 MiB");
    }
    if(total>p.hostCapacity){
        if(p.mapped){p.hostMemory->unmap();p.mapped=nullptr;}
        VkDeviceSize capacity=4096;while(capacity<total)capacity*=2;
        p.host=vsg::Buffer::create(capacity,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_SHARING_MODE_EXCLUSIVE);p.host->compile(p.device);
        p.hostMemory=vsg::DeviceMemory::create(p.device,p.host->getMemoryRequirements(p.device->deviceID),VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        p.host->bind(p.hostMemory,0);check(p.hostMemory->map(0,capacity,0,&p.mapped),"map persistent sensor staging");p.hostCapacity=capacity;
    }
    const auto family=p.device->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT);
    if(!p.readPool){p.readPool=vsg::CommandPool::create(p.device,family,VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);p.readCommand=p.readPool->allocate();p.readFence=vsg::Fence::create(p.device);}
    const auto cb=p.readCommand->vk();check(vkResetCommandBuffer(cb,0),"reset readback commands");
    const auto fence=p.readFence->vk();check(vkResetFences(p.device->vk(),1,&fence),"reset readback fence");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(vkBeginCommandBuffer(cb,&begin),"begin batch readback");
    for(size_t i=0;i<requests.size();++i){const auto& r=requests[i];
        if(r.image){
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.oldLayout=r.layout;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=r.image->image->vk(p.device->deviceID);barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            VkBufferImageCopy copy{};copy.bufferOffset=offsets[i];copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent=r.image->image->extent;
            vkCmdCopyImageToBuffer(cb,barrier.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,p.host->vk(p.device->deviceID),1,&copy);
            barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.newLayout=r.layout;barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        }else{
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
            VkBufferCopy copy{0,offsets[i],r.bytes};vkCmdCopyBuffer(cb,r.buffer->vk(p.device->deviceID),p.host->vk(p.device->deviceID),1,&copy);
        }
    }
    VkMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};hostBarrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;hostBarrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&hostBarrier,0,nullptr,0,nullptr);
    check(vkEndCommandBuffer(cb),"end batch readback");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cb;
    p.submissionPending=true;check(p.device->getQueue(family)->submit(submit,p.readFence),"submit batch readback");check(p.readFence->wait(p.submissionTimeoutNs),"wait batch readback");p.submissionPending=false;
    for(size_t i=0;i<requests.size();++i)std::memcpy(requests[i].destination,static_cast<const char*>(p.mapped)+offsets[i],size_t(requests[i].bytes));
}
}
