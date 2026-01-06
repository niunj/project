// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "SLVector.h"
#include <vulkan/vulkan.h>

#include <string>

namespace SilverLining
{
    namespace Vulkan
    {
        // Encapsulates a vulkan instance
        class Instance
        {
        public:
            //! constructor
            Instance(VkInstance instance);

            //! destructor
            virtual ~Instance();
        public:
            //! get
            VkInstance VulkanInstance(void) const;
        protected:
            //! internal
            bool EnumeratePhysicalDevices();

            //! The physical devices available for this instance
            const SL_VECTOR(VkPhysicalDevice)& PhysicalDevices(void) const;
        protected:
            VkInstance instance = VK_NULL_HANDLE;

            SL_VECTOR(VkPhysicalDevice) physicalDevices;
        };
    }
}