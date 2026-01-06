// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "SLAssert.h"
#include "ShaderReflection.h"
#include "json.hpp"

#include <iostream>


namespace SilverLining
{
namespace Vulkan
{
static VariableType ToType(const std::string& type)
{
    if (type == "float") return FLOAT;
    if (type == "vec2") return VEC2;
    if (type == "vec3") return VEC3;
    if (type == "vec4") return VEC4;
    if (type == "mat3") return MAT3;
    if (type == "mat4") return MAT4;

    return UNKNOWN;
}

ShaderReflection::ShaderReflection()
{
    // no op
}

ShaderReflection::~ShaderReflection()
{
    // no op
}

void ShaderReflection::LoadFromSource(const std::string& source, std::ostream& log)
{
    valid = false;
    using namespace std;
    using json = nlohmann::json;

    try {
        json data = json::parse(source);

        auto loadBuffers = [&](BufferType bufferType) {
            std::string key = bufferType == BT_UBO ? "ubos" : "ssbos";
            for (auto& currentBuffer : data[key.c_str()]) {
                const std::string name = currentBuffer["name"];

                BufferInfo info;
                info.name = name;
                info.size = currentBuffer["block_size"];
                info.set = currentBuffer["set"];
                info.binding = currentBuffer["binding"];
                info.bufferType = bufferType;

                const std::string type = currentBuffer["type"];

                for (auto& member : data["types"][type]["members"]) {
                    std::string name = member["name"];

                    Variable variable;
                    variable.name = name;
                    variable.type = ToType(member["type"]);
                    variable.offset = member["offset"];

                    info.variables.insert({ variable.name, variable });
                }
                bufferInfos.push_back(info);
            }
        };

        loadBuffers(BT_UBO);
        loadBuffers(BT_SSBO);

        const std::string key = "textures";
        for (auto& currentTexture : data[key.c_str()]) {
            const std::string name = currentTexture["name"];
            const std::string strType = currentTexture["type"];

            SamplerInfo info;
            info.name = name;
            if (strType == "sampler2D") {
                info.samplerType = ST_2D;
            } else if (strType == "sampler3D") {
                info.samplerType = ST_3D;
            } else {
                log <<__FUNCTION__<< "Unknown sampler type" << std::endl;
            }
            info.set = currentTexture["set"];
            info.binding = currentTexture["binding"];

            for (const auto& val : currentTexture["array"]) {
                info.arraySize = val;
                break;
            }

            samplerInfos.push_back(info);
        }
    } catch (...) {
        valid = false;
    }

    valid = true;
}

bool ShaderReflection::Valid(void) const
{
    return valid;
}

const SL_VECTOR(BufferInfo)& ShaderReflection::BufferInfos(void) const
{
    return bufferInfos;
}

const SL_VECTOR(SamplerInfo)& ShaderReflection::SamplerInfos(void) const
{
    return samplerInfos;
}
}
}