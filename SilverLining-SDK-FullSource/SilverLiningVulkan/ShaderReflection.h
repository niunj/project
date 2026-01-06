// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "VulkanBuffer.h"
#include "Texture.h"

namespace SilverLining
{
    namespace Vulkan
    {
        //! The type of the variable
        enum VariableType
        {
            FLOAT = 0, VEC2, VEC3, VEC4, MAT3, MAT4, UNKNOWN
        };

        //! A variable (name, type and the offset into the buffer that contains it)
        struct Variable
        {
            std::string name;
            VariableType type;
            int offset;
        };

        //! A buffer is built up of several variables
        //! It has a type (ubo/ssbo)
        //! It has a size, a descriptor set index and a binding index
        struct BufferInfo
        {
            std::string name;
            BufferType bufferType;
            int size;
            int set;
            int binding;
            SL_MAP(std::string, Variable) variables;
        };

        //! A sampler has a name
        //! It has a type (2d/3d/cubemap)
        //! It has a size, a descriptor set index and a binding index
        struct SamplerInfo
        {
           std::string name;
           SamplerType samplerType;
           int set;
           int binding;
           int arraySize = 0;
        };

        //! spirv-cross reflects a shader (.spv) and saves it out into a json file (.spv.json)
        //! This class is used to parse this file and access that information
        class ShaderReflection
        {
        public:
            //! constructor
            ShaderReflection();

            //! destructor
            virtual ~ShaderReflection();
        public:
            //! Load from a json file 
            void LoadFromSource(const std::string& source, std::ostream& log);

            //! Is the parsed source valid
            bool Valid(void) const;

            //! access the buffers described in the shader
            const SL_VECTOR(BufferInfo)& BufferInfos(void) const;

            //! access the samplers described in the shader
            const SL_VECTOR(SamplerInfo)& SamplerInfos(void) const;
        protected:
            SL_VECTOR(BufferInfo) bufferInfos;
            SL_VECTOR(SamplerInfo) samplerInfos;
            bool valid = false;
        };
    }
}
