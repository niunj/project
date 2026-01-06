// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "PhysicalDevice.h"
#include "Instance.h"
#include "VulkanDebug.h"

#include <iostream>
#include <cassert>
#include <sstream>
#include <algorithm>

namespace SilverLining
{
namespace Vulkan
{
PhysicalDevice::PhysicalDevice(const Instance* _instance, VkPhysicalDevice _physicalDevice)
    : instance(_instance)
    , physicalDevice(_physicalDevice)
{
    // Store properties (including limits), features and memory properties of the physical device (so that examples can check against them)
    vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
    vkGetPhysicalDeviceFeatures(physicalDevice, &physicalDeviceFeatures);

    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &physicalDeviceMemoryProperties);

    // Queue family properties, used for setting up requested queues upon device creation
    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    assert(queueFamilyCount > 0);
    queueFamilyProperties.resize(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());

    // Get list of supported extensions
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
    if (extCount > 0) {
        SL_VECTOR(VkExtensionProperties) extensions(extCount);
        if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, &extensions.front()) == VK_SUCCESS) {
            for (auto ext : extensions) {
                supportedExtensions.push_back(ext.extensionName);
            }
        }
    }

    // Get ray tracing pipeline properties, which will be used later on in the sample
    rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 deviceProperties2 {};
    deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProperties2.pNext = &rayTracingPipelineProperties;
    vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties2);

    // Get acceleration structure properties, which will be used later on in the sample
    accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    VkPhysicalDeviceFeatures2 deviceFeatures2 {};
    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures2.pNext = &accelerationStructureFeatures;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &deviceFeatures2);

    // Get mesh shader capabilities
    meshShaderProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT;
    deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProperties2.pNext = &meshShaderProperties;
    vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties2);
}

PhysicalDevice::~PhysicalDevice()
{
    // no op
}

static std::string physicalDeviceTypeString(VkPhysicalDeviceType type)
{
    switch (type) {
#define STR(r) case VK_PHYSICAL_DEVICE_TYPE_ ##r: return #r
        STR(OTHER);
        STR(INTEGRATED_GPU);
        STR(DISCRETE_GPU);
        STR(VIRTUAL_GPU);
        STR(CPU);
#undef STR
    default:
        return "UNKNOWN_DEVICE_TYPE";
    }
}

void PhysicalDevice::PrintDetails()
{
    std::cout << "Device: " << physicalDeviceProperties.deviceName << std::endl;
    std::cout << " Type: " << physicalDeviceTypeString(physicalDeviceProperties.deviceType) << "\n";
    std::cout << " API: " << (physicalDeviceProperties.apiVersion >> 22) << "." << ((physicalDeviceProperties.apiVersion >> 12) & 0x3ff) << "." << (physicalDeviceProperties.apiVersion & 0xfff) << "\n";
}

const VkPhysicalDeviceProperties& PhysicalDevice::PhysicalDeviceProperties(void) const
{
    return physicalDeviceProperties;
}

VkPhysicalDevice PhysicalDevice::VulkanPhysicalDevice(void) const
{
    return physicalDevice;
}

const VkPhysicalDeviceFeatures& PhysicalDevice::PhysicalDeviceFeatures(void) const
{
    return physicalDeviceFeatures;
}

const VkPhysicalDeviceMemoryProperties& PhysicalDevice::PhysicalDeviceMemoryProperties(void) const
{
    return physicalDeviceMemoryProperties;
}


uint32_t PhysicalDevice::GetMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties) const
{
    // Iterate over all memory types available for the device used in this example
    for (uint32_t i = 0; i < physicalDeviceMemoryProperties.memoryTypeCount; i++) {
        if ((typeBits & 1) == 1) {
            if ((physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        typeBits >>= 1;
    }

    throw "Could not find a suitable memory type!";
}

void PhysicalDevice::PrintMemoryDetails() const
{
    std::cout << "=========" << std::endl;
    std::cout << "heaps" << std::endl;
    for (uint32_t i = 0; i < physicalDeviceMemoryProperties.memoryHeapCount; i++) {
        std::cout << "heap [" << i << "], size: " << tools::formatBytes(physicalDeviceMemoryProperties.memoryHeaps[i].size) << std::endl;

        if (physicalDeviceMemoryProperties.memoryHeaps[i].flags == 0) {
            std::cout << "NO FLAGS ";
        }
        if (physicalDeviceMemoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            std::cout << "VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ";
        }
        if (physicalDeviceMemoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_MULTI_INSTANCE_BIT) {
            std::cout << "VK_MEMORY_HEAP_MULTI_INSTANCE_BIT ";
        }
        if (physicalDeviceMemoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_MULTI_INSTANCE_BIT_KHR) {
            std::cout << "VK_MEMORY_HEAP_MULTI_INSTANCE_BIT_KHR ";
        }

        std::cout << std::endl << std::endl;
    }
    std::cout << "=========" << std::endl << std::endl;
    for (uint32_t i = 0; i < physicalDeviceMemoryProperties.memoryTypeCount; i++) {
        std::cout << "memory [" << i << "], heap: " << physicalDeviceMemoryProperties.memoryTypes[i].heapIndex << std::endl;

        if (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            std::cout << "VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ";
        }
        if (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            std::cout << "VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ";
        }
        if (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
            std::cout << "VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ";
        }
        if (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
            std::cout << "VK_MEMORY_PROPERTY_HOST_CACHED_BIT ";
        }
        if (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) {
            std::cout << "VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT ";
        }
        if (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_PROTECTED_BIT) {
            std::cout << "VK_MEMORY_PROPERTY_PROTECTED_BIT ";
        }
        if (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD) {
            std::cout << "VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD ";
        }
        if (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD) {
            std::cout << "VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD ";
        }
        if (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_RDMA_CAPABLE_BIT_NV) {
            std::cout << "VK_MEMORY_PROPERTY_RDMA_CAPABLE_BIT_NV ";
        }
        if (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags == 0) {
            std::cout << "NO FLAGS ";
        }
        std::cout << std::endl << std::endl;
    }
}

void PhysicalDevice::PrintQueueDetails(void) const
{
    std::cout << "Number of queues families " << queueFamilyProperties.size() << std::endl;
    for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++) {
        std::cout << "family [" << i << "]: num queues: " << queueFamilyProperties[i].queueCount;
        if (queueFamilyProperties[i].queueCount / 10 == 0) {
            std::cout << " ";
        }
        std::cout << ", support: ";
        if (queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            std::cout << "graphics ";
        }
        if (queueFamilyProperties[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
            std::cout << "transfer ";
        }
        if (queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            std::cout << "compute ";
        }
        std::cout << std::endl;
    }
}

const SL_VECTOR(VkQueueFamilyProperties)& PhysicalDevice::QueueFamilyProperties(void) const
{
    return queueFamilyProperties;
}

uint32_t PhysicalDevice::QueueFamilyIndexWithFlags(VkQueueFlags queueFlags) const
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++) {
        if (queueFamilyProperties[i].queueFlags & queueFlags) {
            return i;
        }
    }

    throw std::runtime_error("Could not find a matching queue family index");
}

uint32_t PhysicalDevice::QueueFamilyIndexWithFlags(VkQueueFlagBits queueFlags) const
{
    // Dedicated queue for compute
    // Try to find a queue family index that supports compute but not graphics
    if (queueFlags & VK_QUEUE_COMPUTE_BIT) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++) {
            if ((queueFamilyProperties[i].queueFlags & queueFlags) && ((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)) {
                return i;
            }
        }
    }

    // Dedicated queue for transfer
    // Try to find a queue family index that supports transfer but not graphics and compute
    if (queueFlags & VK_QUEUE_TRANSFER_BIT) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++) {
            if ((queueFamilyProperties[i].queueFlags & queueFlags) && ((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) && ((queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0)) {
                return i;
            }
        }
    }

    // For other queue types or if no separate compute queue is present, return the first one to support the requested flags
    for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++) {
        if (queueFamilyProperties[i].queueFlags & queueFlags) {
            return i;
        }
    }

    throw std::runtime_error("Could not find a matching queue family index");
}

bool PhysicalDevice::ExtensionSupported(const std::string& extension) const
{
    return (std::find(supportedExtensions.begin(), supportedExtensions.end(), extension) != supportedExtensions.end());
}

bool PhysicalDevice::GetSupportedDepthFormat(VkFormat& depthFormat) const
{
    // Since all depth formats may be optional, we need to find a suitable depth format to use
    // Start with the highest precision packed format
    SL_VECTOR(VkFormat) depthFormats = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };

    for (auto& format : depthFormats) {
        VkFormatProperties formatProps;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProps);
        // Format must support depth stencil attachment for optimal tiling
        if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depthFormat = format;
            return true;
        }
    }

    return false;
}

const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& PhysicalDevice::RayTracingPipelineProperties(void) const
{
    return rayTracingPipelineProperties;
}

const VkPhysicalDeviceAccelerationStructureFeaturesKHR& PhysicalDevice::RayTracingAccelerationStructureFeatures(void) const
{
    return accelerationStructureFeatures;
}

const Instance* PhysicalDevice::GetInstance(void) const
{
    return instance;
}
}
}
