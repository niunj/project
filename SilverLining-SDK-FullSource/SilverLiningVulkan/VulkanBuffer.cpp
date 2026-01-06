// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "VulkanBuffer.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "VkExtensions.h"
#include "VulkanDebug.h"
#include "VulkanInitializers.h"
#include "SLAssert.h"
#include "VulkanMemoryAllocator.h"
#include "MemoryStats.h"

#include <sstream>

namespace SilverLining
{
namespace Vulkan
{
std::atomic<int> RawBuffer::s_totalCount{ 0 };

static VkBufferUsageFlags getBufferUsageFlags(BufferType bufferType, VkBufferUsageFlags additionalFlags)
{
    VkBufferUsageFlags usageFlags = 0;
    switch (bufferType) {
    case BT_STAGING:
        usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        break;
    case BT_VERTEX_BUFFER:
        usageFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    case BT_INDEX_BUFFER:
        usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    case BT_UBO:
        usageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    case BT_SSBO:
        usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    case BT_INDIRECT_BUFFER:
        usageFlags = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    }

    usageFlags |= additionalFlags;

    return usageFlags;
}

VkResult RawBuffer::Map(VkDeviceSize offset, VkDeviceSize size)
{
    return device->GetVulkanMemoryAllocator()->Map(buffer, offset, size, mapped);
}

void RawBuffer::UnMap()
{
    if (mapped) {
        device->GetVulkanMemoryAllocator()->UnMap(buffer);
        mapped = nullptr;
    }
}

void* RawBuffer::MappedPtr(void)
{
    return mapped;
}

VkResult RawBuffer::Flush(VkDeviceSize offset, VkDeviceSize size)
{
    return device->GetVulkanMemoryAllocator()->Flush(buffer, offset, size);
}

uint64_t RawBuffer::DeviceAddress() const
{
    VkBufferDeviceAddressInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer;
    return device->Extensions().vkGetBufferDeviceAddressKHR(device->VulkanDevice(), &info);
}

RawBuffer::RawBuffer(Device* _device, VkBufferUsageFlags _usageFlags, VkMemoryPropertyFlags _memoryPropertyFlags, VkDeviceSize _sizeInBytes, const std::string& incomingName)
    : buffer(VK_NULL_HANDLE)
    , deviceMemory(0)
    , device(_device)
    , sizeInBytes(_sizeInBytes)
    , usageFlags(_usageFlags)
{
    if (incomingName.empty()) {
        std::stringstream ss;
        ss << "VulkanBuffer[" << s_totalCount << "]";
        name = ss.str();
    } else {
        name = incomingName;
    }

    buffer = device->GetVulkanMemoryAllocator()->CreateBuffer(_usageFlags, _memoryPropertyFlags, _sizeInBytes);

    debugmarker::SetName(device, buffer, name.c_str());

    ++s_totalCount;
}

RawBuffer::~RawBuffer()
{
    device->GetVulkanMemoryAllocator()->DestroyBuffer(buffer);
    --s_totalCount;
}

VkBuffer RawBuffer::VulkanBuffer(void) const
{
    return buffer;
}

uint64_t RawBuffer::SizeInBytes(void) const
{
    return sizeInBytes;
}

const std::string& RawBuffer::Name(void) const
{
    return name;
}

VkBufferUsageFlags RawBuffer::UsageFlags(void) const
{
    return usageFlags;
}

Buffer::Buffer(Device* device, BufferType bufferType, uint64_t _sizeInBytes, const BufferProperties& _bufferProperties, VkBufferUsageFlags additionalFlags, const std::string& name)
    : device(device)
    , stagingBuffer(nullptr)
    , buffer(nullptr)
    , bufferProperties(_bufferProperties)
{
    if (!bufferProperties.dynamic) {
        const VkMemoryPropertyFlags memoryPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        stagingBuffer = new RawBuffer(device, getBufferUsageFlags(BT_STAGING, additionalFlags), memoryPropertyFlags, _sizeInBytes, name);
    }

    VkMemoryPropertyFlags memoryPropertyFlags = 0;
    if (bufferProperties.dynamic) {
        memoryPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    } else {
        memoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    buffer = new RawBuffer(device, getBufferUsageFlags(bufferType, additionalFlags), memoryPropertyFlags, _sizeInBytes, name);

    if (bufferProperties.dynamic) {
        buffer->Map(0, VK_WHOLE_SIZE);
    }

    if (!name.empty()) {
        //         vks::debugmarker::setBufferName(_device->vulkanDevice(), _buffer->vulkanBuffer(), name.c_str());
    }
}

void* Buffer::MappedStagingBuffer(void)
{
    SL_ASSERT(stagingBuffer);

    stagingBuffer->Map(0, stagingBuffer->SizeInBytes());
    void* data = stagingBuffer->MappedPtr();
    return data;
}

VkBuffer Buffer::VulkanBuffer(void) const
{
    return buffer->VulkanBuffer();
}

bool Buffer::SyncToGpu(VkDeviceSize offset, VkDeviceSize sizeInBytes, bool destroyStaging)
{
    SL_ASSERT(stagingBuffer);

    stagingBuffer->UnMap();

    if (buffer) {
        VkCommandBuffer copyCmd = device->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
        VkBufferCopy bufferCopy = { offset, offset, sizeInBytes };

        vkCmdCopyBuffer(copyCmd, stagingBuffer->VulkanBuffer(), buffer->VulkanBuffer(), 1, &bufferCopy);

        device->FlushCommandBuffer(copyCmd);
    }

    if (destroyStaging) {
        delete stagingBuffer;
        stagingBuffer = nullptr;
    }

    return true;
}

bool Buffer::SyncToGpu(bool destroyStaging)
{
    return SyncToGpu(0, stagingBuffer->SizeInBytes(), destroyStaging);
}

Buffer::~Buffer()
{
    if (stagingBuffer) {
        delete stagingBuffer;
        stagingBuffer = nullptr;
    }
    if (buffer) {
        if (bufferProperties.dynamic) {
            buffer->UnMap();
        }
        delete buffer;
    }
}

uint64_t Buffer::SizeInBytes(void) const
{
    return buffer->SizeInBytes();
}

uint64_t Buffer::BufferAddress() const
{
    return buffer->DeviceAddress();
}

RawBuffer* Buffer::GetBuffer(void)
{
    return buffer;
}

bool Buffer::Dynamic(void) const
{
    return bufferProperties.dynamic;
}
Device* Buffer::GetDevice(void)
{
    return device;
}

void  Buffer::SetUserData(void* _userData)
{
    userData = _userData;
}

void* Buffer::GetUserData(void)
{
    return userData;
}

}
}
