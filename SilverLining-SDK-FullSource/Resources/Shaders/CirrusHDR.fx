uniform float4x4 sl_modelView;
uniform float4x4 sl_modelViewProj;
uniform float4 sl_fogColorAndDensity;
uniform float4 sl_lightingColor;
uniform float4 sl_outputScale;
uniform float4 sl_textureOffset;

#ifdef DX9

TEXTURE gDiffuseMap;
sampler gDiffuseSampler = sampler_state {
    Texture = (gDiffuseMap);
    MinFilter = Linear;
    MagFilter = Linear;
    MipFilter = Linear;
    AddressU = Clamp;
    AddressV = Clamp;
};

#else

Texture2D gDiffuseMap;

SamplerState gTriLinearSamClamp {
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

#endif

void VS(  float4 position : POSITION,
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

float4 PS(float4 posH : SV_POSITION,
          float4 color : COLOR,
          float4 texCoord : TEXCOORD ) : SV_TARGET {
    float4 fragIn;

#ifdef DX9
    fragIn = tex2D(gDiffuseSampler, texCoord.xy + sl_textureOffset.xy);
#else
    fragIn = gDiffuseMap.Sample(gTriLinearSamClamp, texCoord.xy + sl_textureOffset.xy);
#endif

    fragIn.xyz *= sl_outputScale.xxx;

    return fragIn * color;
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
