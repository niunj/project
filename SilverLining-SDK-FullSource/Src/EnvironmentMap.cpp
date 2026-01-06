// Copyright (c) 2013-2015 Sundog Software LLC. All rights reserved worldwide.

#include "EnvironmentMap.h"
#include "Atmosphere.h"
#include "Configuration.h"
#include "Renderer.h"
#include "MathUtils.h"
#include "Camera.h"
#include "SLAssert.h"

using namespace SilverLining;

EnvironmentMap::EnvironmentMap(ThreadCameraStreamData* _tcsData, ColorFormat format) : lastFaceRendered(0), floatingPoint(false)
    , tcsData(_tcsData)
{
    cubeMapDim = 256;
    Configuration::GetIntValue("environment-map-size", cubeMapDim);

    enableStars = false;
    Configuration::GetBoolValue("environment-map-enable-stars", enableStars);

    neverDrawClouds = false;
    Configuration::GetBoolValue("environment-map-never-draw-clouds", neverDrawClouds);

    generateMipmaps = false;
    Configuration::GetBoolValue("environment-map-generate-mipmaps", generateMipmaps);

    isDXAndFlipCubeFaces = false;
    if (tcsData->GetRenderer()->GetIsDirectX()) {
        Configuration::GetBoolValue("flip-dx-cube-map-faces", isDXAndFlipCubeFaces);
    }

    cubeMap = 0;
    if (format!=FORMAT_UNKNOWN) {
        floatingPoint = (format == FORMAT_R16G16B16A16_SFLOAT);
        tcsData->GetRenderer()->InitRenderTextureCube(cubeMapDim, cubeMapDim, &cubeMap, floatingPoint, generateMipmaps, name.c_str());
    }
}

EnvironmentMap::~EnvironmentMap()
{
    if (cubeMap) {
        Renderer* renderer = tcsData->GetRenderer();
        renderer->ReleaseRenderTextureCube(cubeMap);
    }
}

void EnvironmentMap::GetToAndUpVector(Vector3& to, Vector3& up, const Vector3& from, int _face) const
{
    CubeFace face = (CubeFace)_face;

    const double offset = 100000.0;

    switch (face) {
    case POSX:
        to = from + Vector3(offset, 0, 0);
        up = isDXAndFlipCubeFaces ? Vector3(0, 1, 0) : Vector3(0, -1, 0);
        break;

    case NEGX:
        to = from + Vector3(-offset, 0, 0);
        up = isDXAndFlipCubeFaces ? Vector3(0, 1, 0) : Vector3(0, -1, 0);
        break;

    case POSY:
        to = from + Vector3(0, offset, 0);
        up = isDXAndFlipCubeFaces ? Vector3(0, 0, -1) : Vector3(0, 0, 1);
        break;

    case NEGY:
        to = from + Vector3(0, -offset, 0);
        up = isDXAndFlipCubeFaces ? Vector3(0, 0, 1) : Vector3(0, 0, -1);
        break;

    case POSZ:
        to = from + Vector3(0, 0, offset);
        up = isDXAndFlipCubeFaces ? Vector3(0, 1, 0) : Vector3(0, -1, 0);
        break;

    case NEGZ:
        to = from + Vector3(0, 0, -offset);
        up = isDXAndFlipCubeFaces ? Vector3(0, 1, 0) : Vector3(0, -1, 0);
        break;
    default:
        std::cout << __FUNCTION__ << ": unknown face!!" << std::endl;
    }
}

bool EnvironmentMap::Create(int facesToRender, bool pFloatingPoint, bool drawClouds, bool drawSunAndMoon, bool geocentricMode, Atmosphere& atmosphere)
{
    Renderer* renderer = tcsData->GetRenderer();

    if (cubeMap && floatingPoint != pFloatingPoint) {
        renderer->ReleaseRenderTextureCube(cubeMap);
        cubeMap = 0;
    }

    floatingPoint = pFloatingPoint;

    if (cubeMap == 0) {
        renderer->InitRenderTextureCube(cubeMapDim, cubeMapDim, &cubeMap, pFloatingPoint, generateMipmaps, name.c_str());
    }

    if (cubeMap) {

        tcsData->SetIsDrawingEnvMap(true);

        Camera* camera = tcsData->GetCamera();

        const SilverLining::Matrix4 savedModelViewMatrix = camera->GetModelViewMatrix();
        const SilverLining::Matrix4 savedProjectionMatrix = camera->GetProjectionMatrix();
        const Matrix4 savedBillboardMatrix = camera->GetBillboardMatrix();

        int savedX, savedY, savedW, savedH;

        bool hasCameraViewPort = true;
        if (!camera->GetViewport(savedX, savedY, savedW, savedH)) {
            renderer->GetViewport(savedX, savedY, savedW, savedH);
            hasCameraViewPort = false;
        }

        float nearDepth, farDepth;
        renderer->GetDepthRange(nearDepth, farDepth);
        renderer->SetDepthRange(0.0f, 1.0f);

        Vector3 from = camera->GetPosition();

        double znear, zfar;
        camera->GetNearFarClip(znear, zfar);
        if (znear < 0) {
            znear = 10.0;
            zfar = 100000.0;
        }

        Camera faceCam(camera, false, true);
        faceCam.SetProjectionMatrix(znear, zfar, MathUtils::PiByTwo(), MathUtils::PiByTwo(), 0.0, 1.0, renderer);
        renderer->SetProjectionMatrix(faceCam.GetProjectionMatrix());

        int faceNum = lastFaceRendered;
        for (int face = 0; face < facesToRender; face++) {
            CubeFace cubeFace = (CubeFace)faceNum;
            lastFaceRendered = faceNum;

            if (renderer->MakeRenderTextureCubeCurrent(cubeMap, true, cubeFace)) {
                if (hasCameraViewPort) {
                    camera->SetViewport(0, 0, cubeMapDim, cubeMapDim);
                } else {
                    renderer->SetViewport(0, 0, cubeMapDim, cubeMapDim);
                }

                Vector3 to, up;
                GetToAndUpVector(to, up, from, cubeFace);

                faceCam.SetModelviewLookat(from, to, up);
                renderer->SetModelviewMatrix(faceCam.GetModelViewMatrix());

                camera->SetModelViewMatrix(faceCam.GetModelViewMatrix());
                camera->SetProjectionMatrix(faceCam.GetProjectionMatrix());

                atmosphere.DrawSky(true, geocentricMode, 0.0, enableStars, true, drawSunAndMoon
                                   , 0, 0, 0, tcsData);

                atmosphere.DrawObjects(drawClouds && !neverDrawClouds, false
                                       , true, 0.0, false, 0, true, true, true, false, tcsData);

                renderer->BindRenderTextureCube(cubeMap);
            }

            faceNum++;
            if (faceNum > 5) faceNum = 0;
        }

        renderer->SetDepthRange(nearDepth, farDepth);

        if (hasCameraViewPort) {
            atmosphere.SetViewport(savedX, savedY, savedW, savedH);
        } else {
            renderer->SetViewport(savedX, savedY, savedW, savedH);
        }

        // restore the camera matrices which we modified in the loop above
        camera->SetModelViewMatrix(savedModelViewMatrix);
        camera->SetProjectionMatrix(savedProjectionMatrix);

        // restore the renderer matrices which we modified in the loop above
        renderer->SetModelviewMatrix(savedModelViewMatrix);
        renderer->SetProjectionMatrix(savedProjectionMatrix);

        renderer->RecomputeFOV();

        tcsData->SetIsDrawingEnvMap(false);
        return true;
    }

    return false;
}

void* EnvironmentMap::GetNativeTexture() const
{
    if (cubeMap) {
        TextureHandle texture;
        const Renderer* renderer = tcsData->GetRenderer();
        renderer->GetRenderTextureCubeTextureHandle(cubeMap, &texture);
        return renderer->GetNativeTexture(texture);
    }

    return 0;
}

