// Copyright 2010-2020 Sundog Software, LLC. All rights reserved worldwide.

// The following parameters may be adjusted to tweak appearance:

// Maximum number of slices to render through the cloud volume.
// Increase this to reducing sampling artifacts. Decrease it
// to increase performance.
#define MAX_SAMPLES 100

// The rate at which detail is added to the clouds with distance
// from the camera. This is needed to counteract sampling artifacts
// from high frequency detail near the camera.
#define DETAIL_FALLOFF 2.0

// Uncomment to enable Reinhard tone mapping - tends to make things too
// transparent though
//#define TONE_MAP

// Smooth lighting approximates the cloud's lighting by just looking
// at the sample's position within the cloud layer relative to the
// light source. It's faster and produces less detail in the lighting
// which actually looks better.
#define SMOOTH_LIGHTING

// Controls how light linearly falls off as a function of depth into
// the cloud layer
#define SMOOTH_LIGHTING_BRIGHTNESS 2.5

// The frequency of the noise used to add detail
#define NOISE_FREQUENCY 2500

// Number of noise octaves
#define NOISE_OCTAVES 1

// Make sure noise octave loop is not slowing us down
#define UNWIND_OCTAVE_LOOP 1

// Enable or disable gamma correction
#define GAMMA_CORRECTION

// Gamma value
#define GAMMA 2.2

// Exposure
#define EXPOSURE 1.2

// Opacity boost
#define OPACITY 1.05

// Opacity threshold at which we do early ray termination
#define OPACITY_THRESHOLD 0.95

// Epsilon value at which we discard the fragment
#define EPSILON 0.001

// Whether to disable tone mapping and gamma correction
//#define HDR

// Defines how bright highlights from the sun can get.
#define MAX_PHASE 10.0

// The boost to ambient light as you reach the top of a cloud layer
#define AMBIENT_TOP_BOOST 1.7

uniform float4x4 sl_projectionMatrix;
uniform Texture3D gDiffuseMap;
uniform Texture3D gDiffuseMap2;
uniform float4 sl_lightColor;
uniform float4 sl_lightObjectDirAndConstTerm;
uniform float4 sl_lightTexCoords;
uniform float4 sl_viewSampleDimensions;
uniform float4 sl_lightSampleDimensions;
uniform float4 sl_voxelDimensions;
uniform float4 sl_lightWorldDirAndExtinction;
uniform float4 sl_cameraTexCoords;
uniform float4 sl_skyLightColor;
uniform float4 sl_originTexCoords;
uniform float4 sl_fadeFlag;
uniform float4 sl_fogColorAndDensity;
uniform float4 sl_multipleScatteringTerm;
uniform float4 sl_noiseOffset;
uniform float4 sl_extinctionCoefficient;
uniform float4 sl_jitter;
uniform float4 sl_unitScale;
uniform float4 sl_outputScale;
uniform float4 sl_roundCenter;


struct SL_Vertex {
    float4 pos :
    SV_Position;
    float4 color :
    COLOR0;
    float3 tex :
    TEXCOORD0;
    float3 eyeDepth :
    TEXCOORD1;
};

#include "Samplers.hlsl"

#define SAMPLE_TEX3D( texture, sampler, coord ) texture.SampleLevel(sampler, coord, 0 )

void getVolumeIntersection(in float3 pos, in float3 dir, out float tNear, out float tFar)
{
    // Intersect the ray with each plane of the box
    float3 invDir = 1.0 / dir;
    float3 tBottom = -pos * invDir;
    float3 tTop = (1.0 - pos) * invDir;

    // Find min and max intersections along each axis
    float3 tMin = min(tTop, tBottom);
    float3 tMax = max(tTop, tBottom);

    // Find largest min and smallest max
    float2 t0 = max(tMin.xx, tMin.yz);
    tNear = max(t0.x, t0.y);
    t0 = min(tMax.xx, tMax.yz);
    tFar = min(t0.x, t0.y);

    // Clamp negative intersections to 0
    tNear = max(0.0, tNear);
    tFar = max(0.0, tFar);
}

float henyeyGreenstein(in float cosa, in float g)
{
    float g2 = g * g;

    return (1.0 - g2) / pow((1.0 + g2 - 2.0 * g * cosa), 1.5);
}

float phaseFunction(in float cosa)
{
    // Rayleigh function
    //return 0.75 * (1.0 + cosa * cosa);

    // Single HG:
    return clamp(henyeyGreenstein(cosa, 0.875), 1.0, MAX_PHASE);
}

void toneMap(inout float4 c)
{
    c *= float4(EXPOSURE, EXPOSURE, EXPOSURE, OPACITY);

    // Simplified Reinhard tone mapping operator
#ifdef TONE_MAP
    c.xyz = (c.xyz / (c.xyz + 1.0));
#endif

#ifdef GAMMA_CORRECTION
    const float gamma = 1.0 / GAMMA;
    c = pow(abs(c), float4(gamma, gamma, gamma, gamma));
#endif

    c = clamp(c, 0.0, 1.0);

    c.xyz *= sl_outputScale.xxx;
}

float computeFade(in float3 posWithinVolume)
{
    if (sl_fadeFlag.x > 0) {
        float distFromCenter = length(posWithinVolume.xz - float2(0.5, 0.5)) * 2.0;
        return 1.0 - exp(-1.0 * sl_fadeFlag.z * distFromCenter * distFromCenter * distFromCenter);
    } else {
        return 0.0;
    }
}

void computeFog(inout float4 c, in float eyeDepth)
{
    float fogExponent = clamp(sl_fogColorAndDensity.w * eyeDepth, 0.0, 1.0);
    float f = exp(-(fogExponent * fogExponent));
    f = clamp(f, 0.0, 1.0);
    //c.xyz = lerp(sl_fogColorAndDensity.xyz, c.xyz, f);
    c *= f;
}


float2 getCloudDensity(in float3 texCoord, in float t)
{
    float3 perturb = float3(0, 0, 0);
    float4 uvw;
    uvw.xyz = ((texCoord + sl_noiseOffset.xyz) / sl_viewSampleDimensions.xyz) / (NOISE_FREQUENCY * sl_unitScale.x);
    uvw.w = 1.0;

#ifndef UNWIND_OCTAVE_LOOP // Shall I trust the compiler that it will effectively unwind this loop ?    
    float scale = 1.0;
    for (int o = 0; o < NOISE_OCTAVES; o++) {
        uvw.xyz += uvw.xyz;
        perturb.xyz += (SAMPLE_TEX3D(gDiffuseMap2, gBiLinearWrapWrap, uvw.xyz).xyz - 0.5) * scale;
        scale *= 0.5;
    }
#else
#if ( NOISE_OCTAVES > 0 )
    perturb.xyz += 1.0  * SAMPLE_TEX3D(gDiffuseMap2, gBiLinearWrapWrap, 2.0 * uvw.xyz).xyz - 0.5;
#endif
#if ( NOISE_OCTAVES > 1 )
    perturb.xyz += 0.5  * SAMPLE_TEX3D(gDiffuseMap2, gBiLinearWrapWrap, 4.0 * uvw.xyz).xyz - 0.25;
#endif
#if ( NOISE_OCTAVES > 2 )
    perturb.xyz += 0.25 * SAMPLE_TEX3D(gDiffuseMap2, gBiLinearWrapWrap, 8.0 * uvw.xyz).xyz - 0.125;
#endif
#if ( NOISE_OCTAVES > 3 )
    perturb.xyz += 0.125 * SAMPLE_TEX3D(gDiffuseMap2, gBiLinearWrapWrap, 16.0 * uvw.xyz).xyz - 0.0625;
#endif
#if ( NOISE_OCTAVES > 4 )
    perturb.xyz += 0.0625 * SAMPLE_TEX3D(gDiffuseMap2, gBiLinearWrapWrap, 32.0 * uvw.xyz).xyz - 0.03125;
#endif
#endif

    float4 sampleCoord;
    sampleCoord.xyz = texCoord + perturb * sl_jitter.xyz * t;
    sampleCoord.w = 1.0;

    if (sl_roundCenter.z < 2.0) {
        float2 center = sl_roundCenter.xy;
        float radius2 = sl_roundCenter.z;
        float2 d = center - sampleCoord.xz;
        float d2 = dot(d, d);
        if (d2 > radius2) {
            return float2(0, 0);
        }
    }

    return SAMPLE_TEX3D(gDiffuseMap, gBiLinearWrapWrapClamp, sampleCoord.xzy).xy;

}

float map(float value, float outMin, float outMax)
{
    return outMin + (outMax - outMin) * value;
}

float map2(float value, float outMax)
{
    return 1.0 + (outMax - 1.0) * value;
}

float PHI = 1.61803398874989484820459;
float seed = 12345.6789;

float rand(float3 xyz)
{
    return frac(tan(length(xyz*PHI - xyz)*seed)*xyz.x);
}


float4 main(SL_Vertex inVert) : SV_TARGET {
    float3 texCoord = inVert.tex;

    float4 texCoord4;
    texCoord4.xyz = texCoord + sl_originTexCoords.xyz;
    texCoord4.w = 1.0;

    float3 view = texCoord - sl_cameraTexCoords.xyz;
    float3 viewDir = normalize(view);

    // Find the intersections of the volume with the viewing ray
    float tminView, tmaxView;
    getVolumeIntersection(sl_cameraTexCoords.xyz, viewDir, tminView, tmaxView);

    float tRange = tmaxView - tminView;

    float3 vv = sl_voxelDimensions.xyz;
    vv = vv * viewDir;
    float intersectionSize = length(tRange * vv);
    float sampleSize = length(sl_voxelDimensions / MAX_SAMPLES);
    float opticalDepth = sl_extinctionCoefficient.x * sampleSize;
    float correctedOpacity = 1.0 - exp(-opticalDepth);

    float constTerm = sl_lightObjectDirAndConstTerm.w;

    float4 fragColor = float4(0, 0, 0, 0);

    float viewInc = tRange / MAX_SAMPLES;

    float3 ambientTerm = sl_skyLightColor.xyz + sl_multipleScatteringTerm.xyz;

    float t = tminView;

    float cosa = dot(normalize(vv), sl_lightTexCoords.xyz);
    float phase = phaseFunction(cosa);

    float3 scattering = sl_lightColor.xyz * constTerm;

    for (int sampleNum = 0; sampleNum < MAX_SAMPLES; sampleNum++)
    {
        float3 sampleTexCoords = (sl_originTexCoords.xyz + sl_cameraTexCoords.xyz) + viewDir * t;

        if (t < 1.0) {
            t += viewInc * map(rand(sampleTexCoords.xyz), 0.8, 1.2); // randomness to combat banding
        } else {
            t += viewInc;
        }

        float2 sampleDensity = getCloudDensity(sampleTexCoords, 1.0 - exp(-DETAIL_FALLOFF * t));
        float texel = sampleDensity.x;
        if (texel < EPSILON) continue;

        float fade = computeFade(sampleTexCoords.xyz - sl_originTexCoords.xyz);

        // apply lighting
        float4 fragSample;
        fragSample.xyz = scattering * sampleDensity.y * phase + ambientTerm * map2(sampleTexCoords.y, AMBIENT_TOP_BOOST);

        fragSample.w = correctedOpacity;

        // apply fog
        float depth;
        float eyeDepth = inVert.eyeDepth.x;
        if (tminView > 0.0) { // outside
            depth = eyeDepth + ((t - tminView) / tRange) * intersectionSize;
        } else { // inside
            depth = (t / tmaxView) * eyeDepth;
        }
        computeFog(fragSample, depth);

        // apply fading
        fragSample *= (1.0 - fade);

        // apply texture
        fragSample *= texel;

        // Under operator for compositing:
        fragColor = fragColor + (1.0 - fragColor.w) * fragSample;

        // Early ray termination!
        if (fragColor.w > OPACITY_THRESHOLD) {
            fragColor.w = 1.0;
            break;
        }
    }

#ifndef HDR
    toneMap(fragColor);
#endif

    return fragColor * inVert.color;
}
