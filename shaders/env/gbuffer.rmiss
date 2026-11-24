// gbuffer.rmiss —— P1.1: 未命中 (天空) → G-buffer 清空 (hit=0)。
//   G0 = (0, 0, 1e6, hit=0)   —— band_shade.comp 输出天空辐亮度 (a=1e6 km 标记,
//                                与旧 band_rt.rmiss P0 契约一致)
//   G1 = (0, 0, 0, matId=0)   —— radar_scatter.comp 清零散射体槽位
//   G2 = 视线方向 ENU xyz      —— 批次2: 天空图 latlong 采样方向 (天顶亮/地平暗
//                                方向化天空背景; hit 像素 G2 仍为面法线, 语义
//                                以 G0.w 区分)
//   G3 = (0,0,0,0)             —— TV 彩色: 天空无贴图槽
// 每 trace 全量重写所有像素, 无跨帧残留。
#version 460
#extension GL_EXT_ray_tracing : enable

layout(set = 0, binding = 1, rgba32f) uniform image2D outG0;
layout(set = 0, binding = 2, rgba32f) uniform image2D outG1;
layout(set = 0, binding = 7, rgba32f) uniform image2D outG2;
layout(set = 0, binding = 12, rgba32f) uniform image2D outG3;

layout(location = 0) rayPayloadInEXT vec4 prd;

void main() {
    const ivec2 px = ivec2(gl_LaunchIDEXT.xy);
    imageStore(outG0, px, vec4(0.0, 0.0, 1.0e6, 0.0));
    imageStore(outG1, px, vec4(0.0, 0.0, 0.0, 0.0));
    imageStore(outG2, px, vec4(normalize(gl_WorldRayDirectionEXT), 0.0));
    imageStore(outG3, px, vec4(0.0));
}
