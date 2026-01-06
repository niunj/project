// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "BufferProperties.h"
#include <cstdint>
#include <vector>

namespace SilverLining
{
    namespace Vulkan
    {
        class Device;
        class Buffer;

        // A frame indexed buffer holds imageCount number of buffers
        // It is offsetable or not offsetable
        class FrameIndexedBuffer
        {
        public:
            FrameIndexedBuffer(const SL_VECTOR(Buffer*)& _buffers, uint64_t _offsetSizeInBytes);
            virtual ~FrameIndexedBuffer();

        public:
            //! Similar to mapped ubos/ssbos that can be changed/bound at a given offset.
            //! buffer size must be a multiple of offset size.
            uint64_t OffsetSizeInBytes(void) const;

            void IncrementOffsetIndex();
            void ResetOffsetIndex(void);
            uint64_t CurrentOffsetInBytes(void) const;
            uint64_t CurrentOffset(void) const;

            SL_VECTOR(Buffer*)& GetBuffers(void);
        private:
            SL_VECTOR(Buffer*) buffers;

            //! Similar to mapped ubos/ssbos that can be changed/bound at a given offset.
            //! buffer size must be a multiple of offset size.
            const uint64_t offsetSizeInBytes = 0;
            int offsetIndex = -1;
        };
    }
}
