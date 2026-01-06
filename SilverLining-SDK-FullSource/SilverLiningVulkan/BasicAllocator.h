// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "VulkanMemoryAllocator.h"
#include "MemAlloc.h"

namespace SilverLining
{
    namespace Vulkan
    {
        class Device;
        class BasicAllocator : public VulkanMemoryAllocator
        {
        public:
            BasicAllocator(Device* _device);
            virtual ~BasicAllocator();

        public:
            virtual VkBuffer CreateBuffer(VkBufferUsageFlags _usageFlags, VkMemoryPropertyFlags _memoryPropertyFlags, VkDeviceSize _sizeInBytes) override;
            virtual void DestroyBuffer(VkBuffer buffer) override;
            virtual VkResult Map(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize sizeInBytes, void*& mappedPtr) override;
            virtual void  UnMap(VkBuffer buffer) override;
            virtual VkResult Flush(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize sizeInBytes) override;

            virtual VkImage CreateImage(const VkImageCreateInfo& imageCreateInfo, VkMemoryPropertyFlags memoryPropertyFlags) override;
            virtual void DestroyImage(VkImage image) override;
            virtual VkDeviceSize SizeInBytes(VkImage image) override;
        private:
            Device* device = nullptr;

            struct BufferProperties
            {
                VkDeviceMemory deviceMemory = VK_NULL_HANDLE;
                VkDeviceSize sizeInBytes = 0;
                uint32_t memoryTypeIndex = 0;
            };

            SL_MAP(VkBuffer, BufferProperties) mapBuffersToProps;

            struct ImageProperties
            {
                VkDeviceMemory deviceMemory = VK_NULL_HANDLE;
                VkDeviceSize sizeInBytes = 0;
            };

            SL_MAP(VkImage, ImageProperties) mapImagesToProps;
        };
    }
}