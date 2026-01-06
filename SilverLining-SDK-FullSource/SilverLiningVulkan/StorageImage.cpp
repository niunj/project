#include "StorageImage.h"
#include "VulkanInitializers.h"
#include "Device.h"
#include "ImageTransitions.h"
#include "VulkanDebug.h"

namespace SilverLining
{
namespace Vulkan
{
StorageImage::StorageImage(Device* device, VkFormat _format, int _width, int _height
                           , VkImageUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkImageTiling imageTiling, int sampleCount, int arrayLayers)
    : Image(device)
    , imageView(0)
    , usageFlags(usageFlags)
{
    format = _format;
    width = _width;
    height = _height;
    numMipMapLevels = 1;

    isCubeMap = arrayLayers==6;

    AllocateImageAndMemory(usageFlags, memoryPropertyFlags, imageTiling, arrayLayers, sampleCount);
}

StorageImage::~StorageImage()
{
    vkDestroyImageView(device->VulkanDevice(), imageView, nullptr);
}

const VkImageView& StorageImage::VulkanImageView(void) const
{
    if (imageView) {
        return imageView;
    }

    VkImageViewCreateInfo imageViewCreateInfo = vkInitializers::imageViewCreateInfo();
    imageViewCreateInfo.viewType = isCubeMap? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format = format;
    imageViewCreateInfo.subresourceRange = {};
    if (usageFlags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (format >= VK_FORMAT_D16_UNORM_S8_UINT) {
            imageViewCreateInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    } else {
        imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    imageViewCreateInfo.subresourceRange.levelCount = 1;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount = isCubeMap? 6 : 1;
    imageViewCreateInfo.image = image;
    StorageImage* nonConst = const_cast<StorageImage*>(this);
    VK_CHECK_RESULT(vkCreateImageView(device->VulkanDevice(), &imageViewCreateInfo, nullptr, &(nonConst->imageView)));

    return imageView;
}
}
}