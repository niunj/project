#version 450

#extension GL_GOOGLE_include_directive : enable

#define SLVULKAN 1
#define USE_UBO 1
#define UBO_USE_SET_BINDING 1
#define SKY_UBO_IS_SSBO 1

#include "Sky-Hosek-Wilkie-HDR-common.glsl15"
#include "Sky-Hosek-Wilkie-HDR.glsl15"
#include "UserFunctions.glsl15"
