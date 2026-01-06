// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "VulkanInitInfo.h"
#include "VulkanBuffer.h"
#include "BlendState.h"

#include <map>
#include <vector>

namespace SilverLining
{
    class BlendState;
    namespace Vulkan
    {
        class Device;

        class Shader;
        class ShaderReflection;
        class VulkanContext;
        class Texture;

        //! This class represents the entire rendering pipeline for a given shader
        //! It is built up from the shader and shader reflection info
        class Pipeline
        {
        public:
            //! constructor
            Pipeline(VulkanContext* _parent, bool _dynamicUbo, int _maxUboCount, bool _dynamicSbo, int _maxSsboCount, VkPrimitiveTopology primitiveTopology, int _numBufferedFrames, const std::string& _name);

            //! destructor
            virtual ~Pipeline();
        public:
            //! Add a shader (and it's reflection) which is part of this pipeline
            void Add(Shader* shader, ShaderReflection* shaderReflection);

            //! build the pipeline
            void Build();

            //! A variable is in a buffer. It has an offset. This function will give that offset, given the name
            int GetVariableLocation(const char* name);

            //! A vec4 is in a buffer. It has an offset. This function will give that offset, given the name
            void SetConstantVector4AtLocation(int frameIndex, int loc, const float* data);

            //! A mat4 is in a buffer. It has an offset. This function will give that offset, given the name
            void SetConstantMatrix4AtLocation(int frameIndex, int loc, const float* data);

            //! record the command of binding the pipeline to this command buffer
            void Bind(VkCommandBuffer commandBuffer, int frameIndex, const SL_ORDERED_MAP(int, Texture*)& enabledTextures, const BlendState& blendState
            , FrontFace frontFace);

            void Bind(VkCommandBuffer commandBuffer, const RawBuffer* buffer, int set, int binding);

            //! Do things that you need to do when the current command buffer is set
            void SetCurrentCommandBuffer(VkCommandBuffer _commandBuffer);

            //! slide dynamic ubo offset
            void IncrementConstantBuffersOffset(VkCommandBuffer commandBuffer, int frameIndex);

            void SettingsChanged();
        protected:
            //! internal
           void* actualUboOffsetInMemory(int frameIndex);

           //! internal
           void ResetUboOffset();

           VkPipeline CreatePipeline(const BlendState& blendState, FrontFace frontFace, VkFormat colorFormat);
           uint32_t GetHash(const BlendState& blendState, FrontFace frontFace) const;
           BlendState HashToBlend(uint32_t hash) const;
           FrontFace HashToFrontFace(uint32_t hash) const;
           VkPipeline GetOrCreatePipeline(const BlendState& blendState, FrontFace frontFace);
        protected:
            SL_MAP(Shader*, ShaderReflection*) mapShaderToShaderReflection;

            Device* device = nullptr;

            VkDescriptorPool descriptorPoolSet0 = VK_NULL_HANDLE;
            VkDescriptorSetLayout descriptorSetLayout0 = VK_NULL_HANDLE;

            SL_VECTOR(VkDescriptorSet) descriptorSet0s;

            // Samplers go here.
            // This is only used to create the pipeline.
            // The actual descriptor bound is gotten via the TextureDescriptorSetManager for binding.
            VkDescriptorSetLayout descriptorSetLayout1 = VK_NULL_HANDLE;

            // layout for an optional additional buffer at set 2, binding 0. Currently, only rain/sleet/snow use it
            // This is only used to create the pipeline.
            // The actual descriptor bound is gotten via the BufferDescriptorSetManager.
            VkDescriptorSetLayout descriptorSetLayout2 = VK_NULL_HANDLE;

            VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
            
            int currentUboOffset = -1;

            struct BufferObject
            {
               RawBuffer* buffer = nullptr;
               void* pData = nullptr;

               bool dynamic = false;
               // only pertinent if dynamic is true
               int baseSize = 0;
               int maxOffsetCount = 0;
            };
            SL_VECTOR(BufferObject) sl_ShaderParameters_UBOs;


            VulkanContext* parent = nullptr;

            const bool dynamicUbo = false;
            const int maxUboCount = 0;

            const bool dynamicSbo = false;
            const int maxSsboCount = 0;

            const VkPrimitiveTopology primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

            const std::string name;

            bool samplersAreArray = false;

            typedef std::tuple<std::uint32_t, VkFormat> PipelineKey;
            SL_ORDERED_MAP(PipelineKey, VkPipeline) mapHashToPipelines;

            const int numBufferedFrames;
        };
    }
}
