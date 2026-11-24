// sensor_rad.vert —— 蓝方辐射亮度 pass 顶点着色 (传感器视角, 世界系几何)。
// 顶点: loc0 pos(vec3), loc3 props(vec2: T_kelvin, emissivity),
//       loc4 uv(vec2), loc5 slot(uint) —— #2: uv/槽号供观察口贴图采样
//       (rad.frag 不消费, 链接期自动裁剪; 声明与场景 binding 0..4 契约一致)。
// 输出: loc0 vProps, loc1 vWorld, loc2 vUv (vis 用), loc3 vSlot (flat, vis 用)。
#version 460

// 独立 set1 视图 UBO (112B)，避免 VSG MatrixStack 覆盖自定义参数:
//   vp   观察口或传感器口的 proj*view
//   cam  传感器口: near, far, tanHalfFov, aspect
//        观察口 (#2 复用): x=skyValid, y=云底 m, z=云影强度, w=TV esun
//   misc x=探测器噪声时变种子(帧号), yzw=太阳方向(指向太阳)
//   eye  相机世界位置
layout(set = 1, binding = 0) uniform SensorView {
    mat4 vp;
    vec4 cam;
    vec4 misc;
    vec4 eye;
} pc;

layout(location = 0) in vec3 inPos;
layout(location = 3) in vec2 inProps;
layout(location = 4) in vec2 inUv;
layout(location = 5) in uint inSlot;
layout(location = 6) in uint inMaterial;

layout(location = 0) out vec2 vProps;
layout(location = 1) out vec3 vWorld;
layout(location = 2) out vec2 vUv;
layout(location = 3) flat out uint vSlot;
layout(location = 4) flat out uint vMaterial;

void main() {
    vProps = inProps;
    vWorld = inPos;
    vUv = inUv;
    vSlot = inSlot;
    vMaterial = inMaterial;
    gl_Position = pc.vp * vec4(inPos, 1.0);
}
