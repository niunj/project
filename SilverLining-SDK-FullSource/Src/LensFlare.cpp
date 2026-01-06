// Copyright (c) 2010-2023 Software, LLC All rights reserved worldwide.

#include "LensFlare.h"
#include "Ephemeris.h"
#include "Billboard.h"
#include "Renderer.h"
#include "Configuration.h"
#include "MathUtils.h"
#include "Camera.h"
#include "Atmosphere.h"
#include "FlareShader.h"
#include "ThreadCameraStreamData.h"
#include "BillboardShader.h"
#include "BillboardBuffer.h"
#include "SLAssert.h"

using namespace SilverLining;
using namespace std;

//#define TIMING

LensFlare::LensFlare(const Atmosphere& _atmosphere, const Ephemeris *pEphemeris, const RandomNumberGenerator* rng, ThreadCameraStreamData* _tcsData)
    : atmosphere(_atmosphere)
    , tcsData(_tcsData)
{
    queryHandle = 0;

    occlusion = 0;

    alwaysOn = false;
    Configuration::GetBoolValue("lens-flare-always-on", alwaysOn);

    ephemeris = pEphemeris;

    disableOcclusion = false;
    Configuration::GetBoolValue("lens-flare-disable-occlusion", disableOcclusion);

    usePBO = true;
    Configuration::GetBoolValue("lens-flare-use-pbo", usePBO);

    Configuration::GetDoubleValue("sun-width-degrees", sunWidth);

    flareSize = 0.2f;
    Configuration::GetFloatValue("lens-flare-size", flareSize);

    lensFlareDisabled = false;
    Configuration::GetBoolValue("disable-lens-flare", lensFlareDisabled);

    flareFalloff = 4.0f;
    Configuration::GetFloatValue("lens-flare-falloff", flareFalloff);

    flareBrightness = 1.0f;
    Configuration::GetFloatValue("lens-flare-brightness", flareBrightness);

    doShines = true;
    Configuration::GetBoolValue("lens-flare-shine", doShines);

    double cubeDimension = 1000.0;
    Configuration::GetDoubleValue("sky-box-size", cubeDimension);
    cubeDimension *= Atmosphere::GetUnitScale();
    sunDistance = cubeDimension * 0.5;

    sunQuad = SL_NEW Billboard();

    const bool billboardsUseShaderInstancing = tcsData->GetBillboardShader()->GetShaderHandle() != 0;

    sunQuad->Initialize(MathUtilsFloat::Epsilon(), MathUtilsFloat::Epsilon(), 0, billboardsUseShaderInstancing, tcsData->GetBillboardBufferProvider()->GetABillboardBuffer());

    sunQuad->SetColor(Color(0, 0, 0));

    for (int i = 0; i < NUM_FLARE_TEXTURES; i++) {
        flareTextures[i] = 0;
    }

    for (int j = 0; j < NUM_SHINE_TEXTURES; j++) {
        shineTextures[j] = 0;
    }

    sunTexture = 0;
    ib = 0;
    vb = 0;

    InitFlares();
}

LensFlare::~LensFlare()
{
    Renderer* renderer = tcsData->GetRenderer();
    for (int i = 0; i < NUM_FLARE_TEXTURES; i++) {
        if (flareTextures[i]) {
            renderer->ReleaseTexture(flareTextures[i]);
        }
    }

    for (int j = 0; j < NUM_SHINE_TEXTURES; j++) {
        if (shineTextures[j]) {
            renderer->ReleaseTexture(shineTextures[j]);
        }
    }

    if (sunQuad) {
        SL_DELETE sunQuad;
    }

    if (sunTexture) {
        renderer->ReleaseTexture(sunTexture);
    }

    if (ib) {
        SL_DELETE ib;
    }

    if (vb) {
        SL_DELETE vb;
    }
}

void LensFlare::DeviceLost()
{
    if (queryHandle) {
        Renderer* renderer = tcsData->GetRenderer();
        renderer->GetOcclusionQueryResults(queryHandle);
        queryHandle = 0;
    }
}

bool LensFlare::InitFlares()
{
    bool useHexFlares = true;
    Configuration::GetBoolValue("use-hex-flares", useHexFlares);
    char buf[1024];

    Renderer* renderer = tcsData->GetRenderer();
    if (renderer) {

        bool enableObjectLabeling = false;
        Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
        const char* name = 0;

        name = (enableObjectLabeling) ? "SilverLining::lensflare/black.tga" : (const char *)0;
        renderer->LoadTextureFromFile("lensflare/black.tga", &sunTexture, false, false, name);

        int i;

        if (useHexFlares) {
            name = (enableObjectLabeling) ? "SilverLining::lensflare/hex.tga" : (const char *)0;
            renderer->LoadTextureFromFile("lensflare/hex.tga", &flareTextures[0], true, true, name);
        } else {
            name = (enableObjectLabeling) ? "SilverLining::Flare Texture" : (const char *)0;
            for (i = 0; i < NUM_FLARE_TEXTURES; i++) {
                sprintf(buf, "lensflare/Flare%d.tga", i+1);
                renderer->LoadTextureFromFile(buf, &flareTextures[i], true, true, name);
            }
        }

        name = (enableObjectLabeling) ? "SilverLining::Shine Texture" : (const char *)0;
        for (i = 0; i < NUM_SHINE_TEXTURES; i++) {
            sprintf(buf, "lensflare/Shine%d.tga", i+1);
            renderer->LoadTextureFromFile(buf, &shineTextures[i], true, true, name);
        }

        BufferProperties bufferProperties;
        bufferProperties.genBuffer = true;
        bufferProperties.dynamic = false;

        name = (enableObjectLabeling) ? "LensFlare::IndexBuffer" : (const char *)0;
        ib = SL_NEW IndexBuffer(4, name, tcsData, bufferProperties);
        if (ib->LockBuffer()) {
            Index *idx = ib->GetIndices();
            idx[0] = 0;
            idx[1] = 1;
            idx[2] = 2;
            idx[3] = 3;
            ib->UnlockBuffer();
        }

        float zmin, zmax;
        renderer->GetDepthRange(zmin, zmax);



        name = (enableObjectLabeling) ? "LensFlare::VertexBuffer" : (const char *)0;
        vb = SL_NEW VertexBuffer(4, name, tcsData, bufferProperties);
        if (vb->LockBuffer()) {
            Color white(1, 1, 1, 1);

            Vertex *v = vb->GetVertices();
            v[0].x = -1;
            v[0].y = -1;
            v[0].z = zmin;
            v[0].w = 1.0f;
            v[0].SetUV(0, 0);
            v[0].SetColor(white);

            v[1].x = -1;
            v[1].y = 1;
            v[1].z = zmin;
            v[1].w = 1.0f;
            v[1].SetUV(0, 1);
            v[1].SetColor(white);

            v[2].x = 1;
            v[2].y = -1;
            v[2].z = zmin;
            v[2].w = 1.0f;
            v[2].SetUV(1, 0);
            v[2].SetColor(white);

            v[3].x = 1;
            v[3].y = 1;
            v[3].z = zmin;
            v[3].w = 1.0f;
            v[3].SetUV(1, 1);
            v[3].SetColor(white);

            vb->UnlockBuffer();
        }
    }

    Color red(1.0, 0.0, 0.0);
    Color green(0.0, 1.0, 0.0);
    Color blue(0.0, 0.0, 1.0);
    Color darkGreen(136.0 / 255.0, 165.0 / 255.0, 22.0 / 255.0);
    Color brown(174.0 / 255.0, 87.0 / 255.0, 0.0);
    Color greenBlue(0.0, 55.0 / 255.0, 111.0 / 255.0);

    if (doShines) {
        flares.push_back(Flare(-1, 1.0f, 0.3f, blue));
        flares.push_back(Flare(-1, 1.0f, 0.2f, green));
        flares.push_back(Flare(-1, 1.0f, 0.25f, red));
    }

    if (useHexFlares) {
        flares.push_back(Flare(0, 1.0f, 0.5f, red));
        flares.push_back(Flare(0, 0.75f, 0.1f, greenBlue));
        flares.push_back(Flare(0, 0.7f, 0.1f, greenBlue));
        flares.push_back(Flare(0, 0.5f, 0.2f, greenBlue));
        flares.push_back(Flare(0, 0.4f, 0.25f, greenBlue));
        flares.push_back(Flare(0, 0.2f, 0.3f, greenBlue));
        flares.push_back(Flare(0, 0.1f, 0.3f, greenBlue));
        flares.push_back(Flare(0, 0.0f, 0.2f, brown));
        flares.push_back(Flare(0, 0.1f, 0.25f, brown));
        flares.push_back(Flare(0, -0.25f, 0.3f, brown));
        flares.push_back(Flare(0, -0.4f, 0.2f, brown));
        flares.push_back(Flare(0, -0.6f, 0.25f, darkGreen));
        flares.push_back(Flare(0, -1.f, 0.3f, darkGreen));
        flares.push_back(Flare(0, -1.3f, 0.4f, darkGreen));
        flares.push_back(Flare(0, -1.6f, 0.7f, darkGreen));
        flares.push_back(Flare(0, -2.0f, 1.0f, brown));
    } else {
        flares.push_back(Flare(3, 1.0f, 0.5f, red));
        flares.push_back(Flare(5, 0.75f, 0.1f, greenBlue));
        flares.push_back(Flare(5, 0.7f, 0.1f, greenBlue));
        flares.push_back(Flare(1, 0.5f, 2.0f, greenBlue));
        flares.push_back(Flare(2, 0.2f, 0.5f, greenBlue));
        flares.push_back(Flare(1, 0.5f, 2.0f, greenBlue));
        flares.push_back(Flare(3, 0.2f, 0.5f, greenBlue));
        flares.push_back(Flare(0, 0.0f, 0.4f, brown));
        flares.push_back(Flare(5, -0.25f, 0.7f, brown));
        flares.push_back(Flare(5, -0.4f, 0.2f, brown));
        flares.push_back(Flare(5, -0.6f, 0.4f, darkGreen));
        flares.push_back(Flare(5, -1.f, 0.3f, darkGreen));
        flares.push_back(Flare(3, -2.0f, 1.5f, brown));
    }
    return true;
}

void LensFlare::Update(bool geocentricMode, const Vector3& outputScale, unsigned long millis, const Camera* camera, const OverriddenBillboardMatrix& overriddenBillboardMatrix)
{
#ifdef TIMING
    DWORD start = timeGetTime();
#endif

    if (!ephemeris || lensFlareDisabled) return;

    // Compute the location of the sun in world coordinates
    Vector3 sunPosHorizon = ephemeris->GetSunPositionHorizon();
    double sunAltitude = MathUtils::ToDegrees(asin(sunPosHorizon.y));
    if (sunAltitude < 0) {
        occlusion = 1.0f;
        return;
    }

    Renderer* renderer = tcsData->GetRenderer();

    sunPosHorizon = sunPosHorizon * camera->GetBasis3x3();

    Vector3 sunPosGeo = ephemeris->GetSunPositionGeographic();
    sunPos = geocentricMode ? sunPosGeo : sunPosHorizon;
    sunPos.Normalize();

    if (renderer) {
        // Compute a camera-centered modelview matrix.
        Matrix4 modelview = camera->GetModelViewMatrix();
        // Blow away any translation
        for (int row = 0; row < 3; row++) {
            modelview.elem[row][3] = 0;
        }
        modelview.elem[3][3] = 1;

        // Get position of sun in clip space
        Matrix4 mvproj = camera->GetProjectionMatrix() * modelview;

        Vector4 sunClip4 = mvproj * (sunPos * sunDistance);
        sunClip = Vector3(sunClip4.x / sunClip4.w, sunClip4.y / sunClip4.w, sunClip4.z / sunClip4.w);

        bool flip = false;
        Configuration::GetBoolValue("lens-flare-flip-y", flip);
        if (flip) {
            sunClip.y *= -1.0;
        }

        // Bail if we always want it on.
        if (alwaysOn) {
            occlusion = 0;
            return;
        }

        // Bail if the sun isn't in the scene at all.

        if (sunClip4.x <= -sunClip4.w || sunClip4.x > sunClip4.w ||
                sunClip4.y <= -sunClip4.w || sunClip4.y > sunClip4.w ||
                sunClip4.z <= 0 || sunClip4.z > sunClip4.w) {
            occlusion = 1.0f;
#ifdef TIMING
            printf("Bailed out of scene\n");
#endif
            return;
        }

        sunClip.z = 0;

        occlusion = 0;

        if (!disableOcclusion) {
#ifdef TIMING
            DWORD occlusionStart = timeGetTime();
#endif
            // Push our occlusion test billboard back to just inside the far clip plane
            double znear, zfar;
            camera->GetNearFarClip(znear, zfar);

            sunPos.Normalize();
            sunPos = sunPos * zfar * 0.99;

            sunQuad->SetWorldPosition(sunPos);

            Configuration::GetDoubleValue("sun-width-degrees", sunWidth);

            const double discPer = (256.0 - (57.0 * 2.0)) / 256.0;
            const double sunDiscSize = sunWidth * (1.0 / discPer);
            sunQuad->SetAngularSize(sunDiscSize);

            // Now, start an occlusion query to see if there's anything in front of it
            // in the depth buffer. This will pick up an occluders from the scene, but won't
            // pick up on clouds that occlude the sun, since they do not write to the depth
            // buffer.

            // We read the query from the previous frame (if any) in order to avoid stalling.
            unsigned int fragments = queryHandle ? renderer->GetOcclusionQueryResults(queryHandle) : 0;

            if (renderer->StartOcclusionQuery(&queryHandle) && sunTexture) {
                // Compute the total fragments the sun billboard takes up
                unsigned int totalFragments;

                double fov = 0;
                if (camera->HasProjectionMatrix()) {
                    fov = camera->GetFieldOfView();
                } else {
                    renderer->GetFOV(fov);
                }
                fov = MathUtils::ToDegrees(fov);
                int x, y, w, h;
                renderer->GetViewport(x, y, w, h);
                double sunWidthScreen = (sunDiscSize / fov) * (double)h;
                totalFragments = (unsigned int)(sunWidthScreen * sunWidthScreen);

                renderer->EnableDepthReads(true);
                renderer->EnableDepthWrites(false);
                renderer->EnableBlending(ZERO, ONE); // Make it invisible
                renderer->EnableLighting(false);

                renderer->SetModelviewMatrix(modelview);

                Camera lensFlareCamera(camera, false, false);
                lensFlareCamera.SetModelViewMatrix(modelview);
                lensFlareCamera.SetProjectionMatrix(camera->GetProjectionMatrix());

                sunQuad->Draw(sunTexture, 0, 1.0, outputScale, false, millis, &lensFlareCamera, overriddenBillboardMatrix, tcsData, false);

                renderer->EndOcclusionQuery(queryHandle);

                renderer->SetModelviewMatrix(camera->GetModelViewMatrix());
                renderer->SetProjectionMatrix(camera->GetProjectionMatrix());

                renderer->DisableBlending();
                renderer->EnableDepthWrites(true);

                // Compute the percent of the sun billboard that was occluded.
                occlusion = 1.0f - ((float)fragments / (float)totalFragments);
                if (occlusion < 0.0f) occlusion = 0.0f;
                if (occlusion > 1.0f) occlusion = 1.0f;

#ifdef TIMING
                DWORD elapsed = timeGetTime() - occlusionStart;
                printf("Occlusion query took %d ms\n", elapsed);
#endif
            }
        }

        // Now, if we think we are not completely occluded, move on to test for
        // occlusion against the clouds, by reading back a pixel from the framebuffer
        // and see if it's white or not. Yes, this stalls as well, see comment above.
        if (occlusion < 1.0f && !disableOcclusion) {

#ifdef TIMING
            DWORD readStart = timeGetTime();
#endif
            // Get the sun position in screen coordinates
            int x, y, w, h;
            renderer->GetViewport(x, y, w, h);

            Vector3 sunPosScreen;
            sunPosScreen.x = x + (1 + sunClip.x) * w / 2;
            sunPosScreen.y = y + (1 + sunClip.y) * h / 2;

            // Grab a pixel from the framebuffer
            // This too may be delayed one frame under the hood.
            unsigned int pixel = 0xffffffff;
            if (renderer->GetPixels((int)sunPosScreen.x, (int)sunPosScreen.y, 1, 1, &pixel, !usePBO)) {
                Vector3 screenColor;
                screenColor.x = (double)((pixel & 0x000000ff) >> 0) / 255.0;
                screenColor.y = (double)((pixel & 0x0000ff00) >> 8) / 255.0;
                screenColor.z = (double)((pixel & 0x00ff0000) >> 16) / 255.0;

                //printf("Color %f %f %f fragments %d\n", screenColor.x, screenColor.y, screenColor.z, fragments);

                // Occlude if it's not white, more so the less white it is.
                double colorDistance = (screenColor - Vector3(1, 1, 1)).SquaredLength() * flareFalloff;
                if (colorDistance > 0) {
                    occlusion += (float)colorDistance;
                }
                if (occlusion > 1.0f) occlusion = 1.0f;
            }

#ifdef TIMING
            DWORD elapsed = timeGetTime() - readStart;
            printf("GetPixels took %d ms\n", elapsed);
#endif
        }
    }

#ifdef TIMING
    printf("Total update %d Occlusion is %f\n", timeGetTime() - start, occlusion);
#endif
}

void LensFlare::Draw(const Vector3& outputScale, const Camera* camera)
{
    if (occlusion >= 1.0f || lensFlareDisabled) return;

#ifdef TIMING
    DWORD start = timeGetTime();
#endif

    Renderer* renderer = tcsData->GetRenderer();
    if (renderer) {
        // Render direct to clip space
        Matrix4 ident;
        renderer->SetProjectionMatrix(ident);

        // Set render state
        renderer->EnableTexture2D(true);
        renderer->EnableDepthReads(false);
        renderer->EnableDepthWrites(false);
        renderer->EnableBlending(ONE, INVSRCCOLOR);
        renderer->EnableBackfaceCulling(false);

        // Get the aspect ratio so the flares aren't squished
        int x, y, w, h;
        renderer->GetViewport(x, y, w, h);
        double aspect = (double)w / (double)h;

        // Render each flare component
        SL_VECTOR(Flare) ::iterator it;
        for (it = flares.begin(); it != flares.end(); it++) {
            Flare f = *it;
            double s = f.size * flareSize;
            Vector3 p = sunClip * f.position;
            Matrix4 mv(s, 0, 0, p.x,
                       0, s * aspect, 0, p.y,
                       0, 0, 1, 0,
                       0, 0, 0, 1);

            Color c = f.color * (1.0f - occlusion) * flareBrightness;

            renderer->SetCurrentColor(c);

            renderer->SetModelviewMatrix(mv);

            TextureHandle tex = 0;
            if (f.texture == -1) { // animate shine textures
                static unsigned int glareTex = 0;
                tex = shineTextures[glareTex];
                glareTex++;
                glareTex = glareTex % NUM_SHINE_TEXTURES;
            } else {
                tex = flareTextures[f.texture];
            }

            if (tex) {
                renderer->EnableTexture(tex, 0);

                ShaderHandle flareShader = tcsData->GetFlareShader()->GetShader();

                if (flareShader) {
                    renderer->BindShader(flareShader);
                    if (renderer->GetType() == Atmosphere::OPENGL32CORE || renderer->GetType() == Atmosphere::OPENGL) {
                        const FlareShaderConstantLocations& constantLocations = tcsData->GetFlareShader()->GetConstantLocations();
                        renderer->SetConstantVectorAtLocation(flareShader, constantLocations.sl_outputScale, outputScale);
                        Matrix4 proj;
                        renderer->SetConstantMatrix4AtLocation(flareShader, constantLocations.sl_modelViewProj, proj * mv);
                        renderer->SetConstantMatrix4AtLocation(flareShader, constantLocations.sl_modelView, mv);
                        renderer->SetConstantVector4AtLocation(flareShader, constantLocations.sl_lightingColor, Vector4(c.r, c.g, c.b, c.a));
                    } else {
                        renderer->SetConstantVector(flareShader, "sl_outputScale", outputScale);
                        Matrix4 proj;
                        renderer->SetConstantMatrix(flareShader, "sl_modelViewProj", proj * mv);
                        renderer->SetConstantMatrix(flareShader, "sl_modelView", mv);
                        renderer->SetConstantVector4(flareShader, "sl_lightingColor", Vector4(c.r, c.g, c.b, c.a));
                    }
                    renderer->DrawStrip(vb->GetHandle(), ib->GetHandle(), 0, 4, 4, false, true);
                    renderer->UnbindShader();
                } else {
                    renderer->UnbindShader();
                    renderer->DrawStrip(vb->GetHandle(), ib->GetHandle(), 0, 4, 4, false, true);
                }
            }
        }

        // Reset
        renderer->EnableDepthReads(true);
        renderer->EnableDepthWrites(true);
        renderer->DisableBlending();
        //ren->EnableBackfaceCulling(true);

        renderer->SetModelviewMatrix(camera->GetModelViewMatrix());
        renderer->SetProjectionMatrix(camera->GetProjectionMatrix());
#ifdef TIMING
        printf("Total draw %d\n", timeGetTime() - start);
#endif
    }
}
