// Copyright (c) 2004-2022  Sundog Software, LLC. All rights reserved worldwide.
#include "CloudLayer.h"
#include "Cloud.h"
#include "Renderer.h"
#include "Metaball.h"
#include "LightningListener.h"
#include "Utils.h"
#include "PrecipitationManager.h"
#include "Billboard.h"
#include "Configuration.h"
#include "Sky.h"
#include "CloudLayerSerializationUtils.h"
#include "MathUtils.h"
#include "Camera.h"
#include "CloudLayerCullSet.h"
#include "BillboardRenderingParameters.h"
#include "MetaballRenderingParameters.h"
#include "SLAssert.h"
#include "CloudLayerTcsUserData.h"
#include <float.h>

#include <fstream>

using namespace std;

namespace SilverLining
{
CloudLayer::CloudLayer(const Atmosphere& _atmosphere)
    : atmosphere(_atmosphere)
{
    baseWidth = 2000;
    baseLength = 2000;

    thickness = 700;
    density = 0.5;
    layerEnabled = true;

    lightningListeners.clear();

    alpha = 0.8;

    isInfinite = false;
    localWindX = 0.0;
    localWindZ = 0.0;
    cloudWrap = false;
    fadeTowardEdges = false;
    growth = false;
    enableCulling = true;

    usingRuntimeDensity = false;
    curveTowardGround = false;
    hasForcedCloudColor = false;
    forcedCloudColorLighting = true;
    usingCloudAtlas = false;
    soft = false;
    handle = 0;

    Configuration::GetBoolValue("enable-cloud-layer-culling", enableCulling);

    double eqRad = 6378137.00, polRad = 6356752.3142;

    Configuration::GetDoubleValue("earth-radius-meters-equatorial", eqRad);
    Configuration::GetDoubleValue("earth-radius-meters-polar", polRad);

    eqRad *= Atmosphere::GetUnitScale();
    polRad *= Atmosphere::GetUnitScale();

    edgeFadeThreshold = 0.1;
    Configuration::GetDoubleValue("cloud-edge-fade-threshold", edgeFadeThreshold);

    bool zup = true;
    Configuration::GetBoolValue("geocentric-z-is-up", zup);

    if (zup) {
        earthRadii = Vector3(eqRad, eqRad, polRad);
    } else {
        earthRadii = Vector3(eqRad, polRad, eqRad);
    }

    cloudLightingBudget = 1000;
    Configuration::GetIntValue("cloud-relight-budget", cloudLightingBudget);
}

void CloudLayer::DeleteTcsUserData(ThreadCameraStreamData* data)
{
    if (data == 0) {
        return;
    }
    TcsUserData* userData = data->GetUserData(this);
    if (userData) {
        SL_DELETE userData;
    }
    data->EraseUserData(this);
}

void CloudLayer::DeleteCloudLayerTcsUserData(ThreadCameraStreamData* data)
{
    if (data == 0) {
        return;
    }
    TcsUserData* userData = data->GetCloudLayerTcsUserData(this);
    if (userData) {
        SL_DELETE userData;
    }
    data->EraseCloudLayerTcsUserData(this);
}

CloudLayerTcsUserData* CloudLayer::GetOrCreateCloudLayerTcsUserData(ThreadCameraStreamData* data) const
{
    if (data == 0) {
        return 0;
    }

    TcsUserData* userData = data->GetCloudLayerTcsUserData(this);
    bool created = false;
    if (userData == 0) {
        userData = SL_NEW CloudLayerTcsUserData(data, this);
        data->SetCloudLayerTcsUserData(this, userData);
        created = true;
    }

    return (CloudLayerTcsUserData*)userData;
}

void CloudLayer::SetRoundEdges(CurveModes mode, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    CurveModes& curveMode = tcsUserData->curveMode;
    bool& curveModeDirty = tcsUserData->curveModeDirty;

    if (mode != curveMode) {
        curveMode = mode;
        curveModeDirty = true;
    }
}

CloudLayer::CurveModes CloudLayer::GetRoundEdges(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    return tcsUserData->curveMode;
}

void CloudLayer::OverrideCloudColor(const Vector3& color, bool doLighting, void *data)
{
    hasForcedCloudColor = true;
    forcedCloudColorLighting = doLighting;
    forcedCloudColor = color;

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);

    const SL_VECTOR(Cloud*)& clouds = GetClouds();
    SL_VECTOR(Cloud*)::const_iterator cit;
    for (cit = clouds.begin(); cit != clouds.end(); cit++) {
        Cloud* c = *cit;
        c->Invalidate(tcsData);
    }
}

void CloudLayer::ClearCloudColorOverride()
{
    hasForcedCloudColor = false;
}

bool CloudLayer::GetOverriddenCloudColor(Vector3& color, bool& doLighting) const
{
    if (hasForcedCloudColor) {
        color = forcedCloudColor;
        doLighting = forcedCloudColorLighting;
    }
    return hasForcedCloudColor;
}

CloudLayer::~CloudLayer()
{
    lightningListeners.clear();
}

const SL_VECTOR(Cloud *)& CloudLayer::GetClouds(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);
    return tcsUserData->clouds;
}

void CloudLayer::ClearClouds(void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);
    SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

    for (SL_VECTOR(Cloud*)::iterator it = clouds.begin(); it != clouds.end(); it++) {
        SL_DELETE(*it);
    }

    clouds.clear();
}

void CloudLayer::AddCloud(Cloud *cloud, ThreadCameraStreamData* tcsData)
{
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);
    SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

    clouds.push_back(cloud);
}

const Vector3& CloudLayer::GetLocalUpVector(CloudLayerTcsUserData* tcsUserData) const
{
    SL_ASSERT(tcsUserData);

    if (!isInfinite) {
        return tcsUserData->localUp;
    } else {
        return tcsUserData->tcsData->GetCamera()->GetUpVector();
    }
}

const Matrix3& CloudLayer::GetLocalBasis(CloudLayerTcsUserData* tcsUserData) const
{
    SL_ASSERT(tcsUserData);

    if (!isInfinite) {
        return tcsUserData->localBasis;
    } else {
        return tcsUserData->tcsData->GetCamera()->GetBasis3x3();
    }
}

const Matrix4& CloudLayer::GetLocalBasis4(CloudLayerTcsUserData* tcsUserData) const
{
    SL_ASSERT(tcsUserData);

    if (!isInfinite) {
        return tcsUserData->localBasis4;
    } else {
        return tcsUserData->tcsData->GetCamera()->GetBasis4x4();
    }
}

void CloudLayer::SetLocalUpVector(const Vector3& up, ThreadCameraStreamData* tcsData) const
{
    SL_ASSERT(tcsData);
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);
    tcsUserData->localUp = up;
}
void CloudLayer::SetLocalBasis(const Matrix3& basis, ThreadCameraStreamData* tcsData) const
{
    SL_ASSERT(tcsData);
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);
    tcsUserData->localBasis = basis;
}
void CloudLayer::SetLocalBasis4(const Matrix4& basis4, ThreadCameraStreamData* tcsData) const
{
    SL_ASSERT(tcsData);
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);
    tcsUserData->localBasis4 = basis4;
}

void CloudLayer::SetDensityMultiplier(double multiplier, void* data )
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    double& coverageMultiplier = tcsUserData->coverageMultiplier;
    bool& densityDirty = tcsUserData->densityDirty;

    if (multiplier != coverageMultiplier) {
        densityDirty = true;
    }
    coverageMultiplier = multiplier;
    usingRuntimeDensity = true;
}

double CloudLayer::GetDensityMultiplier(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    return tcsUserData->coverageMultiplier;
}

double CloudLayer::GetFade(ThreadCameraStreamData* tcsData) const
{
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);
    return tcsUserData->fade;
}

bool CloudLayer::IsRenderable(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    return tcsUserData->renderingEnabled;
}

bool CloudLayer::GetEnabled(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    return tcsUserData->enabledInTcsData;
}

void CloudLayer::SetEnabled(bool enabled, unsigned long fadeTimeMS, void* data, bool _enabledInTcsData)
{
    layerEnabled = enabled;

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    double& fade = tcsUserData->fade;
    double& fadeBegin = tcsUserData->fadeBegin;
    unsigned long & fadeTime = tcsUserData->fadeTime;
    unsigned long & fadeStart = tcsUserData->fadeStart;
    FadeModes& fadeMode = tcsUserData->fadeMode;
    bool& renderingEnabled = tcsUserData->renderingEnabled;

    bool& enabledInTcsData = tcsUserData->enabledInTcsData;
    enabledInTcsData = _enabledInTcsData;

    fadeTime = fadeTimeMS;
    fadeStart = 0;
    fadeBegin = fade;

    if (fadeTimeMS <= 0) {
        fadeMode = NO_FADE;
        renderingEnabled = enabled;
        fade = fadeBegin = renderingEnabled ? 1.0 : 0.0;
    } else {
        if (enabled && enabledInTcsData) {
            fadeMode = FADE_IN;
            renderingEnabled = true;
        } else {
            fadeMode = FADE_OUT;
        }
    }

    tcsUserData->forceLightingUpdate = true;
}

void CloudLayer::ProcessFade(unsigned long now, ThreadCameraStreamData* tcsData) const
{
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    double& fade = tcsUserData->fade;
    const double& fadeBegin = tcsUserData->fadeBegin;
    const unsigned long & fadeTime = tcsUserData->fadeTime;
    unsigned long & fadeStart = tcsUserData->fadeStart;
    FadeModes& fadeMode = tcsUserData->fadeMode;
    bool& renderingEnabled = tcsUserData->renderingEnabled;

    if (fadeMode == NO_FADE) {
        fade = 1.0;
    } else {
        if (fadeStart == 0) {
            fadeStart = now;
        }
        unsigned long dt = now - fadeStart;
        double fadePer = (double)dt / (double)fadeTime;
        if (fadePer > 1.0) fadePer = 1.0;

        if (fadeMode == FADE_IN) {
            fade = fadeBegin + fadePer * (1.0 - fadeBegin);
        } else {
            fade = fadeBegin - fadePer * fadeBegin;
        }

        if (fadePer >= 1.0) {
            if (fadeMode == FADE_OUT) renderingEnabled = false;
            fadeMode = NO_FADE;
        }
    }
}

double CloudLayer::GetBaseAltitude(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);
    return tcsUserData->baseAltitude;
}

void CloudLayer::SetBaseAltitude(double meters, bool updateCloudPositions, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    tcsUserData->baseAltitude = meters;

    if (updateCloudPositions) {

        const Camera* camera = tcsData->GetCamera();

        tcsUserData->localUp = camera->GetUpVector();
        tcsUserData->localBasis = camera->GetBasis3x3();
        tcsUserData->localBasis4 = camera->GetBasis3x3();

        const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

        for (SL_VECTOR(Cloud *)::const_iterator it = clouds.begin(); it != clouds.end(); it++) {
            Cloud *cloud = *it;
            Vector3 cloudPos = cloud->GetWorldPosition(tcsData);
            cloudPos = cloudPos * camera->GetInverseBasis3x3();
            cloudPos.y = meters;
            cloudPos = cloudPos * camera->GetBasis3x3();
            cloud->SetWorldPosition(cloudPos);

            cloud->SetNeedsGeocentricPlacement(true);
        }
    }
}

void CloudLayer::GetLayerPosition(double& east, double& south, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();;
    SL_ASSERT(tcsData);
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    east = tcsUserData->layerX;
    south = tcsUserData->layerZ;
}


void CloudLayer::SetLayerPosition(double x, double z, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    tcsUserData->layerX = x;
    tcsUserData->layerZ = z;

    Vector3 layerPos(x, GetBaseAltitude(tcsData), z);

    const Camera* camera = tcsData->GetCamera();

    tcsUserData->localUp = camera->GetUpVector();
    tcsUserData->localBasis = camera->GetBasis3x3();
    tcsUserData->localBasis4 = camera->GetBasis3x3();

    const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

    for (SL_VECTOR(Cloud *)::const_iterator it = clouds.begin(); it != clouds.end(); it++) {
        Cloud *cloud = *it;
        Vector3 pos = layerPos + cloud->GetLayerPosition();
        pos = pos * camera->GetBasis3x3();
        cloud->SetWorldPosition(pos);
        cloud->SetNeedsGeocentricPlacement(true);
    }
}

void CloudLayer::MoveClouds(double x, double y, double z, ThreadCameraStreamData* tcsData)
{
    if (!IsRenderable(tcsData)) return;

    const Vector3 v(x, y, z);
    const Vector3 localV = v * tcsData->GetCamera()->GetInverseBasis3x3();

    const bool wrappedCloud = (isInfinite || cloudWrap);

    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    // Update the world position of each cloud
    const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;
    for (SL_VECTOR(Cloud *)::const_iterator it = clouds.begin(); it != clouds.end(); it++) {

        Cloud* cloud = *it;

        cloud->SetWorldPosition(cloud->GetWorldPosition(tcsData) + v);

        // If cloud layer is wrapped, update the position of each cloud
        // relative to the layer
        if (wrappedCloud) {
            Vector3 layerPos = cloud->GetLayerPosition() + localV;
            cloud->SetLayerPosition(layerPos);
        }
    }

    // If cloud layer is localized, update the layer's position
    if (!wrappedCloud) {
        tcsUserData->layerX += localV.x;
        tcsUserData->layerZ += localV.z;
        tcsUserData->baseAltitude += localV.y;
    }
}

double CloudLayer::GetWrapFade(Cloud *c, const Vector3& anchor, bool fadeOverride, CloudLayerTcsUserData* tcsUserData) const
{
    if ((isInfinite || cloudWrap) && (fadeTowardEdges&&fadeOverride)) {
        double width, depth, height;
        c->GetSize(width, depth, height);

        double halfWidth = baseWidth * 0.5;
        double halfLength = baseLength * 0.5;

        Vector3 cloudPos = c->GetWorldPosition(tcsUserData->tcsData);
        cloudPos = GetLocalBasis(tcsUserData) * cloudPos;

        double minDist = 1.0; // normalized distance from edge

        double dist = ((anchor.x + halfWidth) - (cloudPos.x + width)) / halfWidth;
        if (dist < minDist) minDist = dist;
        dist = ((cloudPos.x - width) - (anchor.x - halfWidth)) / halfWidth;
        if (dist < minDist) minDist = dist;
        dist = ((anchor.z + halfLength) - (cloudPos.z + depth)) / halfLength;
        if (dist < minDist) minDist = dist;
        dist = ((cloudPos.z - depth) - (anchor.z - halfLength)) / halfLength;
        if (dist < minDist) minDist = dist;

        double t = 1.0 - minDist;
        if (t < 0) t = 0;
        if (t > 1.0) t = 1.0;

        double fade = 1.0 - (t * t * t * t * t);

        return fade;
    }

    return 1.0;
}

bool CloudLayer::IntersectEllipsoid(const Vector3& p0, const Vector3& D, Vector3& intersect) const
{
    Vector3 radii = earthRadii;

    if (radii.x == 0 || radii.y == 0 || radii.z == 0) return false;

    Vector3 oneOverRadii(1.0 / radii.x, 1.0 / radii.y, 1.0 / radii.z);
    Vector3 q = p0 * oneOverRadii;
    Vector3 bUnit = D * oneOverRadii;
    bUnit.Normalize();
    double wMagnitudeSquared = q.Dot(q) - 1.0;

    double t = -bUnit.Dot(q);
    double tSquared = t * t;

    if ((t >= 0.0) && (tSquared >= wMagnitudeSquared)) {
        double temp = t - sqrt(tSquared - wMagnitudeSquared);
        Vector3 r = (q + bUnit * temp);
        intersect = r * radii;
        return true;
    } else {
        return false;
    }
}

void CloudLayer::WrapClouds(bool geocentric, const Camera* camera, CloudLayerTcsUserData* tcsUserData)
{
    const Vector3& camPos = camera->GetPosition();

    double& layerX = tcsUserData->layerX;
    double& layerZ = tcsUserData->layerZ;

    if (isInfinite || cloudWrap) {
        Vector3 anchor;

        if (!isInfinite) {
            anchor = Vector3(layerX, tcsUserData->geocentricAltitude, layerZ);
        } else {
            anchor = camPos * camera->GetInverseBasis3x3();
            layerX = anchor.x;
            layerZ = anchor.z;
        }

        double halfWidth = baseWidth * 0.5;
        double halfLength = baseLength * 0.5;

        const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

        for (SL_VECTOR(Cloud *)::const_iterator it = clouds.begin(); it != clouds.end(); it++) {
            Cloud *c = *it;
            const Vector3& cloudPos = c->GetWorldPositionRef();

            Vector3 newCloudPos = GetLocalBasis(tcsUserData) * cloudPos;

            // Iterate until it's in bounds.
            bool adj;
            bool changed = false;

            double width, depth, height;
            c->GetSize(width, depth, height);

            double maxX = anchor.x + halfWidth;
            double minX = anchor.x - halfWidth;
            double maxZ = anchor.z + halfLength;
            double minZ = anchor.z - halfLength;

            do {
                adj = false;

                if (newCloudPos.x > maxX) {
                    newCloudPos.x = (anchor.x - halfWidth) + (newCloudPos.x - (anchor.x + halfWidth - width));
                    adj = changed = true;
                }
                if (newCloudPos.x < minX) {
                    newCloudPos.x = (anchor.x + halfWidth) - ((anchor.x - halfWidth) - newCloudPos.x + width);
                    adj = changed = true;
                }
                if (newCloudPos.z > maxZ) {
                    newCloudPos.z = (anchor.z - halfLength) + (newCloudPos.z - (anchor.z + halfLength - depth));
                    adj = changed = true;
                }
                if (newCloudPos.z < minZ) {
                    newCloudPos.z = (anchor.z + halfLength) - ((anchor.z - halfLength) - newCloudPos.z + depth);
                    adj = changed = true;
                }
            } while (adj);

            if (changed) {
                Vector3 layerPos = c->GetLayerPosition();
                layerPos.x = newCloudPos.x;
                layerPos.z = newCloudPos.z;
                c->SetLayerPosition(layerPos);

                newCloudPos = newCloudPos * camera->GetBasis3x3();

                c->SetNeedsGeocentricPlacement(true);

                //if (geocentric) {
                //Vector3 cloudDir = newCloudPos;
                //cloudDir.Normalize();
                //newCloudPos = cloudDir * cloudPos.Length();
                //}

                c->SetWorldPosition(newCloudPos);
            }
        }
    }
}

void CloudLayer::AdjustForCurvature(bool geocentric, const Atmosphere* atmosphere, const Camera* camera, CloudLayerTcsUserData* tcsUserData)
{
    bool curve = true;
    Configuration::GetBoolValue("cumulus-round-earth", curve);

    double earthRadius = 6371000;
    Configuration::GetDoubleValue("earth-radius-meters-polar", earthRadius);
    earthRadius *= Atmosphere::GetUnitScale();

    double radius = earthRadius;
    double alpha = 1.0;

    if (GetCurveTowardGround()) {
        double scale = 1.0;
        Configuration::GetDoubleValue("curve-toward-ground-radius-scale", scale);
        double a = sqrt(GetBaseWidth() * GetBaseWidth() + GetBaseLength() * GetBaseLength()) / 2.0;
        double b = GetBaseAltitude(tcsUserData->tcsData);

        if (b > 0) {
            radius = (a*a + b*b) / (2.0 * b);
            radius *= scale;

            if (b < earthRadius) {
                double altitude = atmosphere->GetConditions().GetLocation(tcsUserData->tcsData).GetAltitude();
                alpha = altitude / b;
                if (alpha > 1.0) alpha = 1.0;
                if (alpha < 0) alpha = 0;

                alpha = 1.0 - alpha;
            }

            curve = true;
        } else {
            curve = false;
        }
    }

    if (curve) {
        const double layerX = tcsUserData->layerX;
        const double layerZ = tcsUserData->layerZ;

        Vector3 cloudLayerPos(layerX, GetBaseAltitude(tcsUserData->tcsData), layerZ);

        Vector3 camPos = camera->GetPosition();
        Vector3 camYup = camPos * camera->GetInverseBasis3x3();
        camYup.y = 0;

        double radiusSq = radius * radius;

        const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

        for (SL_VECTOR(Cloud *)::const_iterator it = clouds.begin(); it != clouds.end(); it++) {
            Cloud *c = *it;
            const Vector3& layerPos = c->GetLayerPosition();

            double distSq = 0;
            if (isInfinite) {
                Vector3 cloudYup = c->GetWorldPositionRef() * camera->GetInverseBasis3x3();
                cloudYup.y = 0;
                distSq = (camYup - cloudYup).SquaredLength();
            } else {
                Vector3 planar(layerPos.x, 0, layerPos.z);
                distSq = planar.SquaredLength();
            }

            if (distSq < radiusSq) {
                double displacement = radius - (sqrt(radiusSq - distSq));

                displacement *= alpha;
                if (!geocentric || GetIsInfinite()) {
                    const Vector3& cloudPos = c->GetWorldPositionRef();
                    Vector3 newCloudPos = cloudPos * camera->GetInverseBasis3x3();
                    newCloudPos.y = tcsUserData->geocentricAltitude + layerPos.y - displacement;
                    newCloudPos = newCloudPos * camera->GetBasis3x3();
                    c->SetWorldPosition(newCloudPos);
                } else {
                    Vector3 localPos = layerPos;
                    localPos.y += tcsUserData->geocentricAltitude - displacement;
                    localPos.x += layerX;
                    localPos.z += layerZ;
                    c->SetWorldPosition(localPos * GetLocalBasis(tcsUserData));
                }
            }
        }
    }

}

void CloudLayer::AdjustGeocentricPositions(bool geocentric, const Camera* camera, CloudLayerTcsUserData* tcsUserData)
{
    ThreadCameraStreamData* tcsData = tcsUserData->tcsData;

    if (!geocentric) {
        tcsUserData->geocentricAltitude = GetBaseAltitude(tcsUserData->tcsData);
        return;
    }

    Vector3 bUnit = camera->GetUpVector();
    bUnit.x /= earthRadii.x;
    bUnit.y /= earthRadii.y;
    bUnit.z /= earthRadii.z;
    bUnit.Normalize();
    Vector3 intersection = earthRadii * bUnit;

    {
        double earthRadius = intersection.Length();
        double baseAlt = GetBaseAltitude(tcsData);
        bool geocentricBaseAltitude = (baseAlt > earthRadii.z || baseAlt > earthRadii.y);
        if (!geocentricBaseAltitude) {
            tcsUserData->geocentricAltitude = earthRadius + baseAlt;
        } else {
            tcsUserData->geocentricAltitude = baseAlt;
            return; // SetBaseAltitude() already updated the cloud heights.
        }

        bool ursMethod = false;
        Configuration::GetBoolValue("alternate-geocentric-adjustment", ursMethod);

        const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

        for (SL_VECTOR(Cloud *)::const_iterator it = clouds.begin(); it != clouds.end(); it++) {
            Cloud *c = *it;
            if (c->GetNeedsGeocentricPlacement()) {
                double radius = earthRadius;

                double baseAlt = GetBaseAltitude(tcsData);
                if (geocentricBaseAltitude) {
                    // Old-school geocentric altitude specified
                    radius = baseAlt;
                } else {
                    radius += baseAlt;
                }

                if (ursMethod) {
                    Vector3 localPos = c->GetWorldPosition(tcsData);
                    //localPos = localPos * camera->GetInverseBasis3x3();
                    localPos = GetLocalBasis(tcsUserData) * localPos;

                    localPos.y = radius;

                    c->SetWorldPosition(localPos * GetLocalBasis(tcsUserData));
                    c->SetNeedsGeocentricPlacement(false);
                } else {
                    Vector3 localPos = c->GetLayerPosition();
                    localPos.y += radius;

                    localPos.x += tcsUserData->layerX;
                    localPos.z += tcsUserData->layerZ;

                    c->SetWorldPosition(localPos * GetLocalBasis(tcsUserData));
                    c->SetNeedsGeocentricPlacement(false);
                }
            }
        }
    }
}

void CloudLayer::SetPrecipitation(int type, double intensity, double nearClip, double farClip, bool useDepthBuffer)
{
    if (intensity < 0) intensity = 0;

    if (type < NONE || type >= NUM_PRECIP_TYPES) type = NONE;

    if (type == NONE) {
        precipitationEffects.clear();
        precipitationParameters.clear();
    } else {
        precipitationEffects[type] = intensity;
        PrecipitationParameters& parameters = precipitationParameters[type];
        parameters.nearClip = nearClip;
        parameters.farClip = farClip;
        parameters.useDepthBuffer = useDepthBuffer;
    }
}

void CloudLayer::ProcessAtmosphericEffects(Sky *s, const Atmosphere* atmosphere, const Camera* camera, ThreadCameraStreamData* tcsData) const
{
    if (!IsRenderable(tcsData)) {
        return;
    }

    // A little hacky, but this one method doesn't really justify the effort of a new class IMO.
    if (GetType() == SANDSTORM) {

        Renderer* renderer = tcsData->GetRenderer();

        CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

        Vector3 camPos = GetLocalBasis(tcsUserData) * camera->GetPosition();

        const double layerX = tcsUserData->layerX;
        const double layerZ = tcsUserData->layerZ;

        if (camPos.x < (layerX - GetBaseWidth() * 0.5)) return;
        if (camPos.x >(layerX + GetBaseWidth() * 0.5)) return;
        if (camPos.z > (layerZ + GetBaseLength() * 0.5)) return;
        if (camPos.z < (layerZ - GetBaseLength() * 0.5)) return;

        double fogAltitudeMultiplier = 1.0f;
        Configuration::GetDoubleValue("sandstorm-fog-altitude-multiplier", fogAltitudeMultiplier);

        double cloudBottom = tcsUserData->geocentricAltitude;
        double cloudTop = cloudBottom + GetMaxHeight()*fogAltitudeMultiplier;

        double r = 0, g = 0, b = 0;
        Color fogColor;
        Configuration::GetDoubleValue("sandstorm-fog-red", r);
        Configuration::GetDoubleValue("sandstorm-fog-blue", b);
        Configuration::GetDoubleValue("sandstorm-fog-green", g);
        double density = 0.001;
        Configuration::GetDoubleValue("sandstorm-fog-density", density);

        double blend = 1.0 - ((cloudTop - camPos.y) / (GetMaxHeight()*fogAltitudeMultiplier));

        if (blend > 1.0) blend = 1.0;
        if (blend < 0) blend = 0;

        BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();

        Color horizonColor = billboardParams->GetFogColor();// s->GetAverageHorizonColor(0);
        Color sun = s->GetSunOrMoonColor();
        sun.ScaleToUnitOrLess(tcsData->GetHDREnabled());
        sun = sun.ToGrayscale();

        bool smoothHorizon = true;
        Configuration::GetBoolValue("sandstorm-smooth-horizon-blend", smoothHorizon);

        if (smoothHorizon) {
            r = (blend * horizonColor.r) + ((1.0 - blend) * r * sun.r);
            g = (blend * horizonColor.g) + ((1.0 - blend) * g * sun.g);
            b = (blend * horizonColor.b) + ((1.0 - blend) * b * sun.b);
        } else {
            //Color the sandstorm fog to match the sandstorm cloud color
            double sr = 0, sg = 0, sb = 0;
            Configuration::GetDoubleValue("sandstorm-dust-red", sr);
            Configuration::GetDoubleValue("sandstorm-dust-blue", sb);
            Configuration::GetDoubleValue("sandstorm-dust-green", sg);
            //Use horizoncolor to darken the fog at night
            horizonColor = horizonColor.ToGrayscale();
            sr *= horizonColor.r;
            sb *= horizonColor.b;
            sg *= horizonColor.g;

            //Allow color of fog to be set externally
            bool lightFog = true;
            Configuration::GetBoolValue("sandstorm-light-fog", lightFog);
            if (lightFog) {
                r = (blend * sr) + ((1.0 - blend) * r * sun.r);
                g = (blend * sg) + ((1.0 - blend) * g * sun.g);
                b = (blend * sb) + ((1.0 - blend) * b * sun.b);
            }
        }

        fogColor = Color(r, g, b, 1.0);

        density *= (1.0 - blend);

        double width = GetBaseWidth();
        double depth = GetBaseLength();
        double f;
        double fade = 1.0;
        double left = layerX - width * 0.5;
        double right = layerX + width * 0.5;
        double bottom = layerZ - depth * 0.5;
        double top = layerZ + depth * 0.5;

        if (camPos.x < left + width * edgeFadeThreshold) {
            f = MathUtils::smoothstep(left, left + width * edgeFadeThreshold, camPos.x);
            if (f < fade) fade = f;
        }

        if (camPos.x > right - width * edgeFadeThreshold) {
            f = 1.0 - MathUtils::smoothstep(right - width * edgeFadeThreshold, right, camPos.x);
            if (f < fade) fade = f;
        }

        if (camPos.z < bottom + depth * edgeFadeThreshold) {
            f = MathUtils::smoothstep(bottom, bottom + depth * edgeFadeThreshold, camPos.z);
            if (f < fade) fade = f;
        }

        if (camPos.z > top - depth * edgeFadeThreshold) {
            f = 1.0 - MathUtils::smoothstep(top - depth * edgeFadeThreshold, top, camPos.z);
            if (f < fade) fade = f;
        }

        density *= fade;

        double fogDepth = 0;
        // Set sky fog effects
        if (camPos.y <= cloudTop && camPos.y >= cloudBottom) {
            fogDepth = cloudTop - camPos.y;
            s->SetFogVolumeDistance(fogDepth);

            renderer->EnableFog(true);
            renderer->ConfigureFog(density, 1, 100000, fogColor);

            billboardParams->SetFogColor(fogColor);
            billboardParams->SetFogDensity(density);
        }
    }
}

void CloudLayer::ApplyPrecipitation(const Camera* camera, ThreadCameraStreamData* tcsData)
{
    if (IsRenderable(tcsData)) {
        double edgeFade = GetEdgeFadeFactor(camera, 1, tcsData);

        PrecipitationManager* pm = tcsData->GetPrecipitationManager();
        const double fade = GetFade(tcsData);

        SL_MAP(int, double) ::iterator it;
        for (it = precipitationEffects.begin(); it != precipitationEffects.end(); it++) {
            int type = (*it).first;
            double intensity = (*it).second;
            pm->SetIntensity(type, intensity * edgeFade * fade);

            SL_MAP(int, PrecipitationParameters)::const_iterator paramIter = precipitationParameters.find(type);
            if (paramIter!= precipitationParameters.end()) {
                const PrecipitationParameters& parameters = paramIter->second;
                pm->SetClipPlanes(type, parameters.nearClip, parameters.farClip, parameters.useDepthBuffer);
            }
        }
    }
}

double CloudLayer::GetEdgeFadeFactor(const Camera* camera, double cloudScale, ThreadCameraStreamData* tcsData) const
{
    if (edgeFadeThreshold <= 0) return 1.0;

    CloudTypes type = GetType();
    bool isPlanar = type == STRATUS || type == CIRRUS_FIBRATUS || type == CIRROCUMULUS || type == STRATOCUMULUS || type == STRATOCUMULUS_PARTICLES;

    if (isPlanar && isInfinite) return 1.0;

    Vector3 camPos = camera->GetPosition() * camera->GetInverseBasis3x3();

    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    if (camPos.y > tcsUserData->geocentricAltitude + GetMaxHeight()) return 0;

    const double layerX = tcsUserData->layerX;
    const double layerZ = tcsUserData->layerZ;

    if (camPos.x < (layerX - GetBaseWidth() * 0.5 * cloudScale)) return 0;
    if (camPos.x >(layerX + GetBaseWidth() * 0.5 * cloudScale)) return 0;
    if (camPos.z > (layerZ + GetBaseLength() * 0.5 * cloudScale)) return 0;
    if (camPos.z < (layerZ - GetBaseLength() * 0.5 * cloudScale)) return 0;


    double fade = 1.0;


    const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

    for (SL_VECTOR(Cloud *)::const_iterator it = clouds.begin(); it != clouds.end(); it++) {
        Cloud *cloud = *it;
        double width, depth, height;
        Vector3 pos = cloud->GetWorldPosition(tcsData);
        pos = pos * camera->GetInverseBasis3x3();
        cloud->GetSize(width, depth, height);

        width *= cloudScale;
        depth *= cloudScale;
        height *= cloudScale;

        double left = pos.x - width * 0.5;
        double right = pos.x + width * 0.5;
        double bottom = pos.z - depth * 0.5;
        double top = pos.z + depth * 0.5;

        if (camPos.x < left) continue;
        if (camPos.x > right) continue;
        if (camPos.z < bottom) continue;
        if (camPos.z > top) continue;

        double f;

        if (camPos.x < left + width * edgeFadeThreshold) {
            f = MathUtils::smoothstep(left, left + width * edgeFadeThreshold, camPos.x);
            if (f < fade) fade = f;
        }

        if (camPos.x > right - width * edgeFadeThreshold) {
            f = MathUtils::smoothstep(right - width * edgeFadeThreshold, right, camPos.x);
            if (f < fade) fade = f;
        }

        if (camPos.z < bottom + depth * edgeFadeThreshold) {
            f = MathUtils::smoothstep(bottom, bottom + depth * edgeFadeThreshold, camPos.z);
            if (f < fade) fade = f;
        }

        if (camPos.z > top - depth * edgeFadeThreshold) {
            f = MathUtils::smoothstep(top - depth * edgeFadeThreshold, top, camPos.z);
            if (f < fade) fade = f;
        }

        break;
    }

    if (fade < 0) fade = 0;
    if (fade > 1.0) fade = 1.0;

    return fade;

}

bool CloudLayer::HasPrecipitationAtPosition(double x, double y, double z, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();;

    // Bail if we have no precip at all.
    if (precipitationEffects.size() == 0 || !IsRenderable(tcsData)) return false;

    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    const double layerX = tcsUserData->layerX;
    const double layerZ = tcsUserData->layerZ;

    // Don't assume which way is up.
    const Camera* camera = tcsData->GetCamera();
    Vector3 camPosLocal(x, y, z);
    Vector3 camPos = GetLocalBasis(tcsUserData) * camPosLocal;

    // Bail if you're not under the entire layer at all.
    if (camPos.y > tcsUserData->geocentricAltitude + GetThickness()) return false;
    if (camPos.x < (layerX - GetBaseWidth() * 0.5)) return false;
    if (camPos.x >(layerX + GetBaseWidth() * 0.5)) return false;
    if (camPos.z > (layerZ + GetBaseLength() * 0.5)) return false;
    if (camPos.z < (layerZ - GetBaseLength() * 0.5)) return false;

    const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

    // Now check individual clouds.
    bool hit = false;
    for (SL_VECTOR(Cloud *)::const_iterator it = clouds.begin(); it != clouds.end(); it++) {
        const Cloud *cloud = *it;
        double width, depth, height;
        Vector3 pos = cloud->GetWorldPosition(tcsData);
        pos = pos * camera->GetInverseBasis3x3();
        cloud->GetSize(width, depth, height);

        if (camPos.x < (pos.x - width * 0.5)) continue;
        if (camPos.x >(pos.x + width * 0.5)) continue;
        if (camPos.z < (pos.z - depth * 0.5)) continue;
        if (camPos.z >(pos.z + depth * 0.5)) continue;

        hit = true;
        break;
    }

    return hit;
}

bool CloudLayer::HasPrecipitationAtPosition(const Camera* camera, void* data) const
{
    const Vector3& pos = camera->GetPosition();

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();;
    SL_ASSERT(tcsData);

    return HasPrecipitationAtPosition(pos.x, pos.y, pos.z, tcsData);
}

bool CloudLayer::IsInsideAnyCloud(const Vector3& position, const ThreadCameraStreamData* tcsData, const CloudLayerTcsUserData* tcsUserData, double* distanceInside = 0) const
{
    if (tcsData==0 || tcsUserData == 0) {
        return false;
    }

    const Camera* camera = tcsData->GetCamera();

    // Now check individual clouds.
    const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;
    bool hit = false;
    for (SL_VECTOR(Cloud *)::const_iterator it = clouds.begin(); it != clouds.end(); it++) {
        const Cloud *cloud = *it;
        double width, depth, height;
        Vector3 pos = cloud->GetWorldPosition(tcsData);
        pos = pos * camera->GetInverseBasis3x3();
        cloud->GetSize(width, depth, height);

        if (position.x < (pos.x - width * 0.5)) continue;
        if (position.x >(pos.x + width * 0.5)) continue;
        if (position.z < (pos.z - depth * 0.5)) continue;
        if (position.z >(pos.z + depth * 0.5)) continue;
        if (position.y < (pos.y - height * 0.5)) continue;
        if (position.y > (pos.y + height * 0.5)) continue;

        if (distanceInside) {
            double minDist;
            double x1 = position.x - (pos.x - width * 0.5);
            minDist = x1;
            double x2 = (pos.x + width * 0.5) - position.x;
            if (x2 < minDist) minDist = x2;
            double y1 = position.y - (pos.y - height * 0.5);
            if (y1 < minDist) minDist = y1;
            double y2 = (pos.y + height * 0.5) - position.y;
            if (y2 < minDist) minDist = y2;
            double z1 = position.z - (pos.z - depth * 0.5);
            if (z1 < minDist) minDist = z1;
            double z2 =  (pos.z + depth * 0.5) - position.z;
            if (z2 < minDist) minDist = z2;

            *distanceInside = minDist;

        }

        hit = true;
        break;
    }

    return hit;
}

bool CloudLayer::IsInsideCloud(double x, double y, double z, void* data, double* distanceInside) const
{
    if (distanceInside) {
        *distanceInside = 0;
    }

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();;
    SL_ASSERT(tcsData);
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    const bool renderingEnabled = tcsUserData->renderingEnabled;

    if (!renderingEnabled) return false;

    const double layerX = tcsUserData->layerX;
    const double layerZ = tcsUserData->layerZ;

    // Don't assume which way is up.
    Vector3 camPosLocal(x, y, z);
    Vector3 camPos = GetLocalBasis(tcsUserData) * camPosLocal;
    // Bail if you're not in the entire layer at all.
    if (camPos.y < tcsUserData->geocentricAltitude) return false;
    if (camPos.x < (layerX - GetBaseWidth() * 0.5)) return false;
    if (camPos.x >(layerX + GetBaseWidth() * 0.5)) return false;
    if (camPos.z >(layerZ + GetBaseLength() * 0.5)) return false;
    if (camPos.z < (layerZ - GetBaseLength() * 0.5)) return false;

    return IsInsideAnyCloud(camPos, tcsData, tcsUserData, distanceInside);

}

bool CloudLayer::IsInsideCloud(const Camera* camera, void* data, double *distanceInside) const
{
    const Vector3& pos = camera->GetPosition();

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();;

    return IsInsideCloud(pos.x, pos.y, pos.z, tcsData, distanceInside);
}

void CloudLayer::GetBounds(Vector3 bounds[8], const Camera* camera, ThreadCameraStreamData* tcsData) const
{
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    const double layerX = tcsUserData->layerX;
    const double layerZ = tcsUserData->layerZ;

    Vector3 minimum, maximum;

    double x, z;
    if (GetIsInfinite()) {
        Vector3 camPosYUp = camera->GetPosition() * camera->GetInverseBasis3x3();
        x = camPosYUp.x;
        z = camPosYUp.z;
    } else {
        x = layerX;
        z = layerZ;
    }

    /** Add some slop to account for wrapping */
    double halfWidth = (GetBaseWidth() + GetMaxSize()) * 0.5;
    double halfLength = (GetBaseLength() + GetMaxSize()) * 0.5;

    minimum.x = x - halfWidth;
    minimum.y = tcsUserData->geocentricAltitude;
    minimum.z = z - halfLength;
    maximum.x = x + halfWidth;
    maximum.y = tcsUserData->geocentricAltitude + GetMaxHeight();
    maximum.z = z + halfLength;

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

bool CloudLayer::IsCulled(const Camera* camera, const Camera* cullCamera, ThreadCameraStreamData* tcsData) const
{
    if (GetIsInfinite()) return false;

    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);
    const bool renderingEnabled = tcsUserData->renderingEnabled;

    if (!renderingEnabled) return true;

    Frustum f = cullCamera->GetFrustumWorldSpace();
    if (atmosphere.GetFarCullingDisabled()) {
        f.EnableFarClipCulling(false);
    }

    Vector3 bounds[8];
    GetBounds(bounds, camera, tcsData);

    for (int i = 0; i < f.GetNumCullingPlanes(); i++) {
        const Plane& p = f.GetPlane(i);
        double dist = p.GetDistance();
        const Vector3& N = p.GetNormal();

        if (N.Dot(bounds[0]) + dist > 0) continue;
        if (N.Dot(bounds[1]) + dist > 0) continue;
        if (N.Dot(bounds[2]) + dist > 0) continue;
        if (N.Dot(bounds[3]) + dist > 0) continue;
        if (N.Dot(bounds[4]) + dist > 0) continue;
        if (N.Dot(bounds[5]) + dist > 0) continue;
        if (N.Dot(bounds[6]) + dist > 0) continue;
        if (N.Dot(bounds[7]) + dist > 0) continue;
        return true;
    }
    return false;
}

const Atmosphere& CloudLayer::GetAtmosphere() const
{
    return atmosphere;
}

bool CloudLayer::GetCulled(const Atmosphere* atmosphere, const Camera* camera, const Camera* cullCamera, ThreadCameraStreamData* tcsData) const
{
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);
    return tcsUserData->cloudLayerCullSet->GetCulled();
}

void CloudLayer::DoCulling(const Camera* camera, const Camera* cullCamera, ThreadCameraStreamData* tcsData) const
{
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);
    tcsUserData->cloudLayerCullSet->DoCulling(camera, cullCamera, tcsData);
}

void CloudLayer::RoundEdgesAndApplyDensity(const Camera* camera, const Atmosphere *atm, CloudLayerTcsUserData* tcsUserData)
{
    // Handle curvature & coverage
    Vector3 center;
    double radius = baseWidth;

    double& coverageMultiplier = tcsUserData->coverageMultiplier;
    bool& densityDirty = tcsUserData->densityDirty;

    CloudLayer::CurveModes& curveMode = tcsUserData->curveMode;
    bool& curveModeDirty = tcsUserData->curveModeDirty;

    if (!curveModeDirty && !densityDirty && !GetIsInfinite() && !GetCloudWrapping()) return;

    if (curveMode == SQUARE && (!usingRuntimeDensity || !densityDirty)) return;

    curveModeDirty = false;
    densityDirty = false;

    switch (curveMode) {

    case ALL:
        center = Vector3(0, 0, 0);
        radius = baseWidth * 0.5;
        break;

    case NORTH:
        center = Vector3(0, 0, baseWidth * 0.5);
        radius = baseWidth * 0.5;
        break;

    case SOUTH:
        center = Vector3(0, 0, -baseWidth * 0.5);
        radius = baseWidth * 0.5;
        break;

    case EAST:
        center = Vector3(-baseLength * 0.5, 0, 0);
        radius = baseLength * 0.5;
        break;

    case WEST:
        center = Vector3(baseLength * 0.5, 0, 0);
        radius = baseLength * 0.5;
        break;

    case NORTHWEST:
        center = Vector3(baseLength * 0.5, 0, baseWidth * 0.5);
        radius = baseWidth;
        break;

    case NORTHEAST:
        center = Vector3(-baseLength * 0.5, 0, baseWidth * 0.5);
        radius = baseWidth;
        break;

    case SOUTHEAST:
        center = Vector3(-baseLength * 0.5, 0, -baseWidth * 0.5);
        radius = baseWidth;
        break;

    case SOUTHWEST:
        center = Vector3(baseLength * 0.5, 0, -baseWidth * 0.5);
        radius = baseWidth;
        break;
    }

    double r2 = radius * radius;

    const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

    int nClouds = (int)(clouds.size());
    for (int cloudNum = 0; cloudNum < nClouds; cloudNum++) {
        Cloud *c = clouds[cloudNum];

        bool kill = false;

        double val = c->GetCoverageThreshold();

        if (curveMode != SQUARE) {
            Vector3 pos = c->GetLayerPosition();
            double dx = pos.x - center.x;
            double dz = pos.z - center.z;
            double dist2 = dx*dx + dz*dz;

            // 10% chance of it being there no matter what
            if (dist2 > r2 && val > 0.1) {
                kill = true;
            }
        }

        if (usingRuntimeDensity) {
            if (coverageMultiplier < val) {
                kill = true;
            }
        }

        const MillisecondTimer *timer = atm->GetConditions().GetMillisecondTimer();
        if (kill) {
            c->FadeOut(timer);
        } else {
            c->FadeIn(timer);
        }
    }
}

void CloudLayer::DoUpdates(bool geocentricMode, const Atmosphere* atmosphere, unsigned long now, const Camera* camera, ThreadCameraStreamData* tcsData)
{
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    const bool renderingEnabled = tcsUserData->renderingEnabled;
    if (!renderingEnabled) return;

    AdjustGeocentricPositions(geocentricMode, camera, tcsUserData);

    WrapClouds(geocentricMode, camera, tcsUserData);

    AdjustForCurvature(geocentricMode, atmosphere, camera, tcsUserData);

    RoundEdgesAndApplyDensity(camera, atmosphere, tcsUserData);

    if (growth) {
        int automataUpdates = 0;
        int automataUpdateBudget = 10;
        Configuration::GetIntValue("max-cellular-automata-updates-per-frame", automataUpdateBudget);

        const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;
        int nClouds = (int)(clouds.size());

        if (tcsUserData->lastCloudUpdated >= nClouds) {
            tcsUserData->lastCloudUpdated = 0;
        }

        int cloudsUpdated = 0;
        while (cloudsUpdated < nClouds) {
            Cloud *cloud = clouds[tcsUserData->lastCloudUpdated++];
            if (cloud->Update(now, false, atmosphere->GetRandomNumberGenerator(), tcsData->GetBillboardBufferProvider(), tcsData)) {
                automataUpdates++;
            }
            if (automataUpdates >= automataUpdateBudget) {
                break;
            }
            if (tcsUserData->lastCloudUpdated >= nClouds) {
                tcsUserData->lastCloudUpdated = 0;
            }
            cloudsUpdated++;
        }
    }
}

bool CloudLayer::Draw(int pass, const Vector3 *lightDir,
                      const Color *lightColor, bool invalid, bool wantsUpdate,
                      unsigned long now, const Sky* sky,
                      bool drawLightning, const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
                      , ThreadCameraStreamData* tcsData) const
{
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    CloudLayerCullSet* cloudLayerCullSet = tcsUserData->cloudLayerCullSet;

    if (cloudLayerCullSet->GetCulled()) {
        if (pass == 1) cloudLayerCullSet->ClearCulled();
        return false;
    }

    const bool renderingEnabled = tcsUserData->renderingEnabled;
    if (!renderingEnabled) return true;

    Color finalLightColor = *lightColor;

    if (hasForcedCloudColor) {
        Color originalLight = Color(lightColor->r, lightColor->g, lightColor->b);
        Color forcedColor = Color(forcedCloudColor.x, forcedCloudColor.y, forcedCloudColor.z);
        if (forcedCloudColorLighting) {
            finalLightColor = originalLight * forcedColor;
        } else {
            finalLightColor = forcedColor;
        }
    }

    bool& forceLightingUpdate = tcsUserData->forceLightingUpdate;
    SL_VECTOR(Cloud *)& drawList = tcsUserData->drawList;
    double& lastGamma = tcsUserData->lastGamma;
    bool& firstFrame = tcsUserData->firstFrame;
    bool& firstFrameDrawn = tcsUserData->firstFrameDrawn;

    if (forceLightingUpdate) {
        wantsUpdate = true;
        forceLightingUpdate = false;
    }

    SL_ASSERT(lightDir);
    tcsData->GetMetaballRenderingParams()->SetLightPosition(*lightDir);

    const Camera* camera = tcsData->GetCamera();

    const double layerX = tcsUserData->layerX;
    const double layerZ = tcsUserData->layerZ;

    Vector3 topCenter = Vector3(layerX, tcsUserData->geocentricAltitude + thickness, layerZ) * sceneCamera->GetBasis3x3();
    double deckRadius = sqrt(GetBaseWidth() * GetBaseWidth() + GetBaseLength() * GetBaseLength());
    Vector3 lightPos = (*lightDir) * deckRadius;
    lightPos = lightPos + topCenter;

    if (drawList.size() == 0)
        drawList.reserve(1024);
    drawList.clear();

    if (sky->GetGamma() != lastGamma) {
        lastGamma = sky->GetGamma();
        invalid = true;
    }

    bool needsFirstPass = invalid;

    cloudLayerCullSet->ResizeAndInitCloudsCulledIfNecessary(tcsData);

    SL_VECTOR(bool)& cloudsCulled = cloudLayerCullSet->GetCloudsCulledVector();
    int i = 0;
    const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;
    for (SL_VECTOR(Cloud*)::const_iterator it = clouds.begin(); it != clouds.end(); ++it, ++i) {
        Cloud *cloud = *it;

        cloud->Visit(pass, tcsData);

        if (pass == 0 || !cloudsCulled[i] || firstFrame) {
            drawList.push_back(cloud);
            if (invalid) {
                cloud->Invalidate(tcsData);
                cloud->SetNeedsRelighting(true);
            } else {
                if (wantsUpdate) {
                    if (cloud->LightingChanged(*lightDir, tcsData) || (cloud->IsInvalid(tcsData) && !cloudsCulled[i])) {
                        cloud->SetNeedsRelighting(true);
                        needsFirstPass = true;
                    }
                }
            }
        }

        if (pass == 1) cloudsCulled[i] = false;
    }

    firstFrame = false;

    if (pass == 0 && !needsFirstPass) {
        return false;
    }

    int cloudsDrawn = 0;

    if (DrawSetup(pass, &lightPos, &finalLightColor, tcsData)) {
        Vector3 anchor;
        if (!isInfinite) {
            anchor = Vector3(layerX, 0, layerZ);
        } else {
            anchor = sceneCamera->GetPosition() * camera->GetInverseBasis3x3();
        }

        int skipped = 0;
        SL_VECTOR(Cloud*) ::iterator it;
        for (it = drawList.begin(); it != drawList.end(); it++) {
            Cloud *cloud = *it;
            double wrapFade = GetWrapFade(cloud, anchor, fadeOverride, tcsUserData);
            cloud->SetFade(GetFade(tcsData));
            cloud->SetAlpha(alpha * wrapFade);
            if ((pass > 0) || (cloudsDrawn < cloudLightingBudget) || !firstFrameDrawn) {
                if (pass == 0 && !cloud->GetNeedsRelighting()) continue;

                if (cloud->Draw(pass, lightPos, *lightDir, finalLightColor, invalid, sky, drawLightning, sceneCamera, tcsData)) {
                    if (pass == 0) {
                        cloud->UpdateLightPos(*lightDir);
                        cloud->SetNeedsRelighting(false);
                    }
                    cloudsDrawn++;
                }
            }
        }

        EndDraw(pass);
    }

    firstFrameDrawn = true;
    return true;
}

void CloudLayer::GetSortPosition(double& x, double& y, double& z, const Camera* camera, const ThreadCameraStreamData* tcsData) const
{
    const Renderer* renderer = tcsData->GetRenderer();

    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(const_cast<ThreadCameraStreamData*>(tcsData));
    const double layerX = tcsUserData->layerX;
    const double layerZ = tcsUserData->layerZ;

    Vector3 camPosYUp = camera->GetPosition() *
                        camera->GetInverseBasis3x3();

    if (isInfinite) {

        Vector3 pos = camPosYUp;

        double bottom = tcsUserData->geocentricAltitude;
        double top = tcsUserData->geocentricAltitude + GetMaxHeight();

        double dBottom = MathUtils::Abs(camPosYUp.y - bottom);
        double dTop = MathUtils::Abs(camPosYUp.y - top);

        if (dBottom < dTop) {
            pos.y = bottom;
        } else {
            pos.y = top;
        }

        pos = pos * camera->GetBasis3x3();

        x = pos.x;
        y = pos.y;
        z = pos.z;
    } else {
        Vector3 pos(layerX, tcsUserData->geocentricAltitude, layerZ);

        double bottom = tcsUserData->geocentricAltitude;
        double top = tcsUserData->geocentricAltitude + GetMaxHeight();

        double dBottom = MathUtils::Abs(camPosYUp.y - bottom);
        double dTop = MathUtils::Abs(camPosYUp.y - top);

        if (dBottom < dTop) {
            pos.y = bottom;
        } else {
            pos.y = top;
        }

        pos = pos * camera->GetBasis3x3();
        x = pos.x;
        y = pos.y;
        z = pos.z;
    }
    /*
    if (isInfinite)
    {
    Vector3 pos = atmosphere->GetCamPos() *
    camera->GetInverseBasis3x3();

    pos.y = baseAltitude + GetMaxHeight() * 0.5;

    pos = pos * camera->GetBasis3x3();

    x = pos.x;
    y = pos.y;
    z = pos.z;
    }
    else
    {
    Vector3 pos(layerX, baseAltitude + GetThickness() * 0.5, layerZ);
    pos = pos * camera->GetBasis3x3();
    x = pos.x;
    y = pos.y;
    z = pos.z;
    }
    */
}

void CloudLayer::AddLightningListener(LightningListener *listener)
{
    if (listener)
        lightningListeners.push_back(listener);
}

void CloudLayer::ClearLightningListeners()
{
    lightningListeners.clear();
}

void CloudLayer::LightningNotify(const Vector3& pos)
{
    SL_VECTOR(LightningListener*) ::iterator it;
    for (it = lightningListeners.begin(); it != lightningListeners.end(); it++) {
        (*it)->LightningStartedEvent(pos.x, pos.y, pos.z);
    }
}

bool CloudLayer::Save(const char *filePath, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();

    std::fstream f(filePath, std::fstream::out | std::fstream::binary | std::fstream::trunc);
    if (f.fail()) return false;
    return Serialize(f, tcsData);
}

bool CloudLayer::Restore(const Atmosphere& atm, const char *filePath, void* data)
{
    SL_ASSERT(&atmosphere == &atm);
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();

    std::fstream f(filePath, std::fstream::in | std::fstream::binary);
    if (f.fail()) return false;
    return Unserialize(atm, f, tcsData);
}

bool CloudLayer::Serialize(std::ostream& s, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();;
    CloudLayerSerializationUtils::Serialize(s, *this, tcsData);
    this->SaveClouds(s, tcsData);
    return true;
}

bool CloudLayer::Unserialize(const Atmosphere& atm, std::istream& s, void* data)
{
    SL_ASSERT(&atmosphere == &atm);
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();

    bool ok = CloudLayerSerializationUtils::UnSerialize(s, *this, tcsData);
    if (!ok) {
        return false;
    }

    const Camera* camera = tcsData->GetCamera();

    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    tcsUserData->localUp = camera->GetUpVector();
    tcsUserData->localBasis = camera->GetBasis3x3();
    tcsUserData->localBasis4 = camera->GetBasis4x4();

    this->RestoreClouds(atm, s, tcsData);

    return true;
}

bool CloudLayer::SaveClouds(std::ostream& s, ThreadCameraStreamData* tcsData) const
{
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);
    const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

    int numClouds = (int)clouds.size();
    s.write((char *)&numClouds, sizeof(int));
    for (int i = 0; i < numClouds; i++) {
        clouds[i]->Serialize(s, tcsData);
    }

    return true;
}



bool CloudLayer::ExportToVRML(const char *dirPath, void* data)
{
    if (!dirPath) return false;

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);
    const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

    int numClouds = (int)clouds.size();

    char buf[1024];
    for (int i = 0; i < numClouds; i++) {
#if (defined(WIN32) || defined(WIN64)) && (_MSC_VER > 1310)
        sprintf_s(buf, 1024, "%s/%03d.wrl", dirPath, i);
#else
        sprintf(buf, "%s/%03d.wrl", dirPath, i);
#endif
        clouds[i]->ExportToVRML(buf);
    }

    return true;
}

double CloudLayer::GetSkyCoverage(const Camera* camera, ThreadCameraStreamData* tcsData) const
{
    double coverage = 0;
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);
    CloudLayerCullSet* cloudLayerCullSet = tcsUserData->cloudLayerCullSet;
    if (!cloudLayerCullSet->GetCulled()) {

        Vector3 camPosYUp = camera->GetPosition() * camera->GetInverseBasis3x3();

        Vector3 pos = camPosYUp;

        double bottom = tcsUserData->geocentricAltitude;
        double top = tcsUserData->geocentricAltitude + GetMaxHeight();

        if (camPosYUp.y < bottom) {
            coverage = density;
        } else if (camPosYUp.y < top) {
            double factor = (top - camPosYUp.y) / (top - bottom);
            coverage = density * factor;
        }
    }

    return coverage;
}

bool CloudLayer::Intersect(const Vector3& Origin, const Vector3& Direction, double& range, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();;

    const Camera* camera = tcsData->GetCamera();

    Vector3 camOrigin = Origin * camera->GetInverseBasis3x3();
    Vector3 camDirection = Direction * camera->GetInverseBasis3x3();

    SL_VECTOR(double) hit_ranges;

    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);
    const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;
    for (SL_VECTOR(Cloud*)::const_iterator it = clouds.begin(); it != clouds.end(); it++) {
        Cloud *cloud = *it;

        //
        // The intersection algorithm employed in the following section
        // is based on the slab method.  The AABB for each cloud is computed
        // using the cloud's position and size.  The candidate ray is
        // then tested for intersection between each pair of parallel
        // planes making up the AABB.  If any portion of the ray intersects
        // any slab then the test passes; otherwise, it fails.
        //
        // The 3D cloud layer used here is assumed to be infinite in SilverLining
        // terms and thus follows the camera.  It has no DCS per se and similarly
        // each cloud comprising this layer does not have a DCS.  However, because
        // SilverLining will place its clouds according to the curvature of the
        // earth, there may be some error introduced at the edge of large 3D
        // layers due to the use of AABB's.
        //

        // Get the size of the cloud
        Vector3 cloudSize;
        cloud->GetSize(cloudSize.x, cloudSize.z, cloudSize.y);

        // Compute corners of the cloud box
        Vector3 cloudPos = cloud->GetWorldPosition(tcsData);
        cloudPos = cloudPos * camera->GetInverseBasis3x3();

        Vector3 cloudMinPos = cloudPos - (cloudSize * 0.5);
        Vector3 cloudMaxPos = cloudPos + (cloudSize * 0.5);

        // Calculate intersection parameters for each slab in 3 dimensions
        // If the ray does not intersect any slab then the signs of t_min & t_max
        // will flip causing the test to fail.

        double t_min = -DBL_MAX;
        double t_max = DBL_MAX;

        // Test the slab perpendicular to x axis
        if (camDirection.x != 0.0) {
            double x_inv = 1.0 / camDirection.x;

            double tx_1 = (cloudMinPos.x - camOrigin.x) * x_inv;
            double tx_2 = (cloudMaxPos.x - camOrigin.x) * x_inv;

            t_min = SLMIN(tx_1, tx_2);
            t_max = SLMAX(tx_1, tx_2);
        }

        // Test the slab perpendicular to y axis
        if (camDirection.y != 0.0) {
            double y_inv = 1.0 / camDirection.y;

            double ty_1 = (cloudMinPos.y - camOrigin.y) * y_inv;
            double ty_2 = (cloudMaxPos.y - camOrigin.y) * y_inv;

            t_min = SLMAX(t_min, SLMIN(ty_1, ty_2));
            t_max = SLMIN(t_max, SLMAX(ty_1, ty_2));
        }

        // Test the slab perpendicular to z axis
        if (camDirection.z != 0.0) {
            double z_inv = 1.0 / camDirection.z;

            double tz_1 = (cloudMinPos.z - camOrigin.z) * z_inv;
            double tz_2 = (cloudMaxPos.z - camOrigin.z) * z_inv;

            t_min = SLMAX(t_min, SLMIN(tz_1, tz_2));
            t_max = SLMIN(t_max, SLMAX(tz_1, tz_2));
        }

        // If coarse test passes move on to fine-grained test
        if (t_max > t_min) {
            cloud->Intersect(Origin, Direction, hit_ranges);
        }
    }

    // Sort results and find closest intersection
    double rMin = DBL_MAX;
    SL_VECTOR(double)::const_iterator i;
    for (i = hit_ranges.begin(); i != hit_ranges.end(); ++i) {
        double r = *i;

        // Ignore hits which are behind the ray origin
        if (r < rMin && r > 0.0)
            rMin = r;
    }

    if (hit_ranges.size() > 0 && rMin < DBL_MAX) {
        range = rMin;
        return true;
    }

    return false;
}

void CloudLayer::SetName(const char* _name)
{
    name = SL_STRING(_name);
}

const char* CloudLayer::GetName(void) const
{
    return name.c_str();
}

double CloudLayer::GetBaseAltitudeGeocentric(ThreadCameraStreamData* tcsData) const
{
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);
    return tcsUserData->geocentricAltitude;
}

}