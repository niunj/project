// Copyright (c) 2004-2024 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "SLMap.h"
#include "MemAlloc.h"
#include "SilverLiningTypes.h"

#include <vulkan/vulkan.h>

namespace SilverLining
{
    namespace Vulkan
    {
        class Device;
        class Image;

        class SamplerManager
        {
        public:
            SamplerManager(Device* device);
            virtual ~SamplerManager();
        public:
            VkSampler GetOrCreateSampler(VkFilter minFilter, VkFilter magFilter
                , VkSamplerAddressMode addressModeU, VkSamplerAddressMode addressModeV, VkSamplerAddressMode addressModeW
                , const Image* image);
        private:
            VkSampler GetOrCreateSampler(const VkSamplerCreateInfo& samplerInfo);

            //! for debugging
            bool AreSame(const VkSamplerCreateInfo& samplerInfo, unsigned int samplerHash);

        private:
            SL_MAP(unsigned int, VkSampler) mapSamplerHashToSampler;
            Device* device = nullptr;
        };
    }
}