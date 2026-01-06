// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "SLAssert.h"
#include "BufferPool.h"
#include "VulkanBuffer.h"
#include "LogLevel.h"
#include "Device.h"

namespace SilverLining
{
namespace Vulkan
{
BufferPool::BufferPool(Device* _device, const std::string& _name)
    : device(_device)
    , name(_name)
{

}

BufferPool::~BufferPool()
{
    for (auto& keyVal : pool) {
        delete keyVal.second;
    }
    pool.clear();
}

Buffer* BufferPool::GetFromPool(uint64_t sizeInBytes)
{
    auto it = pool.lower_bound(sizeInBytes);
    if (it != pool.end()) {
        Buffer* buffer = it->second;
        SL_ASSERT(buffer->SizeInBytes() >= sizeInBytes);
        if (buffer->SizeInBytes() < sizeInBytes) {
            device->log(LogLevel::SL_LOG_LEVEL_WARN) << "buffer->SizeInBytes() < sizeInBytes!!!!" << std::endl;
        }
        pool.erase(it);

        device->log(LogLevel::SL_LOG_LEVEL_INFO) << "took from "<<name<<", size: " << GetSize() << std::endl;

        return buffer;
    }
    return nullptr;
}

int BufferPool::GetSize(void) const
{
    return (int)pool.size();
}

void BufferPool::Return(Buffer* buffer)
{
    if (buffer == nullptr) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "buffer == nullptr" << std::endl;
        return;
    }

    pool.insert({ buffer->SizeInBytes(), buffer});

    device->log(LogLevel::SL_LOG_LEVEL_INFO) << "returned to "<<name<<", size: " << GetSize() << std::endl;
}

void BufferPool::GetFromPool(SL_VECTOR(Buffer*)& buffers, uint64_t sizeInBytes, int numBuffers)
{
    buffers.clear();
    for (int i = 0; i < numBuffers; ++i) {
        Buffer* buffer = GetFromPool(sizeInBytes);
        if (buffer) {
            buffers.push_back(buffer);
        }
    }
    if (buffers.size() != numBuffers) {
        for (auto buffer : buffers) {
            Return(buffer);
        }
        buffers.clear();
    }
}
}
}

