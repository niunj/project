// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "SLAssert.h"
#include "Pipeline.h"
#include "Shader.h"
#include "ShaderReflection.h"
#include "VulkanInitializers.h"
#include "VulkanDebug.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "VulkanBuffer.h"
#include "Vertex.h"
#include "VulkanContext.h"
#include "TextureDescriptorSetManager.h"
#include "BufferDescriptorSetManager.h"
#include "BlendUtils.h"
#include "BlendState.h"

#include <sstream>

namespace SilverLining
{
namespace Vulkan
{
Pipeline::Pipeline(VulkanContext* _parent, bool _dynamicUbo, int _maxUboCount, bool _dynamicSbo, int _maxSsboCount, VkPrimitiveTopology _primitiveTopology, int _numBufferedFrames, const std::string& _name)
    : device(_parent->device)
    , parent(_parent)
    , dynamicUbo(_dynamicUbo)
    , maxUboCount(_maxUboCount)
    , dynamicSbo(_dynamicSbo)
    , maxSsboCount(_maxSsboCount)
    , primitiveTopology(_primitiveTopology)
    , name(_name)
    , numBufferedFrames(_numBufferedFrames)
{

}

Pipeline::~Pipeline()
{
    for (auto& keyVal : mapHashToPipelines) {
        vkDestroyPipeline(device->VulkanDevice(), keyVal.second, nullptr);
    }

    vkDestroyPipelineLayout(device->VulkanDevice(), pipelineLayout, nullptr);

    // This is not necessary. Destroying pool destroys allocated descriptor sets
    // Incidentally, it also throws a validation error because we did not create the pool with VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
    //vkFreeDescriptorSets(device->VulkanDevice(), descriptorPoolSet0, 1, &descriptorSet0);
    vkDestroyDescriptorPool(device->VulkanDevice(), descriptorPoolSet0, nullptr);
    vkDestroyDescriptorSetLayout(device->VulkanDevice(), descriptorSetLayout0, nullptr);

    vkDestroyDescriptorSetLayout(device->VulkanDevice(), descriptorSetLayout1, nullptr);

    if (descriptorSetLayout2) {
        vkDestroyDescriptorSetLayout(device->VulkanDevice(), descriptorSetLayout2, nullptr);
    }

    for (auto& keyVal : mapShaderToShaderReflection) {
        delete keyVal.first;
        delete keyVal.second;
    }
    for (auto& sl_ShaderParameters_UBO : sl_ShaderParameters_UBOs) {
        if (sl_ShaderParameters_UBO.buffer) {
            sl_ShaderParameters_UBO.buffer->UnMap();
            delete sl_ShaderParameters_UBO.buffer;
        }
    }
}

void Pipeline::Add(Shader* shader, ShaderReflection* shaderReflection)
{
    mapShaderToShaderReflection.insert({ shader, shaderReflection });
}

static VkSampleCountFlagBits toSampleCountFlagBits(int sampleCount)
{
    if (sampleCount == 1) return VK_SAMPLE_COUNT_1_BIT;
    if (sampleCount == 2) return VK_SAMPLE_COUNT_2_BIT;
    if (sampleCount == 4) return VK_SAMPLE_COUNT_4_BIT;
    if (sampleCount == 8) return VK_SAMPLE_COUNT_8_BIT;
    if (sampleCount == 16) return VK_SAMPLE_COUNT_16_BIT;

    return VK_SAMPLE_COUNT_1_BIT;
}

VkPipeline Pipeline::CreatePipeline(const BlendState& blendState, FrontFace frontFace, VkFormat colorFormat)
{
    SL_VECTOR(VkVertexInputBindingDescription) vertexInputBindingDescriptions = { vkInitializers::vertexInputBindingDescription(0, sizeof(SilverLining::Vertex), VK_VERTEX_INPUT_RATE_VERTEX) };

    const VulkanInitInfo& info = parent->GetInfo();

    int location = 0;
    SL_VECTOR(VkVertexInputAttributeDescription) vertexInputAttributeDescriptions = {
        vkInitializers::vertexInputAttributeDescription(0, location++, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, x))
        , vkInitializers::vertexInputAttributeDescription(0, location++, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, r))
        , vkInitializers::vertexInputAttributeDescription(0, location++, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, u))
    };


    // input state
    VkPipelineVertexInputStateCreateInfo vertexInputState = vkInitializers::pipelineVertexInputStateCreateInfo(vertexInputBindingDescriptions, vertexInputAttributeDescriptions);

    // input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vkInitializers::pipelineInputAssemblyStateCreateInfo(primitiveTopology, 0, VK_FALSE);

    // viewport state
    VkPipelineViewportStateCreateInfo viewportState = vkInitializers::pipelineViewportStateCreateInfo(1, 1, 0);

    VkFrontFace vulkanFrontFace = (frontFace == SL_FRONT_FACE_COUNTER_CLOCKWISE) ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    // rasterization state
    VkPipelineRasterizationStateCreateInfo rasterizationState = vkInitializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, vulkanFrontFace, 0);

    // multisample state
    VkPipelineMultisampleStateCreateInfo multisampleState = vkInitializers::pipelineMultisampleStateCreateInfo(toSampleCountFlagBits(info.sampleCount), 0);

    // depth stencil
    const VkCompareOp depthCompareOp = info.nearDepth < info.farDepth ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_GREATER_OR_EQUAL;
    VkPipelineDepthStencilStateCreateInfo depthStencilState = vkInitializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, depthCompareOp);

    // blend attachment
    VkPipelineColorBlendAttachmentState blendAttachmentState = vkInitializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

    blendAttachmentState.blendEnable = blendState.enabled ? VK_TRUE : VK_FALSE;

    blendAttachmentState.srcColorBlendFactor = BlendUtils::ToVulkanBlendFactor(blendState.srcColorBlendFactor, device);
    blendAttachmentState.dstColorBlendFactor = BlendUtils::ToVulkanBlendFactor(blendState.dstColorBlendFactor, device);
    blendAttachmentState.colorBlendOp = BlendUtils::ToVulkanBlendOp(blendState.colorBlendOp, device);

    blendAttachmentState.srcAlphaBlendFactor = BlendUtils::ToVulkanBlendFactor(blendState.srcAlphaBlendFactor, device);
    blendAttachmentState.dstAlphaBlendFactor = BlendUtils::ToVulkanBlendFactor(blendState.dstAlphaBlendFactor, device);
    blendAttachmentState.alphaBlendOp = BlendUtils::ToVulkanBlendOp(blendState.alphaBlendOp, device);

    VkPipelineColorBlendStateCreateInfo colorBlendState = vkInitializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

    // dynamic states
    SL_VECTOR(VkDynamicState) dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
                                                      , VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE/*, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP*/
                                                    };
    if (primitiveTopology== VK_PRIMITIVE_TOPOLOGY_LINE_STRIP) {
        dynamicStateEnables.push_back(VK_DYNAMIC_STATE_LINE_WIDTH);
        //dynamicStateEnables.push_back(VK_DYNAMIC_STATE_LINE_RASTERIZATION_MODE_EXT);
    }

    VkPipelineDynamicStateCreateInfo dynamicState = vkInitializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0);

    VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo = vkInitializers::graphicsPipelineCreateInfo(pipelineLayout, info.renderPass);

    graphicsPipelineCreateInfo.pVertexInputState = &vertexInputState;
    graphicsPipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
    graphicsPipelineCreateInfo.pViewportState = &viewportState;
    graphicsPipelineCreateInfo.pRasterizationState = &rasterizationState;
    graphicsPipelineCreateInfo.pMultisampleState = &multisampleState;
    graphicsPipelineCreateInfo.pDepthStencilState = &depthStencilState;
    graphicsPipelineCreateInfo.pColorBlendState = &colorBlendState;
    graphicsPipelineCreateInfo.pDynamicState = &dynamicState;

    SL_VECTOR(VkPipelineShaderStageCreateInfo) shaderStageInfos;
    for (auto& keyVal : mapShaderToShaderReflection) {
        const Shader* shader = keyVal.first;
        shaderStageInfos.push_back(shader->PipelineShaderStageCreateInfo());
    }
    graphicsPipelineCreateInfo.stageCount = (uint32_t)shaderStageInfos.size();
    graphicsPipelineCreateInfo.pStages = shaderStageInfos.data();

    VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
    if (info.renderPass == VK_NULL_HANDLE) {
        pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pipelineRenderingCreateInfo.colorAttachmentCount = 1;
        pipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
        pipelineRenderingCreateInfo.depthAttachmentFormat = info.depthFormat;
        pipelineRenderingCreateInfo.stencilAttachmentFormat = info.depthFormat;
        graphicsPipelineCreateInfo.pNext = &pipelineRenderingCreateInfo;
    }

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device->VulkanDevice(), VK_NULL_HANDLE, 1, &graphicsPipelineCreateInfo, nullptr, &pipeline));
    return pipeline;
}

const uint32_t CLEAR_ONE_BIT = 1;
const uint32_t CLEAR_TWO_BIT = 3;
const uint32_t CLEAR_THREE_BIT = 7;
const uint32_t CLEAR_FOUR_BIT = 15;

BlendState Pipeline::HashToBlend(uint32_t hashValue) const
{
    BlendState recoveredBlendState;

    recoveredBlendState.enabled = (hashValue & (CLEAR_ONE_BIT << 0)) >> 0;

    recoveredBlendState.srcColorBlendFactor = (BlendFactor)((hashValue & (CLEAR_FOUR_BIT << 1)) >> 1);
    recoveredBlendState.dstColorBlendFactor = (BlendFactor)((hashValue & (CLEAR_FOUR_BIT << 5)) >> 5);
    recoveredBlendState.colorBlendOp        =    (BlendOp)((hashValue & (CLEAR_THREE_BIT << 9)) >> 9);

    recoveredBlendState.srcAlphaBlendFactor = (BlendFactor)((hashValue & (CLEAR_FOUR_BIT << 12)) >> 12);
    recoveredBlendState.dstAlphaBlendFactor = (BlendFactor)((hashValue & (CLEAR_FOUR_BIT << 16)) >> 16);
    recoveredBlendState.alphaBlendOp           = (BlendOp)((hashValue & (CLEAR_THREE_BIT << 20)) >> 20);

    return recoveredBlendState;
}

FrontFace Pipeline::HashToFrontFace(uint32_t hashValue) const
{
    FrontFace face = (FrontFace)((hashValue & (CLEAR_ONE_BIT << 23)) >> 23);

    return face;
}

uint32_t Pipeline::GetHash(const BlendState& blendState, FrontFace frontFace) const
{
    uint32_t hashValue = 0;
    hashValue |= (blendState.enabled) ? 1 : 0; // bit 0

    hashValue |= blendState.srcColorBlendFactor << 1; // bits 1-4
    hashValue |= blendState.dstColorBlendFactor << 5; // bits 5-8
    hashValue |= blendState.colorBlendOp        << 9; // bits 9-11

    hashValue |= blendState.srcAlphaBlendFactor << 12; // bits 12-15
    hashValue |= blendState.dstAlphaBlendFactor << 16; // bits 16-19
    hashValue |= blendState.alphaBlendOp        << 20; // bits 20-22

    hashValue |= frontFace                      << 23; // bit 23

    SL_ASSERT(HashToBlend(hashValue) == blendState);
    SL_ASSERT(HashToFrontFace(hashValue) == frontFace);

    return hashValue;
}

VkPipeline Pipeline::GetOrCreatePipeline(const BlendState& blendState, FrontFace frontFace)
{
    uint32_t pipelineHash = GetHash(blendState, frontFace);
    auto key = std::make_tuple(pipelineHash, parent->GetCurrentColorFormat());
    auto it = mapHashToPipelines.find(key);
    if (it != mapHashToPipelines.end()) {
        return it->second;
    }

    VkPipeline pipeline = CreatePipeline(blendState, frontFace, parent->GetCurrentColorFormat());
    mapHashToPipelines.insert({ key, pipeline });
    return pipeline;
}

void Pipeline::Build()
{
    SL_MAP(std::string, BufferInfo) uniqueUbos;
    SL_MAP(std::string, BufferInfo) uniqueSsbos;
    SL_MAP(std::string, SamplerInfo) uniqueSamplers;

    for (auto& keyVal : mapShaderToShaderReflection) {
        auto shaderReflection = keyVal.second;

        // collect ubos, ssbos info
        const auto& bufferInfos = shaderReflection->BufferInfos();
        for (const auto& bufferInfo : bufferInfos) {
            if (bufferInfo.bufferType == BT_UBO && uniqueUbos.find(bufferInfo.name) == uniqueUbos.end()) {
                uniqueUbos.insert({ bufferInfo.name, bufferInfo });
            } else if (bufferInfo.bufferType == BT_SSBO && uniqueSsbos.find(bufferInfo.name) == uniqueSsbos.end()) {
                uniqueSsbos.insert({ bufferInfo.name, bufferInfo });
            }
        }

        // collect sampler info
        const auto& samplerInfos = shaderReflection->SamplerInfos();
        for (const auto& samplerInfo : samplerInfos) {
            if (uniqueSamplers.find(samplerInfo.name) == uniqueSamplers.end()) {
                uniqueSamplers.insert({ samplerInfo.name, samplerInfo });
            }
        }
    }

    SL_VECTOR(VkDescriptorSetLayoutBinding) set0Binding;
    SL_VECTOR(VkDescriptorSetLayoutBinding) set2Binding;
    for (const auto& keyVal : uniqueUbos) {
        // our convention is that shader parameters are at set 0
        if (keyVal.second.set == 0) {
            const VkDescriptorType uboType = (dynamicUbo) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            SL_ASSERT(set0Binding.size() == 0);
            set0Binding.push_back(vkInitializers::descriptorSetLayoutBinding(uboType, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, keyVal.second.binding));
        } else {
            SL_ASSERT(set2Binding.size() == 0);
            set2Binding.push_back(vkInitializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, keyVal.second.binding));
        }
    }
    for (const auto& keyVal : uniqueSsbos) {
        // our convention is that shader parameters are at set 0
        if (keyVal.second.set == 0) {
            const VkDescriptorType sboType = (dynamicSbo) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            SL_ASSERT(set0Binding.size() == 0);
            set0Binding.push_back(vkInitializers::descriptorSetLayoutBinding(sboType, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, keyVal.second.binding));
        } else {
            SL_ASSERT(set2Binding.size() == 0);
            set2Binding.push_back(vkInitializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, keyVal.second.binding));
        }
    }

    SL_ASSERT(set0Binding.size() == 1);
    VkDescriptorSetLayoutCreateInfo descriptorSet0LayoutCreateInfo = vkInitializers::descriptorSetLayoutCreateInfo(set0Binding.data(), static_cast<uint32_t>(set0Binding.size()));
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->VulkanDevice(), &descriptorSet0LayoutCreateInfo, nullptr, &descriptorSetLayout0));
    debugmarker::SetName(device, descriptorSetLayout0, name + ":descriptorSetLayout0");

    SL_ASSERT(set2Binding.size() == 0||set2Binding.size()==1);
    if (set2Binding.size() == 1) {
        VkDescriptorSetLayoutCreateInfo descriptorSet2LayoutCreateInfo = vkInitializers::descriptorSetLayoutCreateInfo(set2Binding.data(), static_cast<uint32_t>(set2Binding.size()));
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->VulkanDevice(), &descriptorSet2LayoutCreateInfo, nullptr, &descriptorSetLayout2));
        debugmarker::SetName(device, descriptorSetLayout2, name + ":descriptorSetLayout2");
    }

    SL_VECTOR(VkDescriptorSetLayoutBinding) set1Bindings;
    for (const auto& keyVal : uniqueSamplers) {
        // our convention is that samplers are at set 1 (so that it is easy to change the set texture without having to adjust the set 0 ssbos/ubos (needing another pipeline altogether)
        if (keyVal.second.set != 1) {
            device->log(LogLevel::SL_LOG_LEVEL_WARN) << "keyVal.second.set != 1" << std::endl;
        }
        if (keyVal.second.arraySize == 0) {
            set1Bindings.push_back(vkInitializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, keyVal.second.binding, 1));
        } else {
            set1Bindings.push_back(vkInitializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, keyVal.second.binding, keyVal.second.arraySize));
            samplersAreArray = true;
        }
    }
    if (set1Bindings.size() > 0) {
        VkDescriptorSetLayoutCreateInfo descriptorSet1LayoutCreateInfo = vkInitializers::descriptorSetLayoutCreateInfo(set1Bindings.data(), static_cast<uint32_t>(set1Bindings.size()));

        // This is for array of samplers
        VkDescriptorSetLayoutBindingFlagsCreateInfo setLayoutBindingFlags{};
        SL_VECTOR(VkDescriptorBindingFlags) descriptorBindingFlags;
        if (samplersAreArray && set1Bindings.size() == 1) {
            setLayoutBindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            setLayoutBindingFlags.bindingCount = 1;

            descriptorBindingFlags.push_back(VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
            setLayoutBindingFlags.pBindingFlags = descriptorBindingFlags.data();

            descriptorSet1LayoutCreateInfo.pNext = &setLayoutBindingFlags;
        }

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->VulkanDevice(), &descriptorSet1LayoutCreateInfo, nullptr, &descriptorSetLayout1));
        debugmarker::SetName(device, descriptorSetLayout1, name + ":descriptorSetLayout1");
    }

    SL_VECTOR(VkDescriptorSetLayout) vecDescriptorSetLayout = { descriptorSetLayout0 };
    if (set1Bindings.size() > 0) {
        vecDescriptorSetLayout.push_back(descriptorSetLayout1);
    }
    if (set2Binding.size() > 0) {
        vecDescriptorSetLayout.push_back(descriptorSetLayout2);
    }

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vkInitializers::pipelineLayoutCreateInfo(vecDescriptorSetLayout.data(), (uint32_t)vecDescriptorSetLayout.size());

    VK_CHECK_RESULT(vkCreatePipelineLayout(device->VulkanDevice(), &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));
    debugmarker::SetName(device, pipelineLayout, name + ":PipelineLayout");

    VkPipeline pipeline = GetOrCreatePipeline(parent->GetDefaultBlendState(), parent->GetDefaultFrontFace());

    SL_VECTOR(VkDescriptorPoolSize) poolSizes;

    if (set0Binding.size() != 0) {
        if (set0Binding[0].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            if (dynamicUbo) {
                poolSizes.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, (uint32_t)uniqueUbos.size() });
            } else {
                poolSizes.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)uniqueUbos.size() });
            }
        } else  if (set0Binding[0].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
            if (dynamicSbo) {
                poolSizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, (uint32_t)uniqueSsbos.size() });
            } else {
                poolSizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)uniqueSsbos.size() });
            }
        }
    }

    if (uniqueSamplers.size() == 1 && (uniqueSamplers.begin()->second.arraySize != 0)) {
        samplersAreArray = true;
    }

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = vkInitializers::descriptorPoolCreateInfo(poolSizes, numBufferedFrames);
    VK_CHECK_RESULT(vkCreateDescriptorPool(device->VulkanDevice(), &descriptorPoolCreateInfo, nullptr, &descriptorPoolSet0));

    for (int i = 0; i < numBufferedFrames; ++i) {
        VkDescriptorSet descriptorSet0 = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = vkInitializers::descriptorSetAllocateInfo(descriptorPoolSet0, &descriptorSetLayout0, 1);
        vkAllocateDescriptorSets(device->VulkanDevice(), &descriptorSetAllocateInfo, &descriptorSet0);

        std::stringstream ss;
        ss << name + ":descriptorSet0" << "_" << i;
        debugmarker::SetName(device, descriptorSet0, ss.str());

        descriptorSet0s.push_back(descriptorSet0);
    }

    for (auto& keyVal : uniqueUbos) {

        if (keyVal.second.name != "sl_ShaderParameters_UBO") {
            continue;
        }

        const VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        const VkMemoryPropertyFlags memoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;// | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        for (int i = 0; i < numBufferedFrames; ++i) {
            BufferObject ubo;
            ubo.dynamic = dynamicUbo;
            if (ubo.dynamic) {
                ubo.baseSize = keyVal.second.size;
                size_t minUboAlignment = device->GetPhysicalDevice()->PhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment;
                if (minUboAlignment > 0) {
                    ubo.baseSize = (int)((ubo.baseSize + minUboAlignment - 1) & ~(minUboAlignment - 1));
                }
                ubo.maxOffsetCount = maxUboCount;
                VkDeviceSize uboSize = ubo.baseSize * ubo.maxOffsetCount;
                ubo.buffer = new RawBuffer(device, usageFlags, memoryFlags, uboSize, keyVal.second.name);
            } else {
                ubo.buffer = new RawBuffer(device, usageFlags, memoryFlags, keyVal.second.size, keyVal.second.name);
            }

            ubo.buffer->Map(0, ubo.buffer->SizeInBytes());
            ubo.pData = ubo.buffer->MappedPtr();

            VkDescriptorBufferInfo descriptor{};

            if (dynamicUbo) {
                descriptor = VkDescriptorBufferInfo{ ubo.buffer->VulkanBuffer(), 0, (VkDeviceSize)ubo.baseSize };
            } else {
                descriptor = VkDescriptorBufferInfo{ ubo.buffer->VulkanBuffer(), 0, ubo.buffer->SizeInBytes() };
            }

            const VkDescriptorType uboType = (dynamicUbo) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            VkWriteDescriptorSet writeDescriptorSet = vkInitializers::writeDescriptorSet(descriptorSet0s[i], uboType, keyVal.second.binding, &descriptor);
            vkUpdateDescriptorSets(device->VulkanDevice(), 1, &writeDescriptorSet, 0, VK_NULL_HANDLE);

            sl_ShaderParameters_UBOs.push_back(ubo);
        }

    }
    for (auto& keyVal : uniqueSsbos) {
        if (keyVal.second.name != "sl_ShaderParameters_UBO") {
            continue;
        }
        const VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        const VkMemoryPropertyFlags memoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;// | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        for (int i = 0; i < numBufferedFrames; ++i) {
            BufferObject sbo;
            sbo.dynamic = dynamicSbo;
            if (sbo.dynamic) {
                sbo.baseSize = keyVal.second.size;
                size_t minSboAlignment = device->GetPhysicalDevice()->PhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
                if (minSboAlignment > 0) {
                    sbo.baseSize = (int)((sbo.baseSize + minSboAlignment - 1) & ~(minSboAlignment - 1));
                }
                sbo.maxOffsetCount = maxSsboCount;
                VkDeviceSize sboSize = sbo.baseSize * sbo.maxOffsetCount;
                sbo.buffer = new RawBuffer(device, usageFlags, memoryFlags, sboSize, keyVal.second.name);
            } else {
                sbo.buffer = new RawBuffer(device, usageFlags, memoryFlags, keyVal.second.size, keyVal.second.name);
            }

            sbo.buffer->Map(0, sbo.buffer->SizeInBytes());
            sbo.pData = sbo.buffer->MappedPtr();

            VkDescriptorBufferInfo descriptor{};

            if (dynamicSbo) {
                descriptor = VkDescriptorBufferInfo{ sbo.buffer->VulkanBuffer(), 0, (VkDeviceSize)sbo.baseSize };
            } else {
                descriptor = VkDescriptorBufferInfo{ sbo.buffer->VulkanBuffer(), 0, sbo.buffer->SizeInBytes() };
            }

            const VkDescriptorType sboType = (dynamicSbo) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            VkWriteDescriptorSet writeDescriptorSet = vkInitializers::writeDescriptorSet(descriptorSet0s[i], sboType, keyVal.second.binding, &descriptor);
            vkUpdateDescriptorSets(device->VulkanDevice(), 1, &writeDescriptorSet, 0, VK_NULL_HANDLE);

            sl_ShaderParameters_UBOs.push_back(sbo);
        }
    }
}

int Pipeline::GetVariableLocation(const char* varName)
{
    for (auto& keyVal : mapShaderToShaderReflection) {
        auto& bufferInfos = keyVal.second->BufferInfos();
        for (auto& bufferInfo : bufferInfos) {
            auto it = bufferInfo.variables.find(varName);
            if (it != bufferInfo.variables.end()) {
                return it->second.offset;
            }
        }
    }
    return -1;
}

void* Pipeline::actualUboOffsetInMemory(int frameIndex)
{
    SL_ASSERT(frameIndex < numBufferedFrames);
    if (frameIndex >= numBufferedFrames) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "frameIndex >= numBufferedFrames" << std::endl;
        return nullptr;
    }

    auto& sl_ShaderParameters_UBO = sl_ShaderParameters_UBOs[frameIndex];

    if (sl_ShaderParameters_UBO.buffer == nullptr) {
        return nullptr;
    }

    void* data = (uint8_t*)sl_ShaderParameters_UBO.pData + (currentUboOffset * sl_ShaderParameters_UBO.baseSize);
    return data;
}

void Pipeline::SetConstantVector4AtLocation(int frameIndex, int loc, const float* data)
{
    SL_ASSERT(frameIndex < numBufferedFrames);
    if (frameIndex >= numBufferedFrames) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "frameIndex >= numBufferedFrames" << std::endl;
        return;
    }

    if (loc == -1) {
        return;
    }
    void* pData = actualUboOffsetInMemory(frameIndex);
    if (pData) {
        memcpy((uint8_t*)(pData)+loc, data, sizeof(float) * 4);
    }
}
void Pipeline::SetConstantMatrix4AtLocation(int frameIndex, int loc, const float* data)
{
    SL_ASSERT(frameIndex < numBufferedFrames);
    if (frameIndex >= numBufferedFrames) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "frameIndex >= numBufferedFrames" << std::endl;
        return;
    }

    if (loc == -1) {
        return;
    }

    void* pData = actualUboOffsetInMemory(frameIndex);
    if (pData) {
        float t[16] = { data[0], data[4], data[8], data[12],
                        data[1], data[5], data[9], data[13],
                        data[2], data[6], data[10], data[14],
                        data[3], data[7], data[11], data[15]
                      };
        memcpy((uint8_t*)(pData)+loc, t, sizeof(float) * 16);
    }
}

void Pipeline::Bind(VkCommandBuffer commandBuffer, const RawBuffer* buffer, int set, int binding)
{
    SL_ASSERT(set == 2 && binding==0);
    BufferDescriptorSetManager* bufferDescriptorSetManager = parent->GetBufferDescriptorSetManager();

    SL_ASSERT(descriptorSetLayout2 != VK_NULL_HANDLE);
    VkDescriptorSet bufferDescriptorSet = bufferDescriptorSetManager->GetOrCreateFor(buffer, descriptorSetLayout2);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, set, 1, &bufferDescriptorSet, 0, nullptr);
}

void Pipeline::Bind(VkCommandBuffer commandBuffer, int frameIndex, const SL_ORDERED_MAP(int, Texture*)& enabledTextures, const BlendState& blendState, FrontFace frontFace)
{
    SL_ASSERT(frameIndex < numBufferedFrames);
    if (frameIndex >= numBufferedFrames) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "frameIndex >= numBufferedFrames" << std::endl;
        return;
    }

    if (dynamicUbo || dynamicSbo) {
        // bind in IncrementConstantBuffersOffset instead
    } else {
        if (commandBuffer) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet0s[frameIndex], 0, nullptr);
        }
    }

    TextureDescriptorSetManager* tdsm = parent->GetTextureDescriptorSetManager();
    if (descriptorSetLayout1!=VK_NULL_HANDLE) {

        VkDescriptorSet textureDescriptorSet = VK_NULL_HANDLE;
        if (samplersAreArray == false) {
            if (enabledTextures.size() > 0) {
                textureDescriptorSet = tdsm->GetOrCreateFor(enabledTextures);
                if (textureDescriptorSet == VK_NULL_HANDLE) {
                    device->log(LogLevel::SL_LOG_LEVEL_WARN) << __FUNCTION__ << "textureDescriptorSet == VK_NULL_HANDLE" << std::endl;
                    return;
                }
                if (commandBuffer) {
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &textureDescriptorSet, 0, nullptr);
                }
            } else {
                // can happen. Just don't bind.
            }

        } else if (samplersAreArray) {
            textureDescriptorSet = tdsm->GetOrCreateFor(this, enabledTextures, descriptorSetLayout1);
            if (textureDescriptorSet == VK_NULL_HANDLE) {
                device->log(LogLevel::SL_LOG_LEVEL_WARN) << __FUNCTION__ << "textureDescriptorSet == VK_NULL_HANDLE" << std::endl;
                return;
            }
            if (commandBuffer) {
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &textureDescriptorSet, 0, nullptr);
            }
        }
    }
    if (commandBuffer) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GetOrCreatePipeline(blendState, frontFace));
    }
}

void Pipeline::IncrementConstantBuffersOffset(VkCommandBuffer commandBuffer, int frameIndex)
{
    SL_ASSERT(frameIndex < numBufferedFrames);
    if (frameIndex >= numBufferedFrames) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "frameIndex >= numBufferedFrames" << std::endl;
        return;
    }
    // increment the instance index for the next set of constants to be set
    ++currentUboOffset;
    if (dynamicUbo || dynamicSbo) {
        auto& sl_ShaderParameters_UBO = sl_ShaderParameters_UBOs[frameIndex];
        if (sl_ShaderParameters_UBO.buffer==nullptr) {
            device->log(LogLevel::SL_LOG_LEVEL_WARN) << "sl_ShaderParameters_UBO.buffer==nullptr" << std::endl;
        } else {
            if (currentUboOffset == sl_ShaderParameters_UBO.maxOffsetCount) {
                device->log(LogLevel::SL_LOG_LEVEL_WARN) << "currentUboOffset == maxOffsetCount, wrapping around" << std::endl;
                currentUboOffset = 0;
            }
            const uint32_t offset = currentUboOffset * sl_ShaderParameters_UBO.baseSize;
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet0s[frameIndex], 1, &offset);
        }
    }
}

void Pipeline::ResetUboOffset()
{
    currentUboOffset = -1;
}

void Pipeline::SetCurrentCommandBuffer(VkCommandBuffer unused)
{
    ResetUboOffset();
}

void Pipeline::SettingsChanged()
{
    for (auto& keyVal : mapHashToPipelines) {
        vkDestroyPipeline(device->VulkanDevice(), keyVal.second, nullptr);
    }
    mapHashToPipelines.clear();

    // just create the base pipelines for now
    GetOrCreatePipeline(parent->GetDefaultBlendState(), parent->GetDefaultFrontFace());
}
}
}