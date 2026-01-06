// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "RenderToTexture.h"
#include "Texture.h"
#include "StorageImage.h"
#include "ImageTransitions.h"
#include "Device.h"
#include "VulkanInitializers.h"
#include "VulkanDebug.h"

#include <array>
#include <sstream>

namespace SilverLining
{
namespace Vulkan
{
RenderToTexture::RenderToTexture(int _width, int _height, VkFormat _colorFormat, int _numBufferedFrames, const std::string& name, Device* _device, SamplerManager* _samplerManager)
    : device(_device)
    , width(_width)
    , height(_height)
    , colorFormat(_colorFormat)
    , numBufferedFrames(_numBufferedFrames)
    , samplerManager(_samplerManager)
{
    // This image can be rendered directly to -> VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
    // This image is used for texturing -> VK_IMAGE_USAGE_SAMPLED_BIT

    const VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    for (int i = 0; i < numBufferedFrames; ++i) {
        StorageImage* colorImage = new StorageImage(_device
                , colorFormat, width, height
                , usageFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                , VK_IMAGE_TILING_OPTIMAL, 1, 1);

        std::stringstream ss;
        ss << name << "_" << i;

        colorImage->SetName(ss.str());

        VkCommandBuffer commandBuffer = device->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
        VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        transitions::setImageLayout(commandBuffer, colorImage->VulkanImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
        device->FlushCommandBuffer(commandBuffer);

        colorImages.push_back(colorImage);
    }

    const VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    const VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    const VkSamplerAddressMode addressModeWNotUsed = VK_SAMPLER_ADDRESS_MODE_MAX_ENUM; // don't care

    for (int i = 0; i < numBufferedFrames; ++i) {
        Texture* texture = new Texture(colorImages[i], VK_FILTER_LINEAR, VK_FILTER_LINEAR
                                       , addressModeU, addressModeV, addressModeWNotUsed, Texture::DefaultComponentMapping(), samplerManager);

        textures.push_back(texture);
    }

    if (dynamicRendering == false) {
        std::array<VkAttachmentDescription, 2> attachments = {};
        // Color attachment
        attachments[0].format = colorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Depth attachment
        attachments[1].format = VK_FORMAT_D32_SFLOAT_S8_UINT;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorReference = {};
        colorReference.attachment = 0;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthReference = {};
        depthReference.attachment = 1;
        depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpassDescription = {};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = 1;
        subpassDescription.pColorAttachments = &colorReference;
        subpassDescription.pDepthStencilAttachment = &depthReference;

        // Subpass dependencies for layout transitions
        std::array<VkSubpassDependency, 2> dependencies;

        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo renderPassInfo = vkInitializers::renderPassCreateInfo();
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpassDescription;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        VK_CHECK_RESULT(vkCreateRenderPass(device->VulkanDevice(), &renderPassInfo, nullptr, &renderPass));


        VkFramebufferCreateInfo framebufferCreateInfo = vkInitializers::framebufferCreateInfo();
        framebufferCreateInfo.renderPass = renderPass;
        framebufferCreateInfo.attachmentCount = 1;
        framebufferCreateInfo.width = width;
        framebufferCreateInfo.height = height;
        framebufferCreateInfo.layers = 1;

        for (int i = 0; i < numBufferedFrames; ++i) {
            VkFramebuffer frameBuffer = VK_NULL_HANDLE;
            framebufferCreateInfo.pAttachments = &colorImages[i]->VulkanImageView();
            VK_CHECK_RESULT(vkCreateFramebuffer(device->VulkanDevice(), &framebufferCreateInfo, nullptr, &frameBuffer));
            frameBuffers.push_back(frameBuffer);
        }
    }
}

RenderToTexture::~RenderToTexture()
{
    for (auto texture : textures) {
        delete texture;
    }
    for (auto colorImage : colorImages) {
        delete colorImage;
    }
    for (auto frameBuffer : frameBuffers) {
        vkDestroyFramebuffer(device->VulkanDevice(), frameBuffer, nullptr);
    }
}

void RenderToTexture::MakeCurrent(VkCommandBuffer commandBuffer, int frameIndex)
{
    SL_ASSERT(frameIndex < numBufferedFrames);
    if (frameIndex >= numBufferedFrames) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "frameIndex >= numBufferedFrames" << std::endl;
        return;
    }

    if (dynamicRendering) {
        MakeCurrentDynamicRendering(commandBuffer, frameIndex);
    } else {
        MakeCurrentRenderPass(commandBuffer, frameIndex);
    }
}

void RenderToTexture::MakeCurrentDynamicRendering(VkCommandBuffer commandBuffer, int frameIndex)
{
    VkImageMemoryBarrier2 colorImageBarrier = vkInitializers::imageMemoryBarrier2();
    colorImageBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    colorImageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    colorImageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    colorImageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    colorImageBarrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    colorImageBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

    colorImageBarrier.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    colorImageBarrier.image = colorImages[frameIndex]->VulkanImage();

    VkDependencyInfo dependencyInfo = vkInitializers::dependencyInfo();
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &colorImageBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = colorImages[frameIndex]->VulkanImageView();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue = { 1.0f, 1.0f, 1.0f, 1.0f };

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { 0, 0, (uint32_t)width, (uint32_t)height };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    device->Extensions().vkCmdBeginRenderingKHR(commandBuffer, &renderingInfo);

    const VkViewport viewport = vkInitializers::viewport((float)width, (float)height, 0.0f, 1.0f);
    const VkRect2D scissor = vkInitializers::rect2D(width, height, 0, 0);

    // Update dynamic viewport state
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    // Update dynamic scissor state
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void RenderToTexture::BindDynamicRendering(VkCommandBuffer commandBuffer, int frameIndex)
{
    device->Extensions().vkCmdEndRenderingKHR(commandBuffer);

    VkImageMemoryBarrier2 imageBarrier = vkInitializers::imageMemoryBarrier2();
    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

    imageBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

    imageBarrier.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    imageBarrier.image = colorImages[frameIndex]->VulkanImage();

    VkDependencyInfo dependencyInfo = vkInitializers::dependencyInfo();
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &imageBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void RenderToTexture::MakeCurrentRenderPass(VkCommandBuffer commandBuffer, int frameIndex)
{
    VkClearValue clearValues[2];
    clearValues[0].color = { 1.0f, 1.0f, 1.0f, 1.0f };
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo renderPassBeginInfo = vkInitializers::renderPassBeginInfo();
    renderPassBeginInfo.renderPass = renderPass;
    renderPassBeginInfo.framebuffer = frameBuffers[frameIndex];
    renderPassBeginInfo.renderArea.extent.width = width;
    renderPassBeginInfo.renderArea.extent.height = height;
    renderPassBeginInfo.clearValueCount = 2;
    renderPassBeginInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    const VkViewport viewport = vkInitializers::viewport((float)width, (float)height, 0.0f, 1.0f);
    const VkRect2D scissor = vkInitializers::rect2D(width, height, 0, 0);

    // Update dynamic viewport state
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    // Update dynamic scissor state
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void RenderToTexture::BindRenderPass(VkCommandBuffer commandBuffer, int frameIndex)
{
    vkCmdEndRenderPass(commandBuffer);
}

void RenderToTexture::Bind(VkCommandBuffer commandBuffer, int frameIndex)
{
    SL_ASSERT(frameIndex < numBufferedFrames);
    if (frameIndex >= numBufferedFrames) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "frameIndex >= numBufferedFrames" << std::endl;
        return;
    }

    if (dynamicRendering) {
        BindDynamicRendering(commandBuffer, frameIndex);
    } else {
        BindRenderPass(commandBuffer, frameIndex);
    }
}

Texture* RenderToTexture::GetTexture(int frameIndex)
{
    SL_ASSERT(frameIndex < numBufferedFrames);
    if (frameIndex >= numBufferedFrames) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "frameIndex >= numBufferedFrames" << std::endl;
        return nullptr;
    }

    if (frameIndex < 0) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "frameIndex < 0" << std::endl;
        return nullptr;
    }

    return textures[frameIndex];
}

const Texture* RenderToTexture::GetTexture(int frameIndex) const
{
    SL_ASSERT(frameIndex < numBufferedFrames);
    if (frameIndex >= numBufferedFrames) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "frameIndex >= numBufferedFrames" << std::endl;
        return nullptr;
    }

    return textures[frameIndex];
}

VkImage RenderToTexture::GetImage(int frameIndex) const
{
    SL_ASSERT(frameIndex < numBufferedFrames);
    if (frameIndex >= numBufferedFrames) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "frameIndex >= numBufferedFrames" << std::endl;
        return VK_NULL_HANDLE;
    }

    return colorImages[frameIndex]->VulkanImage();
}

VkFormat RenderToTexture::GetColorFormat(void) const
{
    return colorFormat;
}
}
}