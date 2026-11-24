#version 460
/// trail_ribbon.vert — 运动拖尾条带顶点 shader
/// uv.x = 沿条带方向 (0=最旧 → 1=最新)
/// uv.y = 宽度方向 (0→1)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(push_constant) uniform PC {
    float time_sec;
    float param1;   ///< 阵营色 (0=红, 0.5=蓝, 1=中立)
    float param2;   ///< 条带亮度因子
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
