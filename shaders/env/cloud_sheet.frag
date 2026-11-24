#version 460
/// cloud_sheet.frag — v4 类型化水平云层:
///   卷云 (薄纱拉伸/各向异性) / 层云 (均匀) / 层积云 (团块);
///   fbm 云量 + 风漂移 + 降水变暗 + 云底雨幡 + 距离雾 (能见度真值)。
layout(location = 0) in vec2 vUV;
layout(location = 1) in float vAlt;

layout(set = 0, binding = 0) uniform Env {
    mat4 view_proj;
    mat4 model;
    vec4 camPosTime;
    vec4 sun;
    vec4 wind;
    vec4 misc;        // x=层尺寸 y=覆盖率 z=高度 w=类型枚举
    vec4 ext0;        // x=降水 y=层厚 z=密度
    vec4 ext1;
    vec4 atm;         // x=能见度(米) y=降水强度
    vec4 deepCol;
    vec4 shallowCol;
    vec4 sunLight;    // rgb=太阳直射色 w=昼因子
    vec4 ambient;     // rgb=天光环境色
    vec4 skyHorizon;  // rgb=地平线天色
    vec4 skyZenith;   // rgb=天顶天色
    vec4 moon;
    vec4 astro;
    vec4 oceanWave[16];
    vec4 oceanOpt;
} env;

layout(location = 0) out vec4 outColor;

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0);
    float a = hash(i), b = hash(i + vec2(1, 0));
    float c = hash(i + vec2(0, 1)), d = hash(i + vec2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) { v += a * noise(p); p *= 2.03; a *= 0.5; }
    return v;
}

// 覆盖率 → fbm 密度阈值 (分位数映射)。
// 与 CloudModule.cpp 的 kFbmCoverageThreshold() 严格同源 (同常数)。
// 原因: 5-octave fbm 实测值域约 [0.15,0.81] (均值 0.48), 直接用 1-cover
// 作阈值会导致低云量时完全无云; 此处按实测分布的分位数映射, 使
// cover 语义 = 平均云覆盖率。
float coverageThreshold(float cov) {
    float kMean = 0.482;
    float kSigma = 0.130;
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

void main() {
    if(env.atm.w<=0.) discard;
    float time = env.camPosTime.w;
    float cover = env.misc.y;
    int ltype = int(env.misc.w + 0.5);
    float precip = clamp(env.ext0.x, 0.0, 1.0);
    float density_k = clamp(env.ext0.z, 0.0, 1.0);

    // 风漂移 (风向 xz, 风速驱动); 云团特征尺度 ~ 600 m。
    vec2 windDir = normalize(env.wind.xy + vec2(0.0001));
    vec2 drift = windDir * time * env.wind.z;
    vec2 uv = vUV - drift / 600.0;

    // C3: 片元 ENU 世界位置到相机的真实距离 (供 LOD 与雾)。
    // vUV 1 单位 = 600 m (与 CPU/云影同源的特征尺度)。
    vec3 worldPos = vec3(vUV.x * 600.0, vAlt, vUV.y * 600.0);
    float dist = length(worldPos - env.camPosTime.xyz);
    // 高频细节衰减: 掠射角下远处片元一个像素跨越数百米, 高频噪声被采样成
    // 亮暗条带。随距离把高频 octave 权重衰减回 fbm 均值 (0.482, 保持云量
    // 分位数映射不变), 4000 m 处衰减 ~63%。
    float detailW = exp(-dist / 4000.0);
    const float kFbmMean = 0.482;
    // 远处云体并雾化: alpha 略升 (层连片) + 雾色权重升 (大气透视)。
    float far = 1.0 - exp(-dist / 5000.0);

    // ---- 类型化成形 ----
    float a;
    vec3 albedo = vec3(0.92, 0.93, 0.95);
    if (ltype == 0) {
        // 卷云: 高空薄纱 —— 沿风向强拉伸, 覆盖锐减, 半透明。
        vec2 streak = vec2(dot(uv, windDir), dot(uv, vec2(-windDir.y, windDir.x)));
        float n = fbm(vec2(streak.x * 0.08, streak.y * 0.5));
        a = smoothstep(coverageThreshold(cover) - 0.06,
                       coverageThreshold(cover) + 0.06, n) * 0.55;
        albedo = vec3(0.95, 0.95, 0.98);
    } else if (ltype == 2) {
        // 层积云: 均匀层 + 团块起伏 (高频细节随距离衰减)。
        float n = fbm(uv * 0.35);
        float n2 = mix(kFbmMean, fbm(uv * 1.6 + 7.3), detailW);
        float d = n * 0.55 + n2 * 0.45;
        float th = coverageThreshold(cover);
        a = smoothstep(th - 0.06, th + 0.06, d);
    } else {
        // 层云 (默认): 一期行为 (高频 n2 随距离衰减)。
        float n = fbm(uv * 0.35);
        float n2 = mix(kFbmMean, fbm(uv * 0.9 + 7.3), detailW);
        float d = n * 0.7 + n2 * 0.3;
        float th = coverageThreshold(cover);
        a = smoothstep(th - 0.06, th + 0.06, d);
    }

    // 密度调制不透明度。
    a *= mix(0.5, 1.0, density_k);
    // 远处层连片 (alpha 向上抬, 避免远云碎化闪烁)。
    a = mix(a, min(a * 1.5 + 0.05, 1.0), far * 0.5);
    if (a < 0.015) discard;

    // 物理照明: 反照率 × (太阳直射 × 自遮蔽 + 天光环境)。
    float sunH = clamp(sin(radians(env.sun.w)), 0.0, 1.0);
    float selfShadow = mix(0.75, 1.0, sunH);
    // 降水: 云体变暗增厚 (光学厚度上升)。
    albedo *= mix(1.0, 0.55, precip);
    selfShadow *= mix(1.0, 0.7, precip);
    vec3 col = albedo * (env.sunLight.rgb * selfShadow + env.ambient.rgb);

    // 云底雨幡 (降水 > 0.3): 云底之下垂挂灰暗条纹。
    if (precip > 0.3) {
        float shaft = noise(vec2(vUV.x * 3.0, vUV.y * 3.0)) * precip;
        col = mix(col, col * 0.6, shaft * 0.5);
    }

    // 距离雾: 远处雾色权重增强 (与 far 一致的大气透视)。
    float visibility = max(env.atm.x, 100.0);
    float fogFactor = 1.0 - exp(-dist / visibility);
    vec3 fogColor = env.skyHorizon.rgb;
    col = mix(col, fogColor, fogFactor * (0.4 + 0.3 * far));

    float radius=env.ambient.w;
    float localOpacity=env.atm.w;
    if(radius>0.) localOpacity*=1.-smoothstep(.65*radius,radius,length(worldPos.xz-env.camPosTime.xz));
    outColor = vec4(col, a * 0.75 * (1.0 - fogFactor * 0.2));
    // Partition optical depth so complementary LOD weights preserve extinction.
    outColor.a = 1.-pow(max(1.-outColor.a,1e-6),localOpacity);
}
