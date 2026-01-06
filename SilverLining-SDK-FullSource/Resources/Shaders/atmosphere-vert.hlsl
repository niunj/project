
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
    oTexCoord = texCoord;

    float4 posWorld = mul(position, sl_world);
    float3 nPos = normalize(posWorld.xyz / position.w);
    float dp = dot(nPos, sl_sunPosition.xyz);

    float r = sl_fadeDistVec.z + texCoord.y;
    position.x *= r;
    position.y *= r;

    oPosition = mul(sl_modelViewProj, position);

    float d = texCoord.z;
    float4 density = float4(d, d, d, 1.0);

    float fade = sl_fadeDistVec.y;
    if (dp <= 0) {
        fade = -dp / sl_fadeDistVec.x;
        fade = 1.0 - fade;
    }

    float4 fadeColor = float4(fade, fade, fade, 1.0);
    float4 baseColor = float4(0.5, 0.5, 1.0, 1.0);

    oColor = baseColor * density * fadeColor;
}
