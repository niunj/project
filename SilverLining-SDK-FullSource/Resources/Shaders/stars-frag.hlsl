float4x4 sl_invBasis;
float4x4 sl_modelViewProj;
float4x4 sl_equatorialToHorizon;
float4 sl_fog;
float4 sl_fogDistance;
float4 sl_up;
uniform float4 sl_outputScale;

float4 main(float4 posH : SV_POSITION,
            float4 color : COLOR,
            float3 texCoord : TEXCOORD) : SV_TARGET {

    float4 finalColor = color;
    finalColor.xyz *= sl_outputScale.xxx;
    return finalColor;
}
