
uniform float4 sl_intensity;
float4x4 gMVP;

Texture2D gDiffuseMap;

#include "Samplers.hlsl"

float4 main(float4 posH : SV_POSITION,
            float4 color : COLOR,
            float4 texCoord : TEXCOORD) : SV_TARGET {
    float4 oColor;

    float3 fragIn = gDiffuseMap.Sample(gTriLinearWrapWrap, texCoord.xy).rgb;

    oColor.rgb = sl_intensity.xyz * fragIn * color.rgb;
    oColor.a = 1.0;

    return oColor;
}

