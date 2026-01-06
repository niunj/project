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
uniform float3 sl_luminanceScales;
uniform float4 sl_overcast;
uniform float4 sl_fog;
uniform float4 sl_outputScale;

float Perez(float3 ABC, float3 DE, float costheta, float gamma, float cosGamma)
{
    float perez = (1.0 + ABC.x * exp(ABC.y / costheta)) *
                  (1.0 + ABC.z * exp(DE.x * gamma) + DE.y * cosGamma * cosGamma);

    return perez;
}

float4 xyYtoRGB(float3 xyY, float4x4 XYZtoRGB)
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

    RGB.w = 1.0;

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

    float3 xyY;
    float3 xyYo = float3(0.310, 0.316, 0);

    // Find gamma angles
    float3 normalPos = normalize(position).xyz;
    float cosGammaS = dot(sl_sunPos, normalPos);
    float gammaS = acos(cosGammaS);

    // cos(Theta) was stuffed in u coord
    float costheta = texCoord.x;

    // Evaluate Perez functions
    xyY.x = Perez(sl_xPerezABC, sl_xPerezDE, costheta, gammaS, cosGammaS);
    xyY.y = Perez(sl_yPerezABC, sl_yPerezDE, costheta, gammaS, cosGammaS);
    xyY.z = Perez(sl_YPerezABC, sl_YPerezDE, costheta, gammaS, cosGammaS);

    // Normalize against zenith and sun
    xyY = sl_zenithPerez * (xyY / sl_sunPerez);

    float overcastBlend = sl_overcast.y;
    float overcastFactor = (1.0 + 2.0 * costheta) / 3.0;
    xyYo.z = sl_zenithPerez.z * overcastFactor;

    xyY = lerp(xyY, xyYo, overcastBlend);

    // Add fog

    float4 fogColor;
    fogColor.xyz = sl_fog.xyz;
    fogColor.w = 1.0;
    float fogDensity = sl_fog.w;
    float volumeDistance = sl_overcast.w;
    float fogDistance = volumeDistance / costheta;
    float f = exp(-(fogDensity * fogDistance));

    float4 skyColor = xyYtoRGB(xyY, sl_XYZtoRGB);

    oColor = lerp(fogColor, skyColor, f);
}
