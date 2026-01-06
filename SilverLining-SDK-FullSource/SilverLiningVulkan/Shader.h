// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace SilverLining
{
    namespace Vulkan
    {
        class Device;

        //! Encapsulates a shader
        class Shader
        {
        public:
            //! constructor
            Shader(Device* device);

            //! destructor
            virtual ~Shader(void);
        public:
            //! Load from a file
            void LoadFromFile(const std::string& fileName, VkShaderStageFlagBits shaderType);

            //! Load straight from source
            void LoadFromSource(const std::vector<uint8_t>& source, VkShaderStageFlagBits shaderType);

            //! Is it valide after loading?
            bool Valid(void) const;

            //! create info
            VkShaderModuleCreateInfo ShaderModuleCreateInfo(void) const;

            //! pipeline shader stage create info
            VkPipelineShaderStageCreateInfo PipelineShaderStageCreateInfo(void) const;

            //! the source (spir v)
            const std::vector<uint32_t>& Source(void) const;
        protected:
            VkShaderModuleCreateInfo shaderModuleInfo{};
            VkPipelineShaderStageCreateInfo shaderStageInfo{};
            std::vector<uint32_t> source;

            Device* device = nullptr;

            bool valid;
        };
    }
}