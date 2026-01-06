// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "Device.h"
#include "SLAssert.h"

#include "MemoryStats.h"
#include "VulkanBuffer.h"
#include "Image.h"

#include <sstream>
#include <algorithm>

namespace SilverLining
{
namespace Vulkan
{
MemoryStats::MemoryStats()
{

}

MemoryStats::~MemoryStats()
{

}

void MemoryStats::Created(VkBuffer buffer, VkDeviceSize sizeInBytes)
{
    if (buffer) {
        buffers.insert({ buffer, sizeInBytes });
    }
}

void MemoryStats::AboutToBeDestroyed(VkBuffer buffer)
{
    if (buffer) {
        buffers.erase(buffer);
    }
}

int MemoryStats::NumBuffers(void)
{
    return (int)buffers.size();
}

uint64_t MemoryStats::TotalSizeInBytesBuffers(void) const
{
    uint64_t totalSize = 0;
    for (auto& keyVal : buffers) {
        totalSize += keyVal.second;
    }
    return totalSize;
}

void MemoryStats::Created(VkImage image, VkDeviceSize sizeInBytes)
{
    images.insert({ image, sizeInBytes });
}
void MemoryStats::AboutToBeDestroyed(VkImage image)
{
    images.erase(image);
}

int MemoryStats::NumImages(void)
{
    return (int)images.size();
}

uint64_t MemoryStats::TotalSizeInBytesImages(void) const
{
    uint64_t totalSize = 0;
    for (auto& keyVal : images) {
        totalSize += keyVal.second;
    }
    return totalSize;
}

void MemoryStats::MemoryAllocated()
{
    ++numMemoryAllocations;
    if (numMemoryAllocations > numMemoryAllocationsMaxEver) {
        ++numMemoryAllocationsMaxEver;
    }
}

void MemoryStats::MemoryDeAllocated()
{
    --numMemoryAllocations;
}

int MemoryStats::NumMemoryAllocations() const
{
    return numMemoryAllocations;
}

int MemoryStats::NumMemoryAllocationsMaxEver() const
{
    return numMemoryAllocationsMaxEver;
}

std::string formatBytes(uint64_t sizeInBytes)
{
    std::stringstream ss;
    if (sizeInBytes < 1000) {
        ss << sizeInBytes << " bytes";
    } else if (sizeInBytes > 1000 && sizeInBytes < 1000000) {
        float kb = (float)sizeInBytes / 1000.0f;
        ss << kb << " KB";
    } else if (sizeInBytes > 1000000) {
        float mb = (float)sizeInBytes / 1000000.0f;
        ss << mb << " MB";
    }
    return ss.str();
}

void MemoryStats::PrintStats(void) const
{
    std::cout << "Buffers: " << buffers.size()<< ", size: " << formatBytes(TotalSizeInBytesBuffers()) << std::endl;
    std::cout << "Images : " << images.size() << ", size: " << formatBytes(TotalSizeInBytesImages()) << std::endl;
    std::cout << "Total: " << formatBytes(TotalSizeInBytesBuffers() + TotalSizeInBytesImages()) << std::endl;
    std::cout << "Total allocations: " << NumMemoryAllocations() << std::endl;
}

void MemoryStats::PrintBuffersSorted(void) const
{
#if 0
    SL_VECTOR(RawBuffer*) vecBuffers;
    for (const auto& buffer : buffers) {
        vecBuffers.push_back(buffer);
    }

    std::sort(vecBuffers.begin(), vecBuffers.end(), [](const RawBuffer* lhs, const RawBuffer* rhs) -> bool {
        return lhs->SizeInBytes() > rhs->SizeInBytes();
    });

    for (auto buffer : vecBuffers) {
        std::cout << buffer->Name() << ", size: " << formatBytes(buffer->SizeInBytes()) << std::endl;
    }
#endif
}
}
}