// Copyright (c) 2016-2023 Sundog Software LLC. All rights reserved worldwide.

#include "StratocumulusParticleCloudLayer.h"
#include "StratocumulusParticleCloudQuad.h"
#include "StratocumulusPuff.h"
#include "Utils.h"
#include "Sky.h"
#include "MathUtils.h"
#include "BillboardBuffer.h"
#include "Camera.h"
#include "CloudLayerCullSet.h"
#include "SLAssert.h"
#include "OverriddenBillboardMatrix.h"
#include "BillboardRenderingParameters.h"
#include "BillboardShader.h"
#include "MetaballRenderingParameters.h"
#include "ShadowMap.h"
#include "ThreadCameraStreamData.h"
#include "CloudLayerTcsUserData.h"
#include <algorithm>

namespace SilverLining
{

int StratocumulusParticleCloudLayer::recompileFrameCount;


StratocumulusParticleCloudLayer::StratocumulusParticleCloudLayer(const Atmosphere& atmosphere)
    : CloudLayer(atmosphere)
    , voxelsWidth(0), voxelsDepth(0), wrapDirty(false)
    , Renderable(true)

{
    double lightChangeAngle = 1.0;
    Configuration::GetDoubleValue("cloud-light-change-angle", lightChangeAngle);
    cosLightThreshold = cos(MathUtils::ToRadians(lightChangeAngle));

    voxelSize = 1000.0f;
    Configuration::GetFloatValue("stratocumulus-particle-voxel-size", voxelSize);
    voxelSize *= (float)Atmosphere::GetUnitScale();

    gradient = 0.3f;
    Configuration::GetFloatValue("stratocumulus-particle-puff-gradient", gradient);

    thicknessVariance = 0.2;
    Configuration::GetDoubleValue("stratocumulus-particle-thickness-variance", thicknessVariance);

    ambientBlend = 0.5f;
    Configuration::GetFloatValue("stratocumulus-particle-ambient-blend", ambientBlend);


    diffuseScale = 0.5f;
    Configuration::GetFloatValue("stratocumulus-diffuse-scale", diffuseScale);

    flattening = 0.6f;
    Configuration::GetFloatValue("stratocumulus-voxel-flattening", flattening);

    heightVariation = 1500.0;
    Configuration::GetDoubleValue("stratocumulus-particle-base-height-variation", heightVariation);
    heightVariation *= Atmosphere::GetUnitScale();

    perCloudBillboards = false;
    Configuration::GetBoolValue("cumulus-billboards-per-cloud", perCloudBillboards);
}


class StratocumulusParticleCloudLayerTcsUserData : public TcsUserData
{
public:
    StratocumulusParticleCloudLayerTcsUserData();
    virtual ~StratocumulusParticleCloudLayerTcsUserData();
public:
    bool needsLighting;
    Color finalLightColor;

    Vector3 lastLightPos;

    SL_VECTOR(StratocumulusParticleCloudQuad *) quads;
};

StratocumulusParticleCloudLayerTcsUserData::StratocumulusParticleCloudLayerTcsUserData()
    : needsLighting(false)
{
    // nothing else to do
}

StratocumulusParticleCloudLayerTcsUserData::~StratocumulusParticleCloudLayerTcsUserData()
{
    for (SL_VECTOR(StratocumulusParticleCloudQuad*)::const_iterator it = quads.begin(); it != quads.end(); it++) {
        SL_DELETE *it;
    }
    quads.clear();
}

StratocumulusParticleCloudLayerTcsUserData* StratocumulusParticleCloudLayer::GetOrCreateSpcTcsUserData(ThreadCameraStreamData* data) const
{
    if (data == 0) {
        return 0;
    }
    TcsUserData* userData = data->GetUserData(this);
    if (userData == 0) {
        userData = SL_NEW StratocumulusParticleCloudLayerTcsUserData();
        data->SetUserData(this, userData);
    }
    return (StratocumulusParticleCloudLayerTcsUserData*)userData;
}

StratocumulusParticleCloudLayer::~StratocumulusParticleCloudLayer()
{

}

void StratocumulusParticleCloudLayer::ClearClouds(void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();

    SL_VECTOR(StratocumulusParticleCloudQuad*)& quads = GetOrCreateSpcTcsUserData(tcsData)->quads;

    for (SL_VECTOR(StratocumulusParticleCloudQuad*)::const_iterator it = quads.begin(); it != quads.end(); it++) {
        SL_DELETE *it;
    }
    quads.clear();

    this->RemoveListenener(tcsData->GetRenderer()->GetListener());


    CloudLayer::ClearClouds(tcsData);
}


void StratocumulusParticleCloudLayer::DoCulling(const Camera* camera, const Camera* cullCamera, ThreadCameraStreamData* tcsData) const
{
    const SL_VECTOR(StratocumulusParticleCloudQuad*)& quads = GetOrCreateSpcTcsUserData(tcsData)->quads;
    for (SL_VECTOR(StratocumulusParticleCloudQuad*)::const_iterator it = quads.begin(); it != quads.end(); it++) {
        (*it)->Cull(camera, cullCamera);
    }
}

void StratocumulusParticleCloudLayer::MakeQuad(int startx, int starty, int quadSize, TGALoader& tga, const Atmosphere& atm, ThreadCameraStreamData* tcsData)
{
    unsigned char *noise = tga.GetPixels();
    int noiseWidth = tga.GetWidth();
    int noiseHeight = tga.GetHeight();

    unsigned char noiseThreshold = (unsigned char)(255.0 * GetDensity());

    const RandomNumberGenerator *rng = atm.GetRandomNumberGenerator();

    double midX = GetBaseWidth() * 0.5;
    double midY = GetBaseLength() * 0.5;
    double qmidX = startx + quadSize * 0.5;
    double qmidY = starty + quadSize * 0.5;
    Vector3 quadCenter = Vector3(qmidX * voxelSize - midX, 0, qmidY * voxelSize - midY);

    // Offset position some small amount to help with sorting
    double rx = rng->UniformRandomDouble() - 0.5;
    double rz = rng->UniformRandomDouble() - 0.5;
    quadCenter.x += rx;
    quadCenter.z += rz;

    const Camera* camera = tcsData->GetCamera();

    CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);

    StratocumulusParticleCloudQuad *quad = SL_NEW StratocumulusParticleCloudQuad(this, quadCenter, quadSize * voxelSize, GetThickness(), voxelSize, tcsData);
    quad->SetLocalOrigin(GetWorldPosition(tcsData) * camera->GetInverseBasis3x3());
    quad->UpdateBounds(GetLocalBasis(tcsUserData));

    SL_VECTOR(StratocumulusPuff*)& metaballs = quad->GetMetaballs();

    midX = midY = quadSize * voxelSize * 0.5;
    float fThickness = (float)GetThickness();

    float voxelScale = 1.5f;
    Configuration::GetFloatValue("stratocumulus-particles-voxel-scale", voxelScale);

    const bool voxelShaderInstancing = (tcsData->GetBillboardShader() && tcsData->GetBillboardShader()->GetShaderHandle() != 0);

    const BillboardBufferProvider* provider = tcsData->GetBillboardBufferProvider();

    for (int x = 0; x < quadSize; x++) {
        for (int y = 0; y < quadSize; y++) {
            float u = (float)(x + startx) / (float)voxelsWidth;
            float v = (float)(y + starty) / (float)voxelsDepth;

            if (u >= 1.0f || v >= 1.0f) {
                continue;
            }

            int noiseRow = (int)(v * (float)noiseHeight);
            int noiseCol = (int)(u * (float)noiseWidth);
            unsigned char noiseSample = noise[noiseRow * noiseWidth + noiseCol];

            if (noiseSample < noiseThreshold) {
                int idx = y * voxelsWidth + x;
                float t = (((float)(noiseThreshold - noiseSample) / (float)(noiseThreshold)));
                double alpha = MathUtils::smoothstep(0.0, 0.5, t);
                StratocumulusPuff *ball = SL_NEW StratocumulusPuff((float)alpha, noiseSample, atm.GetRandomNumberGenerator()->UniformRandomFloat());
                float size = fThickness - ((float)noiseSample / 255.0f) * fThickness * (float)thicknessVariance;
                if (size < voxelSize * voxelScale) size = voxelSize * voxelScale;
                //size = GetThickness();
                int atlasIdx = rng->UniformRandomIntRange(1, 16);
                ball->InitializeMetaball(1.414f * (size * 0.5f), 0.0f, atlasIdx, MathUtilsFloat::Epsilon(), MathUtilsFloat::Epsilon(), voxelShaderInstancing, tcsData->GetHDREnabled(), provider->GetABillboardBuffer());
                Vector3 pos(
                    (x * voxelSize - midX) + rng->UniformRandomDouble() * voxelSize * 0.5,
                    rng->UniformRandomDouble() * heightVariation - (heightVariation * 0.5),
                    (y * voxelSize - midY) + rng->UniformRandomDouble() * voxelSize * 0.5
                );
                ball->SetWorldPosition(pos);

                metaballs.push_back(ball);
            }
        }
    }

    SL_VECTOR(StratocumulusParticleCloudQuad*)& quads = GetOrCreateSpcTcsUserData(tcsData)->quads;
    quads.push_back(quad);
}

bool StratocumulusParticleCloudLayer::SeedClouds(const Atmosphere& atm, void* data)
{
    SL_ASSERT(&atm == &atmosphere);
    if (!atmosphere.IsInitialized()) return false;

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);

    tcsData->SetForceImmediate(true);

    voxelsWidth = (int)(GetBaseWidth() / voxelSize);
    voxelsDepth = (int)(GetBaseLength() / voxelSize);

    SL_VECTOR(StratocumulusParticleCloudQuad*)& quads = GetOrCreateSpcTcsUserData(tcsData)->quads;
    for (SL_VECTOR(StratocumulusParticleCloudQuad*)::const_iterator it = quads.begin(); it != quads.end(); it++) {
        SL_DELETE *it;
    }
    quads.clear();

    TGALoader tga;
    if (!tga.Load("stratus.tga", Atmosphere::GetResourceLoader())) return false;

    int quadSize = 10;
    Configuration::GetIntValue("stratocumulus-particle-quad-size", quadSize);

    for (int x = 0; x < voxelsWidth; x += quadSize) {
        for (int y = 0; y < voxelsDepth; y += quadSize) {
            MakeQuad(x, y, quadSize, tga, atmosphere, tcsData);
        }
    }

    tcsData->SetForceImmediate(false);

    return true;
}

void StratocumulusParticleCloudLayer::DoUpdates(bool geocentricMode, const Atmosphere* atmosphere, unsigned long now, const Camera* camera, ThreadCameraStreamData* tcsData)
{
    if (!IsRenderable(tcsData)) return;

    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    AdjustGeocentricPositions(geocentricMode, camera, tcsUserData);
    WrapClouds(geocentricMode, camera, tcsUserData);
    AdjustForCurvature(geocentricMode, atmosphere, camera, tcsUserData);
    RoundEdgesAndApplyDensity(camera, atmosphere, tcsUserData);
}

void StratocumulusParticleCloudLayer::WrapClouds(bool geocentric, const Camera* camera, CloudLayerTcsUserData* tcsUserData)
{
    Vector3 camPos = camera->GetPosition();


    double& layerX = tcsUserData->layerX;
    double& layerZ = tcsUserData->layerZ;

    if (isInfinite || cloudWrap) {
        Vector3 anchor = Vector3(layerX, 0, layerZ);

        if (isInfinite) {
            Vector3 camYUp = camPos * camera->GetInverseBasis3x3();
            layerX = camYUp.x;
            layerZ = camYUp.z;
        }

        double halfWidth = baseWidth * 0.5;
        double halfLength = baseLength * 0.5;

        const SL_VECTOR(StratocumulusParticleCloudQuad*)& quads = GetOrCreateSpcTcsUserData(tcsUserData->tcsData)->quads;
        for (SL_VECTOR(StratocumulusParticleCloudQuad*)::const_iterator it = quads.begin(); it != quads.end(); it++) {
            StratocumulusParticleCloudQuad *q = *it;
            const Vector3& quadPos = q->GetWorldCenter();

            // Flip into y-up
            Vector3 newCloudPos = GetLocalBasis(tcsUserData) * quadPos;
            newCloudPos.y = 0;

            // Iterate until it's in bounds.
            bool adj;
            bool changed = false;

            double quadDim = q->GetSize();

            if (quadDim < halfWidth) {
                do {
                    adj = false;

                    if (newCloudPos.x > anchor.x + halfWidth) {
                        newCloudPos.x = (anchor.x - halfWidth) + (newCloudPos.x - (anchor.x + halfWidth - quadDim));
                        adj = changed = true;
                    }
                    if (newCloudPos.x < anchor.x - halfWidth) {
                        newCloudPos.x = (anchor.x + halfWidth) - ((anchor.x - halfWidth) - newCloudPos.x + quadDim);
                        adj = changed = true;
                    }
                    if (newCloudPos.z > anchor.z + halfLength) {
                        newCloudPos.z = (anchor.z - halfLength) + (newCloudPos.z - (anchor.z + halfLength - quadDim));
                        adj = changed = true;
                    }
                    if (newCloudPos.z < anchor.z - halfLength) {
                        newCloudPos.z = (anchor.z + halfLength) - ((anchor.z - halfLength) - newCloudPos.z + quadDim);
                        adj = changed = true;
                    }
                } while (adj);
            }

            if (changed) {

                // newCloudPos is y-up world coords

                // Dist from anchor
                Vector3 diff = newCloudPos - anchor;
                q->SetLocalCenter(diff);
                q->SetLocalOrigin(anchor);
                q->SetNeedsGeocentricPlacement(true);
                q->Invalidate();

                wrapDirty = true;
            }
        }
    }

}

Vector3 StratocumulusParticleCloudLayer::GetSortPosition(const Camera* camera, const ThreadCameraStreamData* tcsData) const
{
    double x, y, z;
    CloudLayer::GetSortPosition(x, y, z, camera, tcsData);
    return Vector3(x, y, z);
}

void StratocumulusParticleCloudLayer::SetBaseAltitude(double meters, bool updateCloudPositions, void* data) const
{
    CloudLayer::SetBaseAltitude(meters, updateCloudPositions, data);

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);
    CloudLayerTcsUserData* tcsUserData = GetOrCreateCloudLayerTcsUserData(tcsData);

    if (updateCloudPositions) {

        const Camera* camera = tcsData->GetCamera();

        const SL_VECTOR(StratocumulusParticleCloudQuad*)& quads = GetOrCreateSpcTcsUserData(tcsData)->quads;
        for (SL_VECTOR(StratocumulusParticleCloudQuad*)::const_iterator it = quads.begin(); it != quads.end(); it++) {
            StratocumulusParticleCloudQuad *q = *it;
            q->SetLocalOrigin(GetWorldPosition(tcsData) * camera->GetInverseBasis3x3());
            q->UpdateBounds(GetLocalBasis(tcsUserData));
            q->SetNeedsGeocentricPlacement(true);
        }
    }
}

void SILVERLINING_API StratocumulusParticleCloudLayer::SetLayerPosition(double eastCoord, double southCoord, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);

    CloudLayer::SetLayerPosition(eastCoord, southCoord, tcsData);

    const Camera* camera = tcsData->GetCamera();

    CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);

    const SL_VECTOR(StratocumulusParticleCloudQuad*)& quads = GetOrCreateSpcTcsUserData(tcsData)->quads;
    for (SL_VECTOR(StratocumulusParticleCloudQuad*)::const_iterator it = quads.begin(); it != quads.end(); it++) {
        StratocumulusParticleCloudQuad *q = *it;
        q->SetLocalOrigin(GetWorldPosition(tcsData) * camera->GetInverseBasis3x3());
        q->UpdateBounds(GetLocalBasis(tcsUserData));
        q->SetNeedsGeocentricPlacement(true);
    }
}

void StratocumulusParticleCloudLayer::AdjustGeocentricPositions(bool geocentric, const Camera* camera, CloudLayerTcsUserData* tcsUserData)
{
    ThreadCameraStreamData* tcsData = tcsUserData->tcsData;
    const SL_VECTOR(StratocumulusParticleCloudQuad*)& quads = GetOrCreateSpcTcsUserData(tcsData)->quads;

    if (!geocentric) {
        tcsUserData->geocentricAltitude = GetBaseAltitude(tcsData);
        for (SL_VECTOR(StratocumulusParticleCloudQuad*)::const_iterator it = quads.begin(); it != quads.end(); it++) {
            StratocumulusParticleCloudQuad *q = *it;
            if (q->GetNeedsGeocentricPlacement()) {
                Vector3 origin = q->GetLocalOrigin();
                origin.y = tcsUserData->geocentricAltitude;
                q->SetLocalOrigin(origin);
                q->UpdateBounds(GetLocalBasis(tcsUserData));
                q->SetNeedsGeocentricPlacement(false);
            }

        }
        return;
    }

    Renderer* renderer = tcsData->GetRenderer();
    Vector3 bUnit = camera->GetUpVector();
    bUnit.x /= earthRadii.x;
    bUnit.y /= earthRadii.y;
    bUnit.z /= earthRadii.z;
    bUnit.Normalize();
    Vector3 intersection = earthRadii * bUnit;

    double earthRadius = intersection.Length();
    double baseAlt = GetBaseAltitude(tcsData);
    bool geocentricBaseAltitude = (baseAlt > earthRadii.z || baseAlt > earthRadii.y);
    if (!geocentricBaseAltitude) {
        tcsUserData->geocentricAltitude = earthRadius + baseAlt;
    } else {
        tcsUserData->geocentricAltitude = baseAlt;
        //return; // SetBaseAltitude() already updated the cloud heights.
    }

    for (SL_VECTOR(StratocumulusParticleCloudQuad*)::const_iterator it = quads.begin(); it != quads.end(); it++) {
        StratocumulusParticleCloudQuad *q = *it;
        if (q->GetNeedsGeocentricPlacement()) {
            double radius = earthRadius;

            double baseAlt = GetBaseAltitude(tcsData);
            if (geocentricBaseAltitude) {
                // Old-school geocentric altitude specified
                radius = baseAlt;
            } else {
                radius += baseAlt;
            }

            Vector3 origin = q->GetLocalOrigin();
            origin.y = radius;
            q->SetLocalOrigin(origin);
            q->SetNeedsGeocentricPlacement(false);
            q->UpdateBounds(GetLocalBasis(tcsUserData));
        }
    }
}

void StratocumulusParticleCloudLayer::RoundEdgesAndApplyDensity(const Camera* camera, const Atmosphere *atm, CloudLayerTcsUserData* tcsUserData)
{
    // Handle curvature & coverage
    Vector3 center;
    double radius = baseWidth;

    double& coverageMultiplier = tcsUserData->coverageMultiplier;
    bool& densityDirty = tcsUserData->densityDirty;

    CloudLayer::CurveModes& curveMode = tcsUserData->curveMode;
    bool& curveModeDirty = tcsUserData->curveModeDirty;

    if (!curveModeDirty && !GetIsInfinite() && !GetCloudWrapping()) return;

    if (curveMode == SQUARE && (!usingRuntimeDensity || !densityDirty)) return;

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

    ThreadCameraStreamData* tcsData = tcsUserData->tcsData;

    Vector3 layerPos;
    if (GetIsInfinite()) {
        center = center + camera->GetPosition() * camera->GetInverseBasis3x3();
    } else {
        GetLayerPosition(layerPos.x, layerPos.z, tcsData);
        center = (center + layerPos);
    }

    unsigned char noiseThreshold = (unsigned char)(255.0 * GetDensity() * coverageMultiplier);

    const SL_VECTOR(StratocumulusParticleCloudQuad*)& quads = GetOrCreateSpcTcsUserData(tcsData)->quads;
    for (SL_VECTOR(StratocumulusParticleCloudQuad *)::const_iterator it = quads.begin(); it != quads.end(); it++) {
        StratocumulusParticleCloudQuad *q = *it;
        bool somethingChanged = false;

        bool needsDensity = usingRuntimeDensity && densityDirty;
        bool needsRounding = false;
        Vector3 quadCenter;
        if (curveMode != SQUARE) {
            quadCenter = q->GetLocalOrigin() + q->GetLocalCenter();
            double quadRadius = (q->GetSize() * 0.5) * 1.414;
            double dx = quadCenter.x - center.x;
            double dz = quadCenter.z - center.z;
            double dist = sqrt(dx*dx + dz*dz);
            if (dist > (radius - quadRadius)) {
                needsRounding = true;
            }
        }

        double invFadeDist = 1.0 / (q->GetSize() * 0.5);

        SL_VECTOR(StratocumulusPuff *)& metaballs = q->GetMetaballs();
        SL_VECTOR(StratocumulusPuff*)::iterator mit;

        if (needsDensity || needsRounding || wrapDirty) {
            for (mit = metaballs.begin(); mit != metaballs.end(); mit++) {
                StratocumulusPuff *puff = *mit;
                double alphaMultiplier = 1.0;
                if (usingRuntimeDensity) {
                    if (densityDirty || wrapDirty) {
                        unsigned char noiseSample = (*mit)->GetNoiseSample();
                        if (noiseSample < noiseThreshold) {
                            float t = (((float)(noiseThreshold - noiseSample) / (float)(noiseThreshold)));
                            alphaMultiplier = MathUtils::smoothstep(0.0, 0.5, t);
                        } else {
                            alphaMultiplier = 0;
                        }
                    } else {
                        alphaMultiplier = puff->GetAlphaMutiplier();
                    }
                }

                if (needsRounding) {
                    Vector3 pos = puff->GetWorldPosition().ToVector3() + quadCenter;

                    double dx = pos.x - center.x;
                    double dz = pos.z - center.z;
                    double dist2 = dx*dx + dz*dz;

                    if (dist2 > r2) {
                        if (puff->GetRandom() > 0.01f) {
                            //double dist = sqrt(dist2);
                            //double nDist = (dist - radius) * invFadeDist;
                            //if (nDist > 1.0) nDist = 1.0;
                            alphaMultiplier = 0;// 1.0 - nDist;
                        }
                    }
                }

                if (MathUtilsFloat::Abs(puff->GetAlphaMutiplier() - (float)alphaMultiplier) > 0.1f) {
                    puff->SetAlphaMultiplier((float)alphaMultiplier);
                    somethingChanged = true;
                }
            }
        }

        if (somethingChanged) {
            q->Invalidate();
        }
    }

    curveModeDirty = false;
    densityDirty = false;
    wrapDirty = false;
}

void StratocumulusParticleCloudLayer::AdjustForCurvature(bool geocentric, const Atmosphere* atmosphere, const Camera* camera, CloudLayerTcsUserData* tcsUserData)
{
    bool curve = true;
    Configuration::GetBoolValue("cumulus-round-earth", curve);

    double earthRadius = 6371000;
    Configuration::GetDoubleValue("earth-radius-meters-polar", earthRadius);
    earthRadius *= Atmosphere::GetUnitScale();

    double radius = earthRadius;
    double alpha = 1.0;

    ThreadCameraStreamData* tcsData = tcsUserData->tcsData;

    if (GetCurveTowardGround()) {
        double scale = 1.0;
        Configuration::GetDoubleValue("curve-toward-ground-radius-scale", scale);
        double a = sqrt(GetBaseWidth() * GetBaseWidth() + GetBaseLength() * GetBaseLength()) / 2.0;
        double b = GetBaseAltitude(tcsData);

        if (b > 0) {
            radius = (a*a + b*b) / (2.0 * b);
            radius *= scale;

            if (b < earthRadius) {
                double altitude = atmosphere->GetConditions().GetLocation(tcsData).GetAltitude();
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

        Vector3 cloudLayerPos(layerX, GetBaseAltitude(tcsData), layerZ);
        Vector3 camPos = camera->GetPosition();

        double radiusSq = radius * radius;

        const SL_VECTOR(StratocumulusParticleCloudQuad*)& quads = GetOrCreateSpcTcsUserData(tcsData)->quads;
        for (SL_VECTOR(StratocumulusParticleCloudQuad *)::const_iterator it = quads.begin(); it != quads.end(); it++) {
            StratocumulusParticleCloudQuad *q = *it;
            Vector3 layerPos = q->GetLocalCenter();

            double distSq = 0;
            if (isInfinite) {
                distSq = (camPos - q->GetWorldCenter()).SquaredLength();
            } else {
                Vector3 planar(layerPos.x, 0, layerPos.z);
                distSq = planar.SquaredLength();
            }

            if (distSq < radiusSq) {
                double displacement = radius - (sqrt(radiusSq - distSq));

                displacement *= alpha;

                double oldDisplacement = q->GetCurvatureDisplacement();
                if (MathUtils::Abs(oldDisplacement - displacement) > 1.0) {
                    q->SetCurvatureDisplacement(-displacement);
                    q->UpdateBounds();
                }
            }
        }
    }

}
double StratocumulusParticleCloudLayer::GetWrapFade(StratocumulusParticleCloudQuad *q, const Vector3& anchor, bool fadeOverride, CloudLayerTcsUserData* tcsUserData) const
{
    if ((isInfinite || cloudWrap) && (fadeTowardEdges&&fadeOverride)) {

        double width = q->GetSize();

        double halfWidth = baseWidth * 0.5;
        double halfLength = baseLength * 0.5;

        Vector3 cloudPos = q->GetWorldCenter();
        cloudPos = GetLocalBasis(tcsUserData) * cloudPos;

        double minDist = 1.0; // normalized distance from edge

        double dist = ((anchor.x + halfWidth) - (cloudPos.x + width)) / halfWidth;
        if (dist < minDist) minDist = dist;
        dist = ((cloudPos.x - width) - (anchor.x - halfWidth)) / halfWidth;
        if (dist < minDist) minDist = dist;
        dist = ((anchor.z + halfLength) - (cloudPos.z + width)) / halfLength;
        if (dist < minDist) minDist = dist;
        dist = ((cloudPos.z - width) - (anchor.z - halfLength)) / halfLength;
        if (dist < minDist) minDist = dist;

        double t = 1.0 - minDist;
        if (t < 0) t = 0;
        if (t > 1.0) t = 1.0;

        double fade = 1.0 - (t * t * t * t * t);

        return fade;
    }

    return 1.0;
}

void StratocumulusParticleCloudLayer::MoveClouds(double x, double y, double z, ThreadCameraStreamData* tcsData)
{
    if (!IsRenderable(tcsData)) return;

    Vector3 v(x, y, z);

    if (v.SquaredLength() > 0) {

        const Camera* camera = tcsData->GetCamera();

        Vector3 localV = v * camera->GetInverseBasis3x3();

        bool wrappedCloud = (isInfinite || cloudWrap);

        // Update the world position of each quad
        const SL_VECTOR(StratocumulusParticleCloudQuad*)& quads = GetOrCreateSpcTcsUserData(tcsData)->quads;
        for (SL_VECTOR(StratocumulusParticleCloudQuad *)::const_iterator it = quads.begin(); it != quads.end(); it++) {
            StratocumulusParticleCloudQuad *q = *it;
            q->SetLocalOrigin(q->GetLocalOrigin() + localV);
            q->UpdateBounds();
        }

        // If cloud layer is localized, update the layer's position
        if (!wrappedCloud) {
            CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);

            tcsUserData->layerX += localV.x;
            tcsUserData->layerZ += localV.z;
            tcsUserData->baseAltitude += localV.y;
        }
    }
}

bool StratocumulusParticleCloudLayer::DrawSetup(int pass, const Vector3 *lightPos, const Color *lightColor, ThreadCameraStreamData* tcsData) const
{
    return true;
}

bool StratocumulusParticleCloudLayer::EndDraw(int pass) const
{
    return true;
}


bool StratocumulusParticleCloudLayer::Draw(int pass, const Vector3 *lightPos, const Color *lightColor,
        bool invalid, bool wantsLightingUpdate, unsigned long now, const Sky *sky, bool drawLightning, const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
        , ThreadCameraStreamData* tcsData) const
{
    if (!IsRenderable(tcsData)) return true;

    StratocumulusParticleCloudLayerTcsUserData* tcsUserData = GetOrCreateSpcTcsUserData(tcsData);
    bool& needsLighting = tcsUserData->needsLighting;
    Color& finalLightColor = tcsUserData->finalLightColor;
    Vector3& lastLightPos = tcsUserData->lastLightPos;

    if (invalid) needsLighting = true;

    if (pass == 1) {

        CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);
        CloudLayerCullSet* cloudLayerCullSet = tcsUserData->cloudLayerCullSet;

        if (cloudLayerCullSet->GetCulled()) {
            cloudLayerCullSet->ClearCulled();
            return false;
        }

        if (!IsRenderable(tcsData)) return true;

        float ar, ag, ab;
        atmosphere->GetAmbientColor(&ar, &ag, &ab, tcsData);
        Color ambient(ar, ag, ab);

        finalLightColor = *lightColor * diffuseScale;
        finalLightColor.ScaleToUnitOrLess(tcsData->GetHDREnabled());

        finalLightColor = finalLightColor * (1.0f - ambientBlend) + ambient * ambientBlend;

        if (hasForcedCloudColor) {

            Color originalLight = Color(lightColor->r, lightColor->g, lightColor->b);
            Color forcedColor = Color(forcedCloudColor.x, forcedCloudColor.y, forcedCloudColor.z);
            if (forcedCloudColorLighting) {
                finalLightColor = originalLight * forcedColor;
            } else {
                finalLightColor = forcedColor;
            }
        }

        MetaballRenderingParameters* metaballParams = tcsData->GetMetaballRenderingParams();

        metaballParams->SetLightPosition(*lightPos);

        if (((*lightPos).Dot(lastLightPos) < cosLightThreshold)) {
            lastLightPos = *lightPos;
            needsLighting = true;
        }

        Renderer* renderer = tcsData->GetRenderer();
        renderer->SubmitBlendedObject(const_cast<StratocumulusParticleCloudLayer*>(this));
    }
    return true;
}

struct StratocumulusParticleCloudQuadComparator {
    StratocumulusParticleCloudQuadComparator(ThreadCameraStreamData* _tcsData)
        : tcsData(_tcsData)
    {

    }

    bool operator()(const StratocumulusParticleCloudQuad* lhs, const StratocumulusParticleCloudQuad* rhs) const
    {
        const Vector3& camPos = tcsData->GetMetaballRenderingParams()->GetCameraPosition();

        double d1 = (camPos - lhs->GetWorldCenter()).SquaredLength();
        double d2 = (camPos - rhs->GetWorldCenter()).SquaredLength();

        // We want back to front
        return (d1 > d2);
    }

    ThreadCameraStreamData* tcsData;
};


void StratocumulusParticleCloudLayer::DrawBlendedObject(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos
        , bool fadeOverride
        , ThreadCameraStreamData* tcsData) const
{
    CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);

    const Matrix3& basis = GetLocalBasis(tcsUserData);

    MetaballRenderingParameters* metaballParams = tcsData->GetMetaballRenderingParams();

    Vector3 camPos = sceneCamera->GetPosition();
    metaballParams->SetCameraPosition(camPos);

    Color originalAmbient;
    Vector3 forcedAmbient;
    bool ambientOverride = false, doLighting;
    if (GetOverriddenCloudColor(forcedAmbient, doLighting)) {
        originalAmbient = metaballParams->GetAmbientColor();
        Color forcedColor = Color(forcedAmbient.x, forcedAmbient.y, forcedAmbient.z);
        if (doLighting) {
            forcedColor = forcedColor * originalAmbient;
        }
        metaballParams->SetAmbientColor(forcedColor);
        ambientOverride = true;
    }

    BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();

    billboardParams->SetVerticalGradient(gradient);
    billboardParams->SetSoftness(GetIsSoft() ? (float)(voxelSize) : 0);
    billboardParams->SetSpinEnabled(false);
    billboardParams->SetFlattening(flattening);

    StratocumulusParticleCloudLayerTcsUserData* tcsStratoCumulusUserData = GetOrCreateSpcTcsUserData(tcsData);

    SL_VECTOR(StratocumulusParticleCloudQuad*)& quads = tcsStratoCumulusUserData->quads;
    std::sort(quads.begin(), quads.end(), StratocumulusParticleCloudQuadComparator(tcsData));

    recompileFrameCount = 0;

    Vector3 anchor;

    const double layerX = tcsUserData->layerX;
    const double layerZ = tcsUserData->layerZ;

    Renderer* renderer = tcsData->GetRenderer();
    const Camera* camera = tcsData->GetCamera();
    if (!isInfinite) {
        anchor = Vector3(layerX, 0, layerZ);
    } else {
        anchor = camPos * camera->GetInverseBasis3x3();
    }

    const ShadowMap *sm = tcsData->GetShadowMapObject();

    double fadeDistance = 0;
    Configuration::GetDoubleValue("stratocumulus-particles-distance-fade-length", fadeDistance);
    double minFade = 0.5;
    Configuration::GetDoubleValue("stratocumulus-particles-distance-fade-min", minFade);

    const double fade = GetFade(tcsData);

    for (SL_VECTOR(StratocumulusParticleCloudQuad*)::const_iterator it = quads.begin(); it != quads.end(); it++) {

        StratocumulusParticleCloudQuad *quad = *it;

        if (tcsStratoCumulusUserData->needsLighting) quad->Invalidate();

        Matrix4 xlate;
        Vector3 center = quad->GetWorldCenter() + (Vector3(0, quad->GetCurvatureDisplacement(), 0) * basis);
        xlate.elem[0][3] = center.x;
        xlate.elem[1][3] = center.y;
        xlate.elem[2][3] = center.z;

        Camera adjustedCam(renderCamera, true, false);
        adjustedCam.MultiplyModelViewMatrix(xlate);
        renderer->SetModelviewMatrix(adjustedCam.GetModelViewMatrix());

        double edgeFade = GetWrapFade(quad, anchor, fadeOverride, tcsUserData);

        double distanceFade = 1.0;

        if (sm && sm->IsRendering()) {
            distanceFade = 1.0;
        } else {
            if (fadeDistance > 0) {
                Vector3 Normal = (center - sceneCamera->GetPosition());
                double quadSize = quad->GetSize() * fadeDistance;
                distanceFade = (Normal.SquaredLength() / (quadSize * quadSize));
                if (distanceFade > 1.0) distanceFade = 1.0;
                if (distanceFade < minFade) distanceFade = minFade;
            }
        }

        quad->Draw(fade * alpha * edgeFade * distanceFade, basis, tcsStratoCumulusUserData->finalLightColor, atmosphere, sceneCamera, &adjustedCam, overriddenBillboardMatrix, tcsData);

        renderer->SetModelviewMatrix(renderCamera->GetModelViewMatrix());
    }

    tcsStratoCumulusUserData->needsLighting = false;

    if (ambientOverride) {
        metaballParams->SetAmbientColor(originalAmbient);
    }

    billboardParams->SetVerticalGradient(0);
    billboardParams->SetSpinEnabled(true);
    billboardParams->SetFlattening(1.0f);
}

Vector3 StratocumulusParticleCloudLayer::GetWorldPosition(const ThreadCameraStreamData* tcsData) const
{
    double layerX, layerZ;
    double baseAlt;
    GetLayerPosition(layerX, layerZ, const_cast<ThreadCameraStreamData*>(tcsData));
    baseAlt = GetBaseAltitudeGeocentric(const_cast<ThreadCameraStreamData*>(tcsData));

    Vector3 layerPos(layerX, baseAlt, layerZ);

    const Camera* camera = tcsData->GetCamera();

    Vector3 worldPos = layerPos * camera->GetBasis3x3();

    return worldPos;
}

bool StratocumulusParticleCloudLayer::Serialize(std::ostream& stream, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);

    return CloudLayer::Serialize(stream, tcsData);
}

bool StratocumulusParticleCloudLayer::Unserialize(const Atmosphere& atm, std::istream& stream, void* data)
{
    SL_ASSERT(&atmosphere == &atm);
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();

    bool ok = CloudLayer::Unserialize(atm, stream, tcsData);
    if (ok) {
        return SeedClouds(atm, tcsData);
    }

    return false;
}

bool StratocumulusParticleCloudLayer::HasPrecipitationAtPosition(const Camera* camera, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();

    // Bail if we have no precip at all.
    if (precipitationEffects.size() == 0 || !IsRenderable(tcsData)) return false;

    // Don't assume which way is up.
    Vector3 camPos = camera->GetPosition() * camera->GetInverseBasis3x3();

    const CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);

    // Bail if you're not under the entire layer at all.
    if (camPos.y > tcsUserData->geocentricAltitude) return false;

    if (!GetIsInfinite()) {

        ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
        SL_ASSERT(tcsData);

        const double layerX = tcsUserData->layerX;
        const double layerZ = tcsUserData->layerZ;

        if (camPos.x < (layerX - GetBaseWidth() * 0.5)) return false;
        if (camPos.x >(layerX + GetBaseWidth() * 0.5)) return false;
        if (camPos.z > (layerZ + GetBaseLength() * 0.5)) return false;
        if (camPos.z < (layerZ - GetBaseLength() * 0.5)) return false;
    }

    return true;
}



bool StratocumulusParticleCloudLayer::IsInsideCloud(double x, double y, double z, void* data, double* distanceInside) const
{
    if (distanceInside) *distanceInside = 0;

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);

    if (!IsRenderable(tcsData)) return false;

    // Don't assume which way is up.
    Vector3 camPos = Vector3(x, y, z) * tcsData->GetCamera()->GetInverseBasis3x3();

    const CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);
    // Bail if you're not in the entire layer at all.
    if (camPos.y < tcsUserData->geocentricAltitude) return false;
    if (camPos.y > tcsUserData->geocentricAltitude + GetThickness()) return false;

    if (!GetIsInfinite()) {
        const double layerX = tcsUserData->layerX;
        const double layerZ = tcsUserData->layerZ;

        if (camPos.x < (layerX - GetBaseWidth() * 0.5)) return false;
        if (camPos.x >(layerX + GetBaseWidth() * 0.5)) return false;
        if (camPos.z > (layerZ + GetBaseLength() * 0.5)) return false;
        if (camPos.z < (layerZ - GetBaseLength() * 0.5)) return false;
    }

    if (distanceInside) {
        double y1 = abs(camPos.y - tcsUserData->geocentricAltitude);
        double y2 = abs(camPos.y - (tcsUserData->geocentricAltitude + GetThickness()));
        *distanceInside = y1 < y2 ? y1 : y2;
    }

    return true;
}

bool StratocumulusParticleCloudLayer::IsInsideCloud(const Camera* camera, void* data, double* distanceInside ) const
{
    const Vector3& pos = camera->GetPosition();

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();;

    return IsInsideCloud(pos.x, pos.y, pos.z, tcsData, distanceInside);
}

double StratocumulusParticleCloudLayer::GetMaxHeight() const
{
    return GetThickness();
}

double StratocumulusParticleCloudLayer::DistanceToPlane(const Vector3& from, const Vector3& to, double D, const Matrix4& mv, ThreadCameraStreamData* tcsData) const
{
    const double farFarAway = 1000000;

    const Camera* camera = tcsData->GetCamera();

    Vector3 v = to - from;
    Vector3 Rd = v;
    Rd.Normalize();

    Vector3 R0 = from;
    Vector3 Pn = Vector3(0, -1, 0) * camera->GetBasis3x3();

    const double epsilon = 2.4E-7;

    double Vd = Pn.Dot(Rd);
    if (Vd >= -epsilon) {
        // ray points away from plane, try the other plane normal
        Pn = Vector3(0, 1, 0) * camera->GetBasis3x3();
        Vd = Pn.Dot(Rd);
        D = -D;
    }

    if (Vd >= -epsilon) return farFarAway;

    double V0 = -(Pn.Dot(R0) + D);
    double t = V0 / Vd;

    if (t < 0) return farFarAway; // does not intersect

    Vector3 Pi(R0.x + Rd.x * t, R0.y + Rd.y * t, R0.z + Rd.z * t);

    if (tcsData->GetSortByScreenDepth()) {
        return ((mv * Pi).z);
    } else {
        return (Pi - from).Length();
    }
}

double StratocumulusParticleCloudLayer::GetDistance(const Vector3& from, const Vector3& to, const Matrix4& mv, bool rightHanded, const Camera* camera, ThreadCameraStreamData* tcsData) const
{
    bool forcePlanar = false;
    Configuration::GetBoolValue("stratocumulus-force-planar-sort", forcePlanar);

    if (!GetIsInfinite() && !forcePlanar) {
        if (tcsData->GetSortByScreenDepth()) {
            Vector3 clipSpace = mv * GetSortPosition(camera, tcsData);
            return rightHanded ? -clipSpace.z : clipSpace.z;
        } else {
            Vector3 V = from - GetSortPosition(camera, tcsData);
            return V.Length();
        }
    } else {
        // Find ray / plane intersection point.
        Vector3 xformPos = GetWorldPosition(tcsData) * camera->GetInverseBasis3x3();
        double D = xformPos.y;

        // No distance if we're in the cloud
        Vector3 xformFrom = from * camera->GetInverseBasis3x3();
        if (xformFrom.y > xformPos.y && xformFrom.y < (xformPos.y + GetThickness())) {
            return 0;
        }

        double bottomDist = DistanceToPlane(from, to, D, mv, tcsData);
        double topDist = DistanceToPlane(from, to, D + GetThickness(), mv, tcsData);

        if (rightHanded) {
            bottomDist *= -1.0;
            topDist *= -1.0;
        }
        return topDist < bottomDist ? topDist : bottomDist;
    }
}
}