float4x4 gModelview;
float4x4 gMVP;
float gFogDensity;
float4 gFogColor;
float4x4 gTexture;

struct SL_Vertex {
    float4 pos :
    SV_Position;
    float4 color :
    COLOR0;
    float3 tex :
    TEXTURE0;
    float fogLerp :
    FOG;
};

void main(float3 iPosL : POSITION,
          float4 iColor : COLOR,
          float4 iTexCoord : TEXCOORD,

          out SL_Vertex oVert)
{
    oVert.pos = mul(float4(iPosL, 1.0f), gMVP);
    oVert.color = iColor;

    float3 eyePosition = mul(gModelview, float4(iPosL, 1.0f)).xyz;
    float fogDistance = length(eyePosition);
    float fogExponent = fogDistance * gFogDensity;
    oVert.fogLerp = exp(-abs(fogExponent));

    oVert.tex = mul(float4(iTexCoord.xyz, 1.0f), gTexture).xyz;
}
