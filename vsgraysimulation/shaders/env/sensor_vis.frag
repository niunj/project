// sensor_vis.frag —— 观察视口 (可见光近似) #2: 与 RT TV 链同源着色。
// 共享 RT 管线资源: albedo 贴图数组 (槽=贴图) + RGB 物理天空图 (#1) + 体积云 (#5)。
// 与 sensor_rad.vert 顶点契约一致 (loc0 props / loc1 world / loc2 uv / loc3 slot)。
// 照明式与 band_shade.comp TV 分支同源:
//   col = ρ·Esun/π·N·L·云影 + ρ·(晴空·τ_c + 云散射)·(0.5+0.5·N.y), 曝光归一后 γ 编码。
// 背景 (无几何 → 无片元) 由 CPU 每帧写 rgVis 动态 clear 色 (天顶 RGB, γ 已编码)。
#version 460
#extension GL_GOOGLE_include_directive : enable
#ifdef USE_VOXEL_CLOUD_FIELD
#include "voxel_cloud_sensor.glsl"
#endif

layout(set = 1, binding = 0) uniform SensorView { mat4 vp; vec4 cam; vec4 misc; vec4 eye; } pc;
// cam 复用: x=skyValid (RGB 天空图就绪), y=云底 m, z=云影强度, w=TV esun

#ifdef SENSOR_SKY_BACKGROUND
layout(location = 0) in vec2 vUv;
#else
layout(location = 0) in vec2 vProps;
layout(location = 1) in vec3 vWorld;
layout(location = 2) in vec2 vUv;
layout(location = 3) flat in uint vSlot;
#ifdef SENSOR_RADIANCE_OUTPUT
layout(location = 4) flat in uint vMaterial;
#endif
#endif

#ifdef SENSOR_RADIANCE_OUTPUT
// SensorView.cam remains near/far/FOV/aspect in the sensor aperture. Never
// reinterpret its near plane as observer skyValid, or its aspect as irradiance.
layout(set = 0, binding = 5) uniform SensorRadiometry {
    mat4 vp; vec4 cam; vec4 env; vec4 band; vec4 optics;
    vec4 det; vec4 det2; vec4 elec; vec4 env2;
} radiometry;
layout(set = 0, binding = 6) readonly buffer SpectralMaterials { vec4 materials[]; };
#endif

// set0 与 RT TV 链同源资源 (SensorPipeline::build 组装; 缺失时绑 1×1 占位)
layout(set = 0, binding = 0) uniform sampler2DArray albedoTex;
layout(set = 0, binding = 1) readonly buffer TexSlotBuf { vec4 texSlots[]; };  // (Kd.rgb, hasTex)
layout(set = 0, binding = 2) uniform sampler2D skyTex;    // TV RGB 天空图 (latlong)
// #5: 旧 2D 云覆盖图 (binding 3) 已删除 —— 云体由 3D 密度场 slab ray march 实时渲染
layout(set = 0, binding = 4) uniform VolUbo {
    vec4 a;   // 云底 m (<=0 禁用), 云影强度, 云顶 m, 覆盖率 0..1
    vec4 b;   // 密度系数 0..1, 风速 m/s, 风向x, 风向z
    vec4 c;   // 时间 s (漂移), IR 云发射辐亮度 (TV 未用), r10 云型 0-4, pad
} vol;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265;
float solarIrradiance() {
#ifdef SENSOR_RADIANCE_OUTPUT
    return max(radiometry.env.z,0.0);
#else
    return pc.cam.w;
#endif
}

/// ENU 方向 → 天空图 uv (与 band_shade.comp skyUv 同约定)
vec2 skyUv(vec3 dir) {
    const float el = asin(clamp(dir.y, -1.0, 1.0));
    const float az = atan(dir.x, dir.z);
    return vec2(az / (2.0 * PI) + 0.5, clamp(el / (0.5 * PI), 0.0, 1.0));
}

// ---------------------------------------------------------------------------
// #5: continuous 3D cloud matter density shared with the environment volume.
// 600m noise scale, deterministic integer hash, wind in m/s and time in seconds.
// Existing per-type extinction and radiative transfer remain below this block.
// ---------------------------------------------------------------------------
float cHash3(vec3 p) {
    uvec3 cell=uvec3(ivec3(p));
    uint h=cell.x*73856093u ^ cell.y*19349663u ^ cell.z*83492791u;
    h^=h>>16;h*=2246822519u;h^=h>>13;h*=3266489917u;h^=h>>16;
    return float(h&0x00ffffffu)/16777216.0;
}

float cNoise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a000 = cHash3(i);
    float a100 = cHash3(i + vec3(1, 0, 0));
    float a010 = cHash3(i + vec3(0, 1, 0));
    float a110 = cHash3(i + vec3(1, 1, 0));
    float a001 = cHash3(i + vec3(0, 0, 1));
    float a101 = cHash3(i + vec3(1, 0, 1));
    float a011 = cHash3(i + vec3(0, 1, 1));
    float a111 = cHash3(i + vec3(1, 1, 1));
    return mix(mix(mix(a000, a100, f.x), mix(a010, a110, f.x), f.y),
               mix(mix(a001, a101, f.x), mix(a011, a111, f.x), f.y), f.z);
}

float cFbm3(vec3 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) {
        v += a * cNoise3(p);
        p *= 2.03;
        a *= 0.5;
    }
    return v;
}

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

// Per-type optical coefficients; xyz legacy shape slots are not density inputs.
const vec4 CT_A[5] = vec4[5](
    vec4(3.0, 0.35, 1.30, 0.12),   // 0 卷云
    vec4(1.0, 1.00, 1.00, 1.00),   // 1 层云 (r9 锚)
    vec4(1.2, 0.80, 1.10, 1.00),   // 2 层积云
    vec4(0.7, 1.15, 1.35, 1.25),   // 3 积云
    vec4(2.0, 1.50, 1.60, 1.80));
int cloudTypeIdx() { return int(clamp(vol.c.z, 0.0, 4.0)); }

// Pure world-space density, identical to CloudField.h and cloud_volume.frag.
// dist/observer/cloud type affect radiometry or integration quality, never matter density.
float continuousCloudDensity(vec3 P, float base, float top, float coverage,
                             float density, vec2 wind, float speed, float time) {
    if (top <= base || coverage <= 0.0 || density <= 0.0) return 0.0;
    float windLength = length(wind);
    wind = windLength > 1.0e-6 ? wind / windLength : vec2(1.0, 0.0);
    vec3 np = (P - vec3(wind.x, 0.0, wind.y) * time * speed) / 600.0;
    float shape = cFbm3(np * 0.5) - 0.15 * cFbm3(np * 2.5 + 11.7);
    float h = (P.y - base) / max(top - base, 1.0);
    shape *= smoothstep(0.0, 0.2, h) * (1.0 - smoothstep(0.6, 1.0, h));
    float threshold = coverageThreshold(clamp(coverage, 0.0, 1.0) * 0.55 + 0.08);
    return smoothstep(threshold, threshold + 0.12, shape) * clamp(density * 1.6, 0.4, 1.0);
}
float cloudDensityCore(vec3 P, float dist) {
#ifdef USE_VOXEL_CLOUD_FIELD
    return voxelSensorExtinction(P);
#else

    if (vol.a.x <= 0.0) return 0.0; // Existing VolUbo disabled-cloud sentinel.
    return continuousCloudDensity(P, vol.a.x, vol.a.z, vol.a.w, vol.b.x,
                                  vol.b.zw, vol.b.y, vol.c.x);

#endif
}
float cloudDensity(vec3 P, float dist) {
    return cloudDensityCore(P, dist);
}
/// 视线-云 slab 交段 (pos 起点, dir 方向; t0/t1 为段端点 m, 相机前方)
bool cloudSlab(vec3 pos, vec3 dir, out float t0, out float t1) {
#ifdef USE_VOXEL_CLOUD_FIELD
    return voxelSensorSlab(pos,dir,t0,t1);
#else

    t0 = 0.0; t1 = -1.0;
    const float base = vol.a.x, top = vol.a.z;
    if (base <= 0.0 || top <= base) return false;
    float tn = 0.0, tf = 4.0e4;   // r10: 远端 30→40km (与 band_shade.comp 同调)
    if (abs(dir.y) > 1.0e-4) {
        const float ta = (base - pos.y) / dir.y, tb = (top - pos.y) / dir.y;
        tn = max(tn, min(ta, tb));
        tf = min(tf, max(ta, tb));
    } else {
        if (pos.y < base || pos.y > top) return false;        // 水平视线且层外
    }
    if (tf <= max(tn, 0.0)) return false;
    t0 = max(tn, 0.0);
    t1 = tf;
    return t1 > t0;

#endif
}
/// 云内消光系数 (1/km, 随密度系数缩放): r9 3.0→8.0 (与 band_shade.comp 同调);
/// r10 per 云型因子 (卷云冰晶光学薄 ×0.12 / 积雨云深厚 ×1.8)
float cloudExtPerKm() {
#ifdef USE_VOXEL_CLOUD_FIELD
    return 1000.;
#else
 return 8.0 * CT_A[cloudTypeIdx()].w * max(vol.b.x, 0.05); 
#endif
}
/// 第九轮多次散射 (与 band_shade.comp 严格同源): 太阳方向光学深度 + 3 八度
float cloudSunOD(vec3 Ps) {
#ifdef USE_VOXEL_CLOUD_FIELD
    return voxelSensorSunOD(Ps,normalize(pc.misc.yzw));
#else

    const float base = vol.a.x, top = vol.a.z;
    if (base <= 0.0 || top <= base) return 0.0;
    const vec3 sunDir = normalize(pc.misc.yzw);
    if (sunDir.y < 0.05) return 0.0;
    const float tTop = max((top - Ps.y) / max(sunDir.y, 0.05), 0.0);
    if (tTop <= 0.0) return 0.0;
    const float stepLen = tTop / 3.0;
    const float kExt = cloudExtPerKm() * 1.0e-3;
    float od = 0.0, t = 0.5 * stepLen;
    for (int i = 0; i < 3; ++i) {
        od += cloudDensityCore(Ps + sunDir * t, t) * stepLen;
        t += stepLen;
    }
    return od * kExt;

#endif
}
float cloudMultiScatter(float odSun) {
    return 0.50 * exp(-odSun) + 0.28 * exp(-odSun * 0.20) + 0.22 * exp(-odSun * 0.05);
}
/// 视线 march: τ_c = 穿云透射率, cloud = 云量积累 (与 band_shade.comp 同式)
void cloudMarch(vec3 pos, vec3 dir, int steps, float maxDistance, out float tauC, out float cloud) {
    tauC = 1.0; cloud = 0.0;
    float t0, t1;
    if (!cloudSlab(pos, dir, t0, t1)) return;
    t1=min(t1,maxDistance);if(t1<=t0)return;
    // r10d 近密远疏几何级数步长 (与 band_shade.comp 严格同源): 恒定大步长把
    // 8-20km 段 600m 形态特征欠采样成 speckle; 照明积分按步长加权
    const float span = t1 - t0;
    const float ratio = 1.15;
    const float stepBase = span * (ratio - 1.0) / (pow(ratio, 24.0) - 1.0);
    const vec3 sunDir = normalize(pc.misc.yzw);
    // HG(g=0.6) 前向位相归一 + 多次散射饱和底座 0.5 (与 band_shade.comp 同源:
    // 背向 clamp 0.5, 光学厚云柱内散射饱和不塌黑)
    const float g = 0.6;
    // r9 修复: mu = dot(dir, sunDir) (与 band_shade.comp 同源, 散射角定义)
    const float mu = dot(dir, sunDir);
    const float ph = clamp((1.0 - g * g) / (4.0 * PI * pow(1.0 + g * g - 2.0 * g * mu, 1.5))
                           / 0.24, 0.62, 1.6);
    const float kExt = cloudExtPerKm() * 1e-3;
    // 第九轮: 太阳散射波段判据 (TV esun > 0; 与 band_shade.comp ub.env.z 同语义)
    const bool solarBand = solarIrradiance() > 0.0;
    const float baseM = vol.a.x, topM = vol.a.z;
    // r9 亮度语义重构 (与 band_shade.comp 同源): cloud = 路径平均照明 × 总遮挡
    float litW = 0.0, wsum = 0.0;
    float stepLen = stepBase;
    float t = t0 + 0.5 * stepLen;
    for (int i = 0; i < steps; ++i) {
        const vec3 Ps = pos + dir * t;
        const float d = cloudDensity(Ps, t);
        if (d > 1e-8) {
            const float tauS = exp(-d * stepLen * kExt);
            float illum = 1.0;
            if (solarBand && topM > baseM)
                illum = cloudMultiScatter(cloudSunOD(Ps));
            const float odS = -log(max(tauC, 1.0e-3));
            const float powder = 0.5 + 0.5 * (1.0 - exp(-2.0 * odS));
            const float occ = (1.0 - tauS) * tauC;
            const float amb = 0.35 * exp(-odS * 0.5);   // r9: 天空环境散射 (同源)
            litW += occ * stepLen * (ph * illum * (0.45 + 0.55 * powder) + amb);
            wsum += occ * stepLen;
            tauC *= tauS;
            if (tauC < 0.01) break;
        }
        t += stepLen;
        stepLen *= ratio;
    }
    if (wsum > 1.0e-4) cloud = litW / wsum * (1.0 - tauC);
    // r10 大气透视 (与 band_shade.comp 同源): 起点八 8km 长度 45km (仅衰减
    // 半式须弱化, 否则低仰角云被砍半反致"云虚")
    // r10b 黑线修复 (与 band_shade.comp 同源): 消光与发射同步淡出 —— 只压
    // cloud 而 tauC 不变使远场厚云遮光不发光 → 低仰角远处一条黑线
    // r10c (与 band_shade.comp 同源): 45000→30000 远场更快趋天空色
    const float fade = exp(-max(t0 - 8000.0, 0.0) / 30000.0);
    cloud *= fade;
    tauC = mix(tauC, 1.0, 1.0 - fade);
    cloud = clamp(cloud, 0.0, 1.0);
}
/// #5 云影: 地面点 P 沿太阳方向在云 slab 内积分光学厚度 (低步数 march)。
/// 云体厚度自然产生软影/半影; 云底未配置 / 太阳低于地平线附近 → 恒等 1.0。
float cloudShadow(vec3 P, vec3 Ldir) {
    if (vol.a.x <= 0.0 || vol.a.y <= 0.0 || Ldir.y <= 0.05) return 1.0;
    float t0, t1;
    if (!cloudSlab(P, Ldir, t0, t1)) return 1.0;
    const int N = 8;
    const float stepLen = (t1 - t0) / float(N);
    const float kExt = cloudExtPerKm() * 1e-3;
    float od = 0.0;
    float t = t0 + 0.5 * stepLen;
    for (int i = 0; i < N; ++i) {
        od += cloudDensity(P + Ldir * t, t) * stepLen;
        t += stepLen;
    }
    const float tauS = exp(-od * kExt);
    return 1.0 - vol.a.y * (1.0 - tauS);   // 无云=1, 云核=1-strength
}

void main() {
#ifdef SENSOR_RADIANCE_OUTPUT
    // This variant writes linear W/m²/sr RGB. Temperature and emissivity remain
    // material inputs; they are never packed into TV color channels.
    const vec3 sun=normalize(pc.misc.yzw);
    const vec3 skyZen=max(texture(skyTex,vec2(.5,1.0)).bgr,vec3(0.0));
    const vec3 white=vec3(1.0,.97,.92)*(dot(skyZen,vec3(.2126,.7152,.0722))*9.5);
#ifdef SENSOR_SKY_BACKGROUND
    const vec4 farPoint=inverse(pc.vp)*vec4(vUv*2.0-1.0,0.0,1.0); // VSG reverse-Z far plane
    const vec3 direction=normalize(farPoint.xyz/farPoint.w-pc.eye.xyz);
    float tauC,cloud;cloudMarch(pc.eye.xyz,direction,24,1.0e6,tauC,cloud);
    const float disk=smoothstep(cos(.30*PI/180.0),cos(.27*PI/180.0),dot(direction,sun));
    vec3 L=max(texture(skyTex,skyUv(direction)).bgr,vec3(0.0));
    L+=disk*solarIrradiance()*1.47e4*vec3(1.0,.96,.88);
    outColor=vec4(L*tauC+cloud*white,1.0e6);
#else
    const vec3 path=vWorld-pc.eye.xyz;
    const float distance=length(path);
    const vec3 direction=path/max(distance,1.0e-6);
    vec3 normal=normalize(cross(dFdx(vWorld),dFdy(vWorld)));
    if(dot(normal,direction)>0.0)normal=-normal;
    const bool hasMaterial=vMaterial*4u+1u<materials.length();
    const vec4 specA=hasMaterial?materials[vMaterial*4u]:vec4(max(1.0-vProps.y,0.0),0,0,0);
    const vec4 specB=hasMaterial?materials[vMaterial*4u+1u]:vec4(0,.5,0,0);
    vec3 albedo=vec3(clamp(specA.x>=0.0?specA.x:1.0-vProps.y,0.0,1.0));
    if(vSlot>0u&&vSlot<texSlots.length()){
        const vec4 kd=texSlots[vSlot];
        albedo=kd.a>.5?texture(albedoTex,vec3(vUv,float(vSlot)-1.0)).rgb:kd.rgb;
        albedo=clamp(albedo,vec3(0.0),vec3(1.0));
    }
    float zTau,zCloud;cloudMarch(vWorld,vec3(0,1,0),24,1.0e6,zTau,zCloud);
    const vec3 ambient=skyZen*zTau+zCloud*white;
    const float ndl=max(dot(normal,sun),0.0);
    vec3 direct=albedo*solarIrradiance()*ndl/PI;
    // Same GGX dielectric solar reflection as the compute TV path.
    if(specB.x>0.0&&ndl>0.0&&solarIrradiance()>0.0){
        const vec3 V=-direction,H=normalize(sun+V);
        const float nv=max(dot(normal,V),1.0e-4),nh=max(dot(normal,H),0.0),vh=max(dot(V,H),0.0);
        const float a=max(specB.y*specB.y,1.0e-3),a2=a*a,d=nh*nh*(a2-1.0)+1.0;
        const float D=a2/(PI*d*d),visibility=.5/max(mix(2.0*ndl*nv,ndl+nv,.5),1.0e-4);
        direct+=D*visibility*(.02+.98*pow(1.0-vh,5.0))*ndl*solarIrradiance()*specB.x*vec3(1.0,.97,.90);
    }
    vec3 target=direct*cloudShadow(vWorld,sun)+albedo*ambient*(.5+.5*max(normal.y,0.0));
    if(specB.z>.5){
        const vec3 reflected=reflect(direction,normal);
        const float fresnel=.02+.98*pow(1.0-max(dot(normal,-direction),0.0),5.0);
        float rTau,rCloud;cloudMarch(vWorld,reflected,16,1.0e6,rTau,rCloud);
        target+=fresnel*(max(texture(skyTex,skyUv(reflected)).bgr,vec3(0.0))*rTau+rCloud*white);
    }
    const float tau=exp(-max(radiometry.env.x,0.0)*distance*1.0e-3);
    const vec3 pathL=(1.0-tau)*max(texture(skyTex,skyUv(direction)).bgr,vec3(0.0));
    float viewTau,viewCloud;cloudMarch(pc.eye.xyz,direction,24,distance,viewTau,viewCloud);
    outColor=vec4(max(viewTau*(target*tau+pathL)+viewCloud*white,vec3(0.0)),distance*1.0e-3);
#endif
#else
    // 平面法线 (屏幕空间导数)
    vec3 n = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
    if (dot(n, normalize(vWorld - pc.eye.xyz)) > 0.0) n = -n;

    vec3 sun = normalize(pc.misc.yzw);

    // 材质: 贴图槽 (与 band_shade tvAlbedo 同语义) / 程序化场景棋盘格 / Kd
    const bool slotOk = vSlot < texSlots.length();
    const vec4 kd = slotOk ? texSlots[vSlot] : vec4(0.5, 0.5, 0.5, 0.0);
    vec3 albedo;
    if (kd.a > 0.5)
        albedo = clamp(texture(albedoTex, vec3(vUv, float(vSlot) - 1.0)).rgb, vec3(0.02), vec3(1.0));
    else if (abs(vWorld.y) < 0.5) {   // 程序化平地棋盘格 (槽无贴图时保留辨识度)
        float ck = mod(floor(vWorld.x / 4.0) + floor(vWorld.z / 4.0), 2.0);
        albedo = mix(vec3(0.32, 0.34, 0.28), vec3(0.42, 0.44, 0.37), ck);
    } else
        albedo = clamp(kd.rgb, vec3(0.02), vec3(1.0));

    // 热目标可见光辉光: 尾焰 (T>600K) 橙亮, 引擎舱 (T>400K) 暗红 (程序化场景锚点)
    if (vProps.x > 600.0) {
        outColor = vec4(pow(vec3(1.0, 0.72, 0.35) * 1.4, vec3(1.0/2.2)), 1.0);
        return;
    } else if (vProps.x > 400.0) {
        outColor = vec4(pow(mix(albedo, vec3(0.55, 0.18, 0.08), 0.6), vec3(1.0/2.2)), 1.0);
        return;
    }

    // 修正: sunDirEnu 指向太阳, N·L 取 +sun (旧版 -sun 符号反了, 背阳面被照亮)
    const float ndl = max(dot(n, sun), 0.0);
    const float shadow = cloudShadow(vWorld, sun);

    vec3 col;
    if (pc.cam.x > 0.5) {
        // 物理式 (与 RT TV 链同源): 直射漫反射×云影(march 软影) + 云底天空环境项
        // v=1 → 顶行天顶; .bgr: 数据序[0]=蓝 → 显示通道序 (与 band_shade/ clear 色一致)
        const vec3 zrgb = texture(skyTex, vec2(0.5, 1.0)).bgr;
        // #5 环境: 目标点朝天 march (与 band_shade.comp TV hit 同式) ——
        // 晴空 × τ_c + 云散射 (HG 相位; 白云锚 = 太阳白光 × 天顶 luma×9.5,
        // r10d 与 band_shade.comp 同步提锚 —— 云反照率与雪同量级)
        float zTau, zCl;
        cloudMarch(vWorld, vec3(0.0, 1.0, 0.0), 24, 1.0e6, zTau, zCl);
        const vec3 white = vec3(1.0, 0.97, 0.92) * (dot(zrgb, vec3(0.2126, 0.7152, 0.0722)) * 9.5);
        const vec3 Lsolar = albedo * pc.cam.w / PI * ndl * shadow;
        const vec3 Lamb = albedo * (zrgb * zTau + zCl * white)
                        * (0.5 + 0.5 * max(n.y, 0.0));
        // 曝光归一: 满强度漫反射白面 (N·L≈0.85) 映射到 ~1.0
        const float scale = max(pc.cam.w / PI * 0.85 + max(zrgb.r, max(zrgb.g, zrgb.b)), 1.0e-3);
        col = (Lsolar + Lamb) / scale;
    } else {
        // 回退式 (无 RGB 天空图: 非 TV 波段 / 无 RT 模块) —— 旧朗伯近似
        col = albedo * (0.25 + 0.75 * ndl) * vec3(1.0, 0.96, 0.9);
    }

    outColor = vec4(pow(clamp(col, 0.0, 1.0), vec3(1.0/2.2)), 1.0);
#endif
}
