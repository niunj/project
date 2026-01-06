// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "SilverLiningTypes.h"
#include "SLVector.h"

#include <vulkan/vulkan.h>

#include <string>
#include <array>

namespace SilverLining
{
    namespace Vulkan
    {
        class Device;
        class Texture;
        class StorageImage;

        class CubeMapRenderTexture
        {
        public:
            //! constructor
            CubeMapRenderTexture(int width, int height
                , bool floatingPoint, bool generateMipMaps, int _numBufferedFrames, const std::string& name, Device* device);

            //! destructor
            virtual ~CubeMapRenderTexture();
        public:
            //! make current
            bool MakeCurrent(VkCommandBuffer commandBuffer, int frameIndex, CubeFace face);

            //! bind
            bool Bind(VkCommandBuffer commandBuffer, int frameIndex);

            //! get texture
            VkImage GetImage(int frameIndex) const;

            VkFormat GetColorFormat(void) const;
        protected:
            //! internal
            uint32_t ToIndex(CubeFace face) const;

            //! internal
            const VkImageView& CubeFaceImageView(int frameIndex, int index) const;

            //! Set up depth-stencil
            void setupDepthStencil();
            void destroyDepthStencil(void);
        protected:
            static const uint32_t numFaces = 6;

            //! The actual image to which we will render
            SL_VECTOR(StorageImage*) colorImages;

            Device* device;

            const int width;
            const int height;

            int indexWhichIsCurrent = -1;

            // The rtt view of each cube map face
            typedef std::array<VkImageView, numFaces> CubeFaceImageViews;
            SL_VECTOR(CubeFaceImageViews) vecCubeFaceImageViews;

            VkFormat colorFormat;

            SL_VECTOR(StorageImage*) depthStencilImages;

            const int numBufferedFrames;
        };
    }
}