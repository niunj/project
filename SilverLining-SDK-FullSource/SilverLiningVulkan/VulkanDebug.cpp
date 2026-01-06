// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "VulkanDebug.h"
#include "Device.h"
#include <sstream>

namespace SilverLining
{
namespace Vulkan
{
namespace debugmarker
{
bool active = false;

void Setup(Device* device)
{
    // Set flag if at least one function pointer is present
    active = (device->Extensions().vkDebugMarkerSetObjectNameEXT != VK_NULL_HANDLE);
}

void SetObjectName(Device* device, uint64_t object, VkObjectType objectType, const std::string& name)
{
    if (device->Extensions().vkSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectNameInfoEXT nameInfo = {};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = objectType;
        nameInfo.objectHandle = object;
        nameInfo.pObjectName = name.c_str();
        device->Extensions().vkSetDebugUtilsObjectNameEXT(device->VulkanDevice(), &nameInfo);
    }
}

void SetObjectTag(Device* device, uint64_t object, VkObjectType objectType, uint64_t name, size_t tagSize, const void* tag)
{
    // Check for valid function pointer (may not be present if not running in a debugging application)
    if (device->Extensions().vkSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectTagInfoEXT tagInfo = {};
        tagInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        tagInfo.objectType = objectType;
        tagInfo.objectHandle = object;
        tagInfo.tagName = name;
        tagInfo.tagSize = tagSize;
        tagInfo.pTag = tag;
        device->Extensions().vkSetDebugUtilsObjectTagEXT(device->VulkanDevice(), &tagInfo);
    }
}

void BeginRegion(Device* device, VkCommandBuffer cmdbuffer, const char* pMarkerName, glm::vec4 color)
{
    // Check for valid function pointer (may not be present if not running in a debugging application)
    if (device->Extensions().vkCmdBeginDebugUtilsLabelEXT) {
        VkDebugUtilsLabelEXT markerInfo = {};
        markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        memcpy(markerInfo.color, &color[0], sizeof(float) * 4);
        markerInfo.pLabelName = pMarkerName;
        device->Extensions().vkCmdBeginDebugUtilsLabelEXT(cmdbuffer, &markerInfo);
    }
}

void EndRegion(Device* device, VkCommandBuffer cmdBuffer)
{
    // Check for valid function (may not be present if not running in a debugging application)
    if (device->Extensions().vkCmdEndDebugUtilsLabelEXT) {
        device->Extensions().vkCmdEndDebugUtilsLabelEXT(cmdBuffer);
    }
}

void Insert(Device* device, VkCommandBuffer cmdbuffer, std::string markerName, glm::vec4 color)
{
    // Check for valid function pointer (may not be present if not running in a debugging application)
    if (device->Extensions().vkCmdInsertDebugUtilsLabelEXT) {
        VkDebugUtilsLabelEXT markerInfo = {};
        markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        memcpy(markerInfo.color, &color[0], sizeof(float) * 4);
        markerInfo.pLabelName = markerName.c_str();
        device->Extensions().vkCmdInsertDebugUtilsLabelEXT(cmdbuffer, &markerInfo);
    }
}

void SetName(Device* device, VkCommandBuffer cmdBuffer, const std::string& name)
{
    SetObjectName(device, (uint64_t)cmdBuffer, VK_OBJECT_TYPE_COMMAND_BUFFER, name);
}

void SetName(Device* device, VkQueue queue, const std::string& name)
{
    SetObjectName(device, (uint64_t)queue, VK_OBJECT_TYPE_QUEUE, name);
}

void SetName(Device* device, VkImage image, const std::string& name)
{
    SetObjectName(device, (uint64_t)image, VK_OBJECT_TYPE_IMAGE, name);
}

void SetName(Device* device, VkSampler sampler, const std::string& name)
{
    SetObjectName(device, (uint64_t)sampler, VK_OBJECT_TYPE_SAMPLER, name);
}

void SetName(Device* device, VkBuffer buffer, const std::string& name)
{
    SetObjectName(device, (uint64_t)buffer, VK_OBJECT_TYPE_BUFFER, name);
}

void SetName(Device* device, VkDeviceMemory memory, const std::string& name)
{
    SetObjectName(device, (uint64_t)memory, VK_OBJECT_TYPE_DEVICE_MEMORY, name);
}

void SetName(Device* device, VkShaderModule shaderModule, const std::string& name)
{
    SetObjectName(device, (uint64_t)shaderModule, VK_OBJECT_TYPE_SHADER_MODULE, name);
}

void SetName(Device* device, VkPipeline pipeline, const std::string& name)
{
    SetObjectName(device, (uint64_t)pipeline, VK_OBJECT_TYPE_PIPELINE, name);
}

void SetName(Device* device, VkPipelineLayout pipelineLayout, const std::string& name)
{
    SetObjectName(device, (uint64_t)pipelineLayout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, name);
}

void SetName(Device* device, VkRenderPass renderPass, const std::string& name)
{
    SetObjectName(device, (uint64_t)renderPass, VK_OBJECT_TYPE_RENDER_PASS, name);
}

void SetName(Device* device, VkFramebuffer framebuffer, const std::string& name)
{
    SetObjectName(device, (uint64_t)framebuffer, VK_OBJECT_TYPE_FRAMEBUFFER, name);
}

void SetName(Device* device, VkDescriptorSetLayout descriptorSetLayout, const std::string& name)
{
    SetObjectName(device, (uint64_t)descriptorSetLayout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, name);
}

void SetName(Device* device, VkDescriptorSet descriptorSet, const std::string& name)
{
    SetObjectName(device, (uint64_t)descriptorSet, VK_OBJECT_TYPE_DESCRIPTOR_SET, name);
}

void SetName(Device* device, VkSemaphore semaphore, const std::string& name)
{
    SetObjectName(device, (uint64_t)semaphore, VK_OBJECT_TYPE_SEMAPHORE, name);
}

void SetName(Device* device, VkFence fence, const std::string& name)
{
    SetObjectName(device, (uint64_t)fence, VK_OBJECT_TYPE_FENCE, name);
}

void SetName(Device* device, VkEvent _event, const std::string& name)
{
    SetObjectName(device, (uint64_t)_event, VK_OBJECT_TYPE_EVENT, name);
}
}

namespace tools
{
void exitFatal(const std::string& message, int32_t exitCode)
{
    std::cerr << message << "\n";
    exit(exitCode);
}
void exitFatal(const std::string& message, VkResult resultCode)
{
    exitFatal(message, (int32_t)resultCode);
}

std::string ErrorString(VkResult errorCode)
{
    switch (errorCode) {
#define STR(r) case VK_ ##r: return #r
        STR(NOT_READY);
        STR(TIMEOUT);
        STR(EVENT_SET);
        STR(EVENT_RESET);
        STR(INCOMPLETE);
        STR(ERROR_OUT_OF_HOST_MEMORY);
        STR(ERROR_OUT_OF_DEVICE_MEMORY);
        STR(ERROR_INITIALIZATION_FAILED);
        STR(ERROR_DEVICE_LOST);
        STR(ERROR_MEMORY_MAP_FAILED);
        STR(ERROR_LAYER_NOT_PRESENT);
        STR(ERROR_EXTENSION_NOT_PRESENT);
        STR(ERROR_FEATURE_NOT_PRESENT);
        STR(ERROR_INCOMPATIBLE_DRIVER);
        STR(ERROR_TOO_MANY_OBJECTS);
        STR(ERROR_FORMAT_NOT_SUPPORTED);
        STR(ERROR_SURFACE_LOST_KHR);
        STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
        STR(SUBOPTIMAL_KHR);
        STR(ERROR_OUT_OF_DATE_KHR);
        STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
        STR(ERROR_VALIDATION_FAILED_EXT);
        STR(ERROR_INVALID_SHADER_NV);
#undef STR
    default:
        return "UNKNOWN_ERROR";
    }
}

uint32_t alignedSize(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
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
}
}
}
