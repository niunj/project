#include "DefaultAllocator.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "SLAssert.h"
#include "MemoryStats.h"
#include "VulkanInitializers.h"
#include "VulkanDebug.h"

#define VMA_IMPLEMENTATION
#include "sl_vk_mem_alloc.h"

namespace SilverLining
{
namespace Vulkan
{
VmaAllocator DefaultAllocator::CreateAllocator(void)
{
    VmaAllocatorCreateInfo allocatorCreateInfo{};
    allocatorCreateInfo.device = device->VulkanDevice();
    allocatorCreateInfo.physicalDevice = device->GetPhysicalDevice()->VulkanPhysicalDevice();
    allocatorCreateInfo.instance = device->GetPhysicalDevice()->GetInstance()->VulkanInstance();

    if (device->GetPhysicalDevice()->ExtensionSupported(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)
            && device->Extensions().vkGetBufferDeviceAddressKHR) {
        allocatorCreateInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    }

    VmaDeviceMemoryCallbacks callbacks;
    callbacks.pfnAllocate = &DefaultAllocator::VmaAllocateMemory;
    callbacks.pfnFree = &DefaultAllocator::VmaFreeMemory;
    callbacks.pUserData = device;
    allocatorCreateInfo.pDeviceMemoryCallbacks = &callbacks;

    VmaAllocator vmaAllocator = nullptr;
    VkResult result = vmaCreateAllocator(&allocatorCreateInfo, &vmaAllocator);
    if (result != VK_SUCCESS) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "cannot create vma!" << std::endl;
        vmaAllocator = VK_NULL_HANDLE;
    }

    return vmaAllocator;
}

DefaultAllocator::DefaultAllocator(Device* device)
    : device(device)
{
    vmaAllocator = CreateAllocator();
}

DefaultAllocator::~DefaultAllocator()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (mapBuffersToProps.empty() == false) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "buffer leaked!!" << std::endl;
    }
    if (mapImagesToProps.empty() == false) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "images leaked!!" << std::endl;
    }

    device->log(LogLevel::SL_LOG_LEVEL_INFO) << "SilverLining max ever allocations: " << device->GetMemoryStats()->NumMemoryAllocationsMaxEver() << std::endl;

    if (vmaAllocator) {
        vmaDestroyAllocator(vmaAllocator);
    }
}

void DefaultAllocator::VmaAllocateMemory(VmaAllocator allocator, uint32_t memoryType, VkDeviceMemory memory, VkDeviceSize size, void* pUserData)
{
    Device* device = (Device*)(pUserData);
    SL_ASSERT(device);
    device->GetMemoryStats()->MemoryAllocated();
}

void DefaultAllocator::VmaFreeMemory(VmaAllocator allocator, uint32_t memoryType, VkDeviceMemory memory, VkDeviceSize size, void* pUserData)
{
    Device* device = (Device*)(pUserData);
    SL_ASSERT(device);
    device->GetMemoryStats()->MemoryDeAllocated();
}

VkBuffer DefaultAllocator::CreateBuffer(VkBufferUsageFlags _usageFlags, VkMemoryPropertyFlags _memoryPropertyFlags, VkDeviceSize _sizeInBytes)
{
    std::lock_guard<std::mutex> lock(mutex);
    VkBufferCreateInfo bufferCreateInfo = vkInitializers::bufferCreateInfo(_usageFlags, _sizeInBytes);

    VmaAllocationCreateInfo allocationCreateInfo{};
    allocationCreateInfo.requiredFlags = _memoryPropertyFlags;

    VmaAllocationInfo* vmaAllocationInfo = new VmaAllocationInfo();
    memset(&vmaAllocationInfo, sizeof(VmaAllocationInfo), 0);

    VkBuffer buffer = nullptr;
    VmaAllocation vmaAllocation = VK_NULL_HANDLE;
    VK_CHECK_RESULT(vmaCreateBuffer(vmaAllocator, &bufferCreateInfo, &allocationCreateInfo, &buffer, &vmaAllocation, vmaAllocationInfo));

    mapBuffersToProps.insert({ buffer, {vmaAllocation, vmaAllocationInfo} });

    device->GetMemoryStats()->Created(buffer, _sizeInBytes);

    return buffer;
}

void DefaultAllocator::DestroyBuffer(VkBuffer buffer)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = mapBuffersToProps.find(buffer);
    if (it == mapBuffersToProps.end()) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "buffer not found; probably not created by this allocator" << std::endl;
        return;
    }

    device->GetMemoryStats()->AboutToBeDestroyed(buffer);

    VmaAllocationInfo* vmaAllocationInfo = it->second.vmaAllocationInfo;
    delete vmaAllocationInfo;

    VmaAllocation vmaAllocation = it->second.vmaAllocation;
    vmaDestroyBuffer(vmaAllocator, buffer, vmaAllocation);

    mapBuffersToProps.erase(it);
}

VkResult DefaultAllocator::Map(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize sizeInBytes, void*& mappedPtr)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = mapBuffersToProps.find(buffer);
    if (it == mapBuffersToProps.end()) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "buffer not found; probably not created by this allocator" << std::endl;
        mappedPtr = nullptr;
        return VK_ERROR_UNKNOWN;
    }

    VmaAllocation vmaAllocation = it->second.vmaAllocation;
    return vmaMapMemory(vmaAllocator, vmaAllocation, &mappedPtr);
}

void DefaultAllocator::UnMap(VkBuffer buffer)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = mapBuffersToProps.find(buffer);
    if (it == mapBuffersToProps.end()) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "buffer not found; probably not created by this allocator" << std::endl;
        return;
    }

    VmaAllocation vmaAllocation = it->second.vmaAllocation;
    vmaUnmapMemory(vmaAllocator, vmaAllocation);
}

VkResult DefaultAllocator::Flush(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize sizeInBytes)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = mapBuffersToProps.find(buffer);
    if (it == mapBuffersToProps.end()) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "buffer not found; probably not created by this allocator" << std::endl;
        return VK_ERROR_UNKNOWN;
    }

    VmaAllocation vmaAllocation = it->second.vmaAllocation;

    return vmaFlushAllocation(vmaAllocator, vmaAllocation, offset, sizeInBytes);
}

VkImage DefaultAllocator::CreateImage(const VkImageCreateInfo& imageCreateInfo, VkMemoryPropertyFlags memoryPropertyFlags)
{
    std::lock_guard<std::mutex> lock(mutex);
    VmaAllocationCreateInfo allocationCreateInfo{};
    allocationCreateInfo.requiredFlags = memoryPropertyFlags;

    VmaAllocationInfo* vmaAllocationInfo = new VmaAllocationInfo();
    memset(&vmaAllocationInfo, sizeof(VmaAllocationInfo), 0);

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation vmaAllocation = VK_NULL_HANDLE;
    VK_CHECK_RESULT(vmaCreateImage(vmaAllocator, &imageCreateInfo, &allocationCreateInfo, &image, &vmaAllocation, vmaAllocationInfo));

    mapImagesToProps.insert({ image, {vmaAllocation, vmaAllocationInfo} });

    device->GetMemoryStats()->Created(image, vmaAllocationInfo->size);

    return image;
}

void DefaultAllocator::DestroyImage(VkImage image)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = mapImagesToProps.find(image);
    if (it == mapImagesToProps.end()) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "image not found; probably not created by this allocator" << std::endl;
        return;
    }

    device->GetMemoryStats()->AboutToBeDestroyed(image);

    VmaAllocationInfo* vmaAllocationInfo = it->second.vmaAllocationInfo;
    VmaAllocation vmaAllocation = it->second.vmaAllocation;

    delete vmaAllocationInfo;
    vmaDestroyImage(vmaAllocator, image, vmaAllocation);

    mapImagesToProps.erase(it);
}

VkDeviceSize DefaultAllocator::SizeInBytes(VkImage image)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = mapImagesToProps.find(image);
    if (it == mapImagesToProps.end()) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "image not found; probably not created by this allocator" << std::endl;
        return 0;
    }
    // may be slightly larger than the image size required;
    return it->second.vmaAllocationInfo->size;
}

VkResult DefaultAllocator::Map(VkImage image, VkDeviceSize offset, VkDeviceSize sizeInBytes, void*& mappedPtr)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = mapImagesToProps.find(image);
    if (it == mapImagesToProps.end()) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "image not found; probably not created by this allocator" << std::endl;
        mappedPtr = nullptr;
        return VK_ERROR_UNKNOWN;
    }

    VmaAllocation vmaAllocation = it->second.vmaAllocation;
    return vmaMapMemory(vmaAllocator, vmaAllocation, &mappedPtr);
}

void DefaultAllocator::UnMap(VkImage image)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = mapImagesToProps.find(image);
    if (it == mapImagesToProps.end()) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "image not found; probably not created by this allocator" << std::endl;
        return;
    }

    VmaAllocation vmaAllocation = it->second.vmaAllocation;
    vmaUnmapMemory(vmaAllocator, vmaAllocation);
}
}
}