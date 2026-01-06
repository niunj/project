// Copyright 2010 Sundog Software, LLC. All rights reserved worldwide.
uniform float4x4 sl_projectionMatrix;

struct SL_Vertex {
    float4 pos :
    SV_Position;
    float4 color :
    COLOR0;
    float3 tex :
    TEXCOORD0;
    float3 eyeDepth :
    TEXCOORD1;
};


void main(float4 position : POSITION,
          float4 color : COLOR,
          float4 texCoord : TEXCOORD0,

          out SL_Vertex oVert
         )
{
    // The vertex is in eye coords
    oVert.color = color;

    oVert.tex = texCoord.xyz;

    oVert.eyeDepth.x = -position.z;
    oVert.eyeDepth.y = 0;
    oVert.eyeDepth.z = 0;

    oVert.pos = mul(sl_projectionMatrix, position);
}
