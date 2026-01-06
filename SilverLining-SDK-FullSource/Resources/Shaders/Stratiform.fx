uniform float4x4 sl_modelViewProj;
uniform float4x4 sl_modelView;
uniform float4 sl_modelPos;
uniform float4 sl_cameraPos;
uniform float4 sl_fogColorAndDensity;
uniform float4 sl_cloudTint;
uniform float4 sl_displacementVectorAndContrast;
uniform float4 sl_fadeAndDisplacementFactor;
uniform float4 sl_upVectorAndThickness;
uniform float4 sl_sunColor;
uniform float4 sl_skyColor;
uniform float4 sl_lightDirection;
uniform float4 sl_layerThicknessAndIsTop;
uniform float4 sl_layerSizeAndUnitScale;
uniform float4x4 sl_invBasis;
uniform float4 sl_groundColor;
uniform float4 sl_extinctionFactor;
uniform float4 sl_outputScale;

#define PI 3.14159265
#define extinctionCoefficient 0.04618
#define EPSILON_ABOVE 0.1
#define EPSILON_BELOW 0.05
// Artificial boost to make fogbow & glory more visible
#define SINGLE_SCATTERING_SCALE 1.7
#define MIE
#define EDGE_TRANSLUCENCY_ABOVE 0.3
#define EDGE_TRANSLUCENCY_BELOW 0.15
#define MIN_OCCLUSION 0.2

// Sacrifice some accuracy for speed.
//#define FAST

#ifdef DX9

TEXTURE gDiffuseMap;
sampler gDiffuseSampler = sampler_state {
    Texture = (gDiffuseMap);
    MinFilter = Linear;
    MagFilter = Linear;
    MipFilter = Linear;
    AddressU = Wrap;
    AddressV = Wrap;
};

TEXTURE gDiffuseMap2;
sampler gDiffuseSampler2 = sampler_state {
    Texture = (gDiffuseMap2);
    MinFilter = None;
    MagFilter = None;
    MipFilter = None;
    AddressU = Wrap;
    AddressV = Wrap;
};

#else

uniform bool gTextureRepeatU;
uniform bool gTextureRepeatV;

Texture2D gDiffuseMap;
Texture2D gDiffuseMap2;

SamplerState gTriLinearSamWrapWrap {
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = WRAP;
    AddressV = WRAP;
};

SamplerState gTriLinearSamClampClamp {
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

SamplerState gTriLinearSamClampWrap {
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = WRAP;
};

SamplerState gTriLinearSamWrapClamp {
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = WRAP;
    AddressV = CLAMP;
};

SamplerState gNearestSamClamp {
    Filter = MIN_MAG_MIP_POINT;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

float4 Sample(float2 tc)
{
    if (gTextureRepeatU) {
        if (gTextureRepeatV) {
            return gDiffuseMap.Sample(gTriLinearSamWrapWrap, tc);
        } else {
            return gDiffuseMap.Sample(gTriLinearSamWrapClamp, tc);
        }
    } else {
        if (gTextureRepeatV) {
            return gDiffuseMap.Sample(gTriLinearSamClampWrap, tc);
        } else {
            return gDiffuseMap.Sample(gTriLinearSamClampClamp, tc);
        }
    }
}

float4 SampleLevel(float2 tc, int level)
{
    if (gTextureRepeatU) {
        if (gTextureRepeatV) {
            return gDiffuseMap.SampleLevel(gTriLinearSamWrapWrap, tc, level);
        } else {
            return gDiffuseMap.SampleLevel(gTriLinearSamWrapClamp, tc, level);
        }
    } else {
        if (gTextureRepeatV) {
            return gDiffuseMap.SampleLevel(gTriLinearSamClampWrap, tc, level);
        } else {
            return gDiffuseMap.SampleLevel(gTriLinearSamClampClamp, tc, level);
        }
    }
}
#endif

void VS(    float4 position : POSITION,
            float4 color : COLOR,
            float3 texCoord : TEXCOORD0,

            out float4 oPosition : SV_POSITION,
            out float4 oColor : COLOR,
            out float3 oTexCoord : TEXCOORD0,
            out float3 V : TEXCOORD1,
            out float fadeDist : TEXCOORD2
       )
{
    float unitScale = sl_layerSizeAndUnitScale.w;

    oTexCoord = texCoord;

    float4 vcolor = float4(color.x, color.y, color.z, color.w * sl_fadeAndDisplacementFactor.x);
    oColor = vcolor;

    float4 t = float4(oTexCoord.x, oTexCoord.y, 0, 0);
#ifdef DX9
    float4 texel = tex2Dlod(gDiffuseSampler, t);
#else
    float4 texel = SampleLevel(t.xy, 0);
#endif

    float minNSrc = sl_extinctionFactor.y;
    float thicknessFactor = texel.x * (1.0 - minNSrc) + minNSrc;
    float thickness = thicknessFactor * sl_upVectorAndThickness.w;

    float3 displacement = float3(0,-1,0) * (sl_upVectorAndThickness.w - thickness);

    position.xyz += displacement;

    float3 worldPos = position.xyz + sl_modelPos.xyz;
    float4 w4 = mul(sl_invBasis, float4(worldPos, 1.0));
    V = (sl_cameraPos.xyz - w4.xyz);

    fadeDist = length(mul(float4(V, 1.0), sl_invBasis).xz);

    oPosition = mul(sl_modelViewProj, position);
}

void VS_SM2(    float4 position : POSITION,
                float4 color : COLOR,
                float3 texCoord : TEXCOORD0,

                out float4 oPosition : SV_POSITION,
                out float4 oColor : COLOR,
                out float3 oTexCoord : TEXCOORD0,
                out float3 V : TEXCOORD1,
                out float fadeDist : TEXCOORD2
           )
{
    oTexCoord = texCoord;

    float4 vcolor = float4(color.x, color.y, color.z, color.w * sl_fadeAndDisplacementFactor.x);
    oColor = vcolor;

    float3 worldPos = position.xyz + sl_modelPos.xyz;
    float4 w4 = mul(sl_invBasis, float4(worldPos, 1.0));
    V = (sl_cameraPos.xyz - w4.xyz);

    fadeDist = length(mul(float4(V, 1.0), sl_invBasis).xz);

    oPosition = mul(sl_modelViewProj, position);
}

float getH(float4 texel)
{
    return  texel.w * sl_layerThicknessAndIsTop.x;
}

float3 phaseFunctionHG(float costheta, float g)
{
    float hg = (1.0 - g * g) / pow(1.0 + g * g - 2 * g * costheta, 1.5);
    return float3(hg,hg,hg);
}

float3 phaseFunctionMie(float theta)
{
    float u = theta / PI;   // 0-1 => 0 - 180
    u = u * (1801.0 / 2048.0);

#ifdef DX9
    return tex2D(gDiffuseSampler2, float2(u, 0.0)).xyz;
#else
    return gDiffuseMap2.Sample(gNearestSamClamp, float2(u, 0.0)).xyz;
#endif
}

float3 phaseFunctionMieConvolved(float theta)
{
    float u = theta / PI;   // 0-1 => 0 - 180
    u = u * (1801.0 / 2048.0);
#ifdef DX9
    return tex2D(gDiffuseSampler2, float2(u, 1.0)).xyz;
#else
    return gDiffuseMap2.Sample(gNearestSamClamp, float2(u, 1.0)).xyz;
#endif
}

float computeTransparency(float x, float fudge)
{
    //const float effectiveRadius = 0.000007; //  AKA re
    //const float densityOfDroplets = 300000000.0; // AKA N0

    //float extinctionCrossSection = PI * effectiveRadius * effectiveRadius; // AKA sigma
    //float extinctionCoefficient = densityOfDroplets * extinctionCrossSection; // AKA kappa
    float extinction = exp(-extinctionCoefficient * x * sl_extinctionFactor.x * fudge);

    return extinction;
}

#define EPSILON 1E-6
void computeSingleScatteringReflectance(in float Hv, in float Hl, in float ul, in float uv, in float vl, out float3 Ir1, out float3 It1)
{
#ifdef MIE
    float3 phase = phaseFunctionMie(acos(vl));
#else
    float3 phase = phaseFunctionHG(vl, 0.99);
#endif

    float sum = uv + ul;
    float diff = uv - ul;
    if (sum == 0) sum = EPSILON;
    if (diff == 0) diff = EPSILON;

    float3 t1 = (extinctionCoefficient * phase * ul) / sum;
    float t2 = 1.0 - computeTransparency(Hl + Hv, 1.0);

    Ir1 = t1 * (t2 * SINGLE_SCATTERING_SCALE);

    t1 = (extinctionCoefficient * phase * ul) / diff;
    t2 = computeTransparency(Hv, 1.0) - computeTransparency(Hl, 1.0);

    It1 = t1 * (t2 * SINGLE_SCATTERING_SCALE);
}

void computeDoubleScatteringReflectance(in float Hv, in float Hl, in float ul, in float uv, in float vl, out float3 Ir2, out float3 It2)
{
#ifdef MIE
    float3 phase = phaseFunctionMieConvolved(acos(vl));
#else
    float3 phase = phaseFunctionHG(vl, 0.99 * 0.99);
#endif

    float sum = uv + ul;
    float diff = uv - ul;
    if (sum == 0) sum = EPSILON;
    if (diff == 0) diff = EPSILON;

    float3 t1 = (extinctionCoefficient * extinctionCoefficient  * phase * ul) / sum;
    float t2 = 1.0 - computeTransparency(Hl + Hv, 1.0);

    Ir2 = t1 * t2;

    t1 = (extinctionCoefficient * extinctionCoefficient * phase * ul) / diff;
    t2 = computeTransparency(Hv, 1.0) - computeTransparency(Hl, 1.0);

    It2 = t1 * t2;
}

void computeMultipleScatteringReflectance(in float H, in float Hl, in float ul, out float Rms, out float Tms, out float R3plus, out float T3plus)
{
#ifdef FAST
    float b = 1.0;
    float c = 0.02;
    float kc = 0.03;
    float t = 0.7;
    float r = 0.06;
#else
    float l = acos(ul);
    float u = l / PI;   // 0-1 => 0 - 180
    u = u * (10.0 / 2048.0);
    u += (2000.0 / 2048.0);

#ifdef DX9
    float3 k1 = tex2D(gDiffuseSampler2, float2(u, 0.0)).xyz;
    float2 k2 = tex2D(gDiffuseSampler2, float2(u, 1.0)).xy;
#else
    float3 k1 = gDiffuseMap2.Sample(gNearestSamClamp, float2(u, 0.0)).xyz;
    float2 k2 = gDiffuseMap2.Sample(gNearestSamClamp, float2(u, 1.0)).xy;
#endif

    float b = k1.x;
    float c = k1.y;
    float kc = k1.z;
    float t = k2.x;
    float r = k2.y;
#endif

    Tms = 0.9961 / (H - (H - 1.0) * 0.9961);

    Tms *= b + (1.0 - b) * exp(-c * H);

    Rms = 1.0 - Tms;

    float T0 = exp(-kc * Hl);

    float R1 = 0.5 * r * (1.0 - exp(-kc * 2.0 * H));
    float T1 = t * H * kc * exp(-kc * H);

    float R2 = 0.5 * r * t * (1.0 - (2.0 * H * kc + 1.0) * exp(-kc * 2.0 * H));

    float T2a = 0.5 * pow(kc * t * H, 2.0) * exp(-kc * H);
    float T2b = 0.25 * r * r * (1.0 + (2.0 * H * kc - 1.0) * exp(-kc * 2.0 * H)) * exp(-kc * 3.0 * H);
    float T2 = T2a + T2b;

    R3plus = Rms - R1 - R2;
    T3plus = Tms - T0 - T1 - T2;
}

void toneMap(inout float3 c)
{
    float A = 0.9;
    float gamma = 0.65;
    c = float3(A, A, A) * pow(abs(c), float3(gamma, gamma, gamma));
}

float4 PS(float4 posH : SV_POSITION,
          float4 color : COLOR,
          float3 texCoord : TEXCOORD,
          float3 V : TEXCOORD1,
          float fadeDist : TEXCOORD2) : SV_TARGET {

    float unitScale = sl_layerSizeAndUnitScale.z;

    float vertDist = length(V) * unitScale;

    float fadeFalloff = sl_layerSizeAndUnitScale.w;
    float cloudFade = 1.0;

    if (fadeFalloff > 0)
    {
        float len = fadeDist / (sl_layerSizeAndUnitScale.x * 0.5);
        cloudFade = exp(-fadeFalloff * len * len * len) * sl_fadeAndDisplacementFactor.x;
    }

    float4 fogColor = float4(sl_fogColorAndDensity.x, sl_fogColorAndDensity.y, sl_fogColorAndDensity.z, cloudFade);
    float fogExponent = vertDist * sl_fogColorAndDensity.w;
    float fogFactor = clamp(exp(-(fogExponent * fogExponent)), 0.0, 1.0);

#ifdef DX9
    float4 texel = tex2D(gDiffuseSampler, texCoord.xy);
#else
    float4 texel = Sample(texCoord.xy);
#endif

    float H = getH(texel) * unitScale;

    float coverage = sl_fadeAndDisplacementFactor.z;
    float ambientOcclusion = 1.0;
    if (coverage >= 1.0)
    {
        float scudThickness = sl_layerThicknessAndIsTop.y;
        float layerThickness = sl_layerThicknessAndIsTop.x;
        float minNSrc = sl_extinctionFactor.y;
        float thicknessFactor = texel.x * (1.0 - minNSrc) + minNSrc;
        float scudDepth = (layerThickness - (thicknessFactor * layerThickness)) / scudThickness;
        ambientOcclusion = atan2(1, scudDepth) / (PI * 0.5);
        ambientOcclusion = max(MIN_OCCLUSION, ambientOcclusion);
    }

    float3 cloudColor;
    float4 finalColor;

    float3 Lnorm = normalize(mul(sl_invBasis, sl_lightDirection).xyz);
    float3 Vnorm = normalize(V);


    float3 normal = normalize(sl_upVectorAndThickness.xyz);
    bool above = dot(Vnorm, normal) >= 0;

    // Due to curvature of the cloud, the dot product isn't a good test when below.
    if (sl_layerThicknessAndIsTop.w < 1.0)
    {
        above = sl_layerThicknessAndIsTop.z > 0;
    }

    float uv, ul, vl, Hv, Hl;
    float edgeTranslucency;

    if (!above)
    {
        uv = dot(Vnorm, -normal);
        ul = dot(Lnorm, normal);
        float uvc = max(EPSILON_BELOW, uv);
        float ulc = max(EPSILON_BELOW, ul);
        vl = dot(-Vnorm, Lnorm);
        Hv = H / uvc;
        Hl = H / ulc;
        edgeTranslucency = EDGE_TRANSLUCENCY_BELOW;
    } else
    {
        float NORMAL_OFF = (1.0 / 2048.0);
        float3 off = float3(-NORMAL_OFF, 0, NORMAL_OFF);

        float2 texturePos = texCoord.xy;

        // s11 = Current
        float s11 = H;

#ifdef DX9
        float s21 = getH(tex2D(gDiffuseSampler, float2(texturePos.xy + off.zy))) * unitScale;
        float s12 = getH(tex2D(gDiffuseSampler, float2(texturePos.xy + off.yz))) * unitScale;
#else
        float s21 = getH(Sample(float2(texturePos.xy + off.zy))) * unitScale;
        float s12 = getH(Sample(float2(texturePos.xy + off.yz))) * unitScale;
#endif

        float3 va = normalize( float3(off.z * sl_layerSizeAndUnitScale.x, s21 - s11, 0.0) );
        float3 vb = normalize( float3(0.0, s12 - s11, off.z * sl_layerSizeAndUnitScale.y) );

        float3 localNormal = normalize( cross(vb, va) );
        float4 l4 = mul(sl_invBasis, float4(localNormal, 1.0));
        localNormal = l4.xyz / l4.w;

        uv = max(EPSILON_ABOVE,dot(Vnorm, localNormal));
        ul = max(EPSILON_ABOVE,dot(Lnorm, localNormal));
        vl = dot(-Vnorm, Lnorm);

        Hv = H / uv;
        Hl = H / ul;
        edgeTranslucency = EDGE_TRANSLUCENCY_ABOVE;
    }

    float Ir3plus;
    float It0, It3plus;
    float Rms, Tms, R3plus, T3plus;
    float3 Ir1, It1, Ir2, It2;

    It0 = computeTransparency(Hv, edgeTranslucency / sl_extinctionFactor.x);

    float a = 1.0 - It0;

    computeSingleScatteringReflectance(Hv, Hl, ul, uv, vl, Ir1, It1);
    computeDoubleScatteringReflectance(Hv, Hl, ul, uv, vl, Ir2, It2);

    if (above)
    {
        uv = max(EPSILON_ABOVE,dot(Vnorm, normal));
        ul = max(EPSILON_ABOVE,dot(Lnorm, normal));
    } else
    {
        uv = max(EPSILON_BELOW,dot(Vnorm, -normal));
        ul = max(EPSILON_BELOW,dot(Lnorm, normal));
    }

    Hv = H / uv;
    Hl = H / ul;

    computeMultipleScatteringReflectance(H, Hl, ul, Rms, Tms, R3plus, T3plus);

    It3plus = T3plus * (ul / (4.0 * PI * uv));
    Ir3plus = R3plus * (ul / (4.0 * PI * uv));

    float3 Ir = Ir1 + Ir2 + float3(Ir3plus, Ir3plus, Ir3plus);
    float3 It = float3(It0, It0, It0) + It1 + It2 + float3(It3plus, It3plus, It3plus);

    if (above)
    {
        // Top
        cloudColor = Ir * sl_sunColor.xyz + Rms * sl_skyColor.xyz * ambientOcclusion + Tms * sl_groundColor.xyz;
    } else
    {
        // Bottom
        cloudColor = It * sl_sunColor.xyz + Tms * sl_skyColor.xyz + Rms * sl_groundColor.xyz;
    }

    toneMap(cloudColor);

    cloudColor *= sl_cloudTint.xyz;

    finalColor.xyz = lerp(fogColor.xyz, cloudColor, fogFactor) * sl_outputScale.xxx;
    finalColor.a = a * cloudFade;

    if (!above) finalColor.a *= fogFactor;

    return finalColor;
}

#ifdef DX11
technique11 ColorTech {
    pass P0
    {
        SetVertexShader( CompileShader( vs_5_0, VS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader( ps_5_0, PS() ) );
    }
}
#endif

#ifdef DX10
technique10 ColorTech {
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, VS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader( ps_4_0, PS() ) );
    }
}
#endif

#ifdef DX10LEVEL9
technique10 ColorTech {
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0_level_9_1, VS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader( ps_4_0_level_9_1, PS() ) );
    }
}
#endif

#ifdef DX9
technique {
    pass P0
    {
        SetVertexShader( CompileShader( vs_3_0, VS() ) );
        SetPixelShader( CompileShader( ps_3_0, PS() ) );
    }
}
#endif

#ifdef DX9_2_0
technique {
    pass P0
    {
        SetVertexShader( CompileShader( vs_2_0, VS_SM2() ) );
        SetPixelShader( CompileShader( ps_2_0, PS() ) );
    }
}
#endif
