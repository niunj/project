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

        class Image
        {
        public:
            //! constructor
            Image(Device* device);

            //! destructor
            virtual ~Image();
        public:
            //! load from file
            bool LoadFromFile(const std::string& fileName, bool srgb);

            //! load from file. cube map
            bool LoadFromFileCubeMap(const std::string& fileName);

            //! load from buffer (2d)
            bool LoadFromBuffer(void* buffer, VkDeviceSize bufferSize, VkFormat format, int width, int height, const SL_VECTOR(int)& mipMapDataOffsets
            , bool mipMap);

            //! load from buffer (3d)
            bool LoadFromBuffer(void* buffer, VkDeviceSize bufferSize, VkFormat format, int width, int height, int depth, const SL_VECTOR(int)& mipMapDataOffsets
                , bool mipMap);
                
            //! access the device
            const Device* GetDevice(void) const;

            //! query parameters
            int NumMipMapLevels(void) const;
            int Width(void) const;
            int Height(void) const;
            int Depth(void) const;
            bool IsCubeMap(void) const;

            //! get Vulkan internal
            VkFormat VulkanFormat(void) const;
            VkImage VulkanImage(void) const;
            VkDeviceSize SizeInBytes(void) const;

            //! set the name
            void SetName(const std::string& name);
            const std::string& GetName(void) const;
        public:
            //! convert an integer sample count to the flag bits that is recognized by Vulkan
            static VkSampleCountFlagBits ToSampleCountFlagBits(int sampleCount);
        protected:
            //! internal
            bool CopyFromFileIntoImage(const std::string& fileName, bool srgb, uint32_t numFaces);

            //! internal
            bool CopyFromRawDataIntoImage(void* buffer, VkDeviceSize bufferSize, const SL_VECTOR(int)& mipMapDataOffsets, uint32_t numFaces);

            //! internal
            void AllocateImageAndMemory(VkImageUsageFlags usageFlags
                , VkMemoryPropertyFlags memoryPropertyFlags
                , VkImageTiling imageTiling
                , int arrayLayers, int sampleCount);

            //! internal
            void GenerateMipMaps(void);

        protected:
            Device* device;

            VkFormat format = VK_FORMAT_UNDEFINED;

            VkImage image = VK_NULL_HANDLE;

            int width = 0;
            int height = 0;
            int depth = 1;
            int numMipMapLevels = 0;
            bool isCubeMap = false;

            std::string name;
        };
    }
}