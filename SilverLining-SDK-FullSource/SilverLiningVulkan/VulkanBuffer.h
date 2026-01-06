// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "BufferProperties.h"

#include <vulkan/vulkan.h>

#include <string>
#include <atomic>


namespace SilverLining
{
    namespace Vulkan
    {
        class Device;

        enum BufferType
        {
            BT_STAGING = 0,
            BT_VERTEX_BUFFER,
            BT_INDEX_BUFFER,
            BT_UBO,
            BT_SSBO,
            BT_INDIRECT_BUFFER
        };

        //! Per this: https://zeux.io/2020/02/27/writing-an-efficient-vulkan-renderer/
        //! 
        //! UBOs, SSBOs & dynamic vertex/index buffers are best represented using: VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT 
        //! But this memory is at a premium (It ~ 224 Mb on my 3080 Ti)
        //! 
        //! So, we will use the next best type: VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        //! for our UBOs and SSBOs
        //! 
        //! We will also use VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT for staging buffers
        //! 
        //! We will use VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT for our static vertex/index buffers and a staging buffer to transfer data to them

        //! Encapsulates a vulkan buffer and its memory
        class RawBuffer
        {
        public:
            //! constructor
            RawBuffer(Device* device, VkBufferUsageFlags _usageFlags, VkMemoryPropertyFlags _memoryPropertyFlags, VkDeviceSize _sizeInBytes, const std::string& name);
            //! destructor
            virtual ~RawBuffer();
        public:
            //! the vulkan buffer
            VkBuffer VulkanBuffer(void) const;

            //! map
            VkResult Map(VkDeviceSize offset, VkDeviceSize size);

            //! unmap
            void UnMap();

            //! flush
            VkResult Flush(VkDeviceSize offset, VkDeviceSize size);

            //! device address
            uint64_t DeviceAddress() const;

            //! size in bytes
            uint64_t SizeInBytes(void) const;

            //! Pointer to mapped data, if mapped else nullptr
            void* MappedPtr(void);

            const std::string& Name(void) const;

            VkBufferUsageFlags UsageFlags(void) const;
        protected:
            static std::atomic<int> s_totalCount;
        protected:
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory deviceMemory = VK_NULL_HANDLE;

            Device* device = nullptr;

            void* mapped = nullptr;

            const VkDeviceSize sizeInBytes = 0;

            const VkBufferUsageFlags usageFlags = 0;

            std::string name;
        };

        //! A Buffer that is backed up with a staging buffer.
        //! Both the buffer and the staging buffer are internally represented by RawBuffer.
        class Buffer
        {
        public:
            //! constructor
            //! staging means create a staging buffer
            //! stagingOnly means create only the staging buffer
            Buffer(Device* device, BufferType bufferType, uint64_t sizeInBytes, const BufferProperties& _bufferProperties, VkBufferUsageFlags additionalFlags, const std::string& name);

            //! destructor
            virtual ~Buffer(void);

        public:
            //! The pointer to the mapped staging buffer
            void* MappedStagingBuffer(void);

            //! update the actual buffer from the staging buffer
            //! destroyStaging is whether to delete the staging buffer after the 
            //! update has happened
            bool SyncToGpu(bool destroyStaging);

            //! same as above, but takes an offset ad size
            bool SyncToGpu(VkDeviceSize offset, VkDeviceSize size, bool destroyStaging);

            //! access the internal vulkan buffer
            VkBuffer VulkanBuffer(void) const;

            //! size in bytes
            uint64_t SizeInBytes(void) const;

            //! buffer address
            uint64_t BufferAddress() const;

            //! The raw buffer corresponding to the buffer
            RawBuffer* GetBuffer(void);

            //! Whether the actual buffer was constructed as mappable
            bool Dynamic(void) const;

            Device* GetDevice(void);

            void  SetUserData(void* _userData);
            void* GetUserData(void);
        protected:
            RawBuffer* stagingBuffer = nullptr;
            RawBuffer* buffer = nullptr;

            Device* device = nullptr;

            BufferProperties bufferProperties;

            void* userData = nullptr;
        };
    }
}