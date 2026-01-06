// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "Shader.h"
#include "Device.h"
#include "SLAssert.h"

#include <fstream>
#include <vector>
#include <iostream>

namespace SilverLining
{
namespace Vulkan
{
Shader::Shader(Device* device)
    : device(device)
    , valid(false)
    , shaderStageInfo()
    , shaderModuleInfo()
{
    // nothing else
}

Shader::~Shader(void)
{
    if (shaderStageInfo.module != 0) {
        vkDestroyShaderModule(device->VulkanDevice(), shaderStageInfo.module, nullptr);
    }
}

void Shader::LoadFromFile(const std::string& fileName, VkShaderStageFlagBits shaderType)
{
    std::ifstream ifs(fileName, std::ios::binary);
    std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(ifs), {});
    LoadFromSource(buffer, shaderType);
}

void Shader::LoadFromSource(const std::vector<uint8_t>& buffer, VkShaderStageFlagBits shaderType)
{
    valid = false;

    shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleInfo.pNext = nullptr;
    shaderModuleInfo.flags = 0;
    shaderModuleInfo.pCode = (uint32_t*)buffer.data();
    shaderModuleInfo.codeSize = buffer.size();

    VkShaderModule shaderModule = 0;
    VkResult vkResult = vkCreateShaderModule(device->VulkanDevice(), &shaderModuleInfo, nullptr, &shaderModule);
    if (vkResult != VK_SUCCESS) {
        device->log(LogLevel::SL_LOG_LEVEL_WARN) << "failed vkCreateShaderModule" << std::endl;
        return;
    }

    SL_ASSERT(shaderModule != VK_NULL_HANDLE);

    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.pNext = nullptr;
    shaderStageInfo.flags = 0;

    shaderStageInfo.stage = shaderType;

    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = "main";

    uint32_t* pData = (uint32_t*)buffer.data();
    source.insert(source.end(), &pData[0], &pData[buffer.size() / 4]);

    valid = true;
}

bool Shader::Valid(void) const
{
    return valid;
}

VkShaderModuleCreateInfo Shader::ShaderModuleCreateInfo(void) const
{
    return shaderModuleInfo;
}
VkPipelineShaderStageCreateInfo Shader::PipelineShaderStageCreateInfo(void) const
{
    return shaderStageInfo;
}

const std::vector<uint32_t>& Shader::Source(void) const
{
    return source;
}
}
}