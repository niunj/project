// gbuffer.rchit —— P1.1: 波段无关 G-buffer 最近交点写入。
//   G0 = (T_kelvin, emissivity, range_m, hit=1)   —— band_shade.comp 消费
//   G1 = (pos.xyz ENU, matId)                     —— radar_scatter.comp 消费
//   G2 = (geometric normal.xyz ENU, packed optional thermal shading normal)
//   G3 = (uv.x, uv.y, texSlot, 0)                 —— TV 彩色批次: 贴图采样寻址
// 顶点数据经 SSBO (std430; pos 用平铺 float 数组规避 vec3 16B stride)。
// normal 由平铺顶点叉积即时求出 (几何面法线, ENU 世界系; 未做双面翻转 ——
// GLSL 内建 gl_HitTriangleEXT 本环境不可靠, 面朝向由 CCW 绕序保证)。
#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable
#include "surface_thermal.glsl"

layout(set = 0, binding = 1, rgba32f) uniform image2D outG0;
layout(set = 0, binding = 2, rgba32f) uniform image2D outG1;
layout(set = 0, binding = 7, rgba32f) uniform image2D outG2;
layout(set = 0, binding = 12, rgba32f) uniform image2D outG3;

layout(set = 0, binding = 3) buffer PosBuf   { float positions[]; };  // 3*i 平铺
layout(set = 0, binding = 4) buffer IdxBuf   { uint indices[]; };
layout(set = 0, binding = 5) buffer PropBuf  { vec2 props[]; };       // (T_kelvin, emissivity)
layout(set = 0, binding = 6) buffer MatIdBuf { uint matIndices[]; };
layout(set = 0, binding = 10) buffer UvBuf   { vec2 uvs[]; };         // TV 彩色: 逐顶点 uv
layout(set = 0, binding = 11) buffer SlotBuf { uint slotIdx[]; };     // TV 彩色: 贴图槽号
layout(set = 0, binding = 16, std430) readonly buffer ThermalNormals { vec4 shadingNormals[]; };

layout(location = 0) rayPayloadInEXT vec4 prd;
hitAttributeEXT vec2 attribs;   // 三角形交点重心坐标 (u, v)

void main() {
    const uint primitive=uint(gl_InstanceCustomIndexEXT)+uint(gl_PrimitiveID);
    const uint i0 = indices[primitive * 3 + 0];
    const uint i1 = indices[primitive * 3 + 1];
    const uint i2 = indices[primitive * 3 + 2];

    const vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    const vec2 pa = props[i0], pb = props[i1], pc2 = props[i2];
    const float T   = bary.x * pa.x + bary.y * pb.x + bary.z * pc2.x;
    const float eps = bary.x * pa.y + bary.y * pb.y + bary.z * pc2.y;

    const vec3 v0 = vec3(positions[3*i0], positions[3*i0+1], positions[3*i0+2]);
    const vec3 v1 = vec3(positions[3*i1], positions[3*i1+1], positions[3*i1+2]);
    const vec3 v2 = vec3(positions[3*i2], positions[3*i2+1], positions[3*i2+2]);
    const vec3 pos = gl_ObjectToWorldEXT * vec4(bary.x * v0 + bary.y * v1 + bary.z * v2,1);
    vec3 nrm = normalize(transpose(mat3(gl_WorldToObjectEXT))*cross(v1-v0,v2-v0));
    if(dot(nrm,gl_WorldRayDirectionEXT)>0) nrm=-nrm;
    float heatNormal = 0.0;
    if (shadingNormals.length() > 1) {
        const vec3 local = shadingNormals[i0].xyz * bary.x + shadingNormals[i1].xyz * bary.y + shadingNormals[i2].xyz * bary.z;
        heatNormal = packThermalNormal(transpose(mat3(gl_WorldToObjectEXT)) * local);
    }

    const float range = gl_RayTmaxEXT;                     // m
    const uint matId = matIndices[i0];

    // TV 彩色: uv 重心插值 + 槽号 (面常量, 取 i0)
    const vec2 uv = bary.x * uvs[i0] + bary.y * uvs[i1] + bary.z * uvs[i2];
    const uint slot = slotIdx[i0];

    const ivec2 px = ivec2(gl_LaunchIDEXT.xy);
    imageStore(outG0, px, vec4(T, eps, range, 1.0));
    imageStore(outG1, px, vec4(pos, float(matId)));
    imageStore(outG2, px, vec4(nrm, heatNormal));
    imageStore(outG3, px, vec4(uv, float(slot), 0.0));
}
