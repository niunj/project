// Copyright 2006-2019 Sundog Software, LLC. All rights reserved worldwide.

#include "StratusCloudLayer.h"
#include "Renderer.h"
#include "Configuration.h"
#include "PrecipitationManager.h"
#include "Sky.h"
#include "Camera.h"
#include "CloudLayerTcsUserData.h"
#include "BillboardRenderingParameters.h"
#include "StratusTextureData.h"
#include "MathUtils.h"
#include "SLAssert.h"
#include <algorithm>

namespace SilverLining
{
StratusCloudLayer::StratusCloudLayer(const Atmosphere& atmosphere) : CloudLayer(atmosphere)
    , stratusTextureData(0)
{
    bulginess = 0.6;
    Configuration::GetDoubleValue("stratus-cloud-bulgy-exponent", bulginess);

    SetAlpha(0.95);
}

StratusCloudLayer::~StratusCloudLayer()
{
    SL_DELETE stratusTextureData;
}

void StratusCloudLayer::ClearClouds(void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();

    CloudLayer::ClearClouds(tcsData);
    myCloud = 0;
}

void StratusCloudLayer::GetBounds(Vector3 bounds[8], const Camera* camera, ThreadCameraStreamData* tcsData) const
{
    CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);

    double x, z;
    if (GetIsInfinite()) {
        Vector3 camPosYUp = camera->GetPosition() * camera->GetInverseBasis3x3();
        x = camPosYUp.x;
        z = camPosYUp.z;
    } else {
        const double layerX = tcsUserData->layerX;
        const double layerZ = tcsUserData->layerZ;

        x = layerX;
        z = layerZ;
    }

    double cloudWidth=0, cloudDepth=0, cloudHeight=0;

    if (myCloud) {
        myCloud->GetSize(cloudWidth, cloudDepth, cloudHeight);
    }

    Vector3 minimum, maximum;

    minimum.x = x - cloudWidth * 0.5;
    minimum.y = tcsUserData->geocentricAltitude;
    minimum.z = z - cloudDepth * 0.5;
    maximum.x = x + cloudWidth * 0.5;
    maximum.y = tcsUserData->geocentricAltitude + GetMaxHeight();
    maximum.z = z + cloudDepth * 0.5;

    Matrix3 basis = GetLocalBasis(tcsUserData);
    bounds[0] = Vector3(minimum.x, minimum.y, minimum.z) * basis;
    bounds[1] = Vector3(minimum.x, minimum.y, maximum.z) * basis;
    bounds[2] = Vector3(minimum.x, maximum.y, minimum.z) * basis;
    bounds[3] = Vector3(minimum.x, maximum.y, maximum.z) * basis;
    bounds[4] = Vector3(maximum.x, minimum.y, minimum.z) * basis;
    bounds[5] = Vector3(maximum.x, minimum.y, maximum.z) * basis;
    bounds[6] = Vector3(maximum.x, maximum.y, minimum.z) * basis;
    bounds[7] = Vector3(maximum.x, maximum.y, maximum.z) * basis;
}

bool StratusCloudLayer::HasPrecipitationAtPosition(double x, double y, double z, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();

    if (precipitationEffects.size() == 0 || !IsRenderable(tcsData)) return false;

    CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);
    const double layerX = tcsUserData->layerX;
    const double layerZ = tcsUserData->layerZ;

    Matrix3 invBasis = GetLocalBasis(tcsUserData).Transpose();
    Vector3 camPos = Vector3(x, y, z) * invBasis;

    if (!GetIsInfinite()) {
        if (camPos.x < (layerX - GetBaseWidth() * 0.5)) return false;
        if (camPos.x > (layerX + GetBaseWidth() * 0.5)) return false;
        if (camPos.z > (layerZ + GetBaseLength() * 0.5)) return false;
        if (camPos.z < (layerZ - GetBaseLength() * 0.5)) return false;
    }

    return (camPos.y < tcsUserData->geocentricAltitude);
}

bool StratusCloudLayer::IsInsideCloud(const Camera* camera, void* data, double* distanceInside) const
{
    const Vector3& pos = camera->GetPosition();

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();;

    return IsInsideCloud(pos.x, pos.y, pos.z, tcsData, distanceInside);
}

bool StratusCloudLayer::IsInsideCloud(double x, double y, double z, void* data, double* distanceInside) const
{
    if (distanceInside) *distanceInside = 0;

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);
    CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);

    const double layerX = tcsUserData->layerX;
    const double layerZ = tcsUserData->layerZ;

    Matrix3 invBasis = GetLocalBasis(tcsUserData).Transpose();
    Vector3 camPos = Vector3(x, y, z) * invBasis;

    if (!GetIsInfinite()) {
        if (camPos.x < (layerX - GetBaseWidth() * 0.5)) return false;
        if (camPos.x > (layerX + GetBaseWidth() * 0.5)) return false;
        if (camPos.z > (layerZ + GetBaseLength() * 0.5)) return false;
        if (camPos.z < (layerZ - GetBaseLength() * 0.5)) return false;
    }

    if (camPos.y >= tcsUserData->geocentricAltitude && camPos.y <= (tcsUserData->geocentricAltitude + GetThickness())) {

        if (distanceInside) {
            double y1 = abs(camPos.y - tcsUserData->geocentricAltitude);
            double y2 = abs(camPos.y - (tcsUserData->geocentricAltitude + GetThickness()));
            *distanceInside = y1 < y2 ? y1 : y2;
        }

        return true;
    } else {
        return false;
    }
}

void StratusCloudLayer::UpdateOvercast(Sky *s, const Camera* camera, ThreadCameraStreamData* tcsData)
{
    if (!IsRenderable(tcsData)) {
        return;
    }

    CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);

    Matrix3 invBasis = GetLocalBasis(tcsUserData).Transpose();
    Vector3 camPos = camera->GetPosition() * invBasis;
    double cloudBottom = tcsUserData->geocentricAltitude;
    double cloudTop = cloudBottom + GetThickness();

    double overcastThreshold = 0.5;
    Configuration::GetDoubleValue("stratus-overcast-threshold", overcastThreshold);

    double ambientTransmission = 0.2, directTransmission = 0;
    Configuration::GetDoubleValue("stratus-light-transmission-ambient", ambientTransmission);
    Configuration::GetDoubleValue("stratus-light-transmission-direct", directTransmission);

    double blend = (cloudTop - camPos.y) / GetThickness();

    if (blend > 1.0) blend = 1.0;
    if (blend < 0) blend = 0;

    if (GetDensity() > overcastThreshold && GetIsInfinite()) {
        // Set overcast sky effects
        if (camPos.y < cloudTop) {
            s->SetOvercast(true);
            s->SetOvercastParams(blend * GetFade(tcsData) * alpha * GetDensity(), ambientTransmission * GetDensity(),
                                 directTransmission * GetDensity());
        }
    }
}

void StratusCloudLayer::ProcessAtmosphericEffects(Sky *sky, const Atmosphere* atmosphere, const Camera* camera, ThreadCameraStreamData* tcsData) const
{
    if (!IsRenderable(tcsData)) {
        return;
    }

    CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);
    const double layerX = tcsUserData->layerX;
    const double layerZ = tcsUserData->layerZ;

    Matrix3 invBasis = GetLocalBasis(tcsUserData).Transpose();
    Vector3 camPos = camera->GetPosition() * invBasis;

    if (!GetIsInfinite()) {
        if (camPos.x < (layerX - GetBaseWidth() * 0.5)) return;
        if (camPos.x > (layerX + GetBaseWidth() * 0.5)) return;
        if (camPos.z > (layerZ + GetBaseLength() * 0.5)) return;
        if (camPos.z < (layerZ - GetBaseLength() * 0.5)) return;
    }

    double scudThickness;
    Configuration::GetDoubleValue("stratus-scud-thickness", scudThickness);
    scudThickness *= Atmosphere::GetUnitScale();

    double cloudBottom = tcsUserData->geocentricAltitude;
    double cloudTop = cloudBottom + GetThickness();
    //double cloudMiddle = (cloudBottom + cloudTop) * 0.5;
    double scudBottom = cloudBottom - scudThickness;
    double scudTop = cloudTop + scudThickness;

    double r, g, b;
    Color fogColor;
    Configuration::GetDoubleValue("stratus-fog-red", r);
    Configuration::GetDoubleValue("stratus-fog-blue", b);
    Configuration::GetDoubleValue("stratus-fog-green", g);
    double density;
    Configuration::GetDoubleValue("stratus-fog-density", density);

    double blend = (cloudTop - camPos.y) / GetThickness();

    if (blend > 1.0) blend = 1.0;
    if (blend < 0) blend = 0;


    BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();
    Color horizonColor;
    if (sky->GetOvercast()) {
        horizonColor = sky->GetOvercastHorizonColor();
    } else {
        horizonColor = billboardParams->GetFogColor();// s->GetAverageHorizonColor(0);
    }

    Color sun = sky->GetSunOrMoonColor();
    sun.ScaleToUnitOrLess(tcsData->GetHDREnabled());
    sun = sun.ToGrayscale();

    bool lightFog = true;
    Configuration::GetBoolValue("stratus-light-fog", lightFog);
    blend *= blend * blend;
    if (lightFog) {
        r = (blend * horizonColor.r) + ((1.0 - blend) * r * sun.r);
        g = (blend * horizonColor.g) + ((1.0 - blend) * g * sun.g);
        b = (blend * horizonColor.b) + ((1.0 - blend) * b * sun.b);
    }
    fogColor = Color(r, g, b, 1.0);

    double fogDepth = 0;
    // Set sky fog effects
    if (camPos.y <= scudTop && camPos.y >= scudBottom) {
        fogDepth = scudTop - camPos.y;
        sky->SetFogVolumeDistance(fogDepth);
    }

    Renderer* renderer = tcsData->GetRenderer();
    double precipitationFogDensity = (tcsData->GetPrecipitationEnabled()) ? tcsData->GetPrecipitationManager()->GetFogDensity()
                                     : 0;
    if (precipitationFogDensity > 0) {
        renderer->EnableFog(true);
        renderer->ConfigureFog(precipitationFogDensity, 1, 100000, fogColor);
        billboardParams->SetFogColor(fogColor);
        billboardParams->SetFogDensity(precipitationFogDensity);
        sky->SetFogVolumeDistance(1.0 / precipitationFogDensity);
    }

    if (myCloud) {
        const double epsilon = 0.001;

        float scud = myCloud->LookupScud(camera->GetPosition(), tcsData);

        //printf("scud %f\n", scud);

        if (scud > epsilon) {
            double cloudFogDensity = scud * density;// *(fogDepth / scudThickness);
            if (precipitationFogDensity > cloudFogDensity) cloudFogDensity = precipitationFogDensity;

            cloudFogDensity = cloudFogDensity * GetFade(tcsData) + 1.0E-9 * (1.0 - GetFade(tcsData));

            if (cloudFogDensity > billboardParams->GetFogDensity()) {
                renderer->EnableFog(true);
                renderer->ConfigureFog(cloudFogDensity, 1, 100000, fogColor);
                billboardParams->SetFogColor(fogColor);
                billboardParams->SetFogDensity(cloudFogDensity);
            }
        }
    }
}

bool StratusCloudLayer::DrawSetup(int pass, const Vector3 *lightPos, const Color *lightColor, ThreadCameraStreamData* tcsData) const
{
    return true;
}

bool StratusCloudLayer::EndDraw(int pass) const
{
    return true;
}

bool StratusCloudLayer::SeedClouds(const Atmosphere& atm, void* data)
{
    SL_ASSERT(&atm == &atmosphere);
    if (!atmosphere.IsInitialized()) return false;

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);

    tcsData->SetForceImmediate(true);

    ClearClouds(tcsData);

    // Single "cloud" for entire layer, stretching to "infinity" (100km)
    double w, h;

    if (isInfinite) {
        Configuration::GetDoubleValue("stratus-deck-width", w);
        Configuration::GetDoubleValue("stratus-deck-height", h);
        w *= Atmosphere::GetUnitScale();
        h *= Atmosphere::GetUnitScale();
    } else {
        w = GetBaseWidth();
        h = GetBaseLength();
    }

    myCloud = SL_NEW StratusCloud(this, atmosphere.GetRandomNumberGenerator()->UniformRandomDouble());
    double x, z;
    GetLayerPosition(x, z, tcsData);
    myCloud->SetWorldPosition(Vector3(x, GetBaseAltitude(tcsData), z) * tcsData->GetCamera()->GetBasis3x3());
    myCloud->Init(w, h, GetThickness(), GetDensity(), tcsData);
    AddCloud(myCloud, tcsData);

    tcsData->SetForceImmediate(false);

    return true;
}

void StratusCloudLayer::WrapClouds(bool geocentric, const Camera* camera, CloudLayerTcsUserData* tcsUserData)
{
}

bool StratusCloudLayer::RestoreClouds(const Atmosphere& atm, std::istream& s, ThreadCameraStreamData* tcsData)
{
    int nClouds;
    s.read((char *)&nClouds, sizeof(int));
    if (nClouds != 1) return false;

    return SeedClouds(atm, tcsData);
}

ShaderHandle StratusCloudLayer::GetShader(ThreadCameraStreamData* tcsData) const
{
    return tcsData->GetStratusShader()->GetShader();
}

void StratusCloudLayer::ReloadShaders(ThreadCameraStreamData* tcsData)
{
    return tcsData->ReloadStratusShader();
}

StratusTextureData* StratusCloudLayer::GetOrCreateTextureData(double density, double edgeTexSizeX, double edgeTexSizeY)
{
    if (stratusTextureData) {
        return stratusTextureData;
    }

    int tgaWidth = 0;
    int tgaHeight = 0;
    const unsigned char* noise = GetAtmosphere().GetDefaultTcsData()
                                 ->GetStratusNoisePixels(tgaWidth, tgaHeight);

    if (noise == 0) {
        return 0;
    }

    stratusTextureData = SL_NEW StratusTextureData(tgaWidth, tgaHeight, GetIsInfinite());

    const int scudMapW = tgaWidth;
    const int scudMapH = tgaHeight;
    SL_VECTOR(unsigned char*)& scudMap = stratusTextureData->scudMap;

    double scudThickness;
    Configuration::GetDoubleValue("stratus-scud-thickness", scudThickness);
    scudThickness *= Atmosphere::GetUnitScale();

    double maxOpticalThickness;
    Configuration::GetDoubleValue("stratus-max-optical-thickness", maxOpticalThickness);
    maxOpticalThickness *= Atmosphere::GetUnitScale();

    double cloudThickness = GetThickness();

    int texSize = tgaWidth * tgaHeight * 2;

    double tx = (double)(tgaWidth - 1);
    double ty = (double)(tgaHeight - 1);

    double stratusEdgeFalloff = 4.0;
    Configuration::GetDoubleValue("stratus-edge-falloff", stratusEdgeFalloff);

    double densityExponent = 0.6;
    Configuration::GetDoubleValue("stratus-coverage-exponent", densityExponent);

    double curvedDensity = pow(density, densityExponent);

    int numTextures = GetIsInfinite() ? 1 : NUM_SECTIONS;

    for (int texNum = 0; texNum < numTextures; texNum++) {

        unsigned char* pixels = stratusTextureData->pixels[texNum];
        unsigned char* p = pixels;

        int& repeatU = stratusTextureData->repeatU[texNum];
        int& repeatV = stratusTextureData->repeatV[texNum];

        repeatU = repeatV = 1;

        const unsigned char* pnoise = noise;

        for (int y = 0; y < tgaHeight; y++) {
            for (int x = 0; x < tgaWidth; x++) {
                unsigned char src = *pnoise++;

                //unsigned char alpha;

                double blend = 1.0;

                if (!GetIsInfinite()) {
                    double dx = (double)x;
                    double dy = (double)y;

                    double xx = 1.0, yy = 1.0;

                    if (texNum == LEFT || texNum == TOPLEFT || texNum == BOTTOMLEFT) {
                        double t = 1.0 - dx / tx / edgeTexSizeX;
                        t = MathUtils::clamp(t, 0, 1);
                        xx = 1.0 - pow(t, stratusEdgeFalloff);
                        repeatU = 0;
                    } else if (texNum == RIGHT || texNum == TOPRIGHT || texNum == BOTTOMRIGHT) {
                        double t = dx / tx / edgeTexSizeX - (1.0 - edgeTexSizeX) * (1.0 / edgeTexSizeX);
                        t = MathUtils::clamp(t, 0, 1);
                        xx = 1.0 - pow(t, stratusEdgeFalloff);
                        repeatU = 0;
                    }

                    if (texNum == TOP || texNum == TOPLEFT || texNum == TOPRIGHT) {
                        double t = 1.0 - dy / ty / edgeTexSizeY;
                        t = MathUtils::clamp(t, 0, 1);
                        yy = 1.0 - pow(t, stratusEdgeFalloff);
                        repeatV = 0;
                    } else if (texNum == BOTTOM || texNum == BOTTOMLEFT || texNum == BOTTOMRIGHT) {
                        double t = dy / ty / edgeTexSizeY - (1.0 - edgeTexSizeY) * (1.0 / edgeTexSizeY);
                        t = MathUtils::clamp(t, 0, 1);
                        yy = 1.0 - pow(t, stratusEdgeFalloff);
                        repeatV = 0;
                    }

                    blend = xx * yy;
                }

                double adjustedDensity = curvedDensity * blend;

                double nSrc = (double)src / 255.0;
                double nClamped = nSrc;

                double minNSrc = 0;

                if (adjustedDensity >= 1.0) {
                    if (scudThickness < cloudThickness) {
                        double scud = scudThickness * nSrc;
                        double baseThickness = cloudThickness - scudThickness;
                        double thickness = baseThickness + scud;
                        nSrc = thickness / cloudThickness;
                        if (GetIsInfinite()) minNSrc = baseThickness / cloudThickness;
                        if (nSrc < 0) nSrc = 0;
                        if (nSrc > 1.0) nSrc = 1.0;


                        double clampedThickness = cloudThickness > maxOpticalThickness ? maxOpticalThickness : cloudThickness;
                        baseThickness = clampedThickness - scudThickness;
                        thickness = baseThickness + scud;
                        nClamped = thickness / clampedThickness;
                        if (nClamped < 0) nClamped = 0;
                        if (nClamped > 1.0) nClamped = 1.0;

                    }
                } else {
                    if (blend < 1.0 && density >= 1.0) {
                        double scud = scudThickness * nSrc;
                        double baseThickness = cloudThickness - scudThickness;
                        double thickness = baseThickness + scud;
                        nSrc = thickness / cloudThickness;
                        nSrc -= (1.0 - adjustedDensity);
                        if (nSrc < 0) nSrc = 0;
                        if (nSrc > 1.0) nSrc = 1.0;

                        double clampedThickness = cloudThickness > maxOpticalThickness ? maxOpticalThickness : cloudThickness;
                        baseThickness = clampedThickness - scudThickness;
                        thickness = baseThickness + scud;
                        nClamped = thickness / clampedThickness;
                        nClamped -= (1.0 - adjustedDensity);
                        if (nClamped < 0) nClamped = 0;
                        if (nClamped > 1.0) nClamped = 1.0;
                    } else {
                        nSrc -= (1.0 - adjustedDensity);
                        if (nSrc < 0) nSrc = 0;
                        nSrc /= adjustedDensity;
                        nClamped = nSrc;
                    }
                }

                nSrc = pow(nSrc, bulginess);
                nSrc = (nSrc - minNSrc) / (1.0 - minNSrc);
                nClamped = pow(nClamped, bulginess);

                unsigned char dst = (unsigned char)(nSrc * 255.0);
                *p++ = dst;
                scudMap[texNum][y * scudMapH + x] = dst;
                dst = (unsigned char)(nClamped * 255.0);
                *p++ = dst;
            }
        }
    }

    return stratusTextureData;
}
StratusTextureData* StratusCloudLayer::GetTextureData(void)
{
    return stratusTextureData;
}
}