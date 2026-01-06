// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "Buffer.h"
#include <vector>

namespace SilverLining
{
    namespace Vulkan
    {
        class RawBuffer;
        class VulkanContext;
    }

    // This is the equivalent of BufferGl for Vulkan
    // It allows to directly access a ubo/ssbo abstractly on the sl side (e.g. for particles buffer, etc.)
    class BufferVk : public SilverLining::Buffer
    {
    public:
        BufferVk(const char* name, SilverLining::BufferType type, int size, const BufferProperties& _bufferProperties, SilverLining::Vulkan::VulkanContext* _context);
        virtual ~BufferVk();
    public:
        virtual void* Lock(int offset, int sizeInBytes, void*) override;
        virtual void  Unlock(int offset, int sizeInBytes, void*) override;

        virtual void  Bind(int location, void*);
        virtual void  UnBind(int location, void*);

        virtual void DeInitBuffers(void*) { SL_ASSERT(false); };
        virtual void Respec(int sizeInBytes, void*) { SL_ASSERT(false); };
        virtual void SyncToGpu(int offset, int sizeInBytes, void* pSrcData, void*) { SL_ASSERT(false); };

    private:
        SilverLining::Vulkan::VulkanContext* context = nullptr;
        SL_VECTOR(SilverLining::Vulkan::RawBuffer*) buffers;
    };
}
