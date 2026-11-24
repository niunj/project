#version 460
/// detection_volume.vert — 探测体积半球网格顶点 shader
/// 输入: position(局部) + uv(沿径向/俯仰进度) + normal
/// push-constant: time_sec + scan_az (扫描方位角)
/// 输出: gl_Position + 沿深度方向的 uv 供 frag 渐隐
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(push_constant) uniform PC {
    float time_sec;
    float scan_az;       ///< 当前扫描方位 (rad)
    float param2;
    float pad;
} pc;

layout(set = 0, binding = 0) uniform CameraMatrices {
    mat4 view_proj;
    vec3 cam_pos; float _pad;
    mat4 model;
} camera;

layout(location = 0) out vec2 outUV;
layout(location = 1) out float outDist;

void main() {
    outUV = inUV;
    // uv.x = 沿径向进度 (0=原点 → 1=最大距离)
    // dist 因子用于 frag 的 alpha 渐隐
    outDist = inUV.x;
    vec4 world_pos = camera.model * vec4(inPosition, 1.0);
    gl_Position = camera.view_proj * world_pos;
}
