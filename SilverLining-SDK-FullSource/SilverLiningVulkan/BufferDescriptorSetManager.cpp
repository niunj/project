// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "SLAssert.h"

#include "BufferDescriptorSetManager.h"
#include "Device.h"
#include "VulkanBuffer.h"
#include "VulkanInitializers.h"
#include "VulkanDebug.h"

namespace SilverLining
{
namespace Vulkan
{
BufferDescriptorSetManager::BufferDescriptorSetManager(Device* _device)
    : device(_device)
{

}

BufferDescriptorSetManager::~BufferDescriptorSetManager()
{
    // When a pool is destroyed, all descriptor sets allocated from the pool are implicitly freed and become invalid.
    // Descriptor sets allocated from a given pool do not need to be freed before destroying that descriptor pool.
    for (auto& keyVal : mapBufferToDescriptorSet) {
        vkDestroyDescriptorPool(device->VulkanDevice(), keyVal.second.descriptorPool, nullptr);
    }
}

VkDescriptorSet BufferDescriptorSetManager::GetOrCreateFor(const RawBuffer* buffer, VkDescriptorSetLayout descriptorSetLayout2)
{
    auto it = mapBufferToDescriptorSet.find(buffer);
    if (it != mapBufferToDescriptorSet.end()) {
        return it->second.descriptorSet;
    }

    SL_VECTOR(VkDescriptorPoolSize) poolSizes;
    if (buffer->UsageFlags()&VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
        poolSizes.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 });
    } else if (buffer->UsageFlags()&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {
        poolSizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 });
    }

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = vkInitializers::descriptorPoolCreateInfo(poolSizes, 1);
    VK_CHECK_RESULT(vkCreateDescriptorPool(device->VulkanDevice(), &descriptorPoolCreateInfo, nullptr, &descriptorPool));

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = vkInitializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout2, 1);
    vkAllocateDescriptorSets(device->VulkanDevice(), &descriptorSetAllocateInfo, &descriptorSet);

    SL_VECTOR(VkWriteDescriptorSet) writeDescriptorSets;

    VkDescriptorType descriptorType;
    if (buffer->UsageFlags() & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
        descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    } else if (buffer->UsageFlags() & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {
        descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }

    VkDescriptorBufferInfo descriptor = VkDescriptorBufferInfo{ buffer->VulkanBuffer(), 0, buffer->SizeInBytes() };
    writeDescriptorSets.push_back(vkInitializers::writeDescriptorSet(descriptorSet, descriptorType, 0, &descriptor));

    vkUpdateDescriptorSets(device->VulkanDevice(), static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);

    BufferDescriptorSet bds;
    bds.descriptorSet = descriptorSet;
    bds.descriptorPool = descriptorPool;
    mapBufferToDescriptorSet.insert({ buffer, bds});

    return descriptorSet;
}
}
}
