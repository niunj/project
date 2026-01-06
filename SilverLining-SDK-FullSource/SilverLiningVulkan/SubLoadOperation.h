// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once
#include "SLVector.h"
#include <vulkan/vulkan.h>
namespace SilverLining
{
   namespace Vulkan
   {
      class Device;
      class Image;
      class RawBuffer;

      class SubLoadOperation
      {
      public:
         //! constructor
         SubLoadOperation(Image* image);

         //! destructor
         virtual ~SubLoadOperation();
      public:
         void SubLoad(const unsigned char* pSrcData, int width, int height, int depth,
            int x, int y, int z);
      public:
         Device* device = nullptr;
         Image* image = nullptr;

         RawBuffer* stagingBuffer = nullptr;
         VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
         VkFence fence = VK_NULL_HANDLE;

         uint8_t* pDstData = nullptr;

         SL_VECTOR(VkBufferImageCopy) bufferCopyRegions;
         const VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
      };
   }
}