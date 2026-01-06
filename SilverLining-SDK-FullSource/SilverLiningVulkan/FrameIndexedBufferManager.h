// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "BufferProperties.h"
#include <cstdint>

namespace SilverLining
{
    namespace Vulkan
    {
        class Device;
        class BufferPool;
        class Buffer;
        class FrameIndexedBuffer;

        class FrameIndexedBufferManager
        {
        public:
            FrameIndexedBufferManager(uint32_t _numBufferedFrames, Device* _device);
            virtual ~FrameIndexedBufferManager();
        public:
            Buffer* GetAVertexBuffer(int numVertices, const std::string& name, const BufferProperties& bufferProperties);
            void ReturnAVertexBuffer(Buffer* vertexBuffer);

            void ResetOffsetIndex(void);
        private:
            const uint32_t numBufferedFrames = 3;
            Device* device = nullptr;
            BufferPool* vertexBuffersPoolDynamic = nullptr;

            SL_SET(FrameIndexedBuffer*) offsetableVertexBuffers;
        };
    }
}