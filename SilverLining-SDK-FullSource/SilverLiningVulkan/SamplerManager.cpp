// Copyright (c) 2004-2024 Sundog Software, LLC. All rights reserved worldwide.
#include "SLAssert.h"

#include "SamplerManager.h"
#include "Image.h"
#include "Device.h"

#include "VulkanInitializers.h"
#include "VulkanDebug.h"

namespace SilverLining
{
namespace Vulkan
{
SamplerManager::SamplerManager(Device* _device)
    : device(_device)
{

}

SamplerManager::~SamplerManager()
{
    VkDevice vulkanDevice = device->VulkanDevice();
    for (auto& keyVal : mapSamplerHashToSampler) {
        VkSampler sampler = keyVal.second;
        vkDestroySampler(vulkanDevice, sampler, nullptr);
    }
}

VkSampler SamplerManager::GetOrCreateSampler(VkFilter minFilter, VkFilter magFilter
        , VkSamplerAddressMode addressModeU, VkSamplerAddressMode addressModeV, VkSamplerAddressMode addressModeW
        , const Image* image)
{
    VkSamplerCreateInfo samplerInfo = vkInitializers::samplerCreateInfo();
    samplerInfo.magFilter = minFilter;
    samplerInfo.minFilter = magFilter;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (image->IsCubeMap()) {
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = samplerInfo.addressModeU;
        samplerInfo.addressModeW = samplerInfo.addressModeV;
    } else {
        samplerInfo.addressModeU = addressModeU;
        samplerInfo.addressModeV = addressModeV;
        if (image->Depth() == 1) {
            samplerInfo.addressModeW = samplerInfo.addressModeV;
        } else {
            samplerInfo.addressModeW = addressModeW;
        }
    }

    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
    samplerInfo.minLod = 0.0f;
    // Set max level-of-detail to mip level count of the texture
    samplerInfo.maxLod = (float)(image->NumMipMapLevels());

    samplerInfo.maxAnisotropy = 1.0;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    return GetOrCreateSampler(samplerInfo);
}

bool SamplerManager::AreSame(const VkSamplerCreateInfo& samplerInfo, unsigned int samplerHash)
{
    VkFilter minfilterFromHash = (VkFilter)(0x01 & (samplerHash >> 0));
    if (minfilterFromHash != samplerInfo.minFilter) {
        return false;
    }
    VkFilter magfilterFromHash = (VkFilter)(0x01 & (samplerHash >> 1));
    if (magfilterFromHash != samplerInfo.magFilter) {
        return false;
    }

    VkSamplerMipmapMode mipfilterFromHash = (VkSamplerMipmapMode)(0x01 & (samplerHash >> 2));
    if (mipfilterFromHash != samplerInfo.mipmapMode) {
        return false;
    }

    VkSamplerAddressMode uAddressModeFromHash = (VkSamplerAddressMode)(0x07 & (samplerHash >> 3));
    if (uAddressModeFromHash != samplerInfo.addressModeU) {
        return false;
    }
    VkSamplerAddressMode vAddressModeFromHash = (VkSamplerAddressMode)(0x07 & (samplerHash >> 6));
    if (vAddressModeFromHash != samplerInfo.addressModeV) {
        return false;
    }

    VkSamplerAddressMode wAddressModeFromHash = (VkSamplerAddressMode)(0x07 & (samplerHash >> 9));
    if (wAddressModeFromHash != samplerInfo.addressModeW) {
        return false;
    }

    int maxLodFromHash = (0xFF & (samplerHash >> 12));
    if (maxLodFromHash != (int)samplerInfo.maxLod) {
        return false;
    }

    return true;
}

VkSampler SamplerManager::GetOrCreateSampler(const VkSamplerCreateInfo& samplerInfo)
{
    unsigned int samplerHash = 0;

    samplerHash  = samplerInfo.minFilter;       // bit 0
    samplerHash |= samplerInfo.magFilter  << 1; // bit 1
    samplerHash |= samplerInfo.mipmapMode << 2; // bit 2 (doesn't change per above function, but just for completeness)

    samplerHash |= samplerInfo.addressModeU << 3; // bits 3-5
    samplerHash |= samplerInfo.addressModeV << 6; // bits 6-8
    samplerHash |= samplerInfo.addressModeW << 9; // bits 9-11

    int intMaxLod = (int)samplerInfo.maxLod;
    samplerHash |= intMaxLod << 12; // bits 12-19;

    SL_ASSERT(AreSame(samplerInfo, samplerHash));

    auto it = mapSamplerHashToSampler.find(samplerHash);
    if (it != mapSamplerHashToSampler.end()) {
        VkSampler sampler = it->second;
        SL_ASSERT(AreSame(samplerInfo, samplerHash));
        return sampler;
    }

    VkSampler sampler = VK_NULL_HANDLE;
    VK_CHECK_RESULT(vkCreateSampler(device->VulkanDevice(), &samplerInfo, nullptr, &sampler));

    mapSamplerHashToSampler.insert({ samplerHash, sampler });

    //std::cout<< "num of samplers: " << mapSamplerHashToSampler.size() << std::endl;

    return sampler;
}
}
}