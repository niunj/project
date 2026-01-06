// Copyright 2006-2014 Sundog Software, LLC. All rights reserved worldwide.

#include "CirroStratusLayer.h"
#include "Renderer.h"
#include "Configuration.h"
#include "Sky.h"
#include "Camera.h"
#include "CloudLayerTcsUserData.h"
#include "BillboardRenderingParameters.h"
#include "SLAssert.h"
#include "CirrusShader.h"
#include <algorithm>

using namespace SilverLining;

CirroStratusCloudLayer::CirroStratusCloudLayer(const Atmosphere& atmosphere) : CloudLayer(atmosphere)
{

}

CirroStratusCloudLayer::~CirroStratusCloudLayer()
{

}

bool CirroStratusCloudLayer::DrawSetup(int pass, const Vector3 *lightPos, const Color *lightColor, ThreadCameraStreamData* tcsData) const
{
    return true;
}

bool CirroStratusCloudLayer::EndDraw(int pass) const
{
    return true;
}

bool CirroStratusCloudLayer::SeedClouds(const Atmosphere& atm, void* data)
{
    SL_ASSERT(&atm == &atmosphere);
    if (!atmosphere.IsInitialized()) return false;

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);

    tcsData->SetForceImmediate(true);

    const Camera* camera = tcsData->GetCamera();

    // Single "cloud" for entire layer:
    ClearClouds(tcsData);
    CirroStratusCloud *cloud = SL_NEW CirroStratusCloud(this, atmosphere.GetRandomNumberGenerator()->UniformRandomDouble());
    double x, z;
    GetLayerPosition(x, z, tcsData);
    double w = GetBaseWidth();
    double h = GetBaseLength();
    cloud->SetWorldPosition(Vector3(x, GetBaseAltitude(tcsData), z) * camera->GetBasis3x3());
    cloud->Init(w, h, tcsData);
    AddCloud(cloud, tcsData);

    tcsData->SetForceImmediate(false);

    return true;
}

void CirroStratusCloudLayer::ProcessAtmosphericEffects(Sky *sky, const Atmosphere* atmosphere, const Camera* camera, ThreadCameraStreamData* tcsData) const
{
    if (!IsRenderable(tcsData) || GetThickness() <= 0) {
        return;
    }

    const CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);
    const double layerX = tcsUserData->layerX;
    const double layerZ = tcsUserData->layerZ;

    Renderer* renderer = tcsData->GetRenderer();
    Vector3 camPos = camera->GetPosition() * camera->GetInverseBasis3x3();

    if (camPos.x < (layerX - GetBaseWidth() * 0.5)) return;
    if (camPos.x > (layerX + GetBaseWidth() * 0.5)) return;
    if (camPos.z > (layerZ + GetBaseLength() * 0.5)) return;
    if (camPos.z < (layerZ - GetBaseLength() * 0.5)) return;

    double cloudBottom = tcsUserData->geocentricAltitude;
    double cloudTop = cloudBottom + GetThickness();

    double r, g, b;
    Color fogColor;
    Configuration::GetDoubleValue("cirrus-fog-red", r);
    Configuration::GetDoubleValue("cirrus-fog-blue", b);
    Configuration::GetDoubleValue("cirrus-fog-green", g);
    double density;
    Configuration::GetDoubleValue("cirrus-fog-density", density);

    double blend = (cloudTop - camPos.y) / GetThickness();

    if (blend > 1.0) blend = 1.0;
    if (blend < 0) blend = 0;

    BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();

    Color horizonColor = billboardParams->GetFogColor();// s->GetAverageHorizonColor(0);
    Color sun = sky->GetSunOrMoonColor();
    sun.ScaleToUnitOrLess(tcsData->GetHDREnabled());
    sun = sun.ToGrayscale();

    r = (blend * horizonColor.r) + ((1.0 - blend) * r * sun.r);
    g = (blend * horizonColor.g) + ((1.0 - blend) * g * sun.g);
    b = (blend * horizonColor.b) + ((1.0 - blend) * b * sun.b);
    fogColor = Color(r, g, b, 1.0);

    double fogDepth = 0;
    // Set sky fog effects
    if (camPos.y <= cloudTop && camPos.y >= cloudBottom) {
        fogDepth = cloudTop - camPos.y;
        sky->SetFogVolumeDistance(fogDepth);

        renderer->EnableFog(true);
        renderer->ConfigureFog(density, 1, 100000, fogColor);
        billboardParams->SetFogColor(fogColor);
        billboardParams->SetFogDensity(density);
    }
}

void CirroStratusCloudLayer::WrapClouds(bool geocentric, const Camera* camera, CloudLayerTcsUserData* tcsUserData)
{
    const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

    if (!clouds.empty() && GetIsInfinite()) clouds.front()->SetNeedsGeocentricPlacement(true);
}

bool CirroStratusCloudLayer::RestoreClouds(const Atmosphere& atm, std::istream& s, ThreadCameraStreamData* tcsData)
{
    int nClouds;
    s.read((char *)&nClouds, sizeof(int));
    if (nClouds != 1) return false;

    return SeedClouds(atm, tcsData);
}

ShaderHandle CirroStratusCloudLayer::GetShader(ThreadCameraStreamData* tcsData) const
{
    return tcsData->GetCirrusShader()->GetShader();
}

void CirroStratusCloudLayer::ReloadShaders(ThreadCameraStreamData* tcsData)
{
    tcsData->ReloadCirrusShader();
}
