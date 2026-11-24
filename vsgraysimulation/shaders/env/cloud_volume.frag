#version 460
/// cloud_volume.frag — P3-4b 体积积云 (ray-march, 重写版):
///   3D fbm 密度场 + 高度整形 (碗/砧) + Beer-Lambert 自阴影 + powder 银边。
///   壳体由 cloud_volume.vert 提供: ENU 相机跟随水平网格, 每个片元有稳定的
///   水平世界坐标 (vWorldPos.xz), 从相机到该点的射线在云底/层顶云盒内 march
///   一个共享的 3D 体积场 (风漂移平移, 云团以 ENU 锚点为中心散布 —— 与
///   cloud_sheet/ICloudQuery 同源, 而非每片元一团噪声)。
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vWorldPos;   // 壳体上表面 ENU 世界坐标 (y=云底层底)
layout(location = 2) in float vBase;      // 云底层底高度
layout(location = 3) in float vThick;     // 云层厚

layout(set = 0, binding = 0) uniform Env {
    mat4 view_proj;
    mat4 model;
    vec4 camPosTime;   // xyz=相机 ENU, w=时间
    vec4 sun;          // xyz=太阳方向 ENU, w=高度角
    vec4 wind;         // x=风东向 y=风南向 z=风速 w=云量
    vec4 misc;         // x=覆盖率 y=类型(Cumulus=3/Cumulonimbus=4) z=降水 w=密度
    vec4 ext0;         // x=层底 y=层厚 z=unused w=覆盖调制
    vec4 ext1;
    vec4 atm;          // x=能见度(米) y=降水强度
    vec4 deepCol;
    vec4 shallowCol;
    vec4 sunLight;     // rgb=太阳直射色 w=昼因子
    vec4 ambient;
    vec4 skyHorizon;
    vec4 skyZenith;
    vec4 moon;
    vec4 astro;
    vec4 oceanWave[16];
    vec4 oceanOpt;
} env;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265;

// ===========================================================================
// 3D 值噪声 (与 CloudModule.cpp hash3/vnoise3/cloudf3 逐公式同源)
// ===========================================================================
float hash3(vec3 p) {
    uvec3 cell=uvec3(ivec3(p));
    uint h=cell.x*73856093u ^ cell.y*19349663u ^ cell.z*83492791u;
    h^=h>>16;h*=2246822519u;h^=h>>13;h*=3266489917u;h^=h>>16;
    return float(h&0x00ffffffu)/16777216.0;
}
float noise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a000 = hash3(i);
    float a100 = hash3(i + vec3(1, 0, 0));
    float a010 = hash3(i + vec3(0, 1, 0));
    float a110 = hash3(i + vec3(1, 1, 0));
    float a001 = hash3(i + vec3(0, 0, 1));
    float a101 = hash3(i + vec3(1, 0, 1));
    float a011 = hash3(i + vec3(0, 1, 1));
    float a111 = hash3(i + vec3(1, 1, 1));
    return mix(mix(mix(a000, a100, f.x), mix(a010, a110, f.x), f.y),
               mix(mix(a001, a101, f.x), mix(a011, a111, f.x), f.y), f.z);
}
/// 5-octave fbm (与 CPU cloudf3 同参数: 0.5 权, 2.03 频率倍)。
float fbm3(vec3 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) {
        v += a * noise3(p);
        p *= 2.03;
        a *= 0.5;
    }
    return v;
}
/// coverage → 密度阈值 (分位数映射, 与 CPU kFbmCoverageThreshold 严格同源)。
float coverageThreshold(float cov) {
    float kMean = 0.482, kSigma = 0.130;
    float p = clamp(1.0 - cov, 1e-4, 1.0 - 1e-4);
    float z;
    if (p < 0.02425) {
        float q = sqrt(-2.0 * log(p));
        float num = (((((-7.784894002430293e-03 * q - 3.223964580411365e-01) * q
                       - 2.400758277161838) * q - 2.549732539343734) * q
                       + 4.374664141464968) * q + 2.938163982698783);
        float den = ((((7.784695709041462e-03 * q + 3.224671290700398e-01) * q
                      + 2.445134137142996) * q + 3.754408661907416) * q + 1.0);
        z = num / den;
    } else if (p <= 0.97575) {
        float q = p - 0.5;
        float r = q * q;
        float num = (((((-3.969683028665376e+01 * r + 2.209460984245205e+02) * r
                       - 2.759285104469687e+02) * r + 1.383577518672690e+02) * r
                       - 3.066479806614716e+01) * r + 2.506628277459239);
        float den = (((((-5.447609879822406e+01 * r + 1.615858368580409e+02) * r
                       - 1.556989798598866e+02) * r + 6.680131188771972e+01) * r
                       - 1.328068155288572e+01) * r + 1.0);
        z = num * q / den;
    } else {
        float q = sqrt(-2.0 * log(1.0 - p));
        float num = (((((-7.784894002430293e-03 * q - 3.223964580411365e-01) * q
                       - 2.400758277161838) * q - 2.549732539343734) * q
                       + 4.374664141464968) * q + 2.938163982698783);
        float den = ((((7.784695709041462e-03 * q + 3.224671290700398e-01) * q
                      + 2.445134137142996) * q + 3.754408661907416) * q + 1.0);
        z = -num / den;
    }
    return kMean + kSigma * z;
}

// ===========================================================================
/// 世界坐标连续 3D 体积场 (cloudDensityField): 与 cloud_sheet 同源 600m 特征
/// 尺度 + 米制风漂移, 云团由覆盖阈值自然成团 (非显式团块循环)。与 CPU
/// CloudField.h density 同参 (coverage/density_k 显式传入), 保证同源。
float cloudDensityField(vec3 p, float coverage, float density_k, int ltype) {
    if (coverage <= 0.0 || density_k <= 0.0) return 0.0;
    // Deterministic world-space advection shared with CloudField.h.
    vec3 q = p - vec3(env.wind.x, 0.0, env.wind.y) * env.camPosTime.w * env.wind.z;
    // 特征尺度 600m (与 sheet 同源): 频率 = 1/600。
    float freq = 1.0 / 600.0;
    vec3 np = q * freq;
    float base = fbm3(np * 0.5);
    float detail = fbm3(np * 2.5 + 11.7);
    float shape = base - detail * 0.15;
    // 高度整形: 云底凸、云顶平 (相对层底/层厚)。
    float hNorm = (p.y - vBase) / max(vThick, 1.0);
    float bowl = smoothstep(0.0, 0.2, hNorm) * (1.0 - smoothstep(0.6, 1.0, hNorm));
    shape *= bowl;
    float th = coverageThreshold(coverage * 0.55 + 0.08);
    return smoothstep(th, th + 0.12, shape) * clamp(density_k * 1.6, 0.4, 1.0);
}

// ===========================================================================
// Ray-march 盒体求交 (轴对齐: 层底~层顶, 水平无限 → 用片元水平范围)
// ===========================================================================
vec2 boxHit(vec3 ro, vec3 rd, vec3 boxMin, vec3 boxMax) {
    vec3 inv = 1.0 / rd;
    vec3 t0 = (boxMin - ro) * inv;
    vec3 t1 = (boxMax - ro) * inv;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    float tminf = max(max(tmin.x, tmin.y), tmin.z);
    float tmaxf = min(min(tmax.x, tmax.y), tmax.z);
    return vec2(max(tminf, 0.0), tmaxf);
}

void main() {
    if(env.atm.w<=0.) discard;
    vec3 camPos = env.camPosTime.xyz;
    vec3 sunDir = normalize(env.sun.xyz);
    int ltype = int(env.misc.y + 0.5);
    float coverage = clamp(env.misc.x, 0.0, 1.0);
    float densityK = clamp(env.misc.w, 0.0, 1.0);
    float precip = clamp(env.misc.z, 0.0, 1.0);

    // 云盒: 以层中部为中心 (云团实际分布高度), 覆盖层底~层顶 + 余量;
    // 水平范围取壳体附近 (网格中心 ± 3km, 与 sheet 同源量级)。
    vec3 shellC = vec3(vWorldPos.x, vBase + vThick * 0.5, vWorldPos.z);
    vec3 half_ = vec3(3000.0, vThick * 0.5 + 200.0, 3000.0);
    vec3 boxMin = shellC - half_;
    vec3 boxMax = shellC + half_;

    vec3 ro = camPos;
    vec3 rd = normalize(vWorldPos - camPos);
    vec2 tRange = boxHit(ro, rd, boxMin, boxMax);
    if (tRange.y <= tRange.x) discard;
    float t0 = tRange.x;
    float t1 = min(tRange.y, tRange.x + 2000.0);

    if (coverage <= 0.0 || densityK <= 0.0) discard;

    // ---- Ray-march: 世界坐标连续 3D 噪声场 (与 cloud_sheet 同源 600m 尺度 +
    // 风漂移; 云团由覆盖阈值自然成团, 非显式团块循环 —— 避免每片元一团噪声)。
    // 密度场与 CPU continuous field 同参 (coverage/density_k 显式传入)。
    float transmittance = 1.0;
    float powderAccum = 0.0;
    float lightAccum = 0.0;
    int steps = (ltype == 4) ? 32 : 20;
    float step0 = (t1 - t0) / float(steps);
    float t = t0 + 0.5 * step0;
    for (int i = 0; i < steps; ++i) {
        vec3 p = ro + rd * t;   // 世界坐标 (ENU), 云团连续分布。
        // 云团由 3D 噪声 + 覆盖阈值自然成团 (与 cloud_sheet 同源的连续场思路,
        // 扩展到 3D: 600m 特征尺度, 风漂移, 覆盖阈值分位数映射)。
        float d = cloudDensityField(p, coverage, densityK, ltype);
        if (d > 0.02) {
            float extinction = d * 0.6;
            float T = exp(-extinction * step0 * 0.05);
            float powder = clamp(1.0 - exp(-extinction * step0 * 0.2), 0.0, 1.0);
            powderAccum += powder * T;
            lightAccum += d * T;
            transmittance *= T;
        }
        t += step0;
        if (t > t1 || transmittance < 0.02) break;
    }

    float alpha = 1.0 - transmittance;
    if (alpha < 0.03) discard;

    // ---- 着色 ----
    vec3 albedo = (ltype == 4) ? vec3(0.85, 0.85, 0.88) : vec3(0.92, 0.93, 0.95);
    albedo *= mix(1.0, 0.55, precip);
    float sunH = clamp(sin(radians(env.sun.w)), 0.0, 1.0);
    // Direct-light optical depth through the same world density (quality: 4 samples).
    float sunDepth = 0.0;
    vec3 lightPoint = ro + rd * (0.5 * (t0 + t1));
    float lightDistance = max(0.0, (vBase + vThick + 200.0 - lightPoint.y) / max(sunDir.y, 0.05));
    float lightStep = min(lightDistance, 16000.0) / 4.0;
    for (int j = 0; j < 4; ++j) {
        vec3 p = lightPoint + sunDir * ((float(j) + 0.5) * lightStep);
        sunDepth += cloudDensityField(p, coverage, densityK, ltype) * 0.03 * lightStep;
    }
    float selfShadow = exp(-min(sunDepth, 16.0));
    float silver = powderAccum * 0.3 * sunH;
    vec3 col = albedo * (env.sunLight.rgb * selfShadow * 0.8 + env.ambient.rgb);
    col += env.sunLight.rgb * silver;

    float dist = length(vWorldPos - camPos);
    float visibility = max(env.atm.x, 100.0);
    float fogFactor = 1.0 - exp(-dist / visibility);
    col = mix(col, env.skyHorizon.rgb, fogFactor * 0.5);

    float radius=env.ambient.w;
    float localOpacity=env.atm.w;
    if(radius>0.) localOpacity*=1.-smoothstep(.65*radius,radius,length(vWorldPos.xz-env.camPosTime.xz));
    outColor = vec4(col, alpha * 0.85 * (1.0 - fogFactor * 0.2));
    // Partition optical depth so complementary LOD weights preserve extinction.
    outColor.a = 1.-pow(max(1.-outColor.a,1e-6),localOpacity);
}
