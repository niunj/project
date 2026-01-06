// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "CubeMapRenderTexture.h"
#include "Texture.h"
#include "StorageImage.h"
#include "ImageTransitions.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "VulkanInitializers.h"
#include "VulkanDebug.h"

#include <array>
#include <sstream>

namespace SilverLining
{
namespace Vulkan
{
CubeMapRenderTexture::CubeMapRenderTexture(int _width, int _height
        , bool floatingPoint, bool generateMipMaps, int _numBufferedFrames, const std::string& name, Device* _device)
    : device(_device)
    , width(_width)
    , height(_height)
    , numBufferedFrames(_numBufferedFrames)
{
    colorFormat = (floatingPoint) ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
    const VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    for (int i = 0; i < numBufferedFrames; ++i) {
        auto colorImage = new StorageImage(_device
                                           , colorFormat, width, height
                                           , usageFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                                           , VK_IMAGE_TILING_OPTIMAL, 1, numFaces);

        std::stringstream ss;
        ss << name << "_" << i;

        colorImage->SetName(ss.str());

        VkCommandBuffer commandBuffer = device->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
        VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, numFaces };
        transitions::setImageLayout(commandBuffer, colorImage->VulkanImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
        device->FlushCommandBuffer(commandBuffer);

        colorImages.push_back(colorImage);
    }

    for (int i = 0; i < numBufferedFrames; ++i) {
        vecCubeFaceImageViews.push_back({});
    }
    for (int i = 0; i < numBufferedFrames; ++i) {
        vecCubeFaceImageViews[i].fill(VK_NULL_HANDLE);
    }

    setupDepthStencil();
}

CubeMapRenderTexture::~CubeMapRenderTexture()
{
    for (auto& cubeFaceImageViews : vecCubeFaceImageViews) {
        for (auto imageView : cubeFaceImageViews) {
            if (imageView) {
                vkDestroyImageView(device->VulkanDevice(), imageView, nullptr);
            }
        }
    }
    for (auto colorImage : colorImages) {
        delete colorImage;
    }

    destroyDepthStencil();
}

uint32_t CubeMapRenderTexture::ToIndex(CubeFace face) const
{
    if (face == POSX) return 0;
    if (face == NEGX) return 1;
    if (face == POSY) return 2;
    if (face == NEGY) return 3;
    if (face == POSZ) return 4;
    if (face == NEGZ) return 5;

    device->log(LogLevel::SL_LOG_LEVEL_WARN) << "unknown" << std::endl;

    return 0;
}

bool CubeMapRenderTexture::MakeCurrent(VkCommandBuffer commandBuffer, int frameIndex, CubeFace face)
{
    SL_ASSERT(frameIndex < numBufferedFrames);
    if (frameIndex >= numBufferedFrames) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "frameIndex >= numBufferedFrames" << std::endl;
        return false;
    }

    uint32_t index = ToIndex(face);
    indexWhichIsCurrent = index;

    VkImageMemoryBarrier2 colorImageBarrier = vkInitializers::imageMemoryBarrier2();
    colorImageBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    colorImageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    colorImageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    colorImageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    colorImageBarrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    colorImageBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

    colorImageBarrier.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, (uint32_t)indexWhichIsCurrent, 1 };
    colorImageBarrier.image = colorImages[frameIndex]->VulkanImage();

    VkDependencyInfo dependencyInfo = vkInitializers::dependencyInfo();
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &colorImageBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = CubeFaceImageView(frameIndex, index);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue = { 0.0f, 0.0f, 0.0f, 1.0f };

    VkRenderingAttachmentInfo depthStencilAttachment{};
    depthStencilAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthStencilAttachment.imageView = depthStencilImages[frameIndex]->VulkanImageView();
    depthStencilAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthStencilAttachment.clearValue = { 1.0f, 0.0f };

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { 0, 0, (uint32_t)width, (uint32_t)height };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthStencilAttachment;

    device->Extensions().vkCmdBeginRenderingKHR(commandBuffer, &renderingInfo);

    const VkViewport viewport = vkInitializers::viewport((float)width, (float)height, 0.0f, 1.0f, false);
    const VkRect2D scissor = vkInitializers::rect2D(width, height, 0, 0);

    // Update dynamic viewport state
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    // Update dynamic scissor state
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    return true;
}

bool CubeMapRenderTexture::Bind(VkCommandBuffer commandBuffer, int frameIndex)
{
    SL_ASSERT(frameIndex < numBufferedFrames);
    if (frameIndex >= numBufferedFrames) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "frameIndex >= numBufferedFrames" << std::endl;
        return false;
    }

    device->Extensions().vkCmdEndRenderingKHR(commandBuffer);

    VkImageMemoryBarrier2 imageBarrier = vkInitializers::imageMemoryBarrier2();
    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

    imageBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

    imageBarrier.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, (uint32_t)indexWhichIsCurrent, 1 };
    imageBarrier.image = colorImages[frameIndex]->VulkanImage();

    VkDependencyInfo dependencyInfo = vkInitializers::dependencyInfo();
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &imageBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

    return true;
}

const VkImageView& CubeMapRenderTexture::CubeFaceImageView(int frameIndex, int index) const
{
    if (index >= numFaces) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) <<__FUNCTION__ << "index >= numFaces" << std::endl;
        static VkImageView nullHandle = VK_NULL_HANDLE;
        return nullHandle;
    }

    auto& cubeFaceImageViews = vecCubeFaceImageViews[frameIndex];
    if (cubeFaceImageViews[index]) {
        return cubeFaceImageViews[index];
    }

    for (int i = 0; i < numFaces; ++i) {
        VkImageViewCreateInfo imageViewCreateInfo = vkInitializers::imageViewCreateInfo();
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = colorFormat;
        imageViewCreateInfo.subresourceRange = {};

        imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        imageViewCreateInfo.subresourceRange.levelCount = 1;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = i;
        imageViewCreateInfo.subresourceRange.layerCount = 1;
        imageViewCreateInfo.image = colorImages[frameIndex]->VulkanImage();
        CubeMapRenderTexture* nonConst = const_cast<CubeMapRenderTexture*>(this);
        VK_CHECK_RESULT(vkCreateImageView(device->VulkanDevice(), &imageViewCreateInfo, nullptr, &(nonConst->vecCubeFaceImageViews[frameIndex][i])));
    }

    return cubeFaceImageViews[index];
}

VkImage CubeMapRenderTexture::GetImage(int frameIndex) const
{
    SL_ASSERT(frameIndex < numBufferedFrames);
    if (frameIndex >= numBufferedFrames) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "frameIndex >= numBufferedFrames" << std::endl;
        return VK_NULL_HANDLE;
    }

    return colorImages[frameIndex]->VulkanImage();
}

void CubeMapRenderTexture::setupDepthStencil()
{
    VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VkFormat depthFormat;
    device->GetPhysicalDevice()->GetSupportedDepthFormat(depthFormat);

    for (int i = 0; i < numBufferedFrames; ++i) {
        auto depthStencilImage = new StorageImage(device
                , depthFormat, width, height
                , usageFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                , VK_IMAGE_TILING_OPTIMAL, 1, 1);

        depthStencilImages.push_back(depthStencilImage);
    }

}

void CubeMapRenderTexture::destroyDepthStencil(void)
{
    for (auto depthStencilImage : depthStencilImages) {
        delete depthStencilImage;
    }
}

VkFormat CubeMapRenderTexture::GetColorFormat(void) const
{
    return colorFormat;
}
}
}