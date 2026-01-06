// Copyright (c) 2004-2016  Sundog Software, LLC. All rights reserved worldwide.

#include "StratocumulusParticleCloudQuad.h"
#include "StratocumulusParticleCloudLayer.h"
#include "StratocumulusPuff.h"
#include "Configuration.h"
#include "MathUtils.h"
#include "Metaball.h"
#include "Camera.h"
#include "TextureManager.h"
#include "BillboardRenderingParameters.h"
#include "MetaballRenderingParameters.h"
#include "BillboardRenderer.h"
#include "QuadBatches.h"
#include "SLAssert.h"
#include "Camera.h"
#include "CloudCompilationOperation.h"
#include "OverriddenBillboardMatrix.h"
#include "CumulusCloudRenderingParameters.h"

#include <algorithm>

namespace SilverLining
{
int StratocumulusParticleCloudQuad::recompileFrameLimit;
double StratocumulusParticleCloudQuad::sortAngleThreshold;

class StratocumulusParticleCloudQuadCompilationOperation : public CloudCompilationOperation
{
public:
    StratocumulusParticleCloudQuadCompilationOperation(bool isRightHanded);
    virtual ~StratocumulusParticleCloudQuadCompilationOperation();

    void Execute(ThreadCameraStreamData* tcsData);
public:
    StratocumulusParticleCloudQuad* quad;
    TextureHandle cloudTexture;
    double fade;
    Vector3 outputScale;
    unsigned long millis;

    Camera renderCamera;
    OverriddenBillboardMatrix overriddenBillboardMatrix;

};

StratocumulusParticleCloudQuadCompilationOperation::StratocumulusParticleCloudQuadCompilationOperation(bool isRightHanded)
    : renderCamera(isRightHanded)
    , overriddenBillboardMatrix(&renderCamera)
{
}

StratocumulusParticleCloudQuadCompilationOperation::~StratocumulusParticleCloudQuadCompilationOperation()
{
}

void StratocumulusParticleCloudQuadCompilationOperation::Execute(ThreadCameraStreamData* tcsData)
{
    if (quad->compiledObject) {
        SL_DELETE quad->compiledObject;
        quad->compiledObject = 0;
    }

    bool savedFog = false;
    BillboardRenderer* billboardRenderer = tcsData->GetBillboardRenderer();
    billboardRenderer->StartBatchDraw(cloudTexture, fade, outputScale, true, millis, &renderCamera, &overriddenBillboardMatrix, savedFog);

    for (SL_VECTOR(StratocumulusPuff*)::iterator it = quad->metaballs.begin(); it != quad->metaballs.end(); it++) {
        (*it)->DrawInBatch(false, 0, 0, tcsData);
    }

    quad->compiledObject = billboardRenderer->EndBatchDraw(0, savedFog, true, 0);
}

StratocumulusParticleCloudQuad::StratocumulusParticleCloudQuad(StratocumulusParticleCloudLayer *pParent, const Vector3& pCenter, double pSize, double pThickness, double pVoxelSize, ThreadCameraStreamData* _tcsData) :
    compiledObject(0), center(pCenter), width(pSize), thickness(pThickness), parentLayer(pParent), voxelSize(pVoxelSize), culled(false), needsGeo(true),
    curvatureDisplacement(0), hasLastBasis(false)
    , compilationParameters(0)
    , tcsData(_tcsData)
{

    double sortAngle = 10.0;
    Configuration::GetDoubleValue("stratocumulus-particle-sort-angle", sortAngle);
    sortAngleThreshold = cos(MathUtils::ToRadians(sortAngle));

    recompileFrameLimit = 10;
    Configuration::GetIntValue("stratocumulus-particle-recompile-frame-limit", recompileFrameLimit);

    randomness = 0.1;
    Configuration::GetDoubleValue("stratocumulus-particle-randomness", randomness);

    ambientScattering = 1.0f;
    Configuration::GetFloatValue("stratocumulus-particle-ambient-scattering", ambientScattering);

    heightVariation = 1500.0;
    Configuration::GetDoubleValue("stratocumulus-particle-base-height-variation", heightVariation);

    TextureManager* textureManager = 0;
    if (parentLayer && parentLayer->GetAtmosphere().TexturesAreShared()) {
        textureManager = parentLayer->GetAtmosphere().GetDefaultTcsData()->GetTextureManager();
    } else {
        textureManager = tcsData->GetTextureManager();
    }
    SL_ASSERT(textureManager);
    cloudTexture = textureManager->GetStratusTexture();
}

StratocumulusParticleCloudQuad::~StratocumulusParticleCloudQuad()
{
    ClearMetaballs();

    if (compiledObject) SL_DELETE compiledObject;

    if (compilationParameters) SL_DELETE compilationParameters;
}

void StratocumulusParticleCloudQuad::Invalidate()
{
    if (compiledObject) {
        compiledObject->Invalidate();
    }
}

struct StratocumulusPuffComparator {
    StratocumulusPuffComparator(const ThreadCameraStreamData* _tcsData, const Vector3& _offset, const Vector3& _camPos)
        : tcsData(_tcsData)
        , offset(_offset)
        , camPos(_camPos)
    {

    }
public:
    bool operator()(const StratocumulusPuff* lhs, const StratocumulusPuff* rhs)
    {
        if (!lhs || !rhs) return false;

        const Camera* camera = tcsData->GetCamera();

        const Matrix3& basis = camera->GetBasis3x3();

        const Vector3& p1 = lhs->GetWorldPosition().ToVector3() * basis + offset;
        const Vector3& p2 = rhs->GetWorldPosition().ToVector3() * basis + offset;

        double d1 = (p1 - camPos).SquaredLength();
        double d2 = (p2 - camPos).SquaredLength();

        // back to front
        return d1 > d2;
    }

private:
    const ThreadCameraStreamData* tcsData;
    const Vector3 offset;
    const Vector3 camPos;
};



bool StratocumulusParticleCloudQuad::Draw(double fade, const Matrix3& basis, const Color& lightColor, const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, ThreadCameraStreamData* tcsData)
{
    if (culled || fade < 0.01) return true;



    Renderer* renderer = tcsData->GetRenderer();
    renderer->EnableBlending(SRCALPHA, INVSRCALPHA);
    renderer->EnableDepthReads(true);
    renderer->EnableDepthWrites(false);

    BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();
    billboardParams->SetAdditiveBlending(false);

    MetaballRenderingParameters* metaballParams = tcsData->GetMetaballRenderingParams();

    metaballParams->SetOffsetPosition(GetWorldCenter());

    if (parentLayer->GetRecompileFrameCount() < recompileFrameLimit) {
        const Vector3& camPos = sceneCamera->GetPosition();
        Vector3 v1 = camPos - centerWorldCoords;
        Vector3 v2 = lastCameraPos - centerWorldCoords;
        v1.Normalize();
        v2.Normalize();
        double camAngle = MathUtils::Abs(v1.Dot(v2));
        if (camAngle < sortAngleThreshold) {
            lastCameraPos = camPos;
            if (compiledObject) {
                compiledObject->Invalidate();
                parentLayer->IncrementRecompileFrameCount();
            }
        }
    }

    unsigned long millis = atmosphere->GetConditions().GetMillisecondTimer()->GetMilliseconds();

    // Slide the ubo offset by one for this cloud and the set of ubo constants being set for this particular cloud.
    // This is needed for vulkan.
    renderer->IncrementConstantBuffersOffset(tcsData->GetBillboardShader()->GetShaderHandle());

    if (compiledObject && !compiledObject->IsInvalid()) {
        compiledObject->Draw(cloudTexture, fade, atmosphere, millis, renderCamera, overriddenBillboardMatrix, false, tcsData);
    } else {

        std::sort(metaballs.begin(), metaballs.end(), StratocumulusPuffComparator(tcsData, metaballParams->GetOffsetPosition(), metaballParams->GetCameraPosition()));

        for (SL_VECTOR(StratocumulusPuff*)::iterator it = metaballs.begin(); it != metaballs.end(); it++) {
            (*it)->ComputeAlpha();
            (*it)->SetMetaballColor(lightColor, tcsData->GetHDREnabled());
            (*it)->ComputeBillboardColor(0, randomness, ambientScattering, tcsData);
            (*it)->ComputeBillboardColor(1, randomness, ambientScattering, tcsData);
        }

        if (!compilationParameters) {
            compilationParameters = SL_NEW StratocumulusParticleCloudQuadCompilationOperation(renderCamera->IsRightHanded());
        }

        Camera adjustedCam(renderCamera, true, false);
        compilationParameters->quad = this;
        compilationParameters->cloudTexture = cloudTexture;
        compilationParameters->fade = fade;
        compilationParameters->outputScale = atmosphere->GetOutputScale();
        compilationParameters->millis = millis;
        compilationParameters->renderCamera = adjustedCam;
        compilationParameters->overriddenBillboardMatrix = overriddenBillboardMatrix;

        compilationParameters->Execute(tcsData);
    }

    billboardParams->SetAdditiveBlending(true);
    return true;
}

void StratocumulusParticleCloudQuad::UpdateBounds(const Matrix3& basis)
{
    if (!parentLayer) return;

    Vector3 worldCenter = (localOrigin + center + Vector3(0, curvatureDisplacement + thickness * 0.5, 0));
    if (worldCenter == lastWorldPos && basis == lastBasis) return;
    lastWorldPos = worldCenter;

    double halfWidth = width * 0.5 + voxelSize * 0.5;
    double halfHeight = thickness * 0.5 + heightVariation * 0.5;

    bounds[0] = (worldCenter + Vector3(-halfWidth, -halfHeight, -halfWidth)) * basis;
    bounds[1] = (worldCenter + Vector3(-halfWidth, -halfHeight, halfWidth)) * basis;
    bounds[2] = (worldCenter + Vector3(halfWidth, -halfHeight, halfWidth)) * basis;
    bounds[3] = (worldCenter + Vector3(halfWidth, -halfHeight, -halfWidth)) * basis;
    bounds[4] = (worldCenter + Vector3(-halfWidth, thickness + halfHeight, -halfWidth)) * basis;
    bounds[5] = (worldCenter + Vector3(-halfWidth, thickness + halfHeight, halfWidth)) * basis;
    bounds[6] = (worldCenter + Vector3(halfWidth, thickness + halfHeight, halfWidth)) * basis;
    bounds[7] = (worldCenter + Vector3(halfWidth, thickness + halfHeight, -halfWidth)) * basis;

    // Invalidate();

    centerWorldCoords = worldCenter * basis;

    lastBasis = basis;
    hasLastBasis = true;
}

void StratocumulusParticleCloudQuad::UpdateBounds()
{
    if (hasLastBasis) {
        UpdateBounds(lastBasis);
    }
}

const Vector3& StratocumulusParticleCloudQuad::GetWorldCenter() const
{
    return centerWorldCoords;
}

void StratocumulusParticleCloudQuad::SetLocalCenter(const Vector3& pos)
{
    center = pos;
}

const Vector3& StratocumulusParticleCloudQuad::GetLocalCenter() const
{
    return center;
}

void StratocumulusParticleCloudQuad::SetLocalOrigin(const Vector3& pos)
{
    localOrigin = pos;
}

const Vector3& StratocumulusParticleCloudQuad::GetLocalOrigin() const
{
    return localOrigin;
}

bool StratocumulusParticleCloudQuad::Cull(const Camera* camera, const Camera* cullCamera)
{
    Frustum f = cullCamera->GetFrustumWorldSpace();
    if (parentLayer->GetAtmosphere().GetFarCullingDisabled()) {
        f.EnableFarClipCulling(false);
    }

    int cullingPlanes = f.GetNumCullingPlanes();
    for (int i = 0; i < cullingPlanes; i++) {
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
        culled = true;
        return true;
    }

    const CumulusCloudRenderingParameters* cumulusParams = tcsData->GetCumulusCloudRenderingParams();

    const double fade = tcsData->GetBillboardRenderingParams()->CalculateFogFade(center, cullCamera);
    if (fade <= cumulusParams->GetAlphaCullThreshold()) {
        culled = true;
        return true;
    }

    culled = false;
    return false;
}

void StratocumulusParticleCloudQuad::ClearMetaballs()
{
    SL_VECTOR(StratocumulusPuff *)::iterator it;
    for (it = metaballs.begin(); it != metaballs.end(); it++) {
        SL_DELETE *it;
    }

    metaballs.clear();
}

SL_VECTOR(StratocumulusPuff*)& StratocumulusParticleCloudQuad::GetMetaballs()
{
    return metaballs;
}

double StratocumulusParticleCloudQuad::GetSize() const
{
    return width;
}

bool StratocumulusParticleCloudQuad::GetNeedsGeocentricPlacement() const
{
    return needsGeo;
}

void StratocumulusParticleCloudQuad::SetNeedsGeocentricPlacement(bool b)
{
    needsGeo = b;
}

void StratocumulusParticleCloudQuad::SetCurvatureDisplacement(double d)
{
    curvatureDisplacement = d;
}

double StratocumulusParticleCloudQuad::GetCurvatureDisplacement() const
{
    return curvatureDisplacement;
}
}