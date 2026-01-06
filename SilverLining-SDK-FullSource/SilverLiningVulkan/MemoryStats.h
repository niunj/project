// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "MemAlloc.h"
#include <string>

namespace SilverLining
{
    namespace Vulkan
    {
        class RawBuffer;
        class Image;

        class MemoryStats
        {
        public:
            MemoryStats();
            virtual ~MemoryStats();
        public:
            //! buffers
            void Created(VkBuffer buffer, VkDeviceSize sizeInBytes);
            void AboutToBeDestroyed(VkBuffer buffer);

            int NumBuffers(void);
            uint64_t TotalSizeInBytesBuffers(void) const;

            //! images
            void Created(VkImage image, VkDeviceSize sizeInBytes);
            void AboutToBeDestroyed(VkImage image);

            int NumImages(void);
            uint64_t TotalSizeInBytesImages(void) const;

            //! memory allocations
            void MemoryAllocated();
            void MemoryDeAllocated();
            int NumMemoryAllocations() const;
            int NumMemoryAllocationsMaxEver() const;

            void PrintStats(void) const;
            void PrintBuffersSorted(void) const;
        private:
            SL_MAP(VkBuffer, VkDeviceSize) buffers;
            SL_MAP(VkImage, VkDeviceSize) images;

            int numMemoryAllocations = 0;
            int numMemoryAllocationsMaxEver = 0;
        };
    }
}