// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "SLVector.h"
#include <vulkan/vulkan.h>

#include <string>

namespace SilverLining
{
    namespace Vulkan
    {
        class Device;
        class Texture;
        class StorageImage;
        class SamplerManager;

        class RenderToTexture
        {
        public:
            //! constructor
            RenderToTexture(int width, int height, VkFormat _colorFormat, int _numBufferedFrames, const std::string& name, Device* device, SamplerManager* samplerManager);

            //! destructor
            virtual ~RenderToTexture();
        public:
            void MakeCurrent(VkCommandBuffer commandBuffer, int frameIndex);

            void Bind(VkCommandBuffer commandBuffer, int frameIndex);

            //! get the texture
            Texture* GetTexture(int frameIndex);

            //! get the texture. const
            const Texture* GetTexture(int frameIndex) const;

            //! get image
            VkImage GetImage(int frameIndex) const;

            VkFormat GetColorFormat(void) const;
        protected:
            void MakeCurrentDynamicRendering(VkCommandBuffer commandBuffer, int frameIndex);
            void BindDynamicRendering(VkCommandBuffer commandBuffer, int frameIndex);

            void MakeCurrentRenderPass(VkCommandBuffer commandBuffer, int frameIndex);
            void BindRenderPass(VkCommandBuffer commandBuffer, int frameIndex);
        protected:
            //! The format
            const VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

            //! The actual image to which we will render
            SL_VECTOR(StorageImage*) colorImages;

            //! The texture corresponding to the image above
            SL_VECTOR(Texture*) textures;

            Device* device;

            const int width;
            const int height;

            const bool dynamicRendering = true;
            VkRenderPass renderPass = VK_NULL_HANDLE;
            SL_VECTOR(VkFramebuffer) frameBuffers;

            const int numBufferedFrames;

            SamplerManager* samplerManager = nullptr;
        };
    }
}