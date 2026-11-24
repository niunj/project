#version 460
/// ocean.frag — S2 完整光学:
///   方向相关天空反射 (探针级, 含 GGX 太阳耀斑) + 次表面散射 (浪涌透光)
///   + 雅可比白沫 + 岸线浪花 + 海岸线掩膜 + 降水消光雾。
/// 反射档位 oceanOpt.y: 0=天顶地平线梯度 (最省); 1=方向相关 (默认)。
layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vJacobian;

layout(set = 0, binding = 0) uniform Env {
    mat4 view_proj;
    mat4 model;
    vec4 camPosTime;
    vec4 sun;
    vec4 wind;
    vec4 misc;   // x=seaLevel y=size z=Hs(含涌浪) w=peakLen
    vec4 ext0;   // 海洋边界
    vec4 ext1;   // x=fadeWidth y=shape z=choppyQ w=beaufort
    vec4 atm;    // x=能见度(米) y=降水强度
    vec4 deepCol;
    vec4 shallowCol;
    vec4 sunLight;   // rgb=太阳直射色 w=昼因子
    vec4 ambient;    // rgb=天光环境色
    vec4 skyHorizon; // rgb=地平线天色
    vec4 skyZenith;  // rgb=天顶天色
    vec4 moon;
    vec4 astro;
    vec4 oceanWave[16];
    vec4 oceanOpt;       // x=swellH y=reflection z=curvature w=waveCount
    vec4 oceanOpt2;      // x=FFT 平铺边长 y/z=流向 w=FFT 开关
} env;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265;

/// GGX 法线分布 (太阳耀斑, 粗糙度随海况)。
float ggxD(float ndh, float a) {
    float a2 = a * a;
    float d = ndh * ndh * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

/// 方向相关天空色: 反射向量朝向的天空亮度 (探针级近似)。
/// 蔡司穹顶模型: 亮带在地平线随太阳方位偏移 (暮光时亮侧偏暖)。
vec3 skyProbe(vec3 R, vec3 sunDir) {
    vec3 zenith = env.skyZenith.rgb;
    vec3 horizon = env.skyHorizon.rgb;
    // 太阳方位对齐度: 远离太阳的地平线反射压暗 —— 真实低空海面的银色
    // 亮带只出现在太阳方位, 背向太阳一侧反射暗地平线 → 暗蓝灰海面。
    vec2 rAz = normalize(R.xz + vec2(1e-5));
    vec2 sAz = normalize(sunDir.xz + vec2(1e-5));
    float azAlign = clamp(dot(rAz, sAz), 0.0, 1.0);
    vec3 horizonR = horizon * mix(0.5, 1.0, azAlign * azAlign);
    // 基础: 按反射向量仰角的梯度。
    float up = clamp(R.y, 0.0, 1.0);
    vec3 col = mix(horizonR, zenith, up);
    // 太阳方位侧增亮 (地平线亮带): 反射向量与太阳同侧时加强。
    float horizonBoost = pow(azAlign, 4.0) * (1.0 - up) * env.sunLight.w;
    col += env.sunLight.rgb * horizonBoost * 0.35;
    return col;
}

void main() {
    if(env.atm.w<=0.) discard;
    // Explicit NoData from the terrain bridge. Never render unknown as deep water.
    if (vUV.y < 0.0) discard;
    float seaLevel = env.misc.x;
    float Hs = max(env.misc.z, 0.01);
    int reflectionLevel = int(clamp(env.oceanOpt.y, 0.0, 1.0));

    vec3 V = normalize(env.camPosTime.xyz - vWorldPos);
    vec3 N = normalize(vNormal);
    vec3 sunDir = normalize(env.sun.xyz);

    // 海岸线掩膜: vUV.x 存储地形高度 (CPU 侧采样, ENU 米), vUV.y 存储水深。
    float terrainHeight = vUV.x;
    float waterDepth = vUV.y;

    // 陆地 discard: 地形高度 > 海平面。
    if (terrainHeight > seaLevel + 0.5) discard;

    // 海岸线过渡: 水深 < 2m 时渐隐到陆地色。
    float shoreFactor = smoothstep(0.0, 2.0, waterDepth);

    // 距离 (雾/粗糙度衰减用; 曲率开启时含下沉量, 用视距近似)。
    float dist = length(vWorldPos - env.camPosTime.xyz);

    // ---- 海况粗糙度: 蒲福级 + 距离衰减 (远处波不可分辨 → 更粗糙) ----
    float beaufort = env.ext1.w;
    float roughB = clamp(beaufort / 9.0, 0.0, 1.0);
    float roughD = clamp(dist / 4000.0, 0.0, 1.0);
    float rough = clamp(0.06 + 0.24 * roughB + 0.3 * roughD, 0.04, 0.6);

    // ---- 菲涅尔 ----
    // P6: 全偏振菲涅尔 (Fs/Fp 折射角精确式, 对标 Triton ocean-frag:335-342)。
    // oceanOpt.y 高位标记 (>=2) 启用精确式; 0/1 仍走 Schlick (逐位兼容)。
    float fres;
    if (env.oceanOpt.y > 1.5) {
        // 折射方向 (IOR 海水 1.333); cosθ1=入射, cosθ2=折射
        vec3 Srefr = refract(-V, N, 1.0 / 1.333);
        float cosT1 = max(dot(V, N), 0.0);
        float cosT2 = max(dot(-Srefr, N), 0.0);
        const float IOR = 1.333;
        float fp = (cosT1 - IOR * cosT2) / (IOR * cosT2 + cosT1);
        float fs = (cosT2 - IOR * cosT1) / (IOR * cosT1 + cosT2);
        fres = clamp((fs * fs + fp * fp) * 0.5, 0.0, 1.0);
    } else {
        // Schlick 菲涅尔 (旧式)。
        fres = 0.02 + 0.98 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
        // 粗糙表面: 掠射反射权重下降 (能量向漫射转移)。
        fres *= mix(1.0, 0.72, rough / 0.6);
    }

    // ---- 反射 ----
    vec3 R = reflect(-V, N);
    vec3 skyCol;
    if (reflectionLevel >= 1) {
        skyCol = skyProbe(R, sunDir);
    } else {
        skyCol = mix(env.skyHorizon.rgb, env.skyZenith.rgb, clamp(V.y, 0.0, 1.0));
    }

    // ---- 水体颜色 ----
    vec3 deep = env.deepCol.rgb;
    vec3 shallow = env.shallowCol.rgb;
    float crest = clamp((vWorldPos.y - seaLevel) / Hs * 0.5 + 0.5, 0.0, 1.0);
    vec3 waterCol = mix(deep, shallow, crest * 0.6);

    // 水深雾: 深水更暗, 浅水更透明。
    float depthFog = 1.0 - exp(-waterDepth * 0.1);
    waterCol *= mix(0.6, 1.0, depthFog);

    // ---- S2: 层云阴影 (与云量图同源: fbm 沿太阳方向投影采样) ----
    // O2: 云底高度由 atm.z 下发 (宿主从 ICloudQuery::cloudBase() 取真实值,
    // 无云/未提供时回退 1200m); 太阳高度角由 env.sun.w 下发。
    float cloudShade = 1.0;
    if (sunDir.y > 0.1 && env.wind.w > 0.02) {   // 有云量且有太阳
        // 地面点沿太阳方向上行至云底高度的采样点。
        float cloudBase = env.atm.z > 1.0 ? env.atm.z : 1200.0;
        vec2 sUV = (vWorldPos.xz + sunDir.xz * max(cloudBase-vWorldPos.y,0.0) / max(sunDir.y,0.1)) / 600.0;
        // 与 cloud_sheet.frag 同 fbm (特征尺度/漂移一致)。
        vec2 wdir = normalize(env.wind.xy + vec2(0.0001));
        sUV -= wdir * env.camPosTime.w * env.wind.z / 600.0;
        float cn = 0.0, ca = 0.5;
        vec2 sp = sUV * 0.35;
        for (int i = 0; i < 5; ++i) {
            vec2 ip = floor(sp), fp = fract(sp);
            fp = fp * fp * (3.0 - 2.0 * fp);
            float ha = fract(sin(dot(ip, vec2(127.1, 311.7))) * 43758.5453);
            float hb = fract(sin(dot(ip + vec2(1, 0), vec2(127.1, 311.7))) * 43758.5453);
            float hc = fract(sin(dot(ip + vec2(0, 1), vec2(127.1, 311.7))) * 43758.5453);
            float hd = fract(sin(dot(ip + vec2(1, 1), vec2(127.1, 311.7))) * 43758.5453);
            cn += ca * mix(mix(ha, hb, fp.x), mix(hc, hd, fp.x), fp.y);
            sp *= 2.03; ca *= 0.5;
        }
        float cov = clamp(env.wind.w, 0.0, 1.0);
        // 与 cloud_sheet.frag / CloudModule.cpp 同源的分位数阈值映射
        // (fbm 实测值域 [0.15,0.81], 均值 0.482, 标准差 0.13)。
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
        float th = kMean + kSigma * z;
        float alpha = smoothstep(th - 0.06, th + 0.06, cn);
        cloudShade = 1.0 - alpha * 0.45;   // 阴影最多压暗 45%
    }

    // 水面接收太阳直射 + 天光环境 (物理光色: 暮光暖红、夜间趋暗)。
    // 太阳朗伯方向项: 波法线朝太阳侧亮、背侧暗 —— 掠射视角下波浪起伏
    // 产生明暗斑驳 (太阳存在感的主要来源之一)。
    float ndl = max(dot(N, sunDir), 0.0);
    vec3 surfaceLight = env.sunLight.rgb * 0.9 * cloudShade * (0.55 + 0.45 * ndl)
                      + env.ambient.rgb;
    waterCol *= surfaceLight;

    // ---- S2: 次表面散射 (浪涌透光) ----
    // 波峰背光侧: 太阳从浪后透射, 波高越高、视线-太阳对射越强 → 青绿增亮。
    float sss = 0.0;
    if (sunDir.y > 0.0) {   // 仅昼间
        float backLit = clamp(dot(V, -sunDir), 0.0, 1.0);          // 对射
        float crestF = clamp((vWorldPos.y - seaLevel) / max(Hs, 0.01), 0.0, 1.0);
        float thinness = 1.0 - depthFog;                            // 浅水更透
        sss = pow(backLit, 3.0) * crestF * thinness;
    }
    waterCol += vec3(0.02, 0.18, 0.14) * sss * env.sunLight.rgb * 2.0;

    // ---- S2: GGX 太阳耀斑 (粗糙度随海况/距离) ----
    // 标定: 原 spec=specD/(4π) 镜面亮度仅 ~10/255 不可见; 去除 4π 归一
    // 并提高增益后, 掠射反射方向与太阳对准处呈亮闪烁 (HDR 经 aces 压回)。
    vec3 H = normalize(V + sunDir);
    float ndh = max(dot(N, H), 0.0);
    float specD = ggxD(ndh, rough);
    float spec = specD * env.sunLight.w * 1.6;

    vec3 col = mix(waterCol, skyCol, clamp(fres, 0.0, 1.0));
    col += spec * env.sunLight.rgb * 1.2;

    // ---- 月光照明 (夜间月亮存在感) ----
    // env.moon: xyz=月亮方向 ENU (月下时指向地心下方), w=月相 0朔..1望 (<0 无星历)。
    // 朗伯微光 (波面明暗) + 冷色镜面反射带 (月光耀斑, 比太阳弱 1-2 个量级)。
    float moonUp = step(0.0, env.moon.y) * step(0.0, env.moon.w);
    if (moonUp > 0.0) {
        vec3 moonDir = normalize(env.moon.xyz);
        // 波面朗伯: 月光色 ~4100K 冷白, 满月照度约为太阳的 1/400000,
        // 视觉标定 (aces 后夜海应有隐约可辨的波面明暗与月光色)。
        float ndlm = max(dot(N, moonDir), 0.0);
        col += vec3(.78,.83,1.) * ndlm * (2.5e-6*env.moon.w*env.moon.w);
        // 镜面反射带: GGX 与太阳同型, 强度按月相衰减。
        vec3 Hm = normalize(V + moonDir);
        float ndhm = max(dot(N, Hm), 0.0);
        float specM = ggxD(ndhm, rough * 1.25) * (2.5e-6*env.moon.w*env.moon.w);
        col += specM * vec3(0.72, 0.80, 0.95);
    }

    // ---- S1: 物理白沫 (P2 完整 2×2 雅可比 + 风成覆盖 + 岸线浪花) ----
    // 2×2 行列式值域: 平静 J≈1, 波面收敛 J<1, 折叠 J<0 (旧对角近似基准
    // 0.78..0.92 同语义平移: 收敛起点≈0.82, 完全破碎≈0.62 —— 剪切项使
    // 交叉波提前触发, 阈值略抬保持同量级覆盖率)。
    float foam = 1.0 - clamp((vJacobian - 0.62) / (0.82 - 0.62), 0.0, 1.0);
    float windSpeed = env.wind.z;
    if (windSpeed > 6.5)
        foam = clamp(foam + 0.0035 * (windSpeed - 6.5), 0.0, 1.0);
    float surfFoam = (1.0 - smoothstep(0.3, 3.0, waterDepth)) *
                     (0.5 + 0.3 * sin(vWorldPos.x * 0.8 + vWorldPos.z * 0.6 +
                                      env.camPosTime.w * 1.5));
    foam = clamp(foam + surfFoam * 0.55, 0.0, 1.0);

    vec3 foamCol = vec3(0.88, 0.92, 0.94) *
                   (env.sunLight.rgb + env.ambient.rgb * 2.0);
    col = mix(col, foamCol, foam * 0.6);

    // 海洋边界软渐隐: edge = 片元到海域边界的内距离 (>0 在海内)。
    vec2 bd = vWorldPos.xz - env.ext0.xy;
    float edge;
    if (env.ext1.y > 0.5)
        edge = min(env.ext0.z - abs(bd.x), env.ext0.w - abs(bd.y));
    else
        edge = env.ext0.z - length(bd);
    float boundsA = smoothstep(0.0, max(env.ext1.x, 1.0), edge);
    if (boundsA < 0.01) discard;

    // 海岸线混合: 近岸处混合陆地/沙滩色 (岸滩同样随昼夜受光)。
    vec3 landColor = vec3(0.5, 0.45, 0.35) *
                     (env.sunLight.rgb * 0.8 + env.ambient.rgb * 1.5);
    col = mix(landColor, col, shoreFactor);

    // 距离雾 (能见度由 EnvFrame 下发; 降水增强消光)。
    float visibility = max(env.atm.x, 100.0) * (1.0 - 0.6 * env.atm.y);
    visibility = max(visibility, 100.0);
    float fogFactor = 1.0 - exp(-dist / visibility);
    vec3 fogColor = mix(env.skyHorizon.rgb, env.skyZenith.rgb, 0.3);
    col = mix(col, fogColor, fogFactor * 0.5);

    float radius=env.ambient.w;
    float localOpacity=env.atm.w;
    if(radius>0.) localOpacity*=1.-smoothstep(.65*radius,radius,length(vWorldPos.xz-env.camPosTime.xz));
    outColor = vec4(col, 0.92 * boundsA * shoreFactor * (1.0 - fogFactor * 0.3));
    outColor.a *= localOpacity;
}
