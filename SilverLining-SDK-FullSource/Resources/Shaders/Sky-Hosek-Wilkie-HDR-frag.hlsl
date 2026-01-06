uniform float4x4 sl_modelViewProj;
uniform float4x4 sl_XYZtoRGB;
uniform float3 sl_xPerezABC;
uniform float3 sl_xPerezDE;
uniform float3 sl_yPerezABC;
uniform float3 sl_yPerezDE;
uniform float3 sl_YPerezABC;
uniform float3 sl_YPerezDE;
uniform float3 sl_sunPerez;
uniform float3 sl_zenithPerez;
uniform float3 sl_sunPos;
uniform float3 sl_luminanceScales;
uniform float4 sl_overcast;
uniform float4 sl_fog;

uniform float4 sl_HosekRadiances;
uniform float3 sl_XHosekABC;
uniform float3 sl_XHosekDEF;
uniform float3 sl_XHosekGHI;
uniform float3 sl_YHosekABC;
uniform float3 sl_YHosekDEF;
uniform float3 sl_YHosekGHI;
uniform float3 sl_ZHosekABC;
uniform float3 sl_ZHosekDEF;
uniform float4 sl_ZHosekGHI;

uniform float4 sl_outputScale;

float4 main(float4 posH : SV_POSITION,
            float4 color : COLOR,
            float3 texCoord : TEXCOORD) : SV_TARGET {
    float4 finalColor = color;
    finalColor.xyz *= sl_outputScale.xxx;
    return finalColor;
}
