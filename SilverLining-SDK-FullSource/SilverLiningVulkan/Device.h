// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "MemAlloc.h"

#include <vulkan/vulkan.h>
#include <vector>

#include "VkExtensions.h"
#include "LogLevel.h"

#include <iostream>

namespace SilverLining
{
    namespace Vulkan
    {
        class Buffer;
        class PhysicalDevice;
        class RawBuffer;
        class MemoryStats;
        class VulkanMemoryAllocator;

        //! Encapsulates a vulkan device
        class Device
        {
        public:
            //! constructor
            Device(PhysicalDevice* physicalDevice, VkDevice device, VkQueue graphicsQueue, uint32_t graphicsQueueFamilyIndex, VulkanMemoryAllocator* gpuMemoryAllocator);

            //! destructor
            virtual ~Device();
        public:
            //! get
            VkDevice VulkanDevice() const;

            //! physical device corresponding to this device
            const PhysicalDevice* GetPhysicalDevice() const;

            //! graphics queue
            VkQueue GraphicsQueue(void) const;

            //! create a command buffer
            VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel level, bool begin);

            //! use specified queue
            void FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, VkCommandPool pool, bool free);

            //! graphics queue
            void FlushCommandBuffer(VkCommandBuffer commandBuffer);

            //! create a command pool
            VkCommandPool CreateCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

            //! enable debug markers
            bool EnableDebugMarkers(void) const;

            //! what extension are available?
            const vkExtensions& Extensions() const;

            //! get the command pool
            VkCommandPool GetCommandPool(void) const;

            //! get the command pool
            void FullBarrier(VkCommandBuffer commandBuffer);

            //! logging
            std::ostream& log(LogLevel level);

            //! access the vma (could be our own or the externally passed in one)
            VulkanMemoryAllocator* GetVulkanMemoryAllocator(void);

            MemoryStats* GetMemoryStats(void);
        
        protected:
            const PhysicalDevice* physicalDevice = nullptr;

            VkDevice logicalDevice = VK_NULL_HANDLE;

            VkCommandPool graphicsCommandPool = VK_NULL_HANDLE;

            VkQueue graphicsQueue = VK_NULL_HANDLE;

            SL_VECTOR(VkDeviceQueueCreateInfo) queueCreateInfos{};

            uint32_t graphicsQueueFamilyIndex = 0;

            bool enableDebugMarkers = false;

            const float defaultQueuePriority = 0.0f;

            vkExtensions vulkanExtensions;

            LogLevel currentLogLevel = LogLevel::SL_LOG_LEVEL_WARN;

            bool vmaAllocatorOwned = false;

            struct DeviceAllocatorStatsRefCount
            {
                VulkanMemoryAllocator* vmaAllocator = VK_NULL_HANDLE;
                bool vmaAllocatorOwned = false;
                MemoryStats* memoryStats = nullptr;
                int refCount = 0;
            };

            static SL_MAP(VkDevice, DeviceAllocatorStatsRefCount*) s_MapDeviceAllocatorStatsRefCount;
        };
    }
}