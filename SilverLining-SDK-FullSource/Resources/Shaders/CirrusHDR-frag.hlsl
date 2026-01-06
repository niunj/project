uniform float4x4 sl_modelView;
uniform float4x4 sl_modelViewProj;
uniform float4 sl_fogColorAndDensity;
uniform float4 sl_lightingColor;
uniform float4 sl_outputScale;
uniform float4 sl_textureOffset;

Texture2D gDiffuseMap;

#include "Samplers.hlsl"

float4 main(float4 posH : SV_POSITION,
            float4 color : COLOR,
            float4 texCoord : TEXCOORD) : SV_TARGET {
    float4 fragIn;

    fragIn = gDiffuseMap.Sample(gTriLinearClampClamp, texCoord.xy + sl_textureOffset.xy);

    fragIn.xyz *= sl_outputScale.xxx;

    return fragIn * color;
}

