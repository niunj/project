#version 460
/// cloud_sprite.vert — 积云公告板 (顶点 CPU 每帧更新为相机周围面向相机的 quad)。
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform Env {
    mat4 view_proj;
    mat4 model;
    vec4 camPosTime;
    vec4 sun;
    vec4 wind;
    vec4 misc;
    vec4 ext0;
    vec4 ext1;
    vec4 atm;
    vec4 deepCol;
    vec4 shallowCol;
    vec4 sunLight;
    vec4 ambient;
    vec4 skyHorizon;
    vec4 skyZenith;
    vec4 moon;
    vec4 astro;
    vec4 oceanWave[16];
    vec4 oceanOpt;
} env;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vWorldPos;   // 公告板顶点 ENU 世界坐标 (雾距用)

void main() {
    vUV = inUV;
    vWorldPos = inPosition;
    gl_Position = env.view_proj * env.model * vec4(inPosition, 1.0);
}
