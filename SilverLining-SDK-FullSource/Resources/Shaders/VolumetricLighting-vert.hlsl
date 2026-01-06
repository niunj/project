
void main(float4 position : POSITION,
          float4 texCoord : TEXCOORD,

          out float4 oPosition : SV_POSITION,
          out float4 oTexCoord : TEXCOORD
         )
{
    oPosition = position;
    oTexCoord = texCoord;
}

