// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "MemAlloc.h"
#include "SilverLiningTypes.h"

#include <vulkan/vulkan.h>

namespace SilverLining
{
    namespace Vulkan
    {
        class Device;
        class BlendUtils
        {
        public:
            static VkBlendFactor ToVulkanBlendFactor(BlendFactor incomingValue, Device* device);
            static VkBlendOp ToVulkanBlendOp(BlendOp op, Device* device);
        };
    }
}