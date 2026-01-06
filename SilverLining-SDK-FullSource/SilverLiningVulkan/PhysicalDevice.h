// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once
#include "Instance.h"

namespace SilverLining
{
    namespace Vulkan
    {
        class Instance;

        //! Encapsulates a vulkan physical device
        class PhysicalDevice
        {
        public:
            //! constructor
            PhysicalDevice(const Instance* instance, VkPhysicalDevice physicalDevice);

            //! destructor
            virtual ~PhysicalDevice();
        public:
            //! print details
            void PrintDetails(void);

            //! get
            VkPhysicalDevice VulkanPhysicalDevice(void) const;

            //! all the properties (initialized in constructor)
            const VkPhysicalDeviceProperties& PhysicalDeviceProperties(void) const;

            //! All the features supported (initialized in constructor)
            const VkPhysicalDeviceFeatures& PhysicalDeviceFeatures(void) const;

            //! All the memory properties supported (initialized in constructor)
            const VkPhysicalDeviceMemoryProperties& PhysicalDeviceMemoryProperties(void) const;

            //! memory type index
            uint32_t GetMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties) const;

            //! queue family index that supports the flag bits passed in
            //! This is not an or'ed combination of VkQueueFlagBits
            uint32_t QueueFamilyIndexWithFlags(VkQueueFlagBits queueFlags) const;

            //! queue family index that supports all the flag bits passed in
            uint32_t QueueFamilyIndexWithFlags(VkQueueFlags queueFlags) const;

            //! get
            const SL_VECTOR(VkQueueFamilyProperties)& QueueFamilyProperties(void) const;

            //! utility
            void PrintQueueDetails(void) const;

            //! utility
            void PrintMemoryDetails(void) const;

            //! whether an extension is supported
            bool ExtensionSupported(const std::string& extension) const;

            //! whether a depth format is supported
            bool GetSupportedDepthFormat(VkFormat& depthFormat) const;

            //! ray tracing pipeline properties.
            const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& RayTracingPipelineProperties(void) const;

            //! ray tracing acceleration properties.
            const VkPhysicalDeviceAccelerationStructureFeaturesKHR& RayTracingAccelerationStructureFeatures(void) const;

            //! Get the parent instance
            const Instance* GetInstance(void) const;

        protected:
            // Stores physical device properties (for e.g. checking device limits)
            VkPhysicalDeviceProperties physicalDeviceProperties{};

            // Stores the features available on the selected physical device (for e.g. checking if a feature is available)
            VkPhysicalDeviceFeatures physicalDeviceFeatures{};

            // Stores all available memory (type) properties for the physical device
            VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties{};

            VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

            SL_VECTOR(std::string) supportedExtensions;

            SL_VECTOR(VkQueueFamilyProperties) queueFamilyProperties;

            VkPhysicalDeviceRayTracingPipelinePropertiesKHR  rayTracingPipelineProperties{};

            VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};

            VkPhysicalDeviceMeshShaderPropertiesEXT meshShaderProperties{};

            // back pointer to parent instance
            const Instance* instance = nullptr;
        };
    }
}
