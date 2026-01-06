// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include <vulkan/vulkan.h>

namespace SilverLining
{
    namespace Vulkan
    {
        class VulkanMemoryAllocator
        {
        public:
            VulkanMemoryAllocator() {}
            virtual ~VulkanMemoryAllocator() {}
        public:
            virtual VkBuffer CreateBuffer(VkBufferUsageFlags _usageFlags, VkMemoryPropertyFlags _memoryPropertyFlags, VkDeviceSize _sizeInBytes) = 0;
            virtual void DestroyBuffer(VkBuffer buffer) = 0;

            virtual VkResult Map(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize sizeInBytes, void*& mappedPtr) = 0;
            virtual void  UnMap(VkBuffer buffer) = 0;

            virtual VkResult Flush(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize sizeInBytes) = 0;

            virtual VkImage CreateImage(const VkImageCreateInfo& imageCreateInfo, VkMemoryPropertyFlags memoryPropertyFlags) = 0;
            virtual void DestroyImage(VkImage image) = 0;
            virtual VkDeviceSize SizeInBytes(VkImage image) = 0;
        protected:
        };
    }
}
