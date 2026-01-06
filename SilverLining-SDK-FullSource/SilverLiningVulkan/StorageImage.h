// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include <vulkan/vulkan.h>

#include "Image.h"

namespace SilverLining
{
   namespace Vulkan
   {
      class Device;

      //! An image directly made with flags for usage, memory type, tiling and sample count
      //! It is used for storing results of some intermediate rendering (e.g. ray tracing), blit screenshots, etc
      class StorageImage : public Image
      {
      public:
         //! constructor
         StorageImage(Device* device, VkFormat format, int width, int height
            , VkImageUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkImageTiling imageTiling, int sampleCount, int arrayLayers);

         //! destructor
         virtual ~StorageImage();
      private:
         //! not allowed
         StorageImage() = delete;
         //! not allowed
         StorageImage(const StorageImage& rhs) = delete;
         //! not allowed
         StorageImage& operator=(const StorageImage& rhs) = delete;
      public:
         //! get the view. this is lazily created
         const VkImageView& VulkanImageView(void) const;
      protected:

         VkImageView imageView = VK_NULL_HANDLE;
         const VkImageUsageFlags usageFlags = 0;
      };
   }
}

