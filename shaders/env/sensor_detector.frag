// sensor_detector.frag —— 红方信号级 · 探测器段 (P2→P3)。
// 输入: 焦平面辐亮度 L (b1, optics 输出; IR 单通道 r / TV rgb 三通道) + 普朗克
//       LUT (b2, NETD→电子噪声换算)。
// 物理: 焦平面辐照 E = L·π/(4F#²) → 像元光子数 = E·A_pix·t_int/E_ph(λc)
//       → 光电子 = 光子×QE; 噪声 = 散粒(√n) + 读出 + NETD 等效 (Box-Muller 高斯);
//       满阱饱和; 输出 raw = n_e / 满阱 (归一化探测器电子图)。
// TV 波段 (λc<0.8μm, 硅探测器): r/g/b 三通道独立光电转换, 逐通道噪声种子去
//       相关; IR 保持单通道 (ch=0 加零偏移, 输出逐位兼容旧版)。
#version 460

layout(set = 0, binding = 0) uniform SensorUBO { mat4 vp; vec4 cam; vec4 env;
    vec4 band; vec4 optics; vec4 det; vec4 det2; vec4 elec; } ub;
layout(set = 0, binding = 1) uniform sampler2D inTex;
layout(set = 0, binding = 2) uniform sampler2D lutTex;

// Per-view UBO: misc.x = exact measurement seed (frame ID).
layout(set = 1, binding = 0) uniform SensorView { mat4 vp; vec4 cam; vec4 misc; vec4 eye; } pc;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outRaw;

const float PI = 3.14159265;
const float H_PLANCK = 6.62607015e-34;
const float C_LIGHT  = 2.99792458e8;

// LUT 采样: T→M_band(T) (512×1, 与 sensor_rad.frag 一致)
float bandExitance(float T) {
    float u = clamp((T - ub.band.x) / (ub.band.y - ub.band.x), 0.0, 1.0);
    return texture(lutTex, vec2(u * (1.0 - 1.0/512.0) + 0.5/512.0, 0.5)).r;
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
// Box-Muller 高斯
float gauss(vec2 seed) {
    float u1 = max(hash12(seed), 1e-6);
    float u2 = hash12(seed + 17.17);
    return sqrt(-2.0 * log(u1)) * cos(6.2831853 * u2);
}

// 单通道 光电转换+噪声+满阱 → raw。ch=通道种子偏移 (0=r, 1=g, 2=b; IR 恒 0)。
void detConvert(float L, float ch, out float raw, out float n_e, out float sigma) {
    // ---- 光电转换 ----
    float fnum = max(ub.optics.x, 0.5);
    float E_focal = L * PI / (4.0 * fnum * fnum);      // 焦平面辐照 W/m²
    float pitch_m = ub.det2.y * 1e-6;
    float A_pix = pitch_m * pitch_m;
    float t_int = max(ub.det.y, 1e-6);
    float lambdaC = max(ub.band.z, 0.1) * 1e-6;
    float E_ph = H_PLANCK * C_LIGHT / lambdaC;
    float n_photon = E_focal * A_pix * t_int / E_ph;
    n_e = n_photon * ub.det.x;                          // QE

    // ---- NETD → 电子域噪声 (dM/dT 在 T_ref 处中心差分, ε=1 黑体口径) ----
    float T_ref = ub.band.w;
    float dMdT = (bandExitance(T_ref + 1.0) - bandExitance(T_ref - 1.0)) / 2.0;
    float L_netd = dMdT / PI * ub.det.z;                // NETD(K)→辐亮度噪声
    float n_netd = L_netd * PI / (4.0 * fnum * fnum) * A_pix * t_int / E_ph * ub.det.x;
    n_netd = max(n_netd, 0.0);

    // ---- 复合噪声 (散粒 + 读出 + NETD) ----
    float read_e = ub.det2.x;
    sigma = sqrt(max(n_e, 0.0) + read_e * read_e + n_netd * n_netd);
    vec2 seed = gl_FragCoord.xy + vec2(pc.misc.x * 0.731, pc.misc.x * 0.377)
              + vec2(ch * 53.17, ch * 97.31);           // ch=0 → 零偏移 (IR 逐位兼容)
    n_e = max(n_e + gauss(seed) * sigma, 0.0);

    // ---- 满阱饱和 → raw 归一 ----
    float well = max(ub.det.w * 1000.0, 1.0);           // ke- → e-
    raw = clamp(n_e / well, 0.0, 1.0);
}

void main() {
    const bool isTV = ub.band.z < 0.8;
    vec3 Lin = texture(inTex, vUv).rgb;

    float raw, n_e, sigma;
    detConvert(Lin.r, 0.0, raw, n_e, sigma);            // r 通道 (IR = 全部)

    if (isTV) {
        float g_raw, g_e, g_s, b_raw, b_e, b_s;
        detConvert(Lin.g, 1.0, g_raw, g_e, g_s);
        detConvert(Lin.b, 2.0, b_raw, b_e, b_s);
        outRaw = vec4(raw, g_raw, b_raw, 1.0);          // TV: rgb = 三通道 raw
    } else {
        outRaw = vec4(raw, n_e, sigma, 1.0);            // IR: r=raw, g=电子数, b=σ
    }
}
