uniform float4x4 sl_modelViewProj;
uniform float4x4 sl_XYZtoRGB;
uniform float3 sl_xPerezABC;
uniform float3 sl_xPerezDE;
uniform float3 sl_yPerezABC;
uniform float3 sl_yPerezDE;
uniform float3 sl_YPerezABC;
uniform float3 sl_YPerezDE;
uniform float3 sl_sunPerez;
uniform float3 sl_zenithPerez;
uniform float3 sl_sunPos;
uniform float3 sl_moonPerez;
uniform float3 sl_zenithMoonPerez;
uniform float3 sl_moonPos;
uniform float3 sl_luminanceScales;
uniform float4 sl_kAndLdmax;
uniform float4 sl_overcast;
uniform float4 sl_fog;
uniform float4 sl_outputScale;

float Perez(float3 ABC, float3 DE, float costheta, float gamma, float cosGamma)
{
    float perez = (1.0 + ABC.x * exp(ABC.y / costheta)) *
                  (1.0 + ABC.z * exp(DE.x * gamma) + DE.y * cosGamma * cosGamma);

    return perez;
}

float3 toneMap(float3 xyY, float mC, float mR, float k, float Ldmax)
{
    // This is based on Durand's operator, which is based on Ferwerda
    // which is based on Ward...

    // Convert Kcd/m2 to cd/m2
    float Y = xyY.z * 1000.0;

    // deal with negative luminances (nonsensical)
    // max() doesn't work here for some reason...
    if (Y < 0) {
        Y = 0;
    }

    float3 XYZ;
    float R;
    // Convert xyY to XYZ
    XYZ.x = xyY.x * (xyY.z / xyY.y);
    XYZ.y = xyY.z;
    XYZ.z = (1.0 - xyY.x - xyY.y) * (xyY.z / xyY.y);

    const float3 scotopic = float3(-0.702, 1.039, 0.433);
    R = dot(XYZ, scotopic);

    // Uncomment to apply perceptual blue-shift in scotopic conditions (Durand)
    //const float3 blue = float3(0.3, 0.3, 1);
    //xyY = lerp(xyY, blue, k);

    // Apply the photopic and scotopic scales that were precomputed in
    // the app per Ferwerda and Durand's operator
    float Ldp = Y * mC;
    float Lds = R * mR;

    Y = Ldp + k * Lds;
    // Normalize to display range
    Y = Y / Ldmax;

    xyY.z = Y;

    return xyY;
}

float4 xyYtoRGB(float3 xyY, float4x4 XYZtoRGB, float oneOverGamma)
{
    float4 XYZ, RGB;

    // Convert xyY to XYZ
    XYZ.x = xyY.x * (xyY.z / xyY.y);
    XYZ.y = xyY.z;
    XYZ.z = (1.0 - xyY.x - xyY.y) * (xyY.z / xyY.y);
    XYZ.w = 1.0;

    // Convert XYZ to RGB
    RGB = mul(XYZ, XYZtoRGB);

    // Deal with negative values
    float w = min(0, RGB.x);
    w = min(w, RGB.y);
    w = min(w, RGB.z);
    RGB += -w;

    // Scale down if necessary, preserving color
    float f = max(RGB.y, RGB.z);
    f = max(RGB.x, f); // f is now largest color component
    f = max(1.0, f); // if f is less than 1.0, set to 1.0 (to do nothing)

    RGB.xyz = RGB.xyz / f;
    RGB = pow(abs(RGB), oneOverGamma);

    RGB.w = 1.0;

    RGB = saturate(RGB);

    return RGB;
}

void main(float4 position : POSITION,
          float4 color : COLOR,
          float4 texCoord : TEXCOORD,

          out float4 oPosition : SV_POSITION,
          out float4 oColor : COLOR,
          out float3 oTexCoord : TEXCOORD
         )
{
    oPosition = mul(sl_modelViewProj, position);
    oTexCoord = texCoord.xyz;

    float3 xyYs, xyYm;
    float3 xyYso = float3(0.310, 0.316, 0);
    float3 xyYmo = float3(0.310, 0.316, 0);

    // Find gamma angles
    float3 normalPos = normalize(position).xyz;
    float cosGammaS = dot(sl_sunPos, normalPos);
    float gammaS = acos(cosGammaS);
    float cosGammaM = dot(sl_moonPos, normalPos);
    float gammaM = acos(cosGammaM);

    // cos(Theta) was stuffed in u coord
    float costheta = texCoord.x;

    // Evaluate Perez functions
    xyYs.x = Perez(sl_xPerezABC, sl_xPerezDE, costheta, gammaS, cosGammaS);
    xyYs.y = Perez(sl_yPerezABC, sl_yPerezDE, costheta, gammaS, cosGammaS);
    xyYs.z = Perez(sl_YPerezABC, sl_YPerezDE, costheta, gammaS, cosGammaS);

    xyYm.x = Perez(sl_xPerezABC, sl_xPerezDE, costheta, gammaM, cosGammaM);
    xyYm.y = Perez(sl_yPerezABC, sl_yPerezDE, costheta, gammaM, cosGammaM);
    xyYm.z = Perez(sl_YPerezABC, sl_YPerezDE, costheta, gammaM, cosGammaM);

    // Normalize against zenith and sun
    xyYs = sl_zenithPerez * (xyYs / sl_sunPerez);
    xyYm = sl_zenithMoonPerez * (xyYm / sl_moonPerez);

    float overcastBlend = sl_overcast.y;
    float overcastFactor = (1.0 + 2.0 * costheta) / 3.0;
    xyYso.z = sl_zenithPerez.z * overcastFactor;
    xyYmo.z = sl_zenithMoonPerez.z * overcastFactor;

    xyYs = lerp(xyYs, xyYso, overcastBlend);
    xyYm = lerp(xyYm, xyYmo, overcastBlend);

    // Map luminance to 0-1, plus do scotopic / mesopic effects
    xyYs = toneMap(xyYs, sl_luminanceScales.y, sl_luminanceScales.x, sl_kAndLdmax.x,
                   sl_kAndLdmax.y);
    xyYm = toneMap(xyYm, sl_luminanceScales.y, sl_luminanceScales.x, sl_kAndLdmax.x,
                   sl_kAndLdmax.y);

    float4 skyColor = xyYtoRGB(xyYs, sl_XYZtoRGB, sl_kAndLdmax.z) + xyYtoRGB(xyYm, sl_XYZtoRGB, sl_kAndLdmax.z);

    // Add fog
    float4 fogColor;
    fogColor.xyz = sl_fog.xyz;
    fogColor.w = 1.0;
    float fogDensity = sl_fog.w;
    float volumeDistance = sl_overcast.w;
    float fogDistance = volumeDistance / costheta;
    float f = exp(-(fogDensity * fogDistance));

    oColor = lerp(fogColor, skyColor, f);
}
