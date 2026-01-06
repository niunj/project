// Copyright (c) 2013-2017 Sundog Software, LLC. All rights reserved worldwide.

#include "ShadowMap.h"
#include "Atmosphere.h"
#include "Billboard.h"
#include "CloudLayer.h"
#include "Renderer.h"
#include "Configuration.h"
#include "Camera.h"
#include "OverriddenBillboardMatrix.h"
#include "BillboardRenderingParameters.h"
#include "OverriddenCameraPos.h"
#include "CloudLayerSorter.h"
#include "SLAssert.h"

namespace SilverLining
{
ShadowMap::ShadowMap(const Atmosphere& _atmosphere, int texDim, ThreadCameraStreamData* _tcsData) : atmosphere(_atmosphere), texSize(texDim), cleared(false), hasShadows(false), isRendering(false), shadowBlend(1.0f)
    , tcsData(_tcsData)
{
    texture = 0;

    Renderer* renderer = tcsData->GetRenderer();

    bool enableObjectLabeling = false;
    Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
    const char* name = (enableObjectLabeling) ? "SilverLining::ShadowMap::Texture" : (const char *)0;

    renderer->InitRenderTexture(texDim, texDim, &texture, name);

    shadowCamera = SL_NEW Camera(renderer->GetIsRightHanded());

    sorter = SL_NEW CloudLayerSorter();

    //justReset = false;
}

ShadowMap::~ShadowMap()
{
    if (texture) {
        Renderer* renderer = tcsData->GetRenderer();
        renderer->ReleaseRenderTexture(texture);
    }

    SL_DELETE sorter;
    SL_DELETE shadowCamera;
}

const Camera* ShadowMap::GetShadowCamera(void) const
{
    return shadowCamera;
}

void ShadowMap::SortClouds(const Camera* camera, bool enforceStrictWeak, ThreadCameraStreamData* tcsData)
{
    sorter->Sort(camera, enforceStrictWeak, tcsData);
}

const SL_VECTOR(CloudLayer*)& ShadowMap::GetSortedCloudLayers(void) const
{
    return sorter->GetSortedCloudLayers();
}

bool ShadowMap::UpdateShadowCamera(const Vector3& lightDirection, bool wholeLayers, double maxShadowDistance, const Camera* camera, ThreadCameraStreamData* tcsData)
{
    // Prime with incoming cloud layers
    sorter->Prime(atmosphere.GetConditions().GetCloudLayers(), tcsData);

    // get the (unsorted) cloud layer vector
    const SL_VECTOR(CloudLayer *)& cloudLayers = sorter->GetSortedCloudLayers();

    UpdateFrustumWorldSpace(cloudLayers, wholeLayers, maxShadowDistance, camera, tcsData);
    UpdateLightMatricesAndThenShadowCamera(lightDirection, wholeLayers, camera, tcsData);
    cleared = false;

    return false;
}

bool ShadowMap::StartDrawing(const Color& shadowColor, bool moonShadows, double shadowFactor, const Camera* camera, ThreadCameraStreamData* tcsData)
{
    Color fadedColor = shadowColor;

    Renderer* renderer = tcsData->GetRenderer();

    float sunx, suny, sunz;
    if (moonShadows) {
        atmosphere.GetSunOrMoonPosition(&sunx, &suny, &sunz, tcsData);
    } else {
        atmosphere.GetSunPosition(&sunx, &suny, &sunz, tcsData);
    }

    Vector3 sunPos(sunx, suny, sunz);
    sunPos = sunPos * camera->GetInverseBasis3x3();
    float blendCutoff = 0.1f;
    Configuration::GetFloatValue("shadow-sun-height-cutoff", blendCutoff);
    shadowBlend = 1.0f;
    if (sunPos.y < blendCutoff) {
        shadowBlend = ((float)(sunPos.y) / blendCutoff);
        if (shadowBlend < 0.0f) shadowBlend = 0.0f;
    }
    shadowBlend *= (float)shadowFactor;

    Color white(1, 1, 1, 1);

    fadedColor = shadowColor * shadowBlend + white * (1.0f - shadowBlend);
    fadedColor.a = 1.0f;

    BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();

    billboardParams->ForceConstantColor(true, fadedColor);

    if (texture) {

        renderer->GetViewport(savedX, savedY, savedW, savedH);
        renderer->SetViewport(0, 0, texSize, texSize);

        BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();

        savedDensity = billboardParams->GetFogDensity();
        billboardParams->SetFogDensity(1E-9);
        billboardParams->SetSpinEnabled(false);

        if (renderer->MakeRenderTextureCurrent(texture, false)) {
            renderer->ClearScreen(Color(1, 1, 1, 1));
            renderer->SortBlendedObjects(&atmosphere, camera, shadowCamera, OverriddenCameraPos(lightWorldPos), tcsData);
            return true;
        }
    }

    return false;
}

bool ShadowMap::EndDrawing(ThreadCameraStreamData* tcsData)
{
    BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();

    billboardParams->ForceConstantColor(false, Color(1, 1, 1, 1));

    if (texture) {
        Renderer* renderer = tcsData->GetRenderer();
        renderer->SetViewport(savedX, savedY, savedW, savedH);

        BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();

        billboardParams->SetFogDensity(savedDensity);
        billboardParams->SetSpinEnabled(true);

        return renderer->BindRenderTexture(texture, tcsData->GetCamera());
    }

    return false;
}

bool ShadowMap::DrawToRenderTexture(const Camera* sceneCamera, const Camera* renderCamera, bool fadeOverride
                                    , ThreadCameraStreamData* tcsData)
{
    Renderer* renderer = tcsData->GetRenderer();
    if (texture) {
        if (hasShadows && shadowBlend > 0) {
            isRendering = true;
            renderer->DrawBlendedObjects(false, false, &atmosphere, sceneCamera, renderCamera, OverriddenBillboardMatrix(billboard), OverriddenCameraPos(lightWorldPos), fadeOverride, tcsData);
            isRendering = false;
        }

        return true;
    }
    return false;
}

TextureHandle ShadowMap::GetTextureHandle(ThreadCameraStreamData* tcsData)
{
    TextureHandle texHandle = 0;
    Renderer* renderer = tcsData->GetRenderer();
    renderer->GetRenderTextureTextureHandle(texture, &texHandle);
    return texHandle;
}

void ShadowMap::UpdateFrustumWorldSpace(const SL_VECTOR(CloudLayer *)& cloudLayers, bool wholeLayers, double maxShadowDistance, const Camera* camera, ThreadCameraStreamData* tcsData)
{
    hasShadows = false;

    if (cloudLayers.size() == 0) return;

    // Compute camera view frustum corners in world space
    Matrix4 mvp = camera->GetModelViewProjectionMatrix();
    Matrix4 invMvp = mvp.Inverse();

    const Renderer* renderer = tcsData->GetRenderer();
    float nearZ, farZ, zInc;
    camera->GetNDCDepthRange(nearZ, farZ, renderer);

    zInc = farZ - nearZ;

    Vector3 *worldCorners = SL_NEW Vector3[8 + cloudLayers.size() * 8];

    int numCorners = 0;

    if (!wholeLayers) {
        for (float x = -1.0; x <= 1.0; x += 2.0) {
            for (float y = -1.0; y <= 1.0; y += 2.0) {
                for (float z = nearZ; z <= farZ; z += zInc) {
                    Vector3 NDC(x, y, z);
                    Vector3 world = invMvp * NDC;

                    if (maxShadowDistance > 0) {
                        Vector3 cornerFromCamera = world - camera->GetPosition();
                        double dist = cornerFromCamera.SquaredLength();
                        if (dist > maxShadowDistance * maxShadowDistance) {
                            Vector3 toCornerDir = cornerFromCamera;
                            toCornerDir.Normalize();
                            world = camera->GetPosition() + (toCornerDir * maxShadowDistance);
                        }
                    }

                    worldCorners[numCorners] = world;
                    numCorners++;
                }
            }
        }
    }

    bool cullShadowMapLayers = true;
    Configuration::GetBoolValue("cull-shadow-map-layers", cullShadowMapLayers);

    SL_VECTOR(CloudLayer *)::const_iterator it;
    for (it = cloudLayers.begin(); it != cloudLayers.end(); it++) {
        CloudLayer *layer = *it;

        if (layer->GetEnabled() == false) continue;

        if (!wholeLayers && layer->GetType() != STRATOCUMULUS) continue;
        if (layer->GetType() == CIRROCUMULUS ||
                layer->GetType() == CIRRUS_FIBRATUS) continue;

        if (cullShadowMapLayers) {
            if (layer->GetCulled(&atmosphere, camera, camera, tcsData)) continue;
        }

        Vector3 bounds[8];

        /*
        if (justReset) {
            justReset = false;
            for (int i = 0; i < 8; ++i) {
                bounds[i] = Vector3(0, 0, 0);
            }
        } else {
            layer->GetBounds(bounds, atmosphere, camera);
        }
        */

        layer->GetBounds(bounds, camera, tcsData);

        for (int i = 0; i < 8; i++) {
            Vector3 corner = bounds[i];

            if (maxShadowDistance > 0) {
                Vector3 cornerFromCamera = corner - camera->GetPosition();
                double dist = cornerFromCamera.SquaredLength();
                if (dist > maxShadowDistance * maxShadowDistance) {
                    Vector3 toCornerDir = cornerFromCamera;
                    toCornerDir.Normalize();
                    corner = camera->GetPosition() + (toCornerDir * maxShadowDistance);
                }
            }

            worldCorners[numCorners] = corner;
            numCorners++;
        }
    }

    Vector3 min, max;
    for (int corner = 0; corner < numCorners; corner++) {
        Vector3 & layerCorner = worldCorners[corner];
        if (corner == 0) {
            min = layerCorner;
            max = layerCorner;
        } else {
            if (layerCorner.x < min.x) min.x = layerCorner.x;
            if (layerCorner.y < min.y) min.y = layerCorner.y;
            if (layerCorner.z < min.z) min.z = layerCorner.z;
            if (layerCorner.x > max.x) max.x = layerCorner.x;
            if (layerCorner.y > max.y) max.y = layerCorner.y;
            if (layerCorner.z > max.z) max.z = layerCorner.z;
        }
    }

    Vector3 total(0, 0, 0);
    frustumCornersWorld[0] = Vector3(min.x, min.y, max.z);
    frustumCornersWorld[1] = Vector3(min.x, max.y, max.z);
    frustumCornersWorld[2] = Vector3(max.x, min.y, max.z);
    frustumCornersWorld[3] = Vector3(max.x, max.y, max.z);
    frustumCornersWorld[4] = Vector3(min.x, min.y, min.z);
    frustumCornersWorld[5] = Vector3(min.x, max.y, min.z);
    frustumCornersWorld[6] = Vector3(max.x, min.y, min.z);
    frustumCornersWorld[7] = Vector3(max.x, max.y, min.z);
    for (int i = 0; i < 8; i++) {
        total = total + frustumCornersWorld[i];
    }
    frustumCentroidWorld = total * (1.0 / 8.0);

    double maxDist = 0;
    for (int corner = 0; corner < 8; corner++) {
        double dist = (frustumCornersWorld[corner] - frustumCentroidWorld).Length();
        if (dist > maxDist) {
            maxDist = dist;
        }
    }

    SL_DELETE[] worldCorners;

    frustumBoundRadius = maxDist;

    hasShadows = true;
}

// helper
static void GetFrustumBounds(Vector3&  min, Vector3& max, const Matrix4& xform, Vector3 frustumCornersWorld[8])
{
    for (int corner = 0; corner < 8; corner++) {
        Vector3 pos = xform * frustumCornersWorld[corner];
        if (corner == 0) {
            min.x = max.x = pos.x;
            min.y = max.y = pos.y;
            min.z = max.z = pos.z;
        } else {
            if (pos.x < min.x) min.x = pos.x;
            if (pos.x > max.x) max.x = pos.x;
            if (pos.y < min.y) min.y = pos.y;
            if (pos.y > max.y) max.y = pos.y;
            if (pos.z < min.z) min.z = pos.z;
            if (pos.z > max.z) max.z = pos.z;
        }
    }
}

// helper
static Matrix4 GetOrthoMatrix(double left, double right, double top, double bottom, double pnear, double pfar, bool isRightHanded, const Renderer *renderer)
{
    Matrix4 m;

    if (!renderer->GetIsOpenGL()) {
        if (isRightHanded) {
            m.elem[0][0] = 2.0 / (right - left);
            m.elem[0][3] = (left + right) / (left - right);
            m.elem[1][1] = 2.0 / (top - bottom);
            m.elem[1][3] = (top + bottom) / (bottom - top);
            m.elem[2][2] = 1.0 / (pnear - pfar);
            m.elem[2][3] = pnear / (pnear - pfar);
        } else {
            m.elem[0][0] = 2.0 / (right - left);
            m.elem[0][3] = (left + right) / (left - right);
            m.elem[1][1] = 2.0 / (top - bottom);
            m.elem[1][3] = (top + bottom) / (bottom - top);
            m.elem[2][2] = 1.0 / (pfar - pnear);
            m.elem[2][3] = pnear / (pnear - pfar);
        }
    } else {
        if (isRightHanded) {
            m.elem[0][0] = 2.0 / (right - left);
            m.elem[0][3] = -((right + left) / (right - left));

            m.elem[1][1] = 2.0 / (top - bottom);
            m.elem[1][3] = -((top + bottom) / (top - bottom));

            m.elem[2][2] = -2.0 / (pfar - pnear);
            m.elem[2][3] = -((pfar + pnear) / (pfar - pnear));
        } else {
            m.elem[0][0] = 2.0 / (right - left);
            m.elem[1][1] = 2.0 / (top - bottom);
            m.elem[2][2] = 1.0 / (pfar - pnear);
            m.elem[2][3] = pnear / (pnear - pfar);
        }
    }

    return m;
}

void ShadowMap::UpdateLightMatricesAndThenShadowCamera(const Vector3& lightDir, bool wholeLayers, const Camera* camera, ThreadCameraStreamData* tcsData)
{
    // Place the light eye position
    Vector3 lightDirWorld = lightDir * tcsData->GetCamera()->GetBasis3x3();
    Vector3 lightPos = frustumCentroidWorld + (lightDirWorld * frustumBoundRadius);

    lightWorldPos = lightPos;

    Vector3 view;

    // Create a look-at matrix toward the center of the camera's frustum
    if (tcsData->GetCamera()->IsRightHanded())
        view = lightPos - frustumCentroidWorld;
    else
        view = frustumCentroidWorld - lightPos;

    view.Normalize();

    Vector3 worldUp = tcsData->GetCamera()->GetUpVector();
    Vector3 vright = worldUp.Cross(view);
    vright.Normalize();

    Vector3 vup = view.Cross(vright);

    Matrix4 lightView;
    lightView.elem[0][0] = vright.x;
    lightView.elem[0][1] = vright.y;
    lightView.elem[0][2] = vright.z;
    lightView.elem[0][3] = -vright.Dot(lightPos);
    lightView.elem[1][0] = vup.x;
    lightView.elem[1][1] = vup.y;
    lightView.elem[1][2] = vup.z;
    lightView.elem[1][3] = -vup.Dot(lightPos);
    lightView.elem[2][0] = view.x;
    lightView.elem[2][1] = view.y;
    lightView.elem[2][2] = view.z;
    lightView.elem[2][3] = -view.Dot(lightPos);

    // Compute billboard matrix for this view
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            billboard.elem[row][col] = lightView.elem[col][row];
        }
    }

    if (!tcsData->GetCamera()->IsRightHanded()) {
        billboard.elem[2][0] *= -1;
        billboard.elem[2][1] *= -1;
        billboard.elem[2][2] *= -1;
    }

    double pnear = -frustumBoundRadius * 2, pfar = frustumBoundRadius * 2;

    Vector3 min, max;
    GetFrustumBounds(min, max, lightView, frustumCornersWorld);
    double left = min.x;
    double right = max.x;
    double bottom = min.y;
    double top = max.y;

    Matrix4 lightProj;
    // Create ortho proj matrix encompassing the frustum
    {
        Matrix4 m = GetOrthoMatrix(left, right, top, bottom, pnear, pfar, camera->IsRightHanded(), tcsData->GetRenderer());
        lightProj = m;
    }

    shadowCamera->SetUpVector(camera->GetUpVector());
    shadowCamera->SetRightVector(camera->GetRightVector());
    shadowCamera->SetModelViewMatrix(lightView);
    shadowCamera->SetProjectionMatrix(lightProj);
}

const Vector3& ShadowMap::GetLightWorldPos(void) const
{
    return lightWorldPos;
}
}
