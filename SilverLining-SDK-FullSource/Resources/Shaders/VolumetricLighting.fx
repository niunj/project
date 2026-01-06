uniform float4 sl_lightPositionOnScreen;
uniform float4 sl_parameters;
uniform float4 sl_lightColor;
uniform float4 sl_outputScale;

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

#define NUM_SAMPLES 100

float4 PS(float4 posH : SV_POSITION,
          float4 texCoord : TEXCOORD ) : SV_TARGET {
    float4 outColor = float4(0, 0, 0, 0);
    float exposure = sl_parameters.x;
    float decay = sl_parameters.y;
    float density = sl_parameters.z;
    float weight = sl_parameters.w;

    float2 deltaTextCoord = float2( texCoord.xy - sl_lightPositionOnScreen.xy );
    float2 textCoo = texCoord.xy;
    deltaTextCoord *= 1.0 /  float(NUM_SAMPLES) * density;
    float illuminationDecay = 1.0;

    for(int i=0; i < NUM_SAMPLES ; i++)
    {
        textCoo -= deltaTextCoord;

        float4 sample;
        float4 sampleCoords = float4(textCoo, 0, 0);
#ifdef DX9
        sample = tex2D(gDiffuseSampler, sampleCoords);
#else
        sample = gDiffuseMap.Sample(gTriLinearSamClamp, sampleCoords.xy);
#endif
        sample.xyz *= smoothstep(0.95, 1.0, sample.y) * 0.5 + 0.5;

        sample *= illuminationDecay * weight;
        outColor += sample;
        illuminationDecay *= decay;
    }

    outColor *= exposure;

    outColor.a = smoothstep(0.2, 1.0, outColor.y);
    outColor.xyz *= sl_lightColor.xyz * sl_outputScale.xxx;

    return outColor;
}

void VS(  float4 position : POSITION,
          float4 texCoord : TEXCOORD,

          out float4 oPosition : SV_POSITION,
          out float4 oTexCoord : TEXCOORD
       )
{
    oPosition = position;
    oTexCoord = texCoord;
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
