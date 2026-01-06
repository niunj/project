uniform float4x4 sl_modelView;
uniform float4x4 sl_modelViewProj;
uniform float4x4 sl_billboard;
uniform float4 sl_flipTCoordsAndFogDensity;
uniform float4 sl_fogColor;
uniform float4 sl_billboardColor;
uniform float4 sl_fade;
uniform float4x4 sl_basis;
uniform float4 sl_noSpin;
uniform float4 sl_outputScale;

void main(float4 position : POSITION,
          float4 color : COLOR,
          float4 texCoord : TEXCOORD,

          out float4 oPosition : SV_POSITION,
          out float4 oColor : COLOR,
          out float4 oTexCoord : TEXCOORD0,
          out float oAltitude : TEXCOORD1
         )
{
    float3 eyePosition = mul(sl_modelView, float4(position.xyz, 1.0)).xyz;
    float fogDistance = length(eyePosition);
    float fogExponent = clamp(fogDistance * sl_flipTCoordsAndFogDensity.y, 0.0, 1.0);
    float fogBoost = sl_fade.y;
    float fogFactor = clamp(exp(-(fogExponent * fogExponent)) / fogBoost, 0.0, 1.0);
    bool additiveBlend = sl_flipTCoordsAndFogDensity.w > 0.0;
    float4 preFadedColor;

    if (sl_billboardColor.w > 0) {
        preFadedColor.xyz = lerp(sl_fogColor.xyz, sl_billboardColor.xyz, fogFactor);
        preFadedColor.w = sl_billboardColor.w;
    } else {
        float4 c = color * float4(sl_billboardColor.xyz, 1.0);
        preFadedColor = lerp(sl_fogColor, c, fogFactor);
    }

    float voxelFade = 1.0;
    if (sl_fade.z > 0.0) {
        if (texCoord.w > 0.0) {
            voxelFade = abs(texCoord.w) * (sl_fade.w - sl_fade.z);
        } else if (texCoord.w < 0.0) {
            voxelFade = 1.0 - abs(texCoord.w) * (sl_fade.w - sl_fade.z);
        }
        voxelFade = clamp(voxelFade, 0.0, 1.0);
    }

    float2 dir = float2(sign(texCoord.y), sign(texCoord.z));
    float4 vertPos = float4(abs(texCoord.x) * dir.x, abs(texCoord.x) * dir.y, 0, 1.0);

    float4 rotatedPos;

    float gradient = sl_noSpin.y;

    if (sl_noSpin.x > 0.0) {
        rotatedPos = float4(vertPos.xy, 0.0, 1.0);
        float grad = 1.0 - (-dir.y * gradient);
        preFadedColor.xyz *= grad;
    } else {
        float cosLength, sinLength;
        float angle = abs(texCoord.z) + abs(texCoord.y) * sign(texCoord.x) * sl_fade.w;
        angle = fmod(angle, 3.14159265f * 2.0f);
        sincos(angle, sinLength, cosLength);
        rotatedPos.x = (cosLength * vertPos.x + -sinLength * vertPos.y);
        rotatedPos.y = (sinLength * vertPos.x + cosLength * vertPos.y);
        rotatedPos.z = 0;
        rotatedPos.w = 1.0;

        float nposy = sinLength * dir.x + cosLength * dir.y;
        float grad = 1.0 - (-nposy * gradient);
        preFadedColor.xyz *= grad;
    }

    bool isAtlas = position.w > 0.0;
    float scale = isAtlas ? 1.2 : 1.0;

    float4 vert = mul(position, sl_basis);
    float4x4 xlate = float4x4(scale, 0, 0, vert.x, 0, scale, 0, vert.y, 0, 0, scale, vert.z, 0, 0, 0, 1);
    float4x4 xlatebb = mul(xlate, sl_billboard);
    float4x4 mvb = mul(sl_modelViewProj, xlatebb);
    oPosition = mul(mvb, rotatedPos);

    float4 modelPos = mul(xlate, rotatedPos);
    modelPos = mul(sl_basis, modelPos);
    oAltitude = modelPos.y;

    float2 tc = float2(0.5 * sign(texCoord.y) + 0.5, 0.5 * sign(texCoord.z) * sl_flipTCoordsAndFogDensity.x + 0.5);

    if (isAtlas) {
        tc *= float2(0.25, 0.25);
        float idx = position.w - 1.0;
        float row = floor(idx / 4.0);
        float col = idx - row * 4.0;
        tc.x += 0.25 * col;
        tc.y += (0.25 * (3.0 - row));
    }

    if (additiveBlend) {
        oColor = lerp(float4(0, 0, 0, 0), preFadedColor, sl_fade.xxxx * voxelFade * (1.0 - fogExponent * fogExponent));
    } else {
        oColor = float4(preFadedColor.xyz, sl_fade.x * voxelFade * (1.0 - fogExponent * fogExponent) * preFadedColor.a);
    }

    oTexCoord = float4(tc.x, tc.y,
                       0.0, 0.0);
}
