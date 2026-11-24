#version 460
#extension GL_GOOGLE_include_directive : require
#include "celestial_discs.glsl"
// Standalone lunar billboard for the Rocky sky fallback.

layout(location = 0) in vec3 vDir;

layout(set = 0, binding = 0) uniform Env {
    mat4 view_proj;
    mat4 model;
    vec4 camPosTime;
    vec4 sun;          // xyz=太阳方向 ECU, w=高度角(度)
    vec4 wind;
    vec4 misc;
    vec4 ext0; vec4 ext1;
    vec4 atm;
    vec4 deepCol; vec4 shallowCol;
    vec4 sunLight;
    vec4 ambient;
    vec4 skyHorizon;
    vec4 skyZenith;
    // 84..91 天空专属
    vec4 moon;         // xyz=月亮方向 ECEF, w=月相 0..1
    vec4 astro;        // x=保留 y=保留 z=月亮高度角(度)
} env;

layout(location = 0) out vec4 outColor;

const float MOON_ANGULAR_RADIUS=.00452;

void main() {
    vec4 moon=lunarDisplayDisc(normalize(vDir),normalize(env.moon.xyz),normalize(env.sun.xyz),
                                vec3(0,1,0),MOON_ANGULAR_RADIUS,0.,false);
    if(moon.a<=0.)discard;
    outColor=vec4(moon.rgb/moon.a,moon.a);
}
