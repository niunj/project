
uniform float4 sl_intensity;
float4x4 gMVP;

#ifdef DX9
TEXTURE gDiffuseMap;
sampler gDiffuseSampler = sampler_state {
    Texture = (gDiffuseMap);
    MinFilter = Linear;
    MagFilter = Linear;
    MipFilter = Linear;
    AddressU = Wrap;
    AddressV = Wrap;
};
#else
Texture2D gDiffuseMap;
SamplerState gTriLinearSamWrap {
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = WRAP;
    AddressV = WRAP;
};
#endif

void VS(float3 iPosL : POSITION,
        float4 iColor : COLOR,
        float4 iTexCoord : TEXCOORD,

        out float4 oPosH : SV_POSITION,
        out float4 oColor : COLOR,
        out float4 oTexCoord : TEXCOORD )
{
    oPosH = mul(float4(iPosL, 1.0f), gMVP);
    oColor = iColor;
    oTexCoord = iTexCoord;
}

float4 PS(float4 posH : SV_POSITION,
          float4 color : COLOR,
          float4 texCoord : TEXCOORD ) : SV_TARGET {
    float4 oColor;
#ifdef DX9
    float3 fragIn = tex2D(gDiffuseSampler, texCoord.xy).rgb;
#else
    float3 fragIn = gDiffuseMap.Sample(gTriLinearSamWrap, texCoord.xy).rgb;
#endif
    oColor.rgb = sl_intensity.xyz * fragIn * color.rgb;
    oColor.a = 1.0;

    return oColor;
}

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
