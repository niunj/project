// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once
#define SL_ENABLE_ASSERTS 1
#include "SLAssert.h"
#include "VulkanMath.h"

#include <vulkan/vulkan.h>

#include <string>
#include <iostream>

namespace SilverLining
{
    namespace Vulkan
    {
        class Device;
        namespace debugmarker
        {
            // Set to true if function pointer for the debug marker are available
            extern bool active;

            // Get function pointers for the debug report extensions from the device
            void Setup(Device* device);

            // Sets the debug name of an object
            // All Objects in Vulkan are represented by their 64-bit handles which are passed into this function
            // along with the object type
            void SetObjectName(Device* device, uint64_t object, VkObjectType objectType, const std::string& name);

            // Set the tag for an object
            void SetObjectTag(Device* device, uint64_t object, VkObjectType objectType, uint64_t name, size_t tagSize, const void* tag);

            // Start a new debug marker region
            void BeginRegion(Device* device, VkCommandBuffer cmdbuffer, const char* pMarkerName, glm::vec4 color);

            // Insert a new debug marker into the command buffer
            void Insert(Device* device, VkCommandBuffer cmdbuffer, std::string markerName, glm::vec4 color);

            // End the current debug marker region
            void EndRegion(Device* device, VkCommandBuffer cmdBuffer);

            // Object specific naming functions
            void SetName(Device* device, VkCommandBuffer cmdBuffer, const std::string& name);
            void SetName(Device* device, VkQueue queue, const std::string& name);

            void SetName(Device* device, VkImage image, const std::string& name);
            void SetName(Device* device, VkSampler sampler, const std::string& name);
            void SetName(Device* device, VkBuffer buffer, const std::string& name);
            void SetName(Device* device, VkDeviceMemory memory, const std::string& name);

            void SetName(Device* device, VkShaderModule shaderModule, const std::string& name);
            void SetName(Device* device, VkPipeline pipeline, const std::string& name);
            void SetName(Device* device, VkPipelineLayout pipelineLayout, const std::string& name);
            void SetName(Device* device, VkRenderPass renderPass, const std::string& name);
            void SetName(Device* device, VkFramebuffer framebuffer, const std::string& name);

            void SetName(Device* device, VkDescriptorSetLayout descriptorSetLayout, const std::string& name);
            void SetName(Device* device, VkDescriptorSet descriptorSet, const std::string& name);

            void SetName(Device* device, VkSemaphore semaphore, const std::string& name);
            void SetName(Device* device, VkFence fence, const std::string& name);
            void SetName(Device* device, VkEvent _event, const std::string& name);
        };

        namespace tools
        {
            void exitFatal(const std::string& message, int32_t exitCode);
            void exitFatal(const std::string& message, VkResult resultCode);

            /** @brief Returns an error code as a string */
            std::string ErrorString(VkResult errorCode);

            uint32_t alignedSize(uint32_t value, uint32_t alignment);

            std::string formatBytes(uint64_t sizeInBytes);
        }
    }


    // Default fence timeout in nanoseconds
#define DEFAULT_FENCE_TIMEOUT 100000000000

#define VK_CHECK_RESULT(f)																				\
{																										\
VkResult res = (f);																					\
if (res != VK_SUCCESS)																				\
{																									\
std::cout << "Fatal : VkResult is \"" << tools::ErrorString(res) << "\" in " << __FILE__ << " at line " << __LINE__ << "\n"; \
assert(res == VK_SUCCESS);																		\
}																									\
}

}