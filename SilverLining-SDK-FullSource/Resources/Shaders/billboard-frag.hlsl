uniform float4x4 sl_modelView;
uniform float4x4 sl_modelViewProj;
uniform float4x4 sl_billboard;
uniform float4 sl_flipTCoordsAndFogDensity;
uniform float4 sl_fogColor;
uniform float4 sl_billboardColor;
uniform float4 sl_fade;
uniform float4x4 sl_basis;
uniform float4 sl_noSpin;
uniform float4 sl_outputScale;

Texture2D gDiffuseMap;

#include "Samplers.hlsl"

float4 main(float4 posH : SV_POSITION,
            float4 color : COLOR,
            float4 texCoord : TEXCOORD0,
            float altitude : TEXCOORD1) : SV_TARGET {
    float4 fragIn;

    fragIn = gDiffuseMap.Sample(gTriLinearClampWrap, texCoord.xy);


    float4 finalColor = fragIn * color;

    float softness = sl_noSpin.z;
    if (softness > 0)
    {
        float softParticle = clamp(altitude / softness, 0.0, 1.0);
        finalColor = lerp(float4(0,0,0,0), finalColor, softParticle);
    }

    finalColor.xyz *= sl_outputScale.xxx;

    return finalColor;
}

