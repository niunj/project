uniform float4 sl_lightPositionOnScreen;
uniform float4 sl_parameters;
uniform float4 sl_lightColor;
uniform float4 sl_outputScale;

Texture2D gDiffuseMap;

#include "Samplers.hlsl"

#define NUM_SAMPLES 100

float4 main(float4 posH : SV_POSITION,
            float4 texCoord : TEXCOORD) : SV_TARGET {
    float4 outColor = float4(0, 0, 0, 0);
    float exposure = sl_parameters.x;
    float decay = sl_parameters.y;
    float density = sl_parameters.z;
    float weight = sl_parameters.w;

    float2 deltaTextCoord = float2(texCoord.xy - sl_lightPositionOnScreen.xy);
    float2 textCoo = texCoord.xy;
    deltaTextCoord *= 1.0 / float(NUM_SAMPLES) * density;
    float illuminationDecay = 1.0;

    for (int i = 0; i < NUM_SAMPLES; i++)
    {
        textCoo -= deltaTextCoord;

        float4 sample;
        float4 sampleCoords = float4(textCoo, 0, 0);

        sample = gDiffuseMap.Sample(gTriLinearClampClamp, sampleCoords.xy);

        sample.xyz *= smoothstep(0.95, 1.0, sample.y) * 0.5 + 0.5;

        sample *= illuminationDecay * weight;
        outColor += sample;
        illuminationDecay *= decay;
    }

    outColor *= exposure;

    outColor.a = smoothstep(0.2, 1.0, outColor.y);
    outColor.xyz *= sl_lightColor.xyz * sl_outputScale.xxx;

    return outColor;
}
