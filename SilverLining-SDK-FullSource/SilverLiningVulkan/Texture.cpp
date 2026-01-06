// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "Texture.h"
#include "Image.h"
#include "Device.h"
#include "SamplerManager.h"

#include "VulkanInitializers.h"
#include "VulkanDebug.h"

namespace SilverLining
{
namespace Vulkan
{
Texture::Texture(Image* _image, VkFilter minFilter, VkFilter magFilter
                 , VkSamplerAddressMode addressModeU, VkSamplerAddressMode addressModeV, VkSamplerAddressMode addressModeW
                 , VkComponentMapping componentMapping, SamplerManager* _samplerManager)
    : image(_image)
    , samplerManager(_samplerManager)
{
    CreateSampler(minFilter, magFilter, addressModeU, addressModeV, addressModeW);
    CreateImageView(componentMapping);

    descriptor = VkDescriptorImageInfo{ sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
}

Texture::~Texture()
{
    VkDevice vulkanDevice = image->GetDevice()->VulkanDevice();
    vkDestroyImageView(vulkanDevice, imageView, nullptr);
}

void Texture::CreateSampler(VkFilter minFilter, VkFilter magFilter
                            , VkSamplerAddressMode addressModeU, VkSamplerAddressMode addressModeV, VkSamplerAddressMode addressModeW)
{
    sampler = samplerManager->GetOrCreateSampler(minFilter, magFilter, addressModeU, addressModeV, addressModeW, image);
}

void Texture::CreateImageView(VkComponentMapping componentMapping)
{
    VkImageViewCreateInfo imageViewInfo = vkInitializers::imageViewCreateInfo();
    if (image->IsCubeMap()) {
        imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    } else if (image->Depth() == 1) {
        imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    } else {
        imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    }
    imageViewInfo.format = image->VulkanFormat();
    imageViewInfo.components = componentMapping;
    // The subresource range describes the set of mip levels (and array layers) that can be accessed through this image view
    // It's possible to create multiple image views for a single image referring to different (and/or overlapping) ranges of the image
    imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewInfo.subresourceRange.baseMipLevel = 0;
    imageViewInfo.subresourceRange.baseArrayLayer = 0;
    imageViewInfo.subresourceRange.layerCount = (!image->IsCubeMap()) ? 1 : 6;

    imageViewInfo.subresourceRange.levelCount = image->NumMipMapLevels();
    // The view will be based on the texture's image
    imageViewInfo.image = image->VulkanImage();
    VK_CHECK_RESULT(vkCreateImageView(image->GetDevice()->VulkanDevice(), &imageViewInfo, nullptr, &imageView));
}

const VkDescriptorImageInfo& Texture::Descriptor(void) const
{
    return descriptor;
}

const VkDescriptorImageInfo* Texture::DescriptorPtr(void) const
{
    return &descriptor;
}

const Image* Texture::GetImage(void) const
{
    return image;
}

VkComponentMapping Texture::DefaultComponentMapping(void)
{
    return { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
}
}
}