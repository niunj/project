// Copyright (c) 2006-2023 Sundog Software, LLC. All rights reserved worldwide.

#include "Virga.h"
#include "Renderer.h"
#include "Cloud.h"
#include "Billboard.h"
#include "BillboardBuffer.h"
#include "BillboardShader.h"
#include "Configuration.h"
#include "MathUtils.h"
#include "ShadowMap.h"
#include "Camera.h"
#include "OverriddenBillboardMatrix.h"
#include "ThreadCameraStreamData.h"
#include "TextureManager.h"
#include "SLAssert.h"

using namespace SilverLining;
using namespace std;

Virga::Virga(Cloud *parentCloud, double pDiameter, double height, const Vector3& offset,
             const Atmosphere& atm, ThreadCameraStreamData* _tcsData) :  offsetPos(offset), cloud(parentCloud), atmosphere(atm)
    , tcsData(_tcsData)
{
    diameter = pDiameter;

    billboard = SL_NEW Billboard();
    BillboardBuffer buffer = tcsData->GetBillboardBufferProvider()->GetABillboardBuffer();

    const bool useShaders = (tcsData->GetBillboardShader() && tcsData->GetBillboardShader()->GetShaderHandle() != 0);

    billboard->Initialize(MathUtilsFloat::Epsilon(), MathUtilsFloat::Epsilon(), (float)diameter, useShaders, buffer);
    billboard->SetWorldPosition(Vector3f(0.0f, 0.0f, 0.0f));

    double cwidth, cdepth, cheight;
    parentCloud->GetSize(cwidth, cdepth, cheight);
    double cloudRadius = sqrt(cwidth * cwidth + cdepth * cdepth + cheight * cheight) * 0.5;
    SetDepthOffset(cloudRadius);

    virgaAlpha = 0.3f;
    Configuration::GetFloatValue("virga-alpha", virgaAlpha);

    driftSpeed = 0.1f;
    driftRadius = 0.25f;
    Configuration::GetFloatValue("virga-drift-speed", driftSpeed);
    Configuration::GetFloatValue("virga-drift-radius", driftRadius);

    fadeDistance = 1000.0f;
    Configuration::GetFloatValue("virga-fade-distance", fadeDistance);
    fadeDistance *= (float)atm.GetUnitScale();

    yScale = height / diameter;

    driftOffset = atm.GetRandomNumberGenerator()->UniformRandomFloat() * 100.0f;

    TextureManager* textureManager = (atmosphere.TexturesAreShared()) ? atmosphere.GetDefaultTcsData()->GetTextureManager() : tcsData->GetTextureManager();
    virgaTexture = textureManager->GetOrCreateVirgaTexture();
}

Virga::~Virga()
{
    SL_DELETE billboard;
}

void Virga::SetColor(const Color& c, const Vector3& camPos, ThreadCameraStreamData* tcsData)
{
    Color color = c;
    color.ClampToUnitOrLess(tcsData->GetHDREnabled());
    color = color.ToGrayscale();

    float virgaDistance = (float)(GetWorldPosition(tcsData) - camPos).Length();
    float fade = 1.0f - expf(-virgaDistance / fadeDistance);
    color.a = virgaAlpha * fade;

    billboard->SetColor(color);
}

bool Virga::Draw(int pass, ThreadCameraStreamData* tcsData)
{
    if (pass == 1) {
        Renderer* renderer = tcsData->GetRenderer();
        renderer->SubmitBlendedObject(this);
    }

    return true;
}

void Virga::GetDrift(Matrix4& driftMatrix, unsigned long millis) const
{
    float t = ((float)millis * 0.001f + driftOffset) * driftSpeed;

    float radius = (float)diameter * driftRadius;
    driftMatrix.elem[0][3] = cosf(t) * radius; // x
    driftMatrix.elem[1][3] = 0.0;
    driftMatrix.elem[2][3] = sinf(t) * radius; // z
}

Vector3 Virga::GetWorldPosition(const ThreadCameraStreamData* tcsData) const
{
    const Camera* camera = tcsData->GetCamera();

    Vector3 pos = cloud->GetWorldPosition(tcsData) * camera->GetInverseBasis3x3() + offsetPos;
    pos = pos * camera->GetBasis3x3();
    return pos;
}

void Virga::DrawBlendedObject(const Atmosphere* _atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
                              , ThreadCameraStreamData* tcsData) const
{
    SL_ASSERT(&atmosphere == _atmosphere);
    const ShadowMap *sm = tcsData->GetShadowMapObject();

    if (sm && sm->IsRendering()) return;

    Renderer* renderer = tcsData->GetRenderer();

    Vector3 pos = GetWorldPosition(tcsData);
    Matrix4 rotMatrix = sceneCamera->GetBillboardMatrix();

    Matrix4 scale;
    scale.elem[1][1] = yScale;

    if (renderer->GetType() == Atmosphere::DIRECTX11 || renderer->GetType() == Atmosphere::DIRECTX11_1) {
        scale.elem[1][1] *= -1.0;
    }

    unsigned long millis = atmosphere.GetConditions().GetMillisecondTimer()->GetMilliseconds();

    Matrix4 driftMatrix;
    GetDrift(driftMatrix, millis);

    Matrix4 xlate;
    xlate.elem[0][3] = pos.x;
    xlate.elem[1][3] = pos.y;
    xlate.elem[2][3] = pos.z;

    Camera adjustedCamera(renderCamera, true, false);
    adjustedCamera.MultiplyModelViewMatrix(xlate);
    renderer->SetModelviewMatrix(adjustedCamera.GetModelViewMatrix());

    renderer->IncrementConstantBuffersOffset(tcsData->GetBillboardShader()->GetShaderHandle());

    renderer->EnableBlending(SRCALPHA, INVSRCALPHA, true);
    renderer->EnableDepthWrites(false);

    billboard->Draw(virgaTexture, 1, cloud->GetFade() * cloud->GetAlpha(), _atmosphere->GetOutputScale(), true, millis, &adjustedCamera, OverriddenBillboardMatrix(driftMatrix * rotMatrix * scale), tcsData, false);

    renderer->SetModelviewMatrix(renderCamera->GetModelViewMatrix());;
}