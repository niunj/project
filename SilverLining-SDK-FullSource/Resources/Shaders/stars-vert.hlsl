float4x4 sl_invBasis;
float4x4 sl_modelViewProj;
float4x4 sl_equatorialToHorizon;
float4 sl_fog;
float4 sl_fogDistance;
float4 sl_up;
uniform float4 sl_outputScale;

void main(float4 position : POSITION,
          float4 color : COLOR,
          float4 texCoord : TEXCOORD0,

          out float4 oPosition : SV_POSITION,
          out float4 oColor : COLOR,
          out float3 oTexCoord : TEXCOORD0

         )
{
    float4 horizonCoords = mul(sl_equatorialToHorizon, float4(position.xyz, 1.0));
    oPosition = mul(sl_modelViewProj, horizonCoords);

    oTexCoord = texCoord.xyz;

    float magnitude = texCoord.x;

    // At scotopic levels when stars are visible, the varying effect of scattering by
    // wavelength usually isn't visible. So it's not modeled to gain back a little performance.

    // Convert magnitude to luminance
    float luminance = pow(100.0, (-magnitude / 5.0));

    float r2 = luminance / (pow(luminance, 2.0 / 3.0) + 0.02); // The .02 term controls the overall star brightness.
    r2 *= sl_fogDistance.y;
    float4 luminanceRGB = float4(r2, r2, r2, 1);

    float4 starColor = color * luminanceRGB;

    // Add fog
    float4 fogColor;
    fogColor.xyz = sl_fog.xyz;
    fogColor.w = 1.0;
    float fogDensity = sl_fog.w;
    float volumeDistance = sl_fogDistance.x;

    //  float3 starPos = oPosition.xyz;
    float cosTheta = max(1E-10f, dot(normalize(horizonCoords.xyz), sl_up.xyz));
    float fFogDistance = volumeDistance / cosTheta;
    float f = exp(-(fogDensity * fFogDistance));

    f = clamp(f, 0.0, 1.0);

    float4 foggedColor = lerp(fogColor, starColor, f);

    float4 finalColor = foggedColor * f;

    oTexCoord.z = 0;
    oColor = finalColor;
}
