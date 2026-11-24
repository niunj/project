// sensor_electronics.frag —— 红方信号级 · 电子学段 (P3→P4)。
// 输入: raw 探测器电子图 (IR: b1 单通道 r=归一电子; TV: rgb=归一三色)。
// 处理: AGC (32×32 网格采样 + 直方图分位拉伸) → ADC 量化 → Gamma。
//       TV 波段 (ub.band.z=λc μm < 0.8) 锚 = max(P95, 参考白) + ACES filmic
//       逐通道压缩 (§12.4 昼夜倒置修复); IR 保持单通道灰度 (逐位兼容旧输出)。
#version 460

layout(set = 0, binding = 0) uniform SensorUBO { mat4 vp; vec4 cam; vec4 env;
    vec4 band; vec4 optics; vec4 det; vec4 det2; vec4 elec; } ub;
layout(set = 0, binding = 1) uniform sampler2D inTex;
#ifdef SENSOR_SHARED_AGC
layout(set = 0, binding = 2) uniform sampler2D agcBounds;
#endif

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

const vec3 LUMA_W = vec3(0.2126, 0.7152, 0.0722);   // BT.709 亮度权值

// P0 辐亮度 → raw 归一电子域 (sensor_detector.frag detConvert 线性主链同式,
// 不含 PSF/渐晕空间重分布 —— 分位锚是全局统计, 空间重分布不影响量级)。
float rawOfL(float L) {
    const float PI = 3.14159265;
    const float E_PH = 6.62607015e-34 * 2.99792458e8 / (max(ub.band.z, 0.1) * 1e-6);
    const float fnum = max(ub.optics.x, 0.5);
    const float pitchM = ub.det2.y * 1e-6;
    const float tInt = max(ub.det.y, 1e-6);
    return L * PI / (4.0 * fnum * fnum) * pitchM * pitchM * tInt / E_PH * ub.det.x
         / max(ub.det.w * 1000.0, 1.0);
}

void main() {
    // TV 判别: band.z = λc (μm), 可见光波段 < 0.8 (192B UBO 约束下的波段标识)
    const bool isTV = ub.band.z < 0.8;
    const vec3 W = isTV ? LUMA_W : vec3(1.0, 0.0, 0.0);

#ifdef SENSOR_SHARED_AGC
    vec2 bounds = texelFetch(agcBounds, ivec2(0), 0).rg;
    float x_lo = bounds.r, x_hi = bounds.g;
#else
    // ---- AGC: 网格统计 min/max → 直方图 → 分位拉伸 ----
    const int GRID = 32;
    // TV 256 bins (P95 锚需 ~0.4% bin 宽); IR 保持 32 bins 逐位兼容旧输出
    const int BINS = isTV ? 256 : 32;
    float mn = 1e9, mx = -1e9;
    for (int y = 0; y < GRID; ++y)
        for (int x = 0; x < GRID; ++x) {
            vec2 uv = (vec2(float(x), float(y)) + 0.5) / float(GRID);
            float v = dot(texture(inTex, uv).rgb, W);
            mn = min(mn, v); mx = max(mx, v);
        }
    float range = max(mx - mn, 1e-6);

    uint hist[256];
    for (int i = 0; i < 256; ++i) hist[i] = 0u;
    for (int y = 0; y < GRID; ++y)
        for (int x = 0; x < GRID; ++x) {
            vec2 uv = (vec2(float(x), float(y)) + 0.5) / float(GRID);
            float v = dot(texture(inTex, uv).rgb, W);
            int idx = int(clamp((v - mn) / range * float(BINS - 1), 0.0, float(BINS - 1)));
            hist[idx]++;
        }

    float total = float(GRID * GRID);
    // 下分位 bin → x_lo
    float acc = 0.0; float x_lo = mn, x_hi = mx;
    for (int i = 0; i < BINS; ++i) {
        acc += float(hist[i]);
        if (acc >= total * ub.det2.w) {          // det2.w = agcLo (0.002)
            x_lo = mn + (float(i) + 0.5) / float(BINS) * range;
            break;
        }
    }

    if (isTV) {
        // TV 彩色链锚 (昼夜倒置修复 §12.4):
        //   x_hi = max(P95 直方图, 参考白 raw)。
        //   P95 (min(elec.x, 0.95)) 排除太阳盘/耀斑核心 (<5% 极亮尾) 对增益的
        //   压制 —— 正午帧不再被太阳盘 (辐亮度 ≈ 天空×2000) 压黑;
        //   参考白 = 1.0 W/m²/sr 的 raw 换算兜底真夜帧 —— 夜帧亮端 ≈ 2×夜天
        //   辐亮度, 纯帧内分位锚必把夜天拉满, 参考白与场景无关 → 夜天回归暗夜;
        //   多云帧 P95 (云亮端) ≫ 参考白 → AGC 场景自适应保留。
        acc = 0.0;
        for (int i = 0; i < BINS; ++i) {
            acc += float(hist[i]);
            if (acc >= total * min(ub.elec.x, 0.95)) {
                x_hi = mn + (float(i) + 0.5) / float(BINS) * range;
                break;
            }
        }
        x_hi = max(x_hi, rawOfL(1.0));
    } else {
        acc = 0.0;
        for (int i = 0; i < BINS; ++i) {
            acc += float(hist[i]);
            if (acc >= total * ub.elec.x) {      // elec.x = agcHi (0.998)
                x_hi = mn + (float(i) + 0.5) / float(BINS) * range;
                break;
            }
        }
    }
    x_lo = min(x_lo, x_hi - 1e-6);
#endif

#ifdef SENSOR_AGC_STATS
    outColor = vec4(x_lo, x_hi, 0.0, 1.0);
#else
    // ---- ADC 量化 (adcBits) + Gamma, 逐通道 ----
    float levels = exp2(clamp(ub.det2.z, 4.0, 16.0));
    float gamma = 1.0 / max(ub.elec.y, 0.01);

    if (isTV) {
        // 逐通道归一 → Narkowicz ACES filmic 近似: 高光平滑压缩 (耀斑核心
        // 渐近饱和无削顶硬边), 低端 toe 提升暗部; 逐通道应用 (ACES 惯例,
        // 高光去饱和为胶片特性)。替换旧线性 luma-gain (太阳盘/夜帧双向拉偏)。
        vec3 c = max(texture(inTex, vUv).rgb - x_lo, vec3(0.0)) / max(x_hi - x_lo, 1e-6);
        vec3 col = clamp(c * (2.51 * c + 0.03) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
        col = floor(col * levels) / levels;
        col = pow(col, vec3(gamma));
        outColor = vec4(col, 1.0);
    } else {
        float v = texture(inTex, vUv).r;
        float val = clamp((v - x_lo) / max(x_hi - x_lo, 1e-6), 0.0, 1.0);
        val = floor(val * levels) / levels;
        val = pow(val, gamma);
        outColor = vec4(val, val, val, 1.0);
    }
#endif
}
