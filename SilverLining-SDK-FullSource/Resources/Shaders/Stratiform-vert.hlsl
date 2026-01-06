uniform float4x4 sl_modelViewProj;
uniform float4x4 sl_modelView;
uniform float4 sl_modelPos;
uniform float4 sl_cameraPos;
uniform float4 sl_fogColorAndDensity;
uniform float4 sl_cloudTint;
uniform float4 sl_displacementVectorAndContrast;
uniform float4 sl_fadeAndDisplacementFactor;
uniform float4 sl_upVectorAndThickness;
uniform float4 sl_sunColor;
uniform float4 sl_skyColor;
uniform float4 sl_lightDirection;
uniform float4 sl_layerThicknessAndIsTop;
uniform float4 sl_layerSizeAndUnitScale;
uniform float4x4 sl_invBasis;
uniform float4 sl_groundColor;
uniform float4 sl_extinctionFactor;
uniform float4 sl_outputScale;

#define PI 3.14159265
#define extinctionCoefficient 0.04618
#define EPSILON_ABOVE 0.05
#define EPSILON_BELOW 0.03
// Artificial boost to make fogbow & glory more visible
#define SINGLE_SCATTERING_SCALE 1.5
#define MIE
#define EDGE_TRANSLUCENCY_ABOVE 0.3
#define EDGE_TRANSLUCENCY_BELOW 0.15
#define MIN_OCCLUSION 0.2

// Sacrifice some accuracy for speed.
//#define FAST

Texture2D gDiffuseMap;
Texture2D gDiffuseMap2;

#include "Samplers.hlsl"

void main(float4 position : POSITION,
          float4 color : COLOR,
          float3 texCoord : TEXCOORD0,

          out float4 oPosition : SV_POSITION,
          out float4 oColor : COLOR,
          out float3 oTexCoord : TEXCOORD0,
          out float3 V : TEXCOORD1,
          out float fadeDist : TEXCOORD2
         )
{
    float unitScale = sl_layerSizeAndUnitScale.w;

    oTexCoord = texCoord;

    float4 vcolor = float4(color.x, color.y, color.z, color.w * sl_fadeAndDisplacementFactor.x);
    oColor = vcolor;

    float4 t = float4(oTexCoord.x, oTexCoord.y, 0, 0);

    float4 texel = gDiffuseMap.SampleLevel(gTriLinearWrapWrap, t.xy, 0);

    float thickness = texel.x * sl_upVectorAndThickness.w;

    float3 displacement = float3(0, -1, 0) * (sl_upVectorAndThickness.w - thickness);

    position.xyz += displacement;

    float3 worldPos = position.xyz + sl_modelPos.xyz;
    float4 w4 = mul(sl_invBasis, float4(worldPos, 1.0));
    V = (sl_cameraPos.xyz - w4.xyz);

    fadeDist = length(mul(float4(V, 1.0), sl_invBasis).xz);

    oPosition = mul(sl_modelViewProj, position);
}
