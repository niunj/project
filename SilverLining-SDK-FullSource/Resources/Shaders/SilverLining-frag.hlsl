Texture2D gDiffuseMap;
float4x4 gModelview;
float4x4 gMVP;
float4x4 gTexture;
bool gVertexColors;
bool gFog;
float gFogDensity;
float4 gFogColor;
float4 gCurrentColor;
bool gTexture2D;
bool gTextureRepeatU;
bool gTextureRepeatV;

#include "Samplers.hlsl"

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

float4 main(SL_Vertex inVert) : SV_TARGET {
    float4 fragIn = float4(1.0f, 1.0f, 1.0f, 1.0f);

    if (gTexture2D)
    {
        if (gTextureRepeatU && gTextureRepeatV) {
            fragIn = gDiffuseMap.Sample(gTriLinearWrapWrap, inVert.tex.xy);
        } else if (gTextureRepeatU && !gTextureRepeatV) {
            fragIn = gDiffuseMap.Sample(gTriLinearWrapClamp, inVert.tex.xy);
        } else if (!gTextureRepeatU && gTextureRepeatV) {
            fragIn = gDiffuseMap.Sample(gTriLinearClampWrap, inVert.tex.xy);
        } else if (!gTextureRepeatU && !gTextureRepeatV) {
            fragIn = gDiffuseMap.Sample(gTriLinearClampClamp, inVert.tex.xy);
        }
    }

    float4 coloredVert;

    if (gVertexColors)
    {
        coloredVert = fragIn * inVert.color;
    } else
    {
        coloredVert = fragIn * gCurrentColor;
    }

    if (gFog)
    {
        float4 fogged = lerp(gFogColor, coloredVert, inVert.fogLerp);
        fogged.a = coloredVert.a;
        return fogged;
    } else
    {
        return coloredVert;
    }
}
