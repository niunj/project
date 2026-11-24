#version 460
#extension GL_GOOGLE_include_directive : require
#define LUNAR_LUT
#include "celestial_discs.glsl"
#include "../earth_atmosphere_optics.glsl"
/// @file sky_dome.frag
/// @brief Bruneton 预计算大气 LUT 查表渲染 (性能版)。
///
/// 与原版 sky_dome.frag 的区别:
///   - 原版: 16 view samples × 8 light samples = 128 次 ray-march
///   - 本版: 方位插值的分量积分 LUT + 解析相函数
///   - 实际帧率收益由目标设备和场景测量，不预设倍数
///
/// LUT 绑定 (set=1):
///   binding 0: transmittanceLUT (2D: 256x64 RG16F)
///   binding 1: scatteringLUT (3D: 64x32x384 RGBA32F)
///   binding 2: irradianceLUT (2D: 64x16 RGB16F)
///
/// 参考: Bruneton08, Hillaire17

layout(location = 0) in vec3 vDir;

layout(set = 0, binding = 0) uniform Env {
    mat4 view_proj;
    mat4 model;
    vec4 camPosTime;   // xyz=相机 ENU, w=时间
    vec4 sun;          // xyz=太阳方向 ENU, w=高度角(度)
    vec4 wind;
    vec4 misc;         // x=曝光 y=浊度 z=相机海拔(米) w=保留
    vec4 ext0; vec4 ext1;
    vec4 atm;
    vec4 deepCol; vec4 shallowCol;
    vec4 sunLight;
    vec4 ambient;
    vec4 skyHorizon;
    vec4 skyZenith;
    vec4 moon;         // xyz=月亮方向 ENU, w=月相 0..1
    vec4 astro;        // x=本地恒星时(rad) y=纬度(rad) z=月亮高度角(度)
    vec4 diskOptics;  // x/y angular radii; z lower-atmosphere haze; w precipitation
} env;

// ---- LUT 纹理 (set=1) ----
layout(set = 1, binding = 0) uniform sampler2D transmittanceLUT;
layout(set = 1, binding = 1) uniform sampler3D scatteringLUT;
layout(set = 1, binding = 2) uniform sampler2D irradianceLUT;

layout(location = 0) out vec4 outColor;

// ---- 物理常量 (与 CPU 端一致) ----
const float PI = 3.14159265359;
const float R_PLANET = 6371000.0;
const float ATMOSPHERE_HEIGHT = 100000.0;
const float H_RAYLEIGH = 8500.0;
const float H_MIE = 1200.0;
const vec3 BETA_RAYLEIGH = vec3(5.802e-6, 13.558e-6, 33.1e-6);
const vec3 BETA_MIE_SCAT = vec3(3.996e-6);
const vec3 BETA_MIE_EXT = BETA_MIE_SCAT / 0.9;
const vec3 BETA_OZONE = vec3(0.650e-6, 1.881e-6, 0.085e-6);
const float MIE_G = 0.76;

// ---- LUT 参数 ----
const float LUT_PLANET_RADIUS = 6371000.0;
const float LUT_ATMOSPHERE_HEIGHT = 100000.0;
const float LUT_MAX_HEIGHT = 100000.0;

// ---- 辅助函数 ----
float rayleighPhase(float ct) { return (3.0/(16.0*PI))*(1.0+ct*ct); }
float miePhase(float ct, float g) {
    float g2 = g*g;
    return 3.0*(1.0-g2)*(1.0+ct*ct) /
           ((8.0*PI)*(2.0+g2)*pow(1.0+g2-2.0*g*ct, 1.5));
}

vec2 raySphereIntersect2(vec3 origin, vec3 dir, float radius) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float d = b * b - c;
    if (d < 0.0) return vec2(-1.0);
    d = sqrt(d);
    return vec2(-b - d, -b + d);
}

float raySphereIntersect(vec3 origin, vec3 dir, float radius) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float d = b * b - c;
    if (d < 0.0) return -1.0;
    d = sqrt(d);
    float t0 = -b - d, t1 = -b + d;
    if (t0 > 0.0) return t0;
    if (t1 > 0.0) return t1;
    return -1.0;
}

vec3 acesFilmic(vec3 x) {
    return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0);
}

// ---- LUT 采样函数 ----

/// 采样 transmittance LUT
/// @param viewHeight 视角高度 (0..64km)
/// @param sunCosZenith 太阳天顶角余弦 (-1..1)
vec3 sampleTransmittance(float viewHeight, float sunCosZenith) {
    return earthAirTransmission(transmittanceLUT,viewHeight,sunCosZenith);
}

// Packed fourth coordinate: four relative-azimuth cosines, each containing
// separate Rayleigh, Mie and multiple-scattering RGB integrals. Filtering must
// stay inside each altitude block; hardware trilinear filtering must never
// interpolate the last altitude of one component into the first of another.
const int SCATTERING_AZIMUTHS=4;
const int SCATTERING_COMPONENTS=3;
vec2 scatteringCoordinate(vec2 cosine) {
    vec2 x=(sqrt(.1225+2.60*abs(cosine))-.35)/1.30;
    return clamp(.5+.5*sign(cosine)*x,0.,1.);
}
vec3 scatteringIntegral(float h,float mu,float muSun,int azimuth,int component) {
    vec3 size=vec3(textureSize(scatteringLUT,0));
    float heights=size.z/float(SCATTERING_AZIMUTHS*SCATTERING_COMPONENTS);
    vec3 index=vec3(scatteringCoordinate(vec2(muSun,mu))*(size.xy-1.),
        sqrt(clamp(h/LUT_MAX_HEIGHT,0.,1.))*(heights-1.)+
        float(azimuth*SCATTERING_COMPONENTS+component)*heights);
    return texture(scatteringLUT,(index+.5)/size).rgb;
}
vec3 sampleScattering(float h,float mu,float muSun,float nu) {
    mu=clamp(mu,-1.,1.);muSun=clamp(muSun,-1.,1.);
    float horizontal=sqrt(max(0.,(1.-mu*mu)*(1.-muSun*muSun)));
    float relative=horizontal>1e-6 ? clamp((nu-mu*muSun)/horizontal,-1.,1.) : 0.;
    nu=mu*muSun+horizontal*relative;
    float a=(relative+1.)*.5*float(SCATTERING_AZIMUTHS-1);
    int a0=int(floor(a)),a1=min(a0+1,SCATTERING_AZIMUTHS-1);
    vec3 r=mix(scatteringIntegral(h,mu,muSun,a0,0),scatteringIntegral(h,mu,muSun,a1,0),fract(a));
    vec3 m=mix(scatteringIntegral(h,mu,muSun,a0,1),scatteringIntegral(h,mu,muSun,a1,1),fract(a));
    vec3 ms=mix(scatteringIntegral(h,mu,muSun,a0,2),scatteringIntegral(h,mu,muSun,a1,2),fract(a));
    return r*rayleighPhase(nu)+m*miePhase(nu,MIE_G)+ms;
}

/// 采样 irradiance LUT
/// @param viewHeight 视角高度 (0..64km)
/// @param sunCosZenith 太阳天顶角余弦 (-1..1)
vec3 sampleIrradiance(float viewHeight, float sunCosZenith) {
    // 参数归一化到 [0,1]
    float u = (sunCosZenith + 1.0) * 0.5;  // -1..1 → 0..1
    float v = sqrt(clamp(viewHeight / LUT_MAX_HEIGHT,0.0,1.0));
    vec2 size=vec2(textureSize(irradianceLUT,0));
    return texture(irradianceLUT,(clamp(vec2(u,v),0.0,1.0)*(size-1.0)+0.5)/size).rgb;
}

// ---- 星空 ----

#include "celestial_sky.glsl"
#include "sky_weather.glsl"

void main() {
    const float rAtmos = R_PLANET + ATMOSPHERE_HEIGHT;
    const float exposure = env.misc.x;
    const float camAlt = env.misc.z;
    bool atmosphereOn=(int(env.misc.w)&1)!=0;
    bool sunOn=(int(env.misc.w)&2)!=0;

    // 虚拟行星系: 行星心在原点, 相机在 ENU (0, R+h, 0)
    vec3 cameraPos = vec3(0.0, R_PLANET + max(camAlt, 1.0), 0.0);
    vec3 rayDir = normalize(vDir);
    vec3 sunDir = normalize(env.sun.xyz);

    // 射线-大气相交
    vec2 atmosHit = raySphereIntersect2(cameraPos, rayDir, rAtmos);
    bool vacuum=atmosHit.y<0.;
    if(vacuum) atmosHit=vec2(0.);
    float tNear = max(atmosHit.x, 0.0);
    float tFar = atmosHit.y;
    float planetHit = raySphereIntersect(cameraPos, rayDir, R_PLANET);
    bool hitsGround = (planetHit > 0.0 && planetHit < tFar);
    if (hitsGround) tFar = planetHit;

    // ---- LUT 查表计算大气散射 ----
    // Orbital rays are sampled at their first atmospheric entry rather than
    // at the spacecraft height. The LUT therefore represents the actual
    // 100 km boundary even for an observer well outside the atmosphere.
    vec3 entryPoint=cameraPos+rayDir*tNear;
    vec3 entryUp=normalize(entryPoint);
    float viewHeight = max(length(entryPoint)-R_PLANET,0.0);
    float viewCosZenith = dot(rayDir,entryUp);
    float sunCosZenith = dot(sunDir,entryUp);
    float viewSunCosTheta = dot(rayDir, sunDir);

    // 视线方向透射率: 星光/月辉沿**视线**穿过大气, 与太阳方向无关。
    // 旧实现把太阳射线透射率传给 celestialLayer/月盘 —— 夜间太阳在地平线下,
    // 该 LUT 区域因 computeTransmittance 对撞地射线跳过积分而恒 0 (数组零
    // 初始化), 星空/银河被整体乘 0 → 午夜无星 (T4 根因)。
    vec3 viewTransmittance = atmosphereOn ? sampleTransmittance(viewHeight, viewCosZenith) : vec3(1.0);

    // 采样 scattering (单次散射)
    // 量纲: CPU computeScattering 积分按**单位太阳照度**输出 radiance
    // (典型 ~0.003..0.017), 与解析版 sky_dome.frag 一致需乘 SUN_ILLUMINANCE
    // 还原真实量级, 否则 aces 后天空整片近黑 (只比纯黑略亮的"坑爹灰")。
    vec3 scattered=vec3(0);
    vec3 moonDir=normalize(env.moon.xyz);
    if(atmosphereOn&&!vacuum) {
        scattered=EARTH_AIR_SOLAR_SCALE*sampleScattering(viewHeight,viewCosZenith,sunCosZenith,viewSunCosTheta);
        float lunar=earthDisplayMoon(max(env.moon.w,0.),sunCosZenith);
        if(lunar>1.e-5)
            scattered+=EARTH_AIR_SOLAR_SCALE*lunar*sampleScattering(viewHeight,viewCosZenith,
                                             dot(moonDir,entryUp),dot(rayDir,moonDir));
    }

    if(vacuum) {scattered=vec3(0.);viewTransmittance=vec3(1.);}

    if(!hitsGround) {
        bool air=atmosphereOn&&!vacuum;
        if(air)scattered=skyAirDisplay(scattered,rayDir.y,sunDir.y,viewSunCosTheta,camAlt);
        if(air&&(env.diskOptics.z>0.||env.astro.w>0.)) {
            vec2 weather=skyWeatherTransmission(rayDir.y,vec3(env.diskOptics.z,env.astro.w,env.diskOptics.w));
            scattered=skyWeatherRadiance(scattered,rayDir.y,vec2(viewSunCosTheta,dot(rayDir,moonDir)),
                                        vec3(env.sun.w,env.astro.z,env.moon.w),env.diskOptics.w,weather);
            viewTransmittance*=weather.x*weather.y;
        }
        if(sunOn) scattered+=solarDisplayLight(rayDir,sunDir,max(env.diskOptics.x,.0001),camAlt,air,viewTransmittance);
        vec4 moon=env.moon.w>=0. ? lunarDisplayDisc(rayDir,moonDir,sunDir,
                    meanLunarPole(env.astro.x,env.astro.y),max(env.diskOptics.y,.0001),camAlt,air) : vec4(0);
        scattered+=celestialSky(rayDir,viewTransmittance,scattered,air,moon.a)+moon.rgb*viewTransmittance;
    } else if(atmosphereOn) {
        // Background for ground-facing rays outside loaded imagery. The LUT
        // already integrates to the ground, keeping the horizon directional.
        float downward=1.-smoothstep(0.,145000.,tFar);
        float day=smoothstep(-.10,.25,sunCosZenith);
        scattered+=downward*day*vec3(.028,.035,.040);
    }

    // ---- 最终颜色 ----
    outColor = vec4(acesFilmic(scattered * exposure), 1.0);
}
