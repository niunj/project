#include <rocky_env/DynamicScene.h>
#include <stdexcept>
#include <cstring>

namespace rockyenv::sensor {
namespace {
VkDeviceAddress address(vsg::Device* device,vsg::Buffer* buffer) {
    VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; info.buffer=buffer->vk(device->deviceID);
    return device->getExtensions()->vkGetBufferDeviceAddressKHR(device->vk(),&info);
}
vsg::ref_ptr<vsg::Buffer> addressedBuffer(vsg::Device* device,VkDeviceSize bytes,VkBufferUsageFlags usage,VkMemoryPropertyFlags memory) {
    VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO}; flags.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    return vsg::createBufferAndMemory(device,bytes,usage|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,VK_SHARING_MODE_EXCLUSIVE,memory,&flags);
}
class BuildTlas : public vsg::Inherit<vsg::BuildAccelerationStructureCommand,BuildTlas> {
public:
    BuildTlas(vsg::Device* device,const VkAccelerationStructureBuildGeometryInfoKHR& info,VkAccelerationStructureKHR structure,
              uint32_t count,vsg::ref_ptr<vsg::Buffer> scratch,VkDeviceSize alignment,bool update):
        Inherit(device,info,structure,std::vector<uint32_t>{count}),scratch_(scratch),alignment_(alignment),update_(update) {}
    void record(vsg::CommandBuffer& command) const override {
        VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        before.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        before.dstAccessMask=VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR|VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(command.vk(),VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,0,1,&before,0,nullptr,0,nullptr);
        auto info=_accelerationStructureInfo;
        info.mode=update_ ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        info.srcAccelerationStructure=update_ ? _accelerationStructure : VK_NULL_HANDLE;
        info.dstAccelerationStructure=_accelerationStructure;
        info.pGeometries=_accelerationStructureGeometries.data();
        const auto base=address(_device,scratch_);
        info.scratchData.deviceAddress=(base+alignment_-1)/alignment_*alignment_;
        const auto* range=_accelerationStructureBuildRangeInfos.data();
        _device->getExtensions()->vkCmdBuildAccelerationStructuresKHR(command.vk(),1,&info,&range);
        VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        after.srcAccessMask=VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        after.dstAccessMask=VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(command.vk(),VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR|VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,0,1,&after,0,nullptr,0,nullptr);
    }
private:
    vsg::ref_ptr<vsg::Buffer> scratch_;
    VkDeviceSize alignment_;
    bool update_;
};
}
DynamicTlas::DynamicTlas(vsg::Device* device):Inherit(device) {}
void DynamicTlas::writeInstances() {
    std::vector<VkAccelerationStructureInstanceKHR> packed(geometryInstances.size());
    for(std::size_t i=0;i<packed.size();++i) {
        const auto& source=*geometryInstances[i]; auto& target=packed[i];
        for(int row=0;row<3;++row) for(int col=0;col<4;++col) target.transform.matrix[row][col]=source.transform[col][row];
        target.instanceCustomIndex=source.id; target.mask=source.mask;
        target.instanceShaderBindingTableRecordOffset=source.shaderOffset; target.flags=source.flags;
        target.accelerationStructureReference=source.accelerationStructure->handle();
    }
    instancesBuffer_->getDeviceMemory(_device->deviceID)->copy(0,packed.size()*sizeof(packed[0]),packed.data());
}
void DynamicTlas::compile(vsg::Context& context) {
    if(_handle) return;
    _requiredBuildScratchSize=0; // persistent scratch is owned by this TLAS
    if(geometryInstances.empty()) throw std::invalid_argument("TLAS requires geometry instances");
    for(auto& instance:geometryInstances) instance->accelerationStructure->compile(context);
    instancesBuffer_=addressedBuffer(_device,geometryInstances.size()*sizeof(VkAccelerationStructureInstanceKHR),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    writeInstances();
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress=address(_device,instancesBuffer_);
    VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    info.type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    info.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR|VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    info.geometryCount=1; info.pGeometries=&geometry;
    const uint32_t count=uint32_t(geometryInstances.size());
    VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    _device->getExtensions()->vkGetAccelerationStructureBuildSizesKHR(_device->vk(),VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&info,&count,&sizes);
    _buffer=addressedBuffer(_device,sizes.accelerationStructureSize,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    _accelerationStructureInfo={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    _accelerationStructureInfo.type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    _accelerationStructureInfo.buffer=_buffer->vk(_device->deviceID); _accelerationStructureInfo.size=sizes.accelerationStructureSize;
    const auto result=_device->getExtensions()->vkCreateAccelerationStructureKHR(_device->vk(),&_accelerationStructureInfo,nullptr,&_accelerationStructure);
    if(result!=VK_SUCCESS) throw std::runtime_error("create dynamic TLAS failed: "+std::to_string(result));
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    addressInfo.accelerationStructure=_accelerationStructure;
    _handle=_device->getExtensions()->vkGetAccelerationStructureDeviceAddressKHR(_device->vk(),&addressInfo);
    VkPhysicalDeviceAccelerationStructurePropertiesKHR properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2}; properties2.pNext=&properties;
    vkGetPhysicalDeviceProperties2(_device->getPhysicalDevice()->vk(),&properties2);
    const auto alignment=properties.minAccelerationStructureScratchOffsetAlignment;
    scratch_=addressedBuffer(_device,std::max(sizes.buildScratchSize,sizes.updateScratchSize)+alignment,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    auto build=BuildTlas::create(_device,info,_accelerationStructure,count,scratch_,alignment,false);
    context.buildAccelerationStructureCommands.push_back(build);
    update_=BuildTlas::create(_device,info,_accelerationStructure,count,scratch_,alignment,true);
}
void DynamicTlas::update(const std::vector<vsg::dmat4>& transforms,const std::vector<bool>& visible) {
    if(transforms.size()!=geometryInstances.size() || visible.size()!=transforms.size()) throw std::invalid_argument("TLAS topology changed; rebuild scene resources");
    for(std::size_t i=0;i<transforms.size();++i) {
        geometryInstances[i]->transform=vsg::mat4(transforms[i]); geometryInstances[i]->mask=visible[i] ? 255 : 0;
    }
    if(_handle) writeInstances();
}
}
