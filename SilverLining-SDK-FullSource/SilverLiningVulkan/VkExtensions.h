// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include <vulkan/vulkan.h>

namespace SilverLining
{
    namespace Vulkan
    {
        class Device;

        //! Encapsulates and initializes KHR/EXT vulkan extensions
        class vkExtensions
        {
        public:
            //! constructor
            vkExtensions();

            //! destructor
            virtual ~vkExtensions();
        public:
            //! initialize
            void Initialize(Device* device);
        public:
            PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR = nullptr;
            PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
            PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
            PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
            PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
            PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR = nullptr;
            PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
            PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR = nullptr;
            PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR = nullptr;
            PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR = nullptr;

            // swap chain functions
            PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR = nullptr;
            PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
            PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR = nullptr;
            PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR = nullptr;

            PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR = nullptr;
            PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR = nullptr;
            PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR = nullptr;
            PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR = nullptr;
            PFN_vkQueuePresentKHR vkQueuePresentKHR = nullptr;

            // mesh shaders
            PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT = nullptr;

            // debugging extensions
            PFN_vkDebugMarkerSetObjectTagEXT vkDebugMarkerSetObjectTagEXT = VK_NULL_HANDLE;
            PFN_vkDebugMarkerSetObjectNameEXT vkDebugMarkerSetObjectNameEXT = VK_NULL_HANDLE;
            PFN_vkCmdDebugMarkerBeginEXT vkCmdDebugMarkerBeginEXT = VK_NULL_HANDLE;
            PFN_vkCmdDebugMarkerEndEXT vkCmdDebugMarkerEndEXT = VK_NULL_HANDLE;
            PFN_vkCmdDebugMarkerInsertEXT vkCmdDebugMarkerInsertEXT = VK_NULL_HANDLE;

            PFN_vkSetDebugUtilsObjectNameEXT       vkSetDebugUtilsObjectNameEXT = VK_NULL_HANDLE;
            PFN_vkSetDebugUtilsObjectTagEXT        vkSetDebugUtilsObjectTagEXT = VK_NULL_HANDLE;
            PFN_vkCmdBeginDebugUtilsLabelEXT     vkCmdBeginDebugUtilsLabelEXT = VK_NULL_HANDLE;
            PFN_vkCmdEndDebugUtilsLabelEXT       vkCmdEndDebugUtilsLabelEXT = VK_NULL_HANDLE;
            PFN_vkCmdInsertDebugUtilsLabelEXT    vkCmdInsertDebugUtilsLabelEXT = VK_NULL_HANDLE;

            // dynamic rendering
            PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR = nullptr;
            PFN_vkCmdEndRenderingKHR vkCmdEndRenderingKHR = nullptr;

            // blend functions
            PFN_vkCmdSetColorBlendEnableEXT vkCmdSetColorBlendEnableEXT = nullptr;
            PFN_vkCmdSetColorBlendEquationEXT vkCmdSetColorBlendEquationEXT = nullptr;

            // lines
            PFN_vkCmdSetLineRasterizationModeEXT vkCmdSetLineRasterizationModeEXT = nullptr;

            PFN_vkGetImageMemoryRequirements2KHR  vkGetImageMemoryRequirements2KHR = nullptr;
            PFN_vkGetBufferMemoryRequirements2KHR vkGetBufferMemoryRequirements2KHR = nullptr;
        protected:
            bool initialized = false;
        };
    }
}