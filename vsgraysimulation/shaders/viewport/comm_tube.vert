#version 460
/// comm_tube.vert — 通信/交战管道顶点 shader
/// uv.x = 沿管道方向进度 (0=起点 → 1=终点)
/// uv.y = 圆周方向角度 (0→1)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(push_constant) uniform PC {
    float time_sec;     ///< 全局时间 (数据流滚动)
    float param1;       ///< 0=活跃, 1=断开
    float param2;       ///< 数据流速度因子
    float pad;
} pc;

layout(set = 0, binding = 0) uniform CameraMatrices {
    mat4 view_proj;
    vec3 cam_pos; float _pad;
    mat4 model;
} camera;

layout(location = 0) out vec2 outUV;
layout(location = 1) out float outFlow;

void main() {
    outUV = inUV;
    // 数据流相位: 沿管道方向滚动
    outFlow = fract(inUV.x * 4.0 - pc.time_sec * pc.param2 * 2.0);
    gl_Position = camera.view_proj * camera.model * vec4(inPosition, 1.0);
}
