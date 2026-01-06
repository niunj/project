// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "SLAssert.h"

#include "TextureDescriptorSetManager.h"
#include "VulkanInitializers.h"
#include "VulkanDebug.h"
#include "Device.h"
#include "Texture.h"

namespace SilverLining
{
namespace Vulkan
{
TextureDescriptorSetManager::TextureDescriptorSetManager(Device* _device)
    : device(_device)
{
    // The number of sets is the same as the number of descriptors, since each set has 1 descriptor and that is unique to the texture
    SL_VECTOR(VkDescriptorPoolSize) poolSizes;

    // total = theMaxSets * (1 + 2) (for a set with 1 texture and 2 textures respectively)
    //       = theMaxSets * (3)
    poolSizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, theMaxSets*3 });

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = vkInitializers::descriptorPoolCreateInfo(poolSizes, theMaxSets);
    VK_CHECK_RESULT(vkCreateDescriptorPool(device->VulkanDevice(), &descriptorPoolCreateInfo, nullptr, &descriptorPool));

    // for 1 texture
    {
        SL_VECTOR(VkDescriptorSetLayoutBinding) setBindings{ vkInitializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0) };
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = vkInitializers::descriptorSetLayoutCreateInfo(setBindings.data(), static_cast<uint32_t>(setBindings.size()));
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->VulkanDevice(), &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayoutFor1Texture));
    }

    // for 2 textures
    {
        SL_VECTOR(VkDescriptorSetLayoutBinding) setBindings{ vkInitializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0)
                , vkInitializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1) };
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = vkInitializers::descriptorSetLayoutCreateInfo(setBindings.data(), static_cast<uint32_t>(setBindings.size()));
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->VulkanDevice(), &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayoutFor2Texture));
    }
}

TextureDescriptorSetManager::~TextureDescriptorSetManager()
{
    //https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/vkDestroyDescriptorPool.html
    // When a pool is destroyed, all descriptor sets allocated from the pool are implicitly freed and become invalid.
    // Descriptor sets allocated from a given pool do not need to be freed before destroying that descriptor pool.

    vkDestroyDescriptorSetLayout(device->VulkanDevice(), descriptorSetLayoutFor1Texture, nullptr);
    vkDestroyDescriptorSetLayout(device->VulkanDevice(), descriptorSetLayoutFor2Texture, nullptr);
    vkDestroyDescriptorPool(device->VulkanDevice(), descriptorPool, nullptr);

    for (auto& keyVal : mapPipelineToSamplerArrayDescriptorSet) {
        vkDestroyDescriptorPool(device->VulkanDevice(), keyVal.second.descriptorPool, nullptr);
    }
}

bool TextureDescriptorSetManager::Check(const SL_ORDERED_MAP(int, Texture*)& enabledTextures) const
{
    if (enabledTextures.size() == 1) {
        SL_ASSERT(enabledTextures.begin()->first == 0);
        if (enabledTextures.begin()->first != 0) {
            device->log(LogLevel::SL_LOG_LEVEL_WARN) << __FUNCTION__ << "expected texture unit to be 0" << std::endl;
            return false;
        }

        return true;
    } else if (enabledTextures.size() == 2) {
        int binding = 0;
        for (auto& keyVal : enabledTextures) {
            if (keyVal.first != binding) {
                device->log(LogLevel::SL_LOG_LEVEL_WARN) << __FUNCTION__ << "bindings not correct" << std::endl;
                return false;
            }
            ++binding;
        }
        return true;
    } else {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << __FUNCTION__ << "unsupported combo of textures/binding points" << std::endl;
        return false;
    }
}

VkDescriptorSet TextureDescriptorSetManager::GetOrCreateFor(const SL_ORDERED_MAP(int, Texture*)& enabledTextures)
{
    if (enabledTextures.size() == 0) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << __FUNCTION__ << "enabledTextures.size() == 0" << std::endl;
        return VK_NULL_HANDLE;
    }

    if (numAllocs == theMaxSets) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << __FUNCTION__ << "numAllocs == theMaxSets" << std::endl;
        return VK_NULL_HANDLE;
    }

    bool ok = Check(enabledTextures);
    if (!ok) {
        return VK_NULL_HANDLE;
    }

    if (enabledTextures.size() == 1) {
        Texture* texture = enabledTextures.begin()->second;

        auto it = map1TextureToDescriptorSet.find(texture);
        if (it != map1TextureToDescriptorSet.end()) {
            return it->second;
        }

        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = vkInitializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayoutFor1Texture, 1);
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        vkAllocateDescriptorSets(device->VulkanDevice(), &descriptorSetAllocateInfo, &descriptorSet);

        SL_VECTOR(VkWriteDescriptorSet) writeDescriptorSets;
        writeDescriptorSets.push_back(vkInitializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, texture->DescriptorPtr()));
        vkUpdateDescriptorSets(device->VulkanDevice(), static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);

        map1TextureToDescriptorSet.insert({ texture, descriptorSet });

        ++numAllocs;

        return descriptorSet;
    } else if (enabledTextures.size() == 2) {
        SL_VECTOR(Texture*) textures;
        for (auto& keyVal : enabledTextures) {
            textures.push_back(keyVal.second);
        }
        std::tuple<Texture*, Texture*> tupleTextures;
        std::get<0>(tupleTextures) = textures[0];
        std::get<1>(tupleTextures) = textures[1];

        auto it = map2TextureToDescriptorSet.find(tupleTextures);
        if (it != map2TextureToDescriptorSet.end()) {
            return it->second;
        }

        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = vkInitializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayoutFor2Texture, 1);
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        vkAllocateDescriptorSets(device->VulkanDevice(), &descriptorSetAllocateInfo, &descriptorSet);

        SL_VECTOR(VkWriteDescriptorSet) writeDescriptorSets;
        writeDescriptorSets.push_back(vkInitializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, textures[0]->DescriptorPtr()));
        writeDescriptorSets.push_back(vkInitializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, textures[1]->DescriptorPtr()));
        vkUpdateDescriptorSets(device->VulkanDevice(), static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);

        map2TextureToDescriptorSet.insert({ tupleTextures, descriptorSet });

        ++numAllocs;

        return descriptorSet;
    }

    device->log(LogLevel::SL_LOG_LEVEL_WARN) << __FUNCTION__ << "should not have reached here..." << std::endl;
    return VK_NULL_HANDLE;
}

VkDescriptorSet TextureDescriptorSetManager::GetOrCreateFor(const Pipeline* pipeline, const SL_ORDERED_MAP(int, Texture*)& enabledTextures, VkDescriptorSetLayout descriptorSetLayout1)
{
    auto it = mapPipelineToSamplerArrayDescriptorSet.find(pipeline);

    if (it != mapPipelineToSamplerArrayDescriptorSet.end()) {
        return it->second.descriptorSet;
    }

    VkDescriptorPool samplersAreArrayDescriptorPool = VK_NULL_HANDLE;
    SL_VECTOR(VkDescriptorPoolSize) poolSizes = { vkInitializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)enabledTextures.size()) };
    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = vkInitializers::descriptorPoolCreateInfo(poolSizes, 1);
    VK_CHECK_RESULT(vkCreateDescriptorPool(device->VulkanDevice(), &descriptorPoolCreateInfo, nullptr, &samplersAreArrayDescriptorPool));

    uint32_t variableDescCounts[] = { uint32_t(enabledTextures.size()) };
    VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescriptorCountAllocInfo = {};
    variableDescriptorCountAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    variableDescriptorCountAllocInfo.descriptorSetCount = 1;
    variableDescriptorCountAllocInfo.pDescriptorCounts = variableDescCounts;

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = vkInitializers::descriptorSetAllocateInfo(samplersAreArrayDescriptorPool, &descriptorSetLayout1, 1);
    descriptorSetAllocateInfo.pNext = &variableDescriptorCountAllocInfo;

    VkDescriptorSet samplersAreArrayTextureDescriptorSet = VK_NULL_HANDLE;
    vkAllocateDescriptorSets(device->VulkanDevice(), &descriptorSetAllocateInfo, &samplersAreArrayTextureDescriptorSet);

    VkWriteDescriptorSet writeDescriptorSet{};
    SL_VECTOR(VkDescriptorImageInfo) textureDescriptors;
    textureDescriptors.reserve(enabledTextures.size());
    for (const auto& keyVal : enabledTextures) {
        const Texture* texture = keyVal.second;
        textureDescriptors.push_back(texture->Descriptor());
    }

    writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.dstBinding = 0;
    writeDescriptorSet.dstArrayElement = 0;
    writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDescriptorSet.descriptorCount = static_cast<uint32_t>(enabledTextures.size());
    writeDescriptorSet.pBufferInfo = 0;
    writeDescriptorSet.dstSet = samplersAreArrayTextureDescriptorSet;
    writeDescriptorSet.pImageInfo = textureDescriptors.data();

    vkUpdateDescriptorSets(device->VulkanDevice(), 1, &writeDescriptorSet, 0, nullptr);

    SamplerArrayDescriptorSet sads;
    sads.descriptorSet = samplersAreArrayTextureDescriptorSet;
    sads.descriptorPool = samplersAreArrayDescriptorPool;
    mapPipelineToSamplerArrayDescriptorSet.insert({ pipeline, sads});

    return samplersAreArrayTextureDescriptorSet;
}
}
}
