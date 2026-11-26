// sensor_rad.frag —— 蓝方物理级: 波段辐射亮度图 (P0 产品)。
// 目标辐亮度 L = (ε·M_band(T)/π + L_solar)·τ(R) + (1-τ)·L_path (Kirchhoff 路径辐射)
// 批次1: L_solar = ρ·Esun/π·max(N·L,0) —— CPU 后备链无 G-buffer/材质表,
// 平面近似 N=(0,1,0) + Kirchhoff 缺省反照率 ρ=1-ε (GPU 主链走谱材质+G2 真法线)。
// MWIR/LWIR 波段 Esun≈0, 太阳项自然归零; 夜间输出与旧版逐位一致。
// 背景(无几何) = 天空背景辐亮度。
// b1 = 普朗克波段出射度 LUT (512×1, R=x: M_band(T), T∈[band.x, band.y])。
#version 460
#extension GL_GOOGLE_include_directive : require
#include "surface_thermal.glsl"

layout(set = 0, binding = 0) uniform SensorUBO {
    mat4 vp;
    vec4 cam;    // near, far, tanHalfFov, aspect
    vec4 env;    // sigma_per_km, sky_radiance, sun_irradiance, night
    vec4 band;   // lutTmin, lutTmax, lambdaC_um, pad
    vec4 optics; // fNumber, transmission, psfSigma, vignetting
    vec4 det;    // qe, tint_s, netdK, wellE
    vec4 det2;   // readNoiseE, pitchUm, adcBits, agcLo
    vec4 elec;   // agcHi, gamma, texW, texH
    vec4 env2;   // sunDir ENU xyz (批次1, 指向太阳), pad
    vec4 thermalState;
    vec4 thermalSetup; // cloud coverage, band ID, day factor, padding
} ub;

layout(set = 0, binding = 1) uniform sampler2D lutTex;
layout(set = 0, binding = 2) readonly buffer SpectralMaterials { vec4 mats[]; };
layout(set = 1, binding = 0) uniform SensorView { mat4 vp; vec4 cam; vec4 misc; vec4 eye; } pc;

layout(location = 0) in vec2 vProps;
layout(location = 1) in vec3 vWorld;
layout(location = 4) flat in uint vMaterial;

layout(location = 0) out vec4 outRad;

const float PI = 3.14159265;

float bandExitance(float T) {
    float u = clamp((T - ub.band.x) / (ub.band.y - ub.band.x), 0.0, 1.0);
    // LUT 512×1, v=0.5 中心
    return texture(lutTex, vec2(u * (1.0 - 1.0/512.0) + 0.5/512.0, 0.5)).r;
}

void main() {
    float T = vProps.x;
    float eps = vProps.y;

    const uint band = uint(ub.thermalSetup.y);
    if (vMaterial < uint(mats.length()) / 4u) {
        const uint offset = vMaterial * 4u;
        const vec4 response = mats[offset+2u];
        if (band == 2u || band == 3u) {
            const float selected = band == 2u ? mats[offset].z : mats[offset].w;
            if (selected >= 0.0) eps = selected;
        }
        if (response.x > 0.5) {
            vec3 normal = cross(dFdx(vWorld), dFdy(vWorld));
            normal = length(normal) > 1e-10 ? normalize(normal) : vec3(0,1,0);
            if (dot(normal,pc.eye.xyz-vWorld) < 0.0) normal = -normal;
            const float daylight = ub.thermalSetup.z * max(ub.env2.y,0.0);
            const float exposure = (max(dot(normal,ub.env2.xyz),0.0) - max(ub.env2.y,0.0)) * daylight;
            T = surfaceTemperature(T, response, mats[offset+3u], ub.thermalState, ub.thermalSetup.x, exposure);
            eps = wetSurfaceEmissivity(eps, response, ub.thermalState.z);
        }
    }

    // Physical slant range, independent of projection/depth convention. VSG
    // uses reverse-Z; a forward-depth inversion also gave axial rather than
    // slant distance off axis. Misses have no fragments and use the RG clear.
    float range = length(vWorld-pc.eye.xyz);

    float L;
    if (range > 1e8) {
        // 天空背景: 直接用环境辐亮度 (含大气自身辐射, 由 CPU 模型给出)
        L = ub.env.y;
    } else {
        float tau = exp(-ub.env.x * range * 1e-3);           // τ = exp(-σ·R_km)
        // 批次1: 太阳反射 (平面近似 N=(0,1,0), ρ=1-ε Kirchhoff 缺省; Esun=0 → 0)
        float NdotL = max(ub.env2.y, 0.0);
        float L_solar = max(1.0 - eps, 0.0) * ub.env.z * NdotL / PI;
        float L_target = eps * bandExitance(T) / PI + L_solar;  // 目标辐亮度
        float L_path = (1.0 - tau) * ub.env.y;                // 路径辐射 ≈ (1-τ)·天空
        L = L_target * tau + L_path;
    }
    // 输出: r=辐亮度, g=T, b=eps, a=range_km (调试)
    outRad = vec4(L, T, eps, range * 1e-3);
}
