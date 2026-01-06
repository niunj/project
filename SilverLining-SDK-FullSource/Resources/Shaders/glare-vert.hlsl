
uniform float4 sl_intensity;
float4x4 gMVP;


void main(float3 iPosL : POSITION,
          float4 iColor : COLOR,
          float4 iTexCoord : TEXCOORD,

          out float4 oPosH : SV_POSITION,
          out float4 oColor : COLOR,
          out float4 oTexCoord : TEXCOORD)
{
    oPosH = mul(float4(iPosL, 1.0f), gMVP);
    oColor = iColor;
    oTexCoord = iTexCoord;
}
