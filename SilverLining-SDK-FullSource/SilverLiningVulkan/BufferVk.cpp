// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "SLAssert.h"
#include "BufferVk.h"
#include "VulkanBuffer.h"
#include "VulkanContext.h"

namespace SilverLining
{
BufferVk::BufferVk(const char* _name, SilverLining::BufferType type, int sizeInBytes, const BufferProperties& _bufferProperties
                   , SilverLining::Vulkan::VulkanContext* _context)
    : SilverLining::Buffer(_name, type, sizeInBytes, _bufferProperties)
    , context(_context)
{
    std::string name = (_name) ? _name : "";

    SilverLining::Vulkan::BufferType slvkBufferType;
    if (type == SHADER_STORAGE_BUFFER) {
        slvkBufferType = SilverLining::Vulkan::BT_SSBO;
    } else if (type == UNIFORM_BUFFER) {
        slvkBufferType = SilverLining::Vulkan::BT_UBO;
    } else {
        std::cout << __FILE__ << ": uknown buffer type" << std::endl;
    }

    int numBuffers = bufferProperties.frameIndexed ? context->GetInfo().numBufferedFrames : 1;
    for (int i = 0; i < numBuffers; ++i) {
        auto buffer = context->CreateBuffer(slvkBufferType, sizeInBytes, name);
        buffer->Map(0, sizeInBytes);
        memset(buffer->MappedPtr(), 0, sizeInBytes);
        buffers.push_back(buffer);
    }
}
BufferVk::~BufferVk()
{
    for (auto buffer : buffers) {
        buffer->UnMap();
        context->DestroyBuffer(buffer);
    }
}

void* BufferVk::Lock(int offset, int sizeInBytes, void*)
{
    int bufferIndex = bufferProperties.frameIndexed ? context->GetCurrentFrameIndex() : 0;
    SL_ASSERT(bufferIndex < buffers.size());
    auto& buffer = buffers[bufferIndex];

    //std::cout << "buffer index: " << bufferIndex << std::endl;

    return buffer->MappedPtr();
}
void  BufferVk::Unlock(int offset, int sizeInBytes, void*)
{
    // no op
}

void  BufferVk::Bind(int location, void*)
{
    int bufferIndex = bufferProperties.frameIndexed ? context->GetCurrentFrameIndex() : 0;
    SL_ASSERT(bufferIndex < buffers.size());
    auto& buffer = buffers[bufferIndex];

    context->Bind(buffer, location, 0);
}
void BufferVk::UnBind(int location, void*)
{
    // no op
}
}

