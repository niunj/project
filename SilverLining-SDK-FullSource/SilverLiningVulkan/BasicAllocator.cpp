// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "BasicAllocator.h"
#include "VulkanDebug.h"
#include "VulkanInitializers.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "MemoryStats.h"

namespace SilverLining
{
namespace Vulkan
{
BasicAllocator::BasicAllocator(Device* _device)
    : device(_device)
{
    // no op
}

BasicAllocator::~BasicAllocator()
{
    if (mapBuffersToProps.empty() == false) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "buffer leaked!!" << std::endl;
    }
    if (mapImagesToProps.empty() == false) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "images leaked!!" << std::endl;
    }

    device->log(LogLevel::SL_LOG_LEVEL_INFO) << "SilverLining max ever allocations: " << device->GetMemoryStats()->NumMemoryAllocationsMaxEver();
}

VkBuffer BasicAllocator::CreateBuffer(VkBufferUsageFlags _usageFlags, VkMemoryPropertyFlags _memoryPropertyFlags, VkDeviceSize _sizeInBytes)
{
    VkBufferCreateInfo bufferCreateInfo = vkInitializers::bufferCreateInfo(_usageFlags, _sizeInBytes);

    VkBuffer buffer = VK_NULL_HANDLE;
    VK_CHECK_RESULT(vkCreateBuffer(device->VulkanDevice(), &bufferCreateInfo, nullptr, &buffer));

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(device->VulkanDevice(), buffer, &memoryRequirements);

    VkMemoryAllocateInfo memoryAllocateInfo = vkInitializers::memoryAllocateInfo();
    memoryAllocateInfo.allocationSize = memoryRequirements.size;

    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = vkInitializers::memoryAllocateFlagsInfo();
    if (_usageFlags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
        memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    }

    memoryAllocateInfo.memoryTypeIndex = device->GetPhysicalDevice()->GetMemoryTypeIndex(memoryRequirements.memoryTypeBits
                                         , _memoryPropertyFlags);

    VkDeviceMemory deviceMemory = VK_NULL_HANDLE;
    VK_CHECK_RESULT(vkAllocateMemory(device->VulkanDevice(), &memoryAllocateInfo, nullptr, &deviceMemory));
    vkBindBufferMemory(device->VulkanDevice(), buffer, deviceMemory, 0);

    BufferProperties props;
    props.deviceMemory = deviceMemory;
    props.sizeInBytes = _sizeInBytes;
    props.memoryTypeIndex = memoryAllocateInfo.memoryTypeIndex;
    mapBuffersToProps.insert({ buffer, props });

    device->GetMemoryStats()->MemoryAllocated();
    device->GetMemoryStats()->Created(buffer, _sizeInBytes);

    return buffer;
}

void BasicAllocator::DestroyBuffer(VkBuffer buffer)
{
    auto it = mapBuffersToProps.find(buffer);
    if (it == mapBuffersToProps.end()) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "buffer not found; probably not created by this allocator" << std::endl;
        return;
    }

    device->GetMemoryStats()->AboutToBeDestroyed(buffer);

    VkDeviceMemory deviceMemory = it->second.deviceMemory;
    if (buffer) {
        vkDestroyBuffer(device->VulkanDevice(), buffer, nullptr);
    }
    if (deviceMemory) {
        vkFreeMemory(device->VulkanDevice(), deviceMemory, nullptr);
    }

    mapBuffersToProps.erase(it);
    device->GetMemoryStats()->MemoryDeAllocated();
}

VkResult BasicAllocator::Map(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize sizeInBytes, void*& mappedPtr)
{
    auto it = mapBuffersToProps.find(buffer);
    if (it == mapBuffersToProps.end()) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "buffer not found; probably not created by this allocator" << std::endl;
        mappedPtr = nullptr;
        return VK_ERROR_UNKNOWN;
    }

    VkDeviceMemory deviceMemory = it->second.deviceMemory;

    return vkMapMemory(device->VulkanDevice(), deviceMemory, offset, sizeInBytes, 0, &mappedPtr);
}

void  BasicAllocator::UnMap(VkBuffer buffer)
{
    auto it = mapBuffersToProps.find(buffer);
    if (it == mapBuffersToProps.end()) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "buffer not found; probably not created by this allocator" << std::endl;
        return;
    }

    VkDeviceMemory deviceMemory = it->second.deviceMemory;

    vkUnmapMemory(device->VulkanDevice(), deviceMemory);
}

VkResult BasicAllocator::Flush(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize sizeInBytes)
{
    auto it = mapBuffersToProps.find(buffer);
    if (it == mapBuffersToProps.end()) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "buffer not found; probably not created by this allocator" << std::endl;
        return VK_ERROR_UNKNOWN;
    }

    VkMappedMemoryRange mappedRange = {};
    mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;

    VkDeviceMemory deviceMemory = it->second.deviceMemory;
    mappedRange.memory = deviceMemory;

    mappedRange.offset = offset;
    mappedRange.size = sizeInBytes;

    return vkFlushMappedMemoryRanges(device->VulkanDevice(), 1, &mappedRange);
}

VkImage BasicAllocator::CreateImage(const VkImageCreateInfo& imageCreateInfo, VkMemoryPropertyFlags memoryPropertyFlags)
{
    VkImage image = VK_NULL_HANDLE;
    VK_CHECK_RESULT(vkCreateImage(device->VulkanDevice(), &imageCreateInfo, nullptr, &image));

    VkMemoryRequirements memoryRequirements;
    vkGetImageMemoryRequirements(device->VulkanDevice(), image, &memoryRequirements);

    VkMemoryAllocateInfo memoryAllocateInfo = vkInitializers::memoryAllocateInfo();
    memoryAllocateInfo.allocationSize = memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = device->GetPhysicalDevice()->GetMemoryTypeIndex(memoryRequirements.memoryTypeBits
                                         , memoryPropertyFlags);

    VkDeviceMemory deviceMemory = VK_NULL_HANDLE;
    VK_CHECK_RESULT(vkAllocateMemory(device->VulkanDevice(), &memoryAllocateInfo, nullptr, &deviceMemory));
    VK_CHECK_RESULT(vkBindImageMemory(device->VulkanDevice(), image, deviceMemory, 0));

    // save out the allocation size
    VkDeviceSize sizeInBytes = memoryRequirements.size;

    ImageProperties props;
    props.deviceMemory = deviceMemory;
    props.sizeInBytes = sizeInBytes;
    mapImagesToProps.insert({ image, props});

    device->GetMemoryStats()->MemoryAllocated();
    device->GetMemoryStats()->Created(image, sizeInBytes);

    return image;
}

void BasicAllocator::DestroyImage(VkImage image)
{
    auto it = mapImagesToProps.find(image);
    if (it == mapImagesToProps.end()) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "image not found; probably not created by this allocator" << std::endl;
        return;
    }

    device->GetMemoryStats()->AboutToBeDestroyed(image);
    if (image) {
        vkDestroyImage(device->VulkanDevice(), image, nullptr);
    }
    VkDeviceMemory deviceMemory = it->second.deviceMemory;
    if (deviceMemory) {
        vkFreeMemory(device->VulkanDevice(), deviceMemory, nullptr);
    }

    mapImagesToProps.erase(it);
    device->GetMemoryStats()->MemoryDeAllocated();
}

VkDeviceSize BasicAllocator::SizeInBytes(VkImage image)
{
    auto it = mapImagesToProps.find(image);
    if (it == mapImagesToProps.end()) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "image not found; probably not created by this allocator" << std::endl;
        return 0;
    }
    return it->second.sizeInBytes;
}
}
}
