// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "Image.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "VulkanBuffer.h"
#include "VulkanInitializers.h"
#include "VulkanDebug.h"
#include "ImageTransitions.h"
#include "MemoryStats.h"
#include "VulkanMemoryAllocator.h"

#include "SLAssert.h"

#include <algorithm>
#include <fstream>

namespace SilverLining
{
namespace Vulkan
{
VkSampleCountFlagBits Image::ToSampleCountFlagBits(int sampleCount)
{
    if (sampleCount == 1) return VK_SAMPLE_COUNT_1_BIT;
    if (sampleCount == 2) return VK_SAMPLE_COUNT_2_BIT;
    if (sampleCount == 4) return VK_SAMPLE_COUNT_4_BIT;
    if (sampleCount == 8) return VK_SAMPLE_COUNT_8_BIT;
    if (sampleCount == 16) return VK_SAMPLE_COUNT_16_BIT;

    return VK_SAMPLE_COUNT_1_BIT;
}

Image::Image(Device* _device)
    : device(_device)
{

}

Image::~Image()
{
    device->GetVulkanMemoryAllocator()->DestroyImage(image);
}

void Image::AllocateImageAndMemory(VkImageUsageFlags usageFlags
                                   , VkMemoryPropertyFlags memoryPropertyFlags
                                   , VkImageTiling imageTiling
                                   , int arrayLayers, int sampleCount)
{
    VkImageCreateInfo imageCreateInfo = vkInitializers::imageCreateInfo();
    imageCreateInfo.imageType = (depth==1)? VK_IMAGE_TYPE_2D : VK_IMAGE_TYPE_3D;
    imageCreateInfo.format = format;
    imageCreateInfo.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), static_cast<uint32_t>(depth) };
    imageCreateInfo.mipLevels = numMipMapLevels;
    imageCreateInfo.arrayLayers = arrayLayers;
    imageCreateInfo.samples = ToSampleCountFlagBits(sampleCount);
    imageCreateInfo.tiling = imageTiling;
    imageCreateInfo.usage = usageFlags;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (arrayLayers == 6) {
        imageCreateInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    image = device->GetVulkanMemoryAllocator()->CreateImage(imageCreateInfo, memoryPropertyFlags);

}

bool Image::CopyFromRawDataIntoImage(void* pSrcData, VkDeviceSize pSrcDataSize, const SL_VECTOR(int)& mipMapDataOffsetsAllFaces, uint32_t numFaces)
{
    const VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    const VkMemoryPropertyFlags memoryPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const VkDeviceSize sizeInBytes = pSrcDataSize;
    RawBuffer* stagingBuffer = new RawBuffer(device
            , usageFlags
            , memoryPropertyFlags
            , sizeInBytes, "");

    stagingBuffer->Map(0, pSrcDataSize);
    uint8_t* pDstData = (uint8_t*)stagingBuffer->MappedPtr();
    memcpy(pDstData, pSrcData, pSrcDataSize);
    stagingBuffer->UnMap();

    const bool generatingMipMaps = (mipMapDataOffsetsAllFaces.size() / numFaces != numMipMapLevels);

    VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (generatingMipMaps) {
        imageUsageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    AllocateImageAndMemory(imageUsageFlags
                           , VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT // on the gpu
                           , VK_IMAGE_TILING_OPTIMAL, numFaces, 1);

    SL_VECTOR(VkBufferImageCopy) bufferCopyRegions;

    const uint32_t numMipMaps = (uint32_t)mipMapDataOffsetsAllFaces.size() / numFaces;

    for (uint32_t face = 0; face < numFaces; ++face) {
        for (uint32_t mipLevel = 0; mipLevel < numMipMaps; ++mipLevel) {
            VkBufferImageCopy bufferImageCopy = {};

            const uint32_t offsetIntoMipData = (numMipMaps * face) + mipLevel;
            bufferImageCopy.bufferOffset = mipMapDataOffsetsAllFaces[offsetIntoMipData];
            bufferImageCopy.bufferRowLength = 0;
            bufferImageCopy.bufferImageHeight = 0;

            bufferImageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            bufferImageCopy.imageSubresource.mipLevel = mipLevel;
            bufferImageCopy.imageSubresource.baseArrayLayer = face;
            bufferImageCopy.imageSubresource.layerCount = 1;

            bufferImageCopy.imageOffset = { 0,0,0 };
            bufferImageCopy.imageExtent.width = width >> mipLevel;
            bufferImageCopy.imageExtent.height = height >> mipLevel;
            bufferImageCopy.imageExtent.depth = depth>>mipLevel;

            bufferCopyRegions.push_back(bufferImageCopy);
        };
    }

    VkCommandBuffer commandBuffer = device->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

    VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, numMipMaps, 0, numFaces };

    transitions::setImageLayout(commandBuffer, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);

    vkCmdCopyBufferToImage(commandBuffer
                           , stagingBuffer->VulkanBuffer()
                           , image
                           , VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                           , (uint32_t)bufferCopyRegions.size()
                           , bufferCopyRegions.data()
                          );

    VkImageLayout newImageLayout = (generatingMipMaps) ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                   : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    transitions::setImageLayout(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, newImageLayout, subresourceRange);

    device->FlushCommandBuffer(commandBuffer);

    delete stagingBuffer;

    isCubeMap = (numFaces == 6);

    return true;
}

VkFormat toVulkanFormat(uint32_t glInternalFormat, bool srgb)
{
    // This is GL_RGBA8
    if (glInternalFormat == 32856) {
        if (srgb) {
            return VK_FORMAT_R8G8B8A8_SRGB;
        } else {
            return VK_FORMAT_R8G8B8A8_UNORM;
        }
    }

    // This is GL_RGBA16F_ARB
    if (glInternalFormat == 34842) {
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }

    std::cout << __FUNCTION__ << ": unknown format!" << std::endl;
    return VK_FORMAT_UNDEFINED;
}

bool Image::CopyFromFileIntoImage(const std::string& fileName, bool srgb, uint32_t numFaces)
{
#if 0
    std::ifstream ifs(fileName.c_str());
    if (ifs.fail()) {
        return false;
    }

    ktxTexture* ktxTexture = nullptr;
    ktxResult result = ktxTexture_CreateFromNamedFile(fileName.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);

    SL_ASSERT(result == KTX_SUCCESS);
    if (result != KTX_SUCCESS) {
        return false;
    }

    ktx_uint8_t* ktxTextureData = ktxTexture_GetData(ktxTexture);
    ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);

    _numMipMapLevels = ktxTexture->numLevels;
    _width = ktxTexture->baseWidth;
    _height = ktxTexture->baseHeight;
    _format = toVulkanFormat(ktxTexture->glInternalformat, srgb);

    SL_VECTOR(int) dataOffsets;
    for (uint32_t face = 0; face < numFaces; ++face) {
        for (uint32_t i = 0; i < (uint32_t)_numMipMapLevels; ++i) {
            ktx_size_t offset = 0;
            KTX_error_code ret = ktxTexture_GetImageOffset(ktxTexture, i, 0, face, &offset);
            if (ret != KTX_SUCCESS) {
                std::cout << __FUNCTION__ << "ret != KTX_SUCCESS" << std::endl;
            }
            dataOffsets.push_back((int)offset);
        }
    }

    GEN_ASSERT(_numMipMapLevels == dataOffsets.size() / numFaces);
    if (_numMipMapLevels != dataOffsets.size() / numFaces) {
        std::cout << __FUNCTION__ << "numMipMapLevels != dataOffsets.size()/numFaces" << std::endl;
    }

    copyFromRawDataIntoImage(ktxTextureData, ktxTextureSize, dataOffsets, numFaces);

    ktxTexture_Destroy(ktxTexture);
#endif

    return true;
}

static void roundUptoOne(int32_t& val)
{
    if (val == 0) {
        val = 1;
    }
}

void Image::GenerateMipMaps(void)
{
    // Generate the mip chain (glTF uses jpg and png, so we need to create this manually)
    VkCommandBuffer commandBuffer = device->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

    for (uint32_t i = 1; i < (uint32_t)numMipMapLevels; i++) {
        VkImageBlit imageBlit{};

        // This is the previous level, which is the source for the next level
        imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBlit.srcSubresource.layerCount = 1;
        imageBlit.srcSubresource.mipLevel = i - 1;
        imageBlit.srcOffsets[1].x = int32_t(width >> (i - 1));
        imageBlit.srcOffsets[1].y = int32_t(height >> (i - 1));
        imageBlit.srcOffsets[1].z = 1;

        roundUptoOne(imageBlit.srcOffsets[1].x);
        roundUptoOne(imageBlit.srcOffsets[1].y);

        // This is the destination level
        imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBlit.dstSubresource.layerCount = 1;
        imageBlit.dstSubresource.mipLevel = i;
        imageBlit.dstOffsets[1].x = int32_t(width >> i);
        imageBlit.dstOffsets[1].y = int32_t(height >> i);
        imageBlit.dstOffsets[1].z = 1;

        roundUptoOne(imageBlit.dstOffsets[1].x);
        roundUptoOne(imageBlit.dstOffsets[1].y);

        VkImageSubresourceRange mipSubRange = {};
        mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        mipSubRange.baseMipLevel = i;
        mipSubRange.levelCount = 1;
        mipSubRange.layerCount = 1;

        // the next level is in undefined state, because only one level was filled, and it was set as VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        transitions::setImageLayout(commandBuffer, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipSubRange);

        // the source level is in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL state, because only one level was filled, and it was set as VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        vkCmdBlitImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit, VK_FILTER_LINEAR);

        // set this to be the source for the next blit
        transitions::setImageLayout(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mipSubRange);
    }

    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.levelCount = numMipMapLevels;
    subresourceRange.layerCount = 1;

    // transfer the whole image
    transitions::setImageLayout(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);

    device->FlushCommandBuffer(commandBuffer);
}

bool Image::LoadFromBuffer(void* buffer, VkDeviceSize bufferSize, VkFormat _format, int _width, int _height, const SL_VECTOR(int)& mipMapDataOffsets
                           , bool mipMap)
{
    return LoadFromBuffer(buffer, bufferSize, _format, _width, _height, 1, mipMapDataOffsets, mipMap);
}

bool Image::LoadFromBuffer(void* buffer, VkDeviceSize bufferSize, VkFormat _format, int _width, int _height, int _depth, const SL_VECTOR(int)& mipMapDataOffsets
                           , bool mipMap)
{
    SL_ASSERT(buffer);
    if (buffer == nullptr) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << __FUNCTION__ << "(buffer==nullptr)" << std::endl;
        return false;
    }
    if (mipMap) {
        numMipMapLevels = static_cast<uint32_t>(floor(log2(std::max(_width, std::max(_height, _depth)))) + 1.0);
    } else {
        numMipMapLevels = 1;
    }

    width = _width;
    height = _height;
    depth = _depth;
    format = _format;

    bool ok = CopyFromRawDataIntoImage(buffer, bufferSize, mipMapDataOffsets, 1);
    if (!ok) {
        return false;
    }

    if (numMipMapLevels != mipMapDataOffsets.size()) {
        //std::cout << "_numMipMapLevels != mipMapDataOffsets.size(), will generate mip maps" << std::endl;
        GenerateMipMaps();
    }

    return ok;
}

bool Image::LoadFromFile(const std::string& fileName, bool srgb)
{
    bool ok = CopyFromFileIntoImage(fileName, srgb, 1);
    if (!ok) {
        return false;
    }
    return ok;
}

bool Image::LoadFromFileCubeMap(const std::string& fileName)
{
    bool ok = CopyFromFileIntoImage(fileName, false, 6);
    if (!ok) {
        return false;
    }
    return ok;
}

int Image::NumMipMapLevels(void) const
{
    return numMipMapLevels;
}

int Image::Width(void) const
{
    return width;
}

int Image::Height(void) const
{
    return height;
}

int Image::Depth(void) const
{
    return depth;
}

bool Image::IsCubeMap(void) const
{
    return isCubeMap;
}

VkFormat Image::VulkanFormat(void) const
{
    return format;
}

VkImage Image::VulkanImage(void) const
{
    return image;
}

const Device* Image::GetDevice(void) const
{
    return device;
}

VkDeviceSize Image::SizeInBytes(void) const
{
    return device->GetVulkanMemoryAllocator()->SizeInBytes(image);
}
void Image::SetName(const std::string& _name)
{
    name = _name;
    debugmarker::SetName(device,image, name);
}
const std::string& Image::GetName(void) const
{
    return name;
}
}
}

