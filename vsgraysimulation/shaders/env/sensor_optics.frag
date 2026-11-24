// sensor_optics.frag —— 红方信号级 · 光学段 (P1→P2)。
// 输入: P1 入瞳前辐亮度 (b1)。输出: 焦平面辐亮度。
// 模型: 9×9 高斯 PSF (MTF 近似) + cos⁴ 渐晕 + 光学透过率。
#version 460

layout(set = 0, binding = 0) uniform SensorUBO { mat4 vp; vec4 cam; vec4 env;
    vec4 band; vec4 optics; vec4 det; vec4 det2; vec4 elec; } ub;
layout(set = 0, binding = 1) uniform sampler2D inTex;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main() {
    // [判别实验] 恢复原始采样链: 输出=PSF(radColor)，检验 radColor 真实内容
    vec2 texel = 1.0 / ub.elec.zw;
    float sigma = ub.optics.z;
    // separable 简化: 单 pass 十字 17-tap (水平/垂直各 8) + 中心, 各向同性 PSF
    float wsum = 0.0;
    vec3 acc = vec3(0.0);
    for (int i = -8; i <= 8; ++i) {
        for (int j = -8; j <= 8; j += 8) {   // 中心列 + 上下稀疏 (十字近似)
            vec2 off = vec2(float(i), float(j)) * sigma * texel;
            float w = exp(-dot(off, off) / (2.0 * sigma * sigma * texel.x * texel.y));
            acc += texture(inTex, vUv + off).rgb * w;
            wsum += w;
        }
    }
    vec3 L = acc / wsum;

    // cos⁴ 渐晕: 离轴角 θ = atan(r·tanHalfFov), r∈[0,1] 对角归一
    vec2 c = vUv - 0.5;
    c.x *= ub.cam.w;
    float r = length(c) / length(vec2(ub.cam.w, 1.0));
    float theta = atan(r * ub.cam.z);
    float vig = 1.0 - ub.optics.w * (1.0 - pow(cos(theta), 4.0));

    outColor = vec4(L * ub.optics.y * vig, 1.0);
}
