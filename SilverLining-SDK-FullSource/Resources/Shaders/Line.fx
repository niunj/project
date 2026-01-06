uniform float4x4 sl_modelViewProj;
uniform float4 sl_colorConstant;

void VS(  float4 position : POSITION,
          out float4 oPosition : SV_POSITION
       )
{
    oPosition = mul(sl_modelViewProj, position);
}

float4 PS(float4 posH : SV_POSITION) : SV_TARGET {
    return sl_colorConstant;
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