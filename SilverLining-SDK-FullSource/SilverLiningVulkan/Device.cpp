// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "Device.h"
#include "PhysicalDevice.h"
#include "VulkanDebug.h"
#include "VulkanInitializers.h"
#include "MemoryStats.h"
#include "BasicAllocator.h"
#include "DefaultAllocator.h"

#include <iostream>
#include <functional>

namespace SilverLining
{
namespace Vulkan
{

Device::Device(PhysicalDevice* _physicalDevice, VkDevice _logicalDevice, VkQueue _graphicsQueue, uint32_t _graphicsQueueFamilyIndex, VulkanMemoryAllocator* vulkanMemoryAllocator)
    : physicalDevice(_physicalDevice)
    , logicalDevice(_logicalDevice)
    , graphicsQueue(_graphicsQueue)
    , graphicsQueueFamilyIndex(_graphicsQueueFamilyIndex)
    , enableDebugMarkers(false)
{
    // Enable the debug marker extension if it is present (likely meaning a debugging tool is present)
    // PPP: why does this not work? It does not seem to work for the original samples either
    //if (_physicalDevice->extensionSupported(VK_EXT_DEBUG_MARKER_EXTENSION_NAME))
    {
        enableDebugMarkers = true;
    }

    // Create a default command pool for graphics command buffers
    graphicsCommandPool = CreateCommandPool(graphicsQueueFamilyIndex);

    vulkanExtensions.Initialize(this);

    DeviceAllocatorStatsRefCount* allocatorStatsRefcount = nullptr;
    auto it = s_MapDeviceAllocatorStatsRefCount.find(VulkanDevice());
    if (it == s_MapDeviceAllocatorStatsRefCount.end()) {
        allocatorStatsRefcount = new DeviceAllocatorStatsRefCount();
        s_MapDeviceAllocatorStatsRefCount.insert({VulkanDevice(), allocatorStatsRefcount});
    } else {
        allocatorStatsRefcount = it->second;
    }

    SL_ASSERT(allocatorStatsRefcount);

    if (vulkanMemoryAllocator) {
        if (allocatorStatsRefcount->vmaAllocator == nullptr) {
            allocatorStatsRefcount->vmaAllocator = vulkanMemoryAllocator;
            allocatorStatsRefcount->vmaAllocatorOwned = false;
        }
        SL_ASSERT(allocatorStatsRefcount->vmaAllocator == vulkanMemoryAllocator && allocatorStatsRefcount->vmaAllocatorOwned == false);
    } else if (allocatorStatsRefcount->vmaAllocator == nullptr) {
        allocatorStatsRefcount->vmaAllocator = new DefaultAllocator(this);
        allocatorStatsRefcount->vmaAllocatorOwned = true;
    }

    if (allocatorStatsRefcount->memoryStats == nullptr) {
        allocatorStatsRefcount->memoryStats = new MemoryStats();
    }

    ++allocatorStatsRefcount->refCount;
}

Device::~Device()
{
    auto it = s_MapDeviceAllocatorStatsRefCount.find(VulkanDevice());
    SL_ASSERT(it != s_MapDeviceAllocatorStatsRefCount.end());
    DeviceAllocatorStatsRefCount* allocatorStatsRefcount = it->second;
    --allocatorStatsRefcount->refCount;
    if (allocatorStatsRefcount->refCount == 0) {
        if (allocatorStatsRefcount->vmaAllocator && allocatorStatsRefcount->vmaAllocatorOwned) {
            delete allocatorStatsRefcount->vmaAllocator;
        }
        delete allocatorStatsRefcount->memoryStats;
        delete allocatorStatsRefcount;

        s_MapDeviceAllocatorStatsRefCount.erase(it);
    }
    if (graphicsCommandPool) {
        vkDestroyCommandPool(logicalDevice, graphicsCommandPool, nullptr);
    }
}

VkDevice Device::VulkanDevice() const
{
    return logicalDevice;
}

const PhysicalDevice* Device::GetPhysicalDevice() const
{
    return physicalDevice;
}

VkCommandBuffer Device::CreateCommandBuffer(VkCommandBufferLevel level, bool begin)
{
    VkCommandBuffer cmdBuffer;

    VkCommandBufferAllocateInfo cmdBufAllocateInfo = {};
    cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufAllocateInfo.commandPool = graphicsCommandPool;
    cmdBufAllocateInfo.level = level;
    cmdBufAllocateInfo.commandBufferCount = 1;

    VK_CHECK_RESULT(vkAllocateCommandBuffers(logicalDevice, &cmdBufAllocateInfo, &cmdBuffer));

    // If requested, also start the new command buffer
    if (begin) {
        VkCommandBufferBeginInfo cmdBufInfo = vkInitializers::commandBufferBeginInfo();
        VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));
    }

    return cmdBuffer;
}

void Device::FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, VkCommandPool pool, bool free)
{
    SL_ASSERT(commandBuffer != VK_NULL_HANDLE);
    if (commandBuffer == VK_NULL_HANDLE) {
        return;
    }

    VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo = vkInitializers::submitInfo();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    // Create fence to ensure that the command buffer has finished executing
    VkFenceCreateInfo fenceCreateInfo = vkInitializers::fenceCreateInfo();
    VkFence fence;
    VK_CHECK_RESULT(vkCreateFence(logicalDevice, &fenceCreateInfo, nullptr, &fence));

    // Submit to the queue
    VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));

    // Wait for the fence to signal that command buffer has finished executing
    VK_CHECK_RESULT(vkWaitForFences(logicalDevice, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

    vkDestroyFence(logicalDevice, fence, nullptr);

    if (free) {
        vkFreeCommandBuffers(logicalDevice, pool, 1, &commandBuffer);
    }
}

void Device::FlushCommandBuffer(VkCommandBuffer commandBuffer)
{
    FlushCommandBuffer(commandBuffer, graphicsQueue, graphicsCommandPool, true);
}

VkCommandPool Device::CreateCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags)
{
    VkCommandPoolCreateInfo cmdPoolInfo = {};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.queueFamilyIndex = queueFamilyIndex;
    cmdPoolInfo.flags = createFlags;
    VkCommandPool cmdPool;
    VK_CHECK_RESULT(vkCreateCommandPool(logicalDevice, &cmdPoolInfo, nullptr, &cmdPool));
    return cmdPool;
}

VkQueue Device::GraphicsQueue(void) const
{
    return graphicsQueue;
}

bool Device::EnableDebugMarkers(void) const
{
    return enableDebugMarkers;
}

const vkExtensions& Device::Extensions() const
{
    return vulkanExtensions;
}

VkCommandPool Device::GetCommandPool(void) const
{
    return graphicsCommandPool;
}

void Device::FullBarrier(VkCommandBuffer commandBuffer)
{
    VkMemoryBarrier2 memoryBarrier = vkInitializers::memoryBarrier2();

    memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    memoryBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

    VkDependencyInfo dependencyInfo = vkInitializers::dependencyInfo();
    dependencyInfo.memoryBarrierCount = 1;
    dependencyInfo.pMemoryBarriers = &memoryBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

SL_MAP(VkDevice, Device::DeviceAllocatorStatsRefCount*) Device::s_MapDeviceAllocatorStatsRefCount;
VulkanMemoryAllocator* Device::GetVulkanMemoryAllocator(void)
{
    auto it = s_MapDeviceAllocatorStatsRefCount.find(VulkanDevice());
    SL_ASSERT(it != s_MapDeviceAllocatorStatsRefCount.end());
    return it->second->vmaAllocator;
}

MemoryStats* Device::GetMemoryStats(void)
{
    auto it = s_MapDeviceAllocatorStatsRefCount.find(VulkanDevice());
    SL_ASSERT(it != s_MapDeviceAllocatorStatsRefCount.end());
    return it->second->memoryStats;
}

class NullStream : public std::ostream
{
    class NullBuffer : public std::streambuf
    {
    public:
        int overflow(int c)
        {
            return c;
        }
    } m_nb;
public:
    NullStream() : std::ostream(&m_nb) {}
};

std::ostream& Device::log(LogLevel level)
{
    if (level >= currentLogLevel) {
        return std::cout;
    }
    static NullStream null;
    return null;
}
}
}