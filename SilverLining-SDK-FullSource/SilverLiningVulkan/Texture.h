// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace SilverLining
{
    namespace Vulkan
    {
        class Device;
        class Image;
        class SamplerManager;

        enum SamplerType
        {
             ST_2D = 0
           , ST_3D = 1
           , ST_CUBE = 2
        };

        class Texture
        {
        public:
            //! constructor
            Texture(Image* image, VkFilter minFilter, VkFilter magFilter
               , VkSamplerAddressMode addressModeU, VkSamplerAddressMode addressModeV, VkSamplerAddressMode addressModeW
               , VkComponentMapping componentMapping, SamplerManager* samplerManager);

            //! destructor
            virtual ~Texture();
        public:
            //! descriptor
            const VkDescriptorImageInfo& Descriptor(void) const;

            //! descriptor pointer
            const VkDescriptorImageInfo* DescriptorPtr(void) const;

            //! get the image
            const Image* GetImage(void) const;
        public:
            //! default swizzle (rgba)
            static VkComponentMapping DefaultComponentMapping(void);
        protected:
            //! create sampler
            void CreateSampler(VkFilter minFilter, VkFilter magFilter
               , VkSamplerAddressMode addressModeU, VkSamplerAddressMode addressModeV, VkSamplerAddressMode addressModeW);

            //! create image view
            void CreateImageView(VkComponentMapping componentMapping);
        protected:
            const Image* image = nullptr;
            VkSampler sampler = VK_NULL_HANDLE;
            VkImageView imageView = VK_NULL_HANDLE;
            VkDescriptorImageInfo descriptor{};
            SamplerManager* samplerManager = nullptr;
        };
    }
}
