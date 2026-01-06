// Copyright (c) 2013-2023 Sundog Software LLC. All rights reserved worldwide.

#include "CrepuscularRays.h"
#include "Renderer.h"
#include "Atmosphere.h"
#include "VertexBuffer.h"
#include "CloudLayer.h"
#include "MathUtils.h"
#include "Camera.h"
#include "OverriddenBillboardMatrix.h"
#include "OverriddenCameraPos.h"
#include "SLAssert.h"

using namespace SilverLining;

CrepuscularRaysShaderConstantLocations::CrepuscularRaysShaderConstantLocations()
{
    Reset();
}
void CrepuscularRaysShaderConstantLocations::Init(Renderer* renderer, ShaderHandle shader)
{
    Reset();

    sl_lightPositionOnScreen = renderer->GetConstantLocation(shader, "sl_lightPositionOnScreen");
    sl_parameters = renderer->GetConstantLocation(shader, "sl_parameters");
    sl_lightColor = renderer->GetConstantLocation(shader, "sl_lightColor");
    sl_outputScale = renderer->GetConstantLocation(shader, "sl_outputScale");
}
void CrepuscularRaysShaderConstantLocations::Reset(void)
{
    sl_lightPositionOnScreen = -1;
    sl_parameters = -1;
    sl_lightColor = -1;
    sl_outputScale = -1;
}

CrepuscularRays::CrepuscularRays(ThreadCameraStreamData* _tcsData) : cleared(false), effectScale(1.0f), isRendering(false), exposureScale(1.0)
    , tcsData(_tcsData)
{
    exposure = 0.0034f;
    decay = 1.0f;
    density = 0.84f;
    weight = 1.5f;

    Configuration::GetFloatValue("crepuscular-rays-exposure", exposure);
    Configuration::GetFloatValue("crepuscular-rays-decay", decay);
    Configuration::GetFloatValue("crepuscular-rays-density", density);
    Configuration::GetFloatValue("crepuscular-rays-weight", weight);

    texSize = 512;
    Configuration::GetIntValue("crepuscular-rays-texsize", texSize);

    renderTexture = 0;
    Renderer* renderer = tcsData->GetRenderer();

    bool enableObjectLabeling = false;
    Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);

    const char* textureName = (enableObjectLabeling) ? "SilverLining::CrepuscularRays::Texture" : (const char *)0;
    renderer->InitRenderTexture(texSize, texSize, &renderTexture, textureName);

    shader = 0;
    shader = renderer->LoadShaderFromFile("Shaders/VolumetricLighting.cg");

    if (renderer->SupportsConstantLocations()) {
        shaderConstantLocations.Init(renderer, shader);
    }

    bool DX = renderer->GetIsDirectX() || renderer->GetType()==Atmosphere::VULKAN;

    float effectDepth = 0.5;
    Configuration::GetFloatValue("crepuscular-rays-depth", effectDepth);
    float nearDepth, farDepth;
    renderer->GetDepthRange(nearDepth, farDepth);
    effectDepth = (1.0f - effectDepth) * nearDepth + effectDepth * farDepth;

    double cubeDimension = 1000.0;
    Configuration::GetDoubleValue("sky-box-size", cubeDimension);
    cubeDimension *= Atmosphere::GetUnitScale();
    sunDistance = cubeDimension * 0.5;

    const char* name = 0;

    BufferProperties bufferProperties;
    bufferProperties.genBuffer = true;
    bufferProperties.dynamic = false;

    name = (enableObjectLabeling) ? "CrepuscularRays::VertexBuffer" : (const char *)0;
    vertexBuffer = SL_NEW VertexBuffer(4, name, tcsData, bufferProperties);
    if (vertexBuffer && vertexBuffer->LockBuffer()) {
        Vertex *verts = vertexBuffer->GetVertices();
        if (verts) {
            Color white(1, 1, 1, 1);
            verts[0].SetUV(0, DX ? 1.0f : 0.0f);
            verts[0].x = -1;
            verts[0].y = -1;
            verts[0].z = effectDepth;
            verts[0].w = 1.0f;
            verts[0].SetColor(white);

            verts[1].SetUV(1, DX ? 1.0f : 0.0f);
            verts[1].x = 1;
            verts[1].y = -1;
            verts[1].z = effectDepth;
            verts[1].w = 1.0f;
            verts[1].SetColor(white);

            verts[2].SetUV(1, DX ? 0.0f : 1.0f);
            verts[2].x = 1, verts[2].y = 1;
            verts[2].z = effectDepth;
            verts[2].w = 1.0f;
            verts[3].SetColor(white);

            verts[3].SetUV(0, DX ? 0.0f : 1.0f);
            verts[3].x = -1, verts[3].y = 1;
            verts[3].z = effectDepth;
            verts[3].w = 1.0f;
            verts[3].SetColor(white);
        }
        vertexBuffer->UnlockBuffer();
    }

    bufferProperties.genBuffer = true;
    bufferProperties.dynamic = false;

    name = (enableObjectLabeling) ? "CrepuscularRays::IndexBuffer" : (const char *)0;
    indexBuffer = SL_NEW IndexBuffer(4, name, tcsData, bufferProperties);
    if (indexBuffer && indexBuffer->LockBuffer()) {
        Index *indices = indexBuffer->GetIndices();
        if (indices) {
            indices[0] = 3;
            indices[1] = 2;
            indices[2] = 0;
            indices[3] = 1;
        }
        indexBuffer->UnlockBuffer();
    }
}

CrepuscularRays::~CrepuscularRays()
{
    Renderer* renderer = tcsData->GetRenderer();
    if (renderTexture)
        renderer->ReleaseRenderTexture(renderTexture);

    if (vertexBuffer)
        SL_DELETE vertexBuffer;

    if (indexBuffer)
        SL_DELETE indexBuffer;
}

bool CrepuscularRays::DrawToRenderTexture(const Atmosphere& atmosphere, const Camera* sceneCamera, const Camera* renderCamera)
{
    if (effectScale == 0.0f || exposureScale <= 0.0f) return true;

    if (renderTexture) {
        Renderer* renderer = tcsData->GetRenderer();

        if (renderer->MakeRenderTextureCurrent(renderTexture, !cleared)) {
            int savedX, savedY, savedW, savedH;
            renderer->GetViewport(savedX, savedY, savedW, savedH);
            renderer->SetViewport(0, 0, texSize, texSize);
            if (!cleared) renderer->ClearScreen(Color(1, 1, 1, 1));
            isRendering = true; // Flag to turn off cirrus
            renderer->DrawBlendedObjects(false, false, &atmosphere, sceneCamera, renderCamera, OverriddenBillboardMatrix(sceneCamera), OverriddenCameraPos(sceneCamera), true, tcsData);
            renderer->BindRenderTexture(renderTexture, tcsData->GetCamera());
            isRendering = false;
            cleared = true;
            renderer->SetViewport(savedX, savedY, savedW, savedH);

            return true;
        }
    }
    return false;
}

Vector3 CrepuscularRays::ComputeSunPosClip(const Vector3& sunWorld, bool geocentricMode, bool& clipped, bool& behind, const Camera* camera, ThreadCameraStreamData* tcsData)
{
    Renderer* renderer = tcsData->GetRenderer();

    // Blow away any translation
    Matrix4 modelview = camera->GetModelViewMatrix();
    for (int row = 0; row < 3; row++) {
        modelview.elem[row][3] = 0;
    }
    modelview.elem[3][3] = 1;

    // Account for which way is up...
    if (!geocentricMode) modelview = modelview * camera->GetInverseBasis4x4();

    // Get position of sun in clip space
    Vector4 sunClip4 = (camera->GetProjectionMatrix()*modelview) * sunWorld;
    Vector3 sunClip = Vector3(sunClip4.x / sunClip4.w, sunClip4.y / sunClip4.w, sunClip4.z / sunClip4.w);

    behind = sunClip4.z <= 0 || sunClip4.z > sunClip4.w;

    clipped = (sunClip4.x <= -sunClip4.w || sunClip4.x > sunClip4.w ||
               sunClip4.y <= -sunClip4.w || sunClip4.y > sunClip4.w ||
               behind);

    return sunClip;
}

void CrepuscularRays::UpdateLightSource(const Ephemeris *ephemeris, const Color& sunColor, bool geocentricMode, double cloudCoverage, const Camera* camera, ThreadCameraStreamData* tcsData)
{
    lightColor = Vector3(sunColor.r, sunColor.g, sunColor.b);

    effectScale = 1.0;

    // Compute the location of the sun in world coordinates
    Vector3 sunPosHorizon = ephemeris->GetSunPositionHorizon();
    //double sunAltitude = MathUtils::ToDegrees(asin(sunPosHorizon.y));
    Vector3 sunPosGeo = ephemeris->GetSunPositionGeographic();
    Vector3 sunWorld = geocentricMode ? sunPosGeo : sunPosHorizon;
    sunWorld.Normalize();

    bool alwaysOn = false;
    Configuration::GetBoolValue("crepuscular-rays-always-on", alwaysOn);

    bool clipped = false;
    bool behind = false;
    sunPosClip = ComputeSunPosClip(sunWorld * sunDistance, geocentricMode, clipped, behind, camera, tcsData);

    float sunDistanceClip = 1.0;

    Renderer* renderer = tcsData->GetRenderer();

    if (!alwaysOn) {
        if (clipped) {
            // Sun is behind the camera
            effectScale = 0;
        }
        /*
        if (renderer->GetIsDirectX()|| renderer->GetType() == Atmosphere::VULKAN) {
        if (sunPosClip.z <= 0 || sunPosClip.z > 1.0)
        {
        // Sun is behind the camera
        effectScale = 0;
        }
        } else {
        if (sunPosClip.z > 0)
        {
        // Sun is behind the camera
        effectScale = 0;
        }
        }
        */
        sunPosClip.z = 0;
        sunPosTex = sunPosClip * 0.5 + 0.5;
        if (renderer->GetIsDirectX() || renderer->GetType() == Atmosphere::VULKAN) {
            sunPosTex.y = 1.0 - sunPosTex.y;
        }
    } else {
        if (behind) effectScale = 0;

        float rayPosX = 0.5f, rayPosY = 0.5f;
        if (Configuration::GetFloatValue("crepuscular-rays-sun-pos-x", rayPosX) &&
                Configuration::GetFloatValue("crepuscular-rays-sun-pos-y", rayPosY)) {
            sunPosTex.z = 0;
            sunPosTex.x = rayPosX;
            sunPosTex.y = rayPosY;
        } else {
            sunPosClip.z = 0;
            sunPosTex = sunPosClip * 0.5 + 0.5;

            if (renderer->GetIsDirectX() || renderer->GetType() == Atmosphere::VULKAN) {
                sunPosTex.y = 1.0 - sunPosTex.y;
            }
        }
    }

    sunDistanceClip = (float)sunPosClip.Length();

    // Fade out effect as sun goes out of the frame
    effectScale *= 1.0f - MathUtilsTemplate<float>::smoothstep(1.5f, 2.5f, sunDistanceClip);

    // Fade out effect as occluders thin out
    effectScale *= (float)cloudCoverage;
}

void * CrepuscularRays::GetNativeCloudTexture(ThreadCameraStreamData* tcsData)
{
    Renderer* renderer = tcsData->GetRenderer();
    if (renderTexture) {
        TextureHandle tex;
        renderer->GetRenderTextureTextureHandle(renderTexture, &tex);
        return renderer->GetNativeTexture(tex);
    }
    return 0;
}

bool CrepuscularRays::DrawRays(const Vector3& outputScale, ThreadCameraStreamData* tcsData)
{
    cleared = false;

    if (effectScale == 0.0f || exposureScale <= 0.0f) return true;

    Renderer* renderer = tcsData->GetRenderer();

    if (renderTexture && shader && vertexBuffer && indexBuffer) {
        //ren->PushAllState();
        renderer->EnableTexture2D(true);
        renderer->EnableLighting(false);
        renderer->EnableBackfaceCulling(false);
        renderer->EnableBlending(SRCALPHA, ONE);
        // ren->DisableBlending();
        TextureHandle tex;
        renderer->GetRenderTextureTextureHandle(renderTexture, &tex);
        renderer->EnableTexture(tex, 0);
        renderer->EnableDepthReads(true);
        renderer->EnableDepthWrites(false);
        renderer->BindShader(shader);

        if (renderer->SupportsConstantLocations()) {
            renderer->SetConstantVectorAtLocation(shader, shaderConstantLocations.sl_outputScale, outputScale);
            renderer->SetConstantVectorAtLocation(shader, shaderConstantLocations.sl_lightPositionOnScreen, sunPosTex);
            renderer->SetConstantVector4AtLocation(shader, shaderConstantLocations.sl_parameters, Vector4(exposure * effectScale * exposureScale, decay, density, weight));
            renderer->SetConstantVectorAtLocation(shader, shaderConstantLocations.sl_lightColor, lightColor);
        } else {
            renderer->SetConstantVector(shader, "sl_outputScale", outputScale);
            renderer->SetConstantVector(shader, "sl_lightPositionOnScreen", sunPosTex);
            renderer->SetConstantVector4(shader, "sl_parameters", Vector4(exposure * effectScale * exposureScale, decay, density, weight));
            renderer->SetConstantVector(shader, "sl_lightColor", lightColor);
        }

        renderer->DrawStrip(vertexBuffer->GetHandle(), indexBuffer->GetHandle(), 0, 4, 4, false, true);

        renderer->UnbindShader();

        renderer->DisableTexture(0);

        renderer->EnableDepthWrites(true);

        // Restore a more reasonable blend function and disable blending.
        renderer->EnableBlending(SRCALPHA, INVSRCALPHA);
        renderer->DisableBlending();

        //ren->PopAllState();

        return true;

    }

    return false;
}
