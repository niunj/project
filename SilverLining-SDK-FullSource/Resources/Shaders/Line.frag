#version 450

#extension GL_GOOGLE_include_directive : enable

#define SLVULKAN 1
#define USE_UBO 1
#define UBO_USE_SET_BINDING 1

#include "UserFunctions-frag.glsl15"

layout(set = 0, binding = 0, std140) uniform sl_ShaderParameters_UBO {
    mat4 sl_modelViewProj;
    vec4 sl_colorConstant;
};

void main()
{
    writeFragmentData(sl_colorConstant);
}

