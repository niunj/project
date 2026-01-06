// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "SLMap.h"
#include "MemAlloc.h"
#include "SilverLiningTypes.h"

#include <vulkan/vulkan.h>

#include <tuple>

namespace SilverLining
{
   namespace Vulkan
   {
      class Device;
      class Pipeline;
      class Texture;

      // This is because the pipeline can use _different_ textures
      // e.g. for cumulus cloud, the texture could be:
      // -GetCustomAtlasTexture
      // -GetAtlasTexture
      // -GetWispTexture
      class TextureDescriptorSetManager
      {
      public:
         //! constructor
         TextureDescriptorSetManager(Device* device);

         //! destructor
         virtual ~TextureDescriptorSetManager();
      public:
          //! Get or create a descriptor set for the texture in question.
          //! The texture is keyed on the pointer value.
         virtual VkDescriptorSet GetOrCreateFor(const SL_ORDERED_MAP(int, Texture*)& enabledTextures);

         virtual VkDescriptorSet GetOrCreateFor(const Pipeline* pipeline, const SL_ORDERED_MAP(int, Texture*)& enabledTextures, VkDescriptorSetLayout descriptorSetLayout1);

      protected:
         virtual bool Check(const SL_ORDERED_MAP(int, Texture*)& enabledTextures) const;
      protected:
         Device* device = nullptr;

         SL_MAP(Texture*, VkDescriptorSet) map1TextureToDescriptorSet;
         std::map<std::tuple<Texture*, Texture*>, VkDescriptorSet> map2TextureToDescriptorSet;

         static const int theMaxSets = 100;
         int numAllocs = 0;

         VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

         VkDescriptorSetLayout descriptorSetLayoutFor1Texture = VK_NULL_HANDLE;
         VkDescriptorSetLayout descriptorSetLayoutFor2Texture = VK_NULL_HANDLE;

         struct SamplerArrayDescriptorSet
         {
             VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
             VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
         };

         // Assume sampler array descriptor sets are unique to the pipeline (e.g. there is only one rain/snow/sleet per renderer)
         SL_MAP(const Pipeline*, SamplerArrayDescriptorSet) mapPipelineToSamplerArrayDescriptorSet;
      };
   }
}