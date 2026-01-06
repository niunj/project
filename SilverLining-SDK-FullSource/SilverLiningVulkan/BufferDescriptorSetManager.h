// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "SLMap.h"
#include "MemAlloc.h"
#include "SilverLiningTypes.h"

#include <vulkan/vulkan.h>

#include <tuple>
#include <map>

namespace SilverLining
{
    namespace Vulkan
    {
        class Device;
        class RawBuffer;

        class BufferDescriptorSetManager
        {
        public:
            //! constructor
            BufferDescriptorSetManager(Device* device);

            //! destructor
            virtual ~BufferDescriptorSetManager();
        public:
            // the binding is encoded in the VkDescriptorSetLayout as well
            virtual VkDescriptorSet GetOrCreateFor(const RawBuffer* buffer, VkDescriptorSetLayout descriptorSetLayout2);
        private:

            struct BufferDescriptorSet
            {
                VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
                VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
            };

            SL_MAP(const RawBuffer*, BufferDescriptorSet) mapBufferToDescriptorSet;

            Device* device = nullptr;
        };
    }
}
