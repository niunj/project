uniform float4x4 sl_modelView;
uniform float4x4 sl_modelViewProj;
uniform float4 sl_fogColorAndDensity;
uniform float4 sl_lightingColor;
uniform float4 sl_outputScale;;


void main(float4 position : POSITION,
          float4 color : COLOR,
          float4 texCoord : TEXCOORD,

          out float4 oPosition : SV_POSITION,
          out float4 oColor : COLOR,
          out float4 oTexCoord : TEXCOORD
         )
{
    float3 eyePosition = mul(sl_modelView, position).xyz;
    float fogDistance = length(eyePosition);
    float fogExponent = fogDistance * sl_fogColorAndDensity.w;
    float fogFactor = clamp(exp(-abs(fogExponent)), 0.0, 1.0);

    oColor.xyz = lerp(sl_fogColorAndDensity.xyz, sl_lightingColor.xyz, fogFactor);
    oColor.w = fogFactor * sl_lightingColor.a;
    oColor = oColor * color;

    oPosition = mul(sl_modelViewProj, position);

    oTexCoord = texCoord;
}

