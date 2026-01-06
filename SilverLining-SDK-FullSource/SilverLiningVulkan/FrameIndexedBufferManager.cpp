// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "SLAssert.h"
#include "FrameIndexedBufferManager.h"
#include "BufferPool.h"
#include "VulkanBuffer.h"
#include "Vertex.h"
#include "FrameIndexedBuffer.h"
#include "Device.h"

namespace SilverLining
{
namespace Vulkan
{
FrameIndexedBufferManager::FrameIndexedBufferManager(uint32_t _numBufferedFrames, Device* _device)
    : numBufferedFrames(_numBufferedFrames)
    , device(_device)
{
    vertexBuffersPoolDynamic = new BufferPool(device, "vertexBuffersPoolDynamic");
}

FrameIndexedBufferManager::~FrameIndexedBufferManager()
{
    delete vertexBuffersPoolDynamic;
}

Buffer* FrameIndexedBufferManager::GetAVertexBuffer(int numVertices, const std::string& name, const BufferProperties& bufferProperties)
{
    SL_ASSERT(bufferProperties.frameIndexed && bufferProperties.numOfPossibleOffsets >= 1 && bufferProperties.dynamic);

    const uint64_t sizeInBytes = numVertices * sizeof(Vertex) * bufferProperties.numOfPossibleOffsets;

    SL_VECTOR(Buffer*) buffers;
    vertexBuffersPoolDynamic->GetFromPool(buffers, sizeInBytes, numBufferedFrames);
    if (buffers.size() == 0) {
        for (uint32_t i = 0; i < numBufferedFrames; ++i) {
            VkBufferUsageFlags additionalFlags = 0;
            auto buffer = new SilverLining::Vulkan::Buffer(device, BT_VERTEX_BUFFER, sizeInBytes, bufferProperties, additionalFlags, name);
            if (buffer) {
                buffers.push_back(buffer);
            }
        }
    }
    if (buffers.size() != numBufferedFrames) {
        for (auto buffer : buffers) {
            delete buffer;
        }
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "Could not allocate buffers!" << std::endl;
        return nullptr;
    }

    const uint64_t offsetSizeInBytes = (bufferProperties.numOfPossibleOffsets > 1) ? numVertices * sizeof(Vertex) : 0;
    FrameIndexedBuffer* frameIndexedBuffer = new FrameIndexedBuffer(buffers, offsetSizeInBytes);
    for (auto buffer : buffers) {
        buffer->SetUserData(frameIndexedBuffer);
    }
    if (bufferProperties.numOfPossibleOffsets > 1) {
        offsetableVertexBuffers.insert(frameIndexedBuffer);
    }

    return buffers[0];
}

void FrameIndexedBufferManager::ReturnAVertexBuffer(Buffer* vertexBuffer)
{
    SL_ASSERT(vertexBuffer->GetUserData());
    FrameIndexedBuffer* frameIndexedBuffer = static_cast<FrameIndexedBuffer*>(vertexBuffer->GetUserData());
    SL_ASSERT(frameIndexedBuffer);

    auto& buffers = frameIndexedBuffer->GetBuffers();
    for (auto buffer : buffers) {
        buffer->SetUserData(nullptr);
        vertexBuffersPoolDynamic->Return(buffer);
    }
    if (frameIndexedBuffer->OffsetSizeInBytes() > 0) {
        SL_ASSERT(offsetableVertexBuffers.find(frameIndexedBuffer) != offsetableVertexBuffers.end());
        offsetableVertexBuffers.erase(frameIndexedBuffer);
    }
    delete frameIndexedBuffer;
}

void FrameIndexedBufferManager::ResetOffsetIndex(void)
{
    for (auto offsetableBuffer : offsetableVertexBuffers) {
        offsetableBuffer->ResetOffsetIndex();
    }
}
}
}