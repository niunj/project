#version 460
/// faction_halo.vert — 阵营光晕穹顶顶点 shader
/// 穹顶网格顶点, uv.y = 高度分数 (0=底部 → 1=顶部)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(push_constant) uniform PC {
    float time_sec;   ///< 全局时间 (脉冲动画)
    float param1;     ///< 阵营 (0=红, 0.5=蓝, 1=中立)
    float param2;     ///< 脉冲强度
    float pad;
} pc;

layout(set = 0, binding = 0) uniform CameraMatrices {
    mat4 view_proj;
    vec3 cam_pos; float _pad;
    mat4 model;
} camera;

layout(location = 0) out vec2 outUV;

void main() {
    outUV = inUV;
    gl_Position = camera.view_proj * camera.model * vec4(inPosition, 1.0);
}
