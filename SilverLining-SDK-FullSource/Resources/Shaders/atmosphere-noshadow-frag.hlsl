uniform float4x4 sl_modelViewProj;
uniform float4 sl_sunPosition;
uniform float4x4 sl_world;
uniform float3 sl_fadeDistVec;
uniform float4 sl_outputScale;


float4 main(float4 posH : SV_POSITION,
            float4 color : COLOR,
            float4 texCoord : TEXCOORD) : SV_TARGET {
    float4 finalColor = color;
    finalColor.xyz *= sl_outputScale.xxx;
    return finalColor;
}
