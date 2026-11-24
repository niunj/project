#version 460
#extension GL_GOOGLE_include_directive : require
#include "celestial_discs.glsl"
#include "../earth_atmosphere_optics.glsl"
/// sky_dome.frag — 原生平面天空: Rayleigh + Mie + 臭氧单次散射 ray march,
/// 日盘/月盘 (月相球面明暗 + 地球反照), 程序化星空与银河。
///
/// 散射常量/相函数/密度函数与行星求交移植自 rocky.atmo.glsl / rocky.atmo.sky.frag
/// (rocky c++, Copyright 2025-2026 Pelican Mapping, MIT License)。
/// 与地球后端差异: 工作在锚点 ENU 平面坐标, 行星为虚拟球 (球心 (0,-R,0)),
/// 相机/射线/太阳方向均为 ENU; 高度 10km 内曲率误差可忽略。
layout(location = 0) in vec3 vDir;

layout(set = 0, binding = 0) uniform Env {
    mat4 view_proj;
    mat4 model;
    vec4 camPosTime;
    vec4 sun;          // xyz=太阳方向 ENU, w=高度角(度)
    vec4 wind;
    vec4 misc;
    // 48..83 环境模块公共段 (本 shader 不使用, 声明以保持块布局一致)
    vec4 ext0; vec4 ext1;
    vec4 atm;
    vec4 deepCol; vec4 shallowCol;
    vec4 sunLight;
    vec4 ambient;
    vec4 skyHorizon;
    vec4 skyZenith;
    // 84..91 天空专属
    vec4 moon;         // xyz=月亮方向 ENU, w=月相 0..1
    vec4 astro;        // x=本地恒星时(rad) y=纬度(rad) z=月亮高度角(度)
    vec4 diskOptics;  // x/y angular radii; z lower-atmosphere haze; w precipitation
} env;

layout(location = 0) out vec4 outColor;

// ---- 物理常量 (Bruneton 2017 散射系数, 与 rocky 一致) ----
const float PI = 3.14159265359;
const float R_PLANET = 6371000.0;      // 地球平均半径 (m)
const float ATMO_THICKNESS = 100000.0; // 大气壳厚度 100km
const float H_RAYLEIGH = 8500.0;
const float H_MIE = 1200.0;
const vec3  BETA_RAYLEIGH = vec3(5.802e-6, 13.558e-6, 33.1e-6);
const vec3  BETA_MIE_SCAT = vec3(3.996e-6);
const vec3  BETA_MIE_EXT  = BETA_MIE_SCAT / 0.9;
const vec3  BETA_OZONE    = vec3(0.650e-6, 1.881e-6, 0.085e-6);
const float OZONE_CENTER = 25000.0;
const float OZONE_WIDTH  = 15000.0;
const float OZONE_SIGMA = 9000.0;
const float MIE_G = 0.76;
const vec3 SUN_ILLUMINANCE=vec3(20.0);
const float DEG2RAD = PI / 180.0;

#define VIEW_SAMPLES 16
#define VIEW_SAMPLES_TWILIGHT 24
#define LIGHT_SAMPLES 8

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
vec2 raySphereIntersect2(vec3 origin, vec3 dir, float radius) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float d = b * b - c;
    if (d < 0.0) return vec2(-1.0);
    d = sqrt(d);
    return vec2(-b - d, -b + d);
}
float rayleighPhase(float ct) { return (3.0/(16.0*PI))*(1.0+ct*ct); }
float miePhase(float ct, float g) {
    float g2 = g*g;
    return 3.0*(1.0-g2)*(1.0+ct*ct) /
           ((8.0*PI)*(2.0+g2)*pow(1.0+g2-2.0*g*ct, 1.5));
}
float rayleighDensity(float h){ return exp(-h/H_RAYLEIGH); }
float mieDensity(float h){ return exp(-h/H_MIE); }
// Match AtmosphereLUT's ozone column in the analytic fallback. Switching
// render paths must not change the twilight absorption spectrum.
float ozoneDensity(float h){ float d=(h-OZONE_CENTER)/OZONE_SIGMA;return exp(-d*d); }
float distToAtmo(vec3 origin, vec3 dir, float rAtmos) {
    return max(raySphereIntersect2(origin, dir, rAtmos).y, 0.0);
}
vec3 transmittance(vec3 origin, vec3 dir, float pathLen, float turbScale) {
    float stepLen = pathLen / float(LIGHT_SAMPLES);
    vec3 od = vec3(0.0);
    for (int i = 0; i < LIGHT_SAMPLES; ++i) {
        float t = (float(i)+0.5)*stepLen;
        vec3 p = origin + dir*t;
        float h = length(p) - R_PLANET;
        od += (BETA_RAYLEIGH*rayleighDensity(h)
             + BETA_MIE_EXT*mieDensity(h)*turbScale
             + BETA_OZONE*ozoneDensity(h)) * stepLen;
    }
    return exp(-od);
}
vec3 acesFilmic(vec3 x) {
    return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0);
}

// ================= 星空 (赤道坐标程序化恒星场) =================

#include "celestial_sky.glsl"
#include "sky_weather.glsl"

void main() {
    const float rAtmos = R_PLANET + ATMO_THICKNESS;
    const float turbScale = max(env.misc.y, 0.2);   // 浊度放大 Mie
    const float exposure = env.misc.x;
    const float camAlt = env.misc.z;                // 相机海拔 (米)
    bool atmosphereOn=(int(env.misc.w)&1)!=0;
    bool sunOn=(int(env.misc.w)&2)!=0;

    // 虚拟行星系: 行星心在原点, 相机在 ENU (0, R+h, 0), +Y 朝上。
    vec3 cameraPos = vec3(0.0, R_PLANET + max(camAlt, 1.0), 0.0);
    vec3 rayDir = normalize(vDir);
    vec3 sunDir = normalize(env.sun.xyz);

    vec2 atmosHit = raySphereIntersect2(cameraPos, rayDir, rAtmos);
    // Vacuum still contains the celestial background and solar/lunar discs.
    if(atmosHit.y<0.) atmosHit=vec2(0.);
    float tNear = max(atmosHit.x, 0.0);
    float tFar = atmosHit.y;
    float planetHit = raySphereIntersect(cameraPos, rayDir, R_PLANET);
    bool hitsGround = (planetHit > 0.0 && planetHit < tFar);
    if (hitsGround) tFar = planetHit;

    // Twilight low-sun paths graze the dense lower atmosphere; a fixed 16-step
    // march under-resolves the Mie lobe near the horizon and leaves visible
    // banding. Increase samples while the sun is within ~12° of the horizon.
    const int viewSamples = env.sun.w > -12.0 ? VIEW_SAMPLES_TWILIGHT : VIEW_SAMPLES;
    float stepLen = (tFar - tNear) / float(viewSamples);
    vec3 rayleighSum = vec3(0.0), mieSum = vec3(0.0);
    vec3 lunarSum=vec3(0.);
    vec3 moonDir=normalize(env.moon.xyz);
    float lunar=earthDisplayMoon(max(env.moon.w,0.),sunDir.y);
    float lunarNu=dot(rayDir,moonDir);
    vec3 viewOpticalDepth = vec3(0.0);

    for (int i = 0; i < VIEW_SAMPLES_TWILIGHT; ++i) {
        if (i >= viewSamples) break;
        float t = tNear + (float(i)+0.5)*stepLen;
        vec3 sp = cameraPos + rayDir*t;
        float h = length(sp) - R_PLANET;
        float rhoR = rayleighDensity(h);
        float rhoM = mieDensity(h);
        float rhoO3 = ozoneDensity(h);
        vec3 localExt = BETA_RAYLEIGH*rhoR + BETA_MIE_EXT*rhoM*turbScale + BETA_OZONE*rhoO3;
        viewOpticalDepth += localExt*stepLen;
        vec3 viewTransmittance = exp(-viewOpticalDepth);

        float sunPlanetHit = raySphereIntersect(sp, sunDir, R_PLANET);
        vec3 sunTransmittance;
        if (sunPlanetHit > 0.0) {
            sunTransmittance = vec3(0.0);
        } else {
            float sunPathLen = distToAtmo(sp, sunDir, rAtmos);
            sunTransmittance = transmittance(sp, sunDir, sunPathLen, turbScale);
        }
        rayleighSum += viewTransmittance*sunTransmittance*rhoR*stepLen;
        mieSum      += viewTransmittance*sunTransmittance*rhoM*stepLen;
        if(lunar>1.e-5&&raySphereIntersect(sp,moonDir,R_PLANET)<=0.) {
            vec3 moonT=transmittance(sp,moonDir,distToAtmo(sp,moonDir,rAtmos),turbScale);
            lunarSum+=viewTransmittance*moonT*
                (BETA_RAYLEIGH*rhoR*rayleighPhase(lunarNu)+BETA_MIE_SCAT*rhoM*turbScale*miePhase(lunarNu,MIE_G))*stepLen;
        }
    }

    float cosTheta = dot(rayDir, sunDir);
    float phaseR = rayleighPhase(cosTheta);
    float phaseM = miePhase(cosTheta, MIE_G);
    vec3 scattered = SUN_ILLUMINANCE *
        (BETA_RAYLEIGH*phaseR*rayleighSum + BETA_MIE_SCAT*turbScale*phaseM*mieSum+lunar*lunarSum);
    // Second-order scattering, isotropic closure: the single-scattered field
    // at each sample is scattered once more toward the camera. This brightens
    // the thick twilight / low-sun atmosphere where first-order alone reads
    // too dark, while the daylight regime (thin column) is barely affected.
    // The factor 1/(4pi) is the isotropic phase; sunTransmittance/mieSum
    // carry the real optical depth so energy stays bounded.
    if(!hitsGround) {
        float twilight = 1.0 - smoothstep(2.0, 14.0, env.sun.w);   // low sun -> 1
        if (twilight > 0.001) {
            vec3 secondOrder = SUN_ILLUMINANCE * turbScale *
                (BETA_RAYLEIGH*rayleighSum + BETA_MIE_SCAT*mieSum) *
                rayleighPhase(cosTheta) * (1.0/(4.0*PI));
            scattered += secondOrder * twilight * 0.35;
        }
    }
    if(!atmosphereOn)scattered=vec3(0.0);

    // 沿视线整程大气透过率 (日/月/星消光共用)。
    vec3 viewTrans = atmosphereOn ? exp(-viewOpticalDepth) : vec3(1.0);

    if(!hitsGround) {
        bool air=atmosphereOn&&tFar>tNear;
        if(air)scattered=skyAirDisplay(scattered,rayDir.y,sunDir.y,cosTheta,camAlt);
        if(air&&(env.diskOptics.z>0.||env.astro.w>0.)) {
            vec2 weather=skyWeatherTransmission(rayDir.y,vec3(env.diskOptics.z,env.astro.w,env.diskOptics.w));
            scattered=skyWeatherRadiance(scattered,rayDir.y,vec2(cosTheta,lunarNu),
                                        vec3(env.sun.w,env.astro.z,env.moon.w),env.diskOptics.w,weather);
            viewTrans*=weather.x*weather.y;
        }
        if(sunOn) scattered+=solarDisplayLight(rayDir,sunDir,max(env.diskOptics.x,.0001),camAlt,air,viewTrans);
        vec4 moon=env.moon.w>=0. ? lunarDisplayDisc(rayDir,normalize(env.moon.xyz),sunDir,
                    meanLunarPole(env.astro.x,env.astro.y),max(env.diskOptics.y,.0001),camAlt,air) : vec4(0);
        scattered+=celestialSky(rayDir,viewTrans,scattered,air,moon.a)+moon.rgb*viewTrans;
    }

    outColor = vec4(acesFilmic(scattered * exposure), 1.0);
}
