#version 460
/// cloud_sprite.frag — v4 类型化积云公告板:
///   积云 (蓬松团块) / 积雨云 (塔状 + 砧状顶 + 雨幡);
///   银边 (背光边缘发亮) + 向光面渐变 + 底部暗 + 降水变暗 + 距离雾。
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vWorldPos;

layout(set = 0, binding = 0) uniform Env {
    mat4 view_proj;
    mat4 model;
    vec4 camPosTime;
    vec4 sun;
    vec4 wind;
    vec4 misc;        // x=覆盖率 y=类型枚举 z=降水 w=密度
    vec4 ext0;
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
    for (int i = 0; i < 5; ++i) { v += a * noise(p); p *= 2.11; a *= 0.5; }
    return v;
}

void main() {
    if(env.atm.w<=0.) discard;
    float cover = env.misc.x;
    int ltype = int(env.misc.y + 0.5);
    float precip = clamp(env.misc.z, 0.0, 1.0);
    float density_k = clamp(env.misc.w, 0.0, 1.0);

    // UV 噪声扰动, 破除公告板方块感/硬边。
    vec2 uv = vUV;
    uv += 0.10 * (vec2(noise(vUV * 8.0), noise((vUV + 17.0) * 8.0)) - 0.5);

    // ---- 类型化轮廓 ----
    vec2 c = uv - 0.5;
    float body;
    float verticality;   // 塔状程度 (着色用)
    if (ltype == 4) {
        // 积雨云: 塔身 (下部收窄) + 砧状顶 (顶部横向展开)。
        float tower = smoothstep(1.15, 0.35, length(c * vec2(1.25 - uv.y * 0.55, 0.9)) * 2.0);
        float anvil = smoothstep(1.05, 0.45,
                                 length((uv - vec2(0.5, 0.82)) * vec2(0.45, 1.6)) * 2.2);
        body = max(tower, anvil);
        verticality = 1.0;
    } else {
        // 积云 (默认): 一期软圆盘 (竖直略压扁)。
        body = smoothstep(1.05, 0.4, length(c * vec2(0.95, 1.35)) * 2.1);
        verticality = 0.0;
    }

    // 蓬松细节 + 顶部团块。
    float detail = fbm(uv * 4.5);
    float puffs = fbm(uv * vec2(3.5, 6.0) + vec2(0.0, 2.0));
    float density = clamp(0.35 + detail * 0.55 + puffs * 0.35 * smoothstep(0.2, 0.8, uv.y), 0.0, 1.0);

    float a = body * density;
    a = pow(a, 1.1);
    a *= clamp(0.25 + cover, 0.0, 1.0);
    a *= mix(0.6, 1.0, density_k);
    if (a < 0.02) discard;

    // ---- 着色: 向光面渐变 + 底部暗 + 银边 ----
    float vert = smoothstep(0.05, 0.9, vUV.y);
    vec3 albedo = mix(vec3(0.62, 0.65, 0.70), vec3(1.0, 1.0, 1.0), vert);
    // 降水: 云体变暗 (积雨云底尤其灰暗)。
    albedo *= mix(1.0, 0.5 + 0.2 * (1.0 - uv.y), precip);
    vec3 col = albedo * (env.sunLight.rgb * mix(0.55, 1.0, vert) + env.ambient.rgb);

    // 月光照明: 夜间 (sunLight→0) 云体被月光照亮 —— 冷色调, 强度按月相
    // (env.moon.w<0 = 无星历; 月亮方向月下时指向地心下方, 面向项归零)。
    float moonUp = step(0.0, env.moon.y) * step(0.0, env.moon.w);
    if (moonUp > 0.0) {
        vec3 moonDir = normalize(env.moon.xyz);
        float moonFace = clamp(dot(normalize(vWorldPos - env.camPosTime.xyz),
                                   -moonDir), 0.0, 1.0);
        col += vec3(0.58, 0.66, 0.82) * env.moon.w *
               (0.10 + 0.16 * vert) * (0.35 + 0.65 * moonFace);
    }

    // 银边 (silver lining): 背光侧云体边缘发亮 —— 视线与太阳方向相反且
    // 云体密度低 (边缘) 时, 光透过云体前向散射。
    vec3 V = normalize(env.camPosTime.xyz - vWorldPos);
    vec3 sunDir = normalize(env.sun.xyz);
    float backLit = clamp(dot(V, -sunDir), 0.0, 1.0);
    float edge = 1.0 - smoothstep(0.15, 0.55, a);   // 密度低处=云缘
    float silver = pow(backLit, 3.0) * edge * env.sunLight.w;
    col += env.sunLight.rgb * silver * 0.85;

    // 积雨云雨幡: 云底之下垂挂灰暗条纹 (uv.y 低处)。
    if (ltype == 4 || precip > 0.3) {
        float shaft = noise(vec2(uv.x * 5.0, uv.y * 2.0)) *
                      (1.0 - smoothstep(0.0, 0.45, uv.y)) *
                      max(precip, ltype == 4 ? 0.6 : 0.0);
        col = mix(col, col * 0.55, shaft * 0.6);
    }

    // 距离雾: 片元到相机的真实 ENU 距离, 能见度由 EnvFrame 下发。
    float dist = length(vWorldPos - env.camPosTime.xyz);
    float visibility = max(env.atm.x, 100.0);
    float fogFactor = 1.0 - exp(-dist / visibility);
    vec3 fogColor = env.skyHorizon.rgb;
    col = mix(col, fogColor, fogFactor * 0.3);

    float radius=env.ambient.w;
    float localOpacity=env.atm.w;
    if(radius>0.) localOpacity*=1.-smoothstep(.65*radius,radius,length(vWorldPos.xz-env.camPosTime.xz));
    outColor = vec4(col, clamp(a, 0.0, 0.92) * (1.0 - fogFactor * 0.15));
    // Partition optical depth so complementary LOD weights preserve extinction.
    outColor.a = 1.-pow(max(1.-outColor.a,1e-6),localOpacity);
}
