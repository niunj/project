// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "SLVector.h"
#include "SilverLiningTypes.h"
#include <cstdint>
#include <map>

namespace SilverLining
{
    namespace Vulkan
    {
        class Device;
        class Buffer;

        class BufferPool
        {
        public:
            BufferPool(Device* _device, const std::string& _name);
            virtual ~BufferPool();
        public:
            Buffer* GetFromPool(uint64_t sizeInBytes);
            void GetFromPool(SL_VECTOR(Buffer*)& buffers, uint64_t sizeInBytes, int numBuffers);
            void Return(Buffer* buffer);
            int GetSize(void) const;
        private:
            Device* device = nullptr;

            std::multimap<uint64_t, Buffer*> pool;

            const std::string name;
        };
    }
}
