uniform float4x4 sl_modelViewProj;
uniform float4 sl_lightingColor;
uniform float4 sl_outputScale;


void main(float4 position : POSITION,
          float4 color : COLOR,
          float4 texCoord : TEXCOORD,

          out float4 oPosition : SV_POSITION,
          out float4 oColor : COLOR,
          out float4 oTexCoord : TEXCOORD
         )
{
    oColor = sl_lightingColor;

    oPosition = mul(sl_modelViewProj, position);

    oTexCoord = texCoord;
}

