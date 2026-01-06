uniform float4x4 sl_modelViewProj;
uniform float4 sl_lightingColor;
uniform float4 sl_outputScale;

Texture2D gDiffuseMap;

#include "Samplers.hlsl"

float4 main(float4 posH : SV_POSITION,
            float4 color : COLOR,
            float4 texCoord : TEXCOORD) : SV_TARGET {
    float4 fragIn;


    fragIn = gDiffuseMap.Sample(gTriLinearClampClamp, texCoord.xy);

    fragIn.xyz *= sl_outputScale.xxx;

    return fragIn * color;
}

