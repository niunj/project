
uniform float4x4 sl_modelViewProj;
uniform float4 sl_sunPosition;
//uniform float4x4 sl_world;
uniform float3 sl_fadeDistVec;
uniform float4 sl_outputScale;
uniform float4x4 sl_billboard;

void VS(  float4 position : POSITION,
          float4 color : COLOR,
          float4 texCoord : TEXCOORD,

          out float4 oPosition : SV_POSITION,
          out float4 oColor : COLOR,
          out float4 oTexCoord : TEXCOORD
       )

{
    oTexCoord = texCoord;

    //float4 posWorld = mul(position, sl_world);
    //float3 nPos = normalize(posWorld.xyz / position.w);
    //float dp = dot(nPos, sl_sunPosition.xyz);

    float r = sl_fadeDistVec.z + texCoord.y;
    position.x *= r;
    position.y *= r;

    oPosition = mul(sl_modelViewProj, position);

    float3 nPos = normalize(mul(sl_billboard, position));
    float dp = dot(nPos, sl_sunPosition.xyz);

    float d = texCoord.z;
    float4 density = float4(d, d, d, 1.0);

    float fade = sl_fadeDistVec.y;
    if (dp <= 0) {
        fade = -dp / sl_fadeDistVec.x;
        fade = clamp(1.0 - fade, 0.0, 1.0);
    }

    float4 fadeColor = float4(fade, fade, fade, 1.0);
    float4 baseColor = float4(0.5, 0.5, 1.0, 1.0);

    oColor = baseColor * density * fadeColor;
}

float4 PS(float4 posH : SV_POSITION,
          float4 color : COLOR,
          float4 texCoord : TEXCOORD ) : SV_TARGET {
    float4 finalColor = color;
    finalColor.xyz *= sl_outputScale.xxx;
    return finalColor;
}

#ifdef DX11
technique11 ColorTech {
    pass P0
    {
        SetVertexShader( CompileShader( vs_5_0, VS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader( ps_5_0, PS() ) );
    }
}
#endif

#ifdef DX10
technique10 ColorTech {
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, VS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader( ps_4_0, PS() ) );
    }
}
#endif

#ifdef DX10LEVEL9
technique10 ColorTech {
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0_level_9_1, VS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader( ps_4_0_level_9_1, PS() ) );
    }
}
#endif

#ifdef DX9
technique {
    pass P0
    {
        SetVertexShader( CompileShader( vs_3_0, VS() ) );
        SetPixelShader( CompileShader( ps_3_0, PS() ) );
    }
}
#endif

#ifdef DX9_2_0
technique {
    pass P0
    {
        SetVertexShader( CompileShader( vs_2_0, VS() ) );
        SetPixelShader( CompileShader( ps_2_0, PS() ) );
    }
}
#endif
