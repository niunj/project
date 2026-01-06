// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include <vulkan/vulkan.h>

#include "MemAlloc.h"
#include "VulkanMemoryAllocator.h"

#include <mutex>

namespace SilverLining
{
   namespace Vulkan
   {
    VK_DEFINE_HANDLE(VmaAllocator)

    // Forward declaration
    VK_DEFINE_HANDLE(VmaAllocation)
    struct VmaAllocationInfo;


    class Device;

    class DefaultAllocator : public VulkanMemoryAllocator
    {
    public:
        DefaultAllocator(Device* _device);
        virtual ~DefaultAllocator();
    public:
        VkBuffer CreateBuffer(VkBufferUsageFlags _usageFlags, VkMemoryPropertyFlags _memoryPropertyFlags, VkDeviceSize _sizeInBytes) override;
        void DestroyBuffer(VkBuffer buffer) override;

        VkResult Map(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize sizeInBytes, void*& mappedPtr) override;
        void  UnMap(VkBuffer buffer) override;

        VkResult Flush(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize sizeInBytes) override;

        VkImage CreateImage(const VkImageCreateInfo& imageCreateInfo, VkMemoryPropertyFlags memoryPropertyFlags) override;
        void DestroyImage(VkImage image) override;
        VkDeviceSize SizeInBytes(VkImage image) override;

        VkResult Map(VkImage image, VkDeviceSize offset, VkDeviceSize sizeInBytes, void*& mappedPtr);
        void  UnMap(VkImage image);
    private:
        //! when an own allocator allocates/deallocates memory
        static void VmaAllocateMemory(VmaAllocator allocator, uint32_t memoryType, VkDeviceMemory memory, VkDeviceSize size, void* pUserData);
        static void VmaFreeMemory(VmaAllocator allocator, uint32_t memoryType, VkDeviceMemory memory, VkDeviceSize size, void* pUserData);
    private:
        VmaAllocator CreateAllocator(void);
    private:
        Device* device = VK_NULL_HANDLE;
        VmaAllocator vmaAllocator = VK_NULL_HANDLE;

        struct BufferVmaProperties
        {
            VmaAllocation vmaAllocation = VK_NULL_HANDLE;
            VmaAllocationInfo* vmaAllocationInfo = nullptr;
        };

        std::mutex mutex;

        SL_MAP(VkBuffer, BufferVmaProperties) mapBuffersToProps;

        struct ImageProperties
        {
            VmaAllocation vmaAllocation = VK_NULL_HANDLE;
            VmaAllocationInfo* vmaAllocationInfo = nullptr;
        };

        SL_MAP(VkImage, ImageProperties) mapImagesToProps;
    };
}
}