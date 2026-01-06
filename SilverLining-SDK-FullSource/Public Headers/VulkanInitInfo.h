// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#include <vulkan/vulkan.h>

namespace SilverLining
{
    namespace Vulkan
    {
        class VulkanMemoryAllocator;

        struct VulkanInitInfo
        {
            VkInstance instance = VK_NULL_HANDLE;
            VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
            VkDevice device = VK_NULL_HANDLE;
            VkQueue graphicsQueue = VK_NULL_HANDLE;
            uint32_t graphicsQueueFamilyIndex = 0;

            VkRenderPass renderPass = VK_NULL_HANDLE;
            VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
            VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
            VkFormat depthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;

            float nearDepth = 0.0f;
            float farDepth = 1.0f;

            // 2 if double buffered, 3 if triple buffered and so on
            uint32_t numBufferedFrames = 3;

            VulkanMemoryAllocator* vulkanMemoryAllocator = nullptr;
        };
    }
}