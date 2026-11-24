#version 450
#extension GL_GOOGLE_include_directive : require
#define CLOUD_LIGHT_FIELD_RENDER
#define CLOUD_REFLECTION
#define main cloudLayer
#include "planet_cloud_common.glsl"
#undef main
void main(){cloudLayer();}
