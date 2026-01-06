// Copyright (c) 2006-2017 Sundog Software, LLC. All rights reserved worldwide.

#include "CumulonimbusCloudLayer.h"
#include "CumulonimbusCloud.h"
#include "MathUtils.h"
#include "Configuration.h"
#include "Billboard.h"
#include "BillboardBuffer.h"
#include "BillboardShader.h"
#include "CloudLayerTcsUserData.h"
#include "Camera.h"
#include "SLAssert.h"

using namespace SilverLining;

CumulonimbusCloudLayer::CumulonimbusCloudLayer(const Atmosphere& atmosphere) : CumulusCloudLayer(atmosphere)
{
    ReadConfiguration("cumulonimbus");

    const char *dischargeModeStr = NULL;
    Configuration::GetStringValue("lightning-discharge-mode", dischargeModeStr);
    dischargeMode = AUTO_DISCHARGE;
    if (dischargeModeStr) {
        if (strcmp(dischargeModeStr, "auto-discharge") == 0) {
            dischargeMode = AUTO_DISCHARGE;
        } else if (strcmp(dischargeModeStr, "fire-and-forget") == 0) {
            dischargeMode = FIRE_AND_FORGET;
        } else if (strcmp(dischargeModeStr, "fire-on-off") == 0) {
            dischargeMode = FORCE_ON_OFF;
        }
    }
}

CumulusCloud *CumulonimbusCloudLayer::CreateCloud(CloudLayer *parentLayer, double coverageTreshold, bool voxelsUseShaderInstancing, bool _drawIndirect, ThreadCameraStreamData* tcsData)
{
    return SL_NEW CumulonimbusCloud(parentLayer, coverageTreshold, voxelsUseShaderInstancing, _drawIndirect, tcsData);
}

void CumulonimbusCloudLayer::SetBaseAltitude(double meters, bool updateCloudPositions, void* data) const
{
    CloudLayer::SetBaseAltitude(meters, updateCloudPositions, data);

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);


    if (updateCloudPositions) {

        const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

        for (SL_VECTOR(Cloud *)::const_iterator it = clouds.begin(); it != clouds.end(); it++) {
            Cloud *cloud = *it;
            cloud->CloudPlaced(atmosphere, tcsData);
        }
    }
}

bool CumulonimbusCloudLayer::SeedClouds(const Atmosphere& atm, void* data)
{
    SL_ASSERT(&atm == &atmosphere);
    if (!atmosphere.IsInitialized()) return false;

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);

    tcsData->SetForceImmediate(true);

    const Camera* camera = tcsData->GetCamera();

    ClearClouds(tcsData);

    ScanCloudFiles(atmosphere);

    SetupSurfaces(tcsData);

    parameters.width = GetBaseWidth();
    parameters.depth = GetBaseLength();
    parameters.height = GetThickness();
    parameters.spinRate = GetSpinRate();

    if (!CreateCloudFromFile(atmosphere, parameters.height, tcsData)) {

        const bool voxelShaderInstancing = (tcsData->GetBillboardShader() && tcsData->GetBillboardShader()->GetShaderHandle() != 0);

        CumulusCloud *cloud = CreateCloud(this, atmosphere.GetRandomNumberGenerator()->UniformRandomDouble(), voxelShaderInstancing, tcsData->DrawIndirect(), tcsData);
        cloud->Init(parameters, atmosphere, tcsData);

        double worldWidth, worldDepth, worldHeight;
        cloud->GetSize(worldWidth, worldDepth, worldHeight);
        maxHeight = worldHeight;

        double velocity, heading;
        atmosphere.GetConditions().GetWind(velocity, heading, GetBaseAltitude(tcsData));
        double dx = localWindX + sin(MathUtils::ToRadians(heading)) * velocity;
        double dz = localWindZ + cos(MathUtils::ToRadians(heading)) * velocity;
        cloud->ApplyWindShear(Vector3(dx, 0, dz) * camera->GetBasis3x3(), tcsData);

        double x, z;
        GetLayerPosition(x, z, tcsData);
        Vector3 pos(x, GetBaseAltitude(tcsData), z);
        pos = pos * camera->GetBasis3x3();
        cloud->SetWorldPosition(pos);
        cloud->SetNeedsGeocentricPlacement(true);

        AddCloud(cloud, tcsData);

        const BillboardBufferProvider* provider = tcsData->GetBillboardBufferProvider();

        for (int i = 0; i < parameters.initialEvolveSteps; i++) {
            cloud->IncrementTimeStep(0, atmosphere.GetRandomNumberGenerator(), provider, tcsData);
        }

        cloud->CloudPlaced(atmosphere, tcsData);
    }

    tcsData->SetForceImmediate(false);

    return true;
}

bool CumulonimbusCloudLayer::ForceLightning(bool value, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    const CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);
    const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

    if (clouds.size() > 0) {
        CumulonimbusCloud *cl = dynamic_cast<CumulonimbusCloud *>(clouds[0]);
        if (cl) {
            return cl->ForceLightning(value);
        }
    }

    return false;
}
void CumulonimbusCloudLayer::SetLightningDischargeMode(LightningDischargeMode value, void *data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();

    dischargeMode = value;

    const CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);
    const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

    if (clouds.size() > 0) {
        CumulonimbusCloud *cl = dynamic_cast<CumulonimbusCloud *>(clouds[0]);
        if (cl) {
            cl->ResetLightningTimer();
        }
    }

}
LightningDischargeMode CumulonimbusCloudLayer::GetLightningDischargeMode()
{
    return dischargeMode;
}
void CumulonimbusCloudLayer::SetEnabled(bool enabled, unsigned long fadeTimeMS, void* data, bool _enabledInTcsData)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    CloudLayer::SetEnabled(enabled, fadeTimeMS, tcsData, _enabledInTcsData);

    const CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);
    const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

    if (clouds.size() > 0) {
        CumulonimbusCloud *cl = dynamic_cast<CumulonimbusCloud *>(clouds[0]);
        if (cl) {
            cl->ResetLightningTimer();
        }
    }

}