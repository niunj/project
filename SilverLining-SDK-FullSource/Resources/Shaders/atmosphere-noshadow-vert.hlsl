uniform float4x4 sl_modelViewProj;
uniform float4 sl_sunPosition;
uniform float4x4 sl_world;
uniform float3 sl_fadeDistVec;
uniform float4 sl_outputScale;

void main(float4 position : POSITION,
          float4 color : COLOR,
          float4 texCoord : TEXCOORD,

          out float4 oPosition : SV_POSITION,
          out float4 oColor : COLOR,
          out float4 oTexCoord : TEXCOORD
         )

{
    float r = sl_fadeDistVec.z + texCoord.y;
    position.x = r * cos(texCoord.x);
    position.y = r * sin(texCoord.x);
    oPosition = mul(sl_modelViewProj, position);

    float fade = sl_fadeDistVec.y;
    float d = texCoord.z;
    float4 density = float4(d, d, d, 1.0);
    float4 baseColor = float4(0.5, 0.5, 1.0, 1.0);
    float4 fadeColor = float4(fade, fade, fade, 1.0);

    oColor = baseColor * density * fadeColor;
    oTexCoord = texCoord;
}
