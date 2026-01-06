// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.

#include "SLAssert.h"
#include "Atmosphere.h"
#include "Billboard.h"
#include "Configuration.h"
#include "Utils.h"
#include "MathUtils.h"
#include "BillboardBuffer.h"
#include "OverriddenBillboardMatrix.h"
#include "BillboardRenderingParameters.h"
#include "SharedVertexBuffer.h"
#include "ScopedConstantPrep.h"
#include "BillboardRenderer.h"
#include "BillboardShader.h"
#include "Camera.h"

using namespace std;


namespace SilverLining
{
Billboard::Billboard() : sharedVb(0), vbIdx(-1), centerPos(0, 0, 0), billboardColor(1, 1, 1, 1), radius(0), fadeRate(0), atlasIndex(0), rotateSpeed(0), useShader(false)
{
}

void Billboard::Initialize(float _rotateAngle, float _rotateSpeed, const float diameter, bool _useShader, const BillboardBuffer& buffer)
{
    sharedVb = buffer.sharedVb;
    vbIdx = buffer.vbIdx;

    useShader = _useShader;

    if (sharedVb) {
        sharedVb->AddReference();
    }

    rotateAngle = _rotateAngle;
    rotateSpeed = _rotateSpeed;

    absRotateSpeed = MathUtilsFloat::Abs(rotateSpeed);

    if (vbIdx >= 0) {

        SL_ASSERT(sharedVb && sharedVb->GetVertexBuffer());

        Vertex vert[4];

        if (diameter <= 0) {
            vert[0].x = -0.5;
            vert[0].y = 0.5;
            vert[0].z = 0;
            vert[0].w = 0;
            vert[0].SetUV(0, 1);
            vert[0].SetColor(Color(1, 1, 1));

            vert[1].x = -0.5;
            vert[1].y = -0.5;
            vert[1].z = 0;
            vert[1].w = 0;
            vert[1].SetUV(0, 0);
            vert[1].SetColor(Color(1, 1, 1));

            vert[2].x = 0.5;
            vert[2].y = -0.5;
            vert[2].z = 0;
            vert[2].w = 0;
            vert[2].SetUV(1, 0);
            vert[2].SetColor(Color(1, 1, 1));

            vert[3].x = 0.5;
            vert[3].y = 0.5;
            vert[3].z = 0;
            vert[3].w = 0;
            vert[3].SetUV(1, 1);
            vert[3].SetColor(Color(1, 1, 1));

            for (int i = 0; i < 4; i++) {
                float x = cosf(rotateAngle) * vert[i].x - sinf(rotateAngle) * vert[i].y;
                float y = sinf(rotateAngle) * vert[i].x + cosf(rotateAngle) * vert[i].y;
                vert[i].x = x;
                vert[i].y = y;
            }

        } else {
            radius = (float)diameter * 0.5f;

            if (useShader) {
                // Use instancing technique; stuff world pos in x,y,z
                // point size in abs(u), direction in v,t sign, rotation in abs(t)
                // rotation speed in abs(v), rotation direction in u sign
                for (int i = 0; i < 4; i++) {
                    vert[i].x = (float)centerPos.x;
                    vert[i].y = (float)centerPos.y;
                    vert[i].z = (float)centerPos.z;
                    vert[i].w = (float)atlasIndex;
                    vert[i].SetU(radius * rotateSpeed > 1.0f ? 1.0f : -1.0f);
                    vert[i].SetS(0);
                    vert[i].SetColor(Color(1, 1, 1, 1));

                    switch (i) {
                    case 0:
                        vert[i].SetV(-MathUtilsFloat::Abs(rotateSpeed));
                        vert[i].SetT(rotateAngle);
                        break;

                    case 1:
                        vert[i].SetV(-MathUtilsFloat::Abs(rotateSpeed));
                        vert[i].SetT(-rotateAngle);
                        break;

                    case 2:
                        vert[i].SetV(MathUtilsFloat::Abs(rotateSpeed));
                        vert[i].SetT(-rotateAngle);
                        break;

                    case 3:
                        vert[i].SetV(MathUtilsFloat::Abs(rotateSpeed));
                        vert[i].SetT(rotateAngle);
                        break;
                    }
                }
            } else {
                vert[0].x = -radius;
                vert[0].y = radius;

                vert[1].x = -radius;
                vert[1].y = -radius;

                vert[2].x = radius;
                vert[2].y = -radius;

                vert[3].x = radius;
                vert[3].y = radius;

                for (int i = 0; i < 4; i++) {
                    float x = cosf(rotateAngle) * vert[i].x - sinf(rotateAngle) * vert[i].y;
                    float y = sinf(rotateAngle) * vert[i].x + cosf(rotateAngle) * vert[i].y;
                    vert[i].x = x;
                    vert[i].y = y;
                }
            }
        }
        // Updating everything. Not just the colors. Hence, false
        sharedVb->GetVertexBuffer()->Update(vbIdx, vert, 4, false);
    }
}

Billboard::~Billboard()
{
    if (sharedVb) {
        sharedVb->RemoveReference();
    }
}

const Color Billboard::theWhiteColor(1,1,1);
void Billboard::UpdateQuadVertices(bool changeSize, double pointSize, bool rotate, float fadeRate)
{
    if (!sharedVb) return;

    VertexBuffer *vb = sharedVb->GetVertexBuffer();

    if (!vb) return;

    Vertex vert[4];

    float half = (float)pointSize * 0.5f;

    if (useShader) {
        // Use instancing technique; stuff world pos in x,y,z
        // point size in abs(u), relative pos in signs of v,t, rotation in abs(t),
        // rotation speed in abs(v), rotation direction in sign(u)
        for (int i = 0; i < 4; i++) {
            vert[i].x = (float)centerPos.x;
            vert[i].y = (float)centerPos.y;
            vert[i].z = (float)centerPos.z;
            vert[i].w = (float)atlasIndex;
            vert[i].SetS(fadeRate);

            if (changeSize) {
                radius = half;
            }

            if (rotateSpeed < 0.0f) {
                vert[i].SetU(-radius); //u *= -1.0f;
            } else {
                vert[i].SetU(radius);
            }

            switch (i) {
            case 0:
                vert[i].SetV(-absRotateSpeed);
                vert[i].SetT(rotateAngle);
                break;

            case 1:
                vert[i].SetV(-absRotateSpeed);
                vert[i].SetT(-rotateAngle);
                break;

            case 2:
                vert[i].SetV(absRotateSpeed);
                vert[i].SetT(-rotateAngle);
                break;

            case 3:
                vert[i].SetV(absRotateSpeed);
                vert[i].SetT(rotateAngle);
                break;
            }

            vert[i].SetColor(theWhiteColor);
        }

    } else {
        vb->Get(vbIdx, vert, 4);

        if (changeSize) {
            vert[0].x = -half;
            vert[0].y = half;

            vert[1].x = -half;
            vert[1].y = -half;

            vert[2].x = half;
            vert[2].y = -half;

            vert[3].x = half;
            vert[3].y = half;

            for (int i = 0; i < 4; i++) {
                if (rotate) {
                    float x = cosf(rotateAngle) * vert[i].x - sinf(rotateAngle) * vert[i].y;
                    float y = sinf(rotateAngle) * vert[i].x + cosf(rotateAngle) * vert[i].y;
                    vert[i].x = x;
                    vert[i].y = y;
                }
            }
        }
    }
    // Updating everything. Not just the colors. Hence, false
    vb->Update(vbIdx, vert, 4, false);
}

void Billboard::SetScreenSize(double diameter, const Vector3& camPos, ThreadCameraStreamData* tcsData)
{
    Renderer* renderer = tcsData->GetRenderer();

    Vector3f dist = camPos;
    dist = centerPos - dist;
    double z = dist.Length();

    double fovV;
    if (tcsData->GetCamera()->HasProjectionMatrix()) {
        fovV = tcsData->GetCamera()->GetFieldOfView();
    } else {
        renderer->GetFOV(fovV);
    }

    int x, y, w, h;
    renderer->GetViewport(x, y, w, h);

    double fovH = fovV * ((double)w / (double)h);

    double e = 1.0 / tan(fovH / 2.0);

    double pointSize = (diameter * z) / (e * w);

    UpdateQuadVertices(true, pointSize);
}

void Billboard::SetWorldSize(double diameter)
{
    UpdateQuadVertices(true, diameter);
}

void Billboard::SetAngularSize(double degrees)
{
    Matrix4 mv;

    Vector3f dist = centerPos;

    double r = dist.Length();
    double pointSize = r * tan(MathUtils::ToRadians(degrees * 0.5)) * 2.0;

    UpdateQuadVertices(true, pointSize, false);
}

void Billboard::SetWorldPosition(const Vector3f& pos)
{
    centerPos = pos;

    if (sharedVb) {
        if (useShader) {
            UpdateQuadVertices(false, 0);
        }
    }
}

void Billboard::SetColor(const Color& col)
{
    billboardColor = col;
}

Color Billboard::GetColor() const
{
    return billboardColor;
}

void Billboard::SetFadeRate(float pFadeRate)
{
    if (fadeRate != pFadeRate) {
        if (sharedVb) {
            if (useShader) {
                UpdateQuadVertices(false, 0, true, pFadeRate);
                fadeRate = pFadeRate;
            }
        }
    }
}

bool Billboard::IsCulled(const Frustum& f) const
{
    return false;
}


bool Billboard::DrawInBatch(bool drawImmediately, const Camera* renderCamera, const OverriddenBillboardMatrix* overriddenBillboardMatrix
                            , ThreadCameraStreamData* tcsData) const
{
    Renderer* renderer = tcsData->GetRenderer();
    if (!sharedVb) return false;

    VertexBuffer *vb = sharedVb->GetVertexBuffer();
    IndexBuffer *ib = sharedVb->GetIndexBuffer();

    const BillboardShader* billboardShader = tcsData->GetBillboardShader();

    if (billboardShader) {
        if (drawImmediately) {
            BillboardShadingUtilities shadingUtils(billboardShader->GetShaderHandle(), billboardShader->GetConstantLocations(), tcsData);
            shadingUtils.SetShaderColor(billboardColor);

            if (ib) {
                renderer->DrawStrip(vb->GetHandle(), ib->GetHandle(), vbIdx, 4, vb->GetNumVertices(), true, true);
            } else {
                renderer->DrawQuads(vb->GetHandle(), 4, vbIdx, false);
            }

            shadingUtils.UnsetShaderColor();
        } else {
            // Defer actual drawing until batch is done, so we can do it all at once
            tcsData->GetBillboardRenderer()->AddBillboardQuad(BillboardQuad(vbIdx, vb, billboardColor));
        }
    } else {
        Matrix4 xlateMatrix;
        xlateMatrix.elem[0][3] = centerPos.x;
        xlateMatrix.elem[1][3] = centerPos.y;
        xlateMatrix.elem[2][3] = centerPos.z;

        renderer->SetCurrentColor(billboardColor);

        SL_ASSERT(renderCamera&&overriddenBillboardMatrix);

        Matrix4 newModelView = renderCamera->GetModelViewMatrix() * (xlateMatrix * overriddenBillboardMatrix->GetBillboardMatrix());
        renderer->SetModelviewMatrix(newModelView);

        if (ib) {
            renderer->DrawStrip(vb->GetHandle(), ib->GetHandle(), vbIdx, 4, vb->GetNumVertices(), false, true);
        } else {
            renderer->DrawQuads(vb->GetHandle(), 4, vbIdx, false);
        }
        if (billboardShader == 0) {
            renderer->SetModelviewMatrix(renderCamera->GetModelViewMatrix());
        }
        SL_ASSERT(renderer->AreEqual(renderCamera));
    }

    return true;
}

bool Billboard::Draw(TextureHandle textureHandle, int pass, double fade, const Vector3& outputScale, bool objectSpace, unsigned long millis, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix
                     , ThreadCameraStreamData* tcsData, bool infiniteDepth) const
{
    if (!sharedVb || !textureHandle) return false;

    VertexBuffer *vb = sharedVb->GetVertexBuffer();
    IndexBuffer *ib = sharedVb->GetIndexBuffer();

    Renderer* renderer = tcsData->GetRenderer();

    // for debugging:
    // renderer->DisableBlending();

    renderer->EnableBackfaceCulling(false);

    renderer->EnableTexture(textureHandle, 0);

    const BillboardShader* billboardShader = tcsData->GetBillboardShader();

    bool useShader = billboardShader != 0;

    bool savedFog = false;

    if (useShader) {
        ScopedConstantPrep prep(renderer, billboardShader->GetShaderHandle(), false);
        BillboardShadingUtilities shadingUtils(billboardShader->GetShaderHandle(), billboardShader->GetConstantLocations(), tcsData);
        shadingUtils.BindAndSaveFogAndSetupConstants(fade, outputScale, objectSpace, millis, renderCamera, overriddenBillboardMatrix, savedFog, infiniteDepth);
        shadingUtils.SetShaderColor(billboardColor);
    } else {
        renderer->SetCurrentColor(billboardColor);

        Matrix4 xlateMatrix;
        xlateMatrix.elem[0][3] = centerPos.x;
        xlateMatrix.elem[1][3] = centerPos.y;
        xlateMatrix.elem[2][3] = centerPos.z;

        Matrix4 m = overriddenBillboardMatrix.isSet ? overriddenBillboardMatrix.billboardMatrix : overriddenBillboardMatrix.billboardMatrixOriginal;

        Matrix4 newModelView = renderCamera->GetModelViewMatrix() * (xlateMatrix * m);
        renderer->SetModelviewMatrix(xlateMatrix * m);
    }

    if (ib) {
        renderer->DrawStrip(vb->GetHandle(), ib->GetHandle(), vbIdx, 4, vb->GetNumVertices(),
                            useShader, true);
    } else {
        renderer->DrawQuads(vb->GetHandle(), 4, vbIdx, useShader);
    }

    if (useShader) {
        BillboardShadingUtilities shadingUtils(billboardShader->GetShaderHandle(), billboardShader->GetConstantLocations(), tcsData);
        // Restore does not work right for vulkan, because of the way recording works/we use host mapped pointer for the ubo updates
        // Alternatively, we could just set the color (forced or otherwise)
        if (renderer->GetType() != Atmosphere::VULKAN) {
            shadingUtils.UnsetShaderColor();
        }
        shadingUtils.UnbindAndRestoreFog(savedFog);
    }

    if (useShader == false) {
        renderer->SetModelviewMatrix(renderCamera->GetModelViewMatrix());
    }
    SL_ASSERT(renderer->AreEqual(renderCamera));

    renderer->DisableTexture(0);

    return true;
}

void Billboard::GetRandomRotateAngleAndSpeed(const RandomNumberGenerator* rng, const float spinRate, float& rotateAngle, float& rotateSpeed)
{
    rotateAngle = rng->UniformRandomFloat() * 2.0f * (float)MathUtils::Pi();
    rotateSpeed = rng->UniformRandomFloat() * spinRate * 2.0f - spinRate;
    if (rotateAngle == 0.0f) rotateAngle = MathUtilsFloat::Epsilon();
    if (rotateSpeed == 0.0f) rotateSpeed = MathUtilsFloat::Epsilon();
}
}
