// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.

#include "Atmosphere.h"
#include "Stars.h"
#include "VertexBuffer.h"
#include "Ephemeris.h"
#include "starcat.h"
#include "Profiler.h"
#include "LuminanceMapper.h"
#include "Glare.h"
#include "Utils.h"
#include "Configuration.h"
#include "Sky.h"
#include "BillboardBuffer.h"
#include "MathUtils.h"
#include "Camera.h"
#include "ScopedConstantPrep.h"
#include "StarShader.h"
#include "ResourceLoader.h"
#include "SLAssert.h"

#include <string>

#define MAG_0_LUMINANCE 0.05
#define DUSK MathUtils::ToRadians(5)

using namespace SilverLining;
using namespace std;

static double GetLuminance(double visualMagnitude)
{
    return MAG_0_LUMINANCE * exp(-0.921 * visualMagnitude);
}

ShaderHandle Stars::GetShader(ThreadCameraStreamData* tcsData) const
{
    return tcsData->GetStarShader()->GetShader();
}

void Stars::ReloadShaders(ThreadCameraStreamData* tcsData)
{
    tcsData->ReloadStarShader();
}

static void ReadStar(Star& star, const std::string& starString)
{
    stringstream ssStar(starString);

    std::string val;
    int i = 0;
    while (std::getline(ssStar, val, ',')) {
        if (i == 0) {
            star.visualMagnitude = atof(val.c_str());// std::stod(val);
        }
        if (i == 1) {
            star.ra = atof(val.c_str()); //std::stod(val);
        }
        if (i == 2) {
            star.dec = atof(val.c_str()); //std::stod(val);
        }
        if (i == 3) {
            star.r = atoi(val.c_str()); //std::stoi(val);
        }
        if (i == 4) {
            star.g = atoi(val.c_str()); //std::stoi(val);
        }
        if (i == 5) {
            star.b = atoi(val.c_str()); //std::stoi(val);
        }
        if (i == 6) {
            star.raProperMotion = atof(val.c_str()); //std::stod(val);
        }
        if (i == 7) {
            star.decProperMotion = atof(val.c_str()); //std::stod(val);
        }
        ++i;
    }
}

bool Stars::ReadStarsFromFile(const char* strStarCatalogFile)
{
    if (strStarCatalogFile == 0) {
        return false;
    }
    ResourceLoader* resourceLoader = Atmosphere::GetResourceLoader();
    if (resourceLoader == 0) {
        return false;
    }
    char* starsData = 0;
    unsigned int dataLen = 0;
    const bool ok = resourceLoader->LoadResource(strStarCatalogFile, starsData, dataLen, true);
    if (ok == false) {
        resourceLoader->FreeResource(starsData);
        return false;
    }

    std::stringstream ss;
    ss << std::string(starsData);

    SL_VECTOR(Star) vectorStars;
    Star star;

    std::string starString;
    while (std::getline(ss, starString)) {
        if (starString.empty() == false) {
            ReadStar(star, starString);
            vectorStars.push_back(star);
        }
    }
    stars = SL_NEW Star[vectorStars.size()];
    memcpy(const_cast<Star*>(stars), vectorStars.data(), sizeof(Star)*vectorStars.size());
    starsReadHere = true;

    return true;
}

Stars::Stars(const Ephemeris* _ephemeris, GlareManager* gmgr, double pTurbidity, bool _billboardsUseShaderInstancing, const BillboardBufferProvider* _billboardBufferProvider, ThreadCameraStreamData* _tcsData)
    : billboardsUseShaderInstancing(_billboardsUseShaderInstancing)
    , billboardBufferProvider(_billboardBufferProvider)
    , tcsData(_tcsData)
    , stars(0)
    , starsReadHere(false)
{
    Vertex *verts = 0;

    Configuration::GetDoubleValue("minimum-star-glare-magnitude", glareThreshold);

    maxLightPollution = 0.01f;
    Configuration::GetFloatValue("star-maximum-light-pollution", maxLightPollution);

    ephemeris = _ephemeris;
    turbidity = pTurbidity;
    glareManager = gmgr;

    nStars = 0;

    lastStarEpochCenturies = 0;
    lastPlanetEpochCenturies = 0;

    Configuration::GetBoolValue("disable-star-glare", noGlare);
    if (!glareManager->HasGlare(tcsData)) noGlare = true;

    minMagnitude = 10;
    Configuration::GetDoubleValue("minimum-star-magnitude", minMagnitude);

    starBrightness = 0;
    Configuration::GetDoubleValue("star-magnitude-adjustment", starBrightness);

    double starDistance = 100.0;
    Configuration::GetDoubleValue("sky-box-size", starDistance);
    starDistance *= Atmosphere::GetUnitScale();
    fStarDistance = 0.5f * (float)starDistance;

    geoZUp = true;
    Configuration::GetBoolValue("geocentric-z-is-up", geoZUp);

    primeMeridianZ = false;
    Configuration::GetBoolValue("z-is-on-prime-meridian", primeMeridianZ);

    const char* strStarCatalogFile = NULL;
    Configuration::GetStringValue("star-catalog-file", strStarCatalogFile);
    const bool hasCatalogFile = (strStarCatalogFile && strcmp(strStarCatalogFile, "none") != 0);
    if (hasCatalogFile) {
        bool ok = ReadStarsFromFile(strStarCatalogFile);
        if (ok == false) {
            stars = starsDefinedInHeader;
        }
    } else {
        stars = starsDefinedInHeader;
    }

    totalStars = 0;
    int i = 0;
    for (;;) {
        if (stars[i].visualMagnitude != 9999) {
            totalStars++;
            if (stars[i].visualMagnitude < minMagnitude) {
                nStars++;
            }
        } else {
            break;
        }

        i++;
    }

    bool enableObjectLabeling = false;
    Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
    const char* name = (enableObjectLabeling) ? "Stars::VertexBuffer" : (const char *)0;

    BufferProperties bufferProperties;
    bufferProperties.genBuffer = true;
    bufferProperties.dynamic = true;

    starVerts = SL_NEW VertexBuffer(nStars, name, tcsData, bufferProperties);

    UpdateStars(tcsData);

    planetGlares = SL_NEW Glare*[NUM_PLANETS];

    planetVerts = SL_NEW VertexBuffer(NUM_PLANETS, name, tcsData, bufferProperties);
    if (planetVerts) {
        planetVerts->LockBuffer();
        verts = planetVerts->GetVertices();

        if (verts) {
            for (i = 0; i < NUM_PLANETS; i++) {
                if (i == MARS) {
                    verts[i].SetColor(255, 153, 51, 255);
                } else if (i == EARTH) {
                    verts[i].SetColor(0, 0, 0, 0);
                } else {
                    verts[i].SetColor(255, 255, 255, 255);
                }

                verts[i].x = verts[i].y = verts[i].z = 0;
                verts[i].SetU(0);
                verts[i].SetV(0);

                if (!noGlare) {
                    Glare *glare = SL_NEW Glare(glareManager, billboardsUseShaderInstancing, billboardBufferProvider->GetABillboardBuffer(), tcsData);
                    glare->SetUserVector(Vector3(verts[i].x, verts[i].y, verts[i].z));
                    planetGlares[i] = glare;
                    planetGlareVector.push_back(glare);
                } else {
                    planetGlares[i] = 0;
                }
            }
        }
        planetVerts->UnlockBuffer();
    }

    Configuration::GetDoubleValue("star-point-size", starPointSize);
}

Stars::~Stars()
{
    if (starVerts) {
        SL_DELETE starVerts;
    }

    if (planetVerts) {
        SL_DELETE planetVerts;
    }

    SL_VECTOR(Glare*) ::iterator it;
    for (it = glares.begin(); it != glares.end(); it++) {
        SL_DELETE(*it);
    }

    for (it = planetGlareVector.begin(); it != planetGlareVector.end(); it++) {
        SL_DELETE(*it);
    }

    glares.clear();
    planetGlareVector.clear();

    SL_DELETE[] planetGlares;

    if (starsReadHere) {
        SL_DELETE[] stars;
    }
}

void Stars::UpdateStars(ThreadCameraStreamData* tcsData)
{
    SL_VECTOR(Glare*)::iterator it;
    for (it = glares.begin(); it != glares.end(); it++) {
        SL_DELETE(*it);
    }
    glares.clear();

    const ShaderHandle starShader = tcsData->GetStarShader()->GetShader();

    if (starVerts) {
        if (starVerts->LockBuffer()) {
            Vertex *verts = starVerts->GetVertices();

            if (verts) {

                double epochCenturies = ephemeris->GetEpochCenturies();
                double yearCorrection = 50.0 + epochCenturies * 100.0;

                double baseLuminance = GetLuminance(3.0);

                int j = 0;
                for (int i = 0; i < totalStars; i++) {
                    if (stars[i].visualMagnitude >= minMagnitude) {
                        continue;
                    }

                    if (j >= nStars) {
                        break;
                    }

                    double ra = stars[i].ra + stars[i].raProperMotion * yearCorrection;
                    double dec = stars[i].dec + stars[i].decProperMotion * yearCorrection;

                    double luminance = GetLuminance(stars[i].visualMagnitude + starBrightness);

                    verts[j].x = fStarDistance * (float)(cos(ra) * cos(dec));
                    verts[j].y = fStarDistance * (float)(sin(ra) * cos(dec));
                    verts[j].z = fStarDistance * (float)(sin(dec));

                    if (starShader) {
                        verts[j].SetColor(stars[i].r, stars[i].g, stars[i].b, 255);
                        verts[j].SetU((float)(stars[i].visualMagnitude + starBrightness));
                        verts[j].SetV((float)luminance);
                    } else {
                        double it = GetLuminance(stars[i].visualMagnitude + starBrightness) / baseLuminance;
                        verts[j].SetColor(Color(it, it, it));
                    }

                    if ((!noGlare) && stars[i].visualMagnitude < glareThreshold) {
                        Glare *glare = SL_NEW Glare(glareManager, billboardsUseShaderInstancing, billboardBufferProvider->GetABillboardBuffer(), tcsData);
                        glare->SetUserVector(Vector3(verts[j].x, verts[j].y, verts[j].z));
                        glare->SetIntensity(luminance, tcsData);
                        glares.push_back(glare);
                    }

                    j++;
                }
            }
        }
        starVerts->UnlockBuffer();
    }
}

void Stars::UpdatePlanets(ThreadCameraStreamData* tcsData)
{
    const ShaderHandle starShader = tcsData->GetStarShader()->GetShader();

    if (planetVerts && planetVerts->LockBuffer()) {
        Vertex *verts = planetVerts->GetVertices();
        if (verts) {
            for (int planet = 0; planet < NUM_PLANETS; planet++) {
                if (planet != EARTH) {
                    double ra, dec, mag;
                    ephemeris->GetPlanetPosition(planet, ra, dec, mag);

                    verts[planet].x = fStarDistance * (float)(cos(ra) * cos(dec));
                    verts[planet].y = fStarDistance * (float)(sin(ra) * cos(dec));
                    verts[planet].z = fStarDistance * (float)(sin(dec));

                    verts[planet].SetU((float)(mag + starBrightness));
                    verts[planet].SetV((float)GetLuminance(mag + starBrightness));

                    if (!starShader) {
                        // Convert magnitude to luminance
                        double luminance = pow(100.0, (-(mag + starBrightness) / 5.0));

                        double r2 = luminance / (pow(luminance, 2.0 / 3.0) + 0.02); // The .02 term controls the overall star brightness.

                        Color planetColor(r2, r2, r2);
                        planetColor.ClampToUnitOrLess(tcsData->GetHDREnabled());
                        verts[planet].SetColor(planetColor);
                    }

                    if (planetGlares[planet]) {
                        Vector3 worldPos(0, 0, 0);
                        float luminance = 0;
                        if (mag < glareThreshold) {
                            worldPos = Vector3(verts[planet].x, verts[planet].y, verts[planet].z);
                            planetGlares[planet]->SetUserVector(worldPos);
                            luminance = (float)GetLuminance(mag + starBrightness);
                        }
                        planetGlares[planet]->SetWorldPosition(worldPos);
                        planetGlares[planet]->SetIntensity(luminance, tcsData);
                    }
                }
            }
        }
        planetVerts->UnlockBuffer();
    }
}

bool Stars::Draw(const FogParameters& fogParams, bool geocentricMode, float lightPollution, const Vector3& outputScale, unsigned long millis, const Camera* starCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix
                 , ThreadCameraStreamData* tcsData, bool forceDepth)
{
    Timer t("Render stars");

    double epochCenturies = ephemeris->GetEpochCenturies();

    // Update star positions if the year has changed
    if (MathUtils::Abs(epochCenturies - lastStarEpochCenturies) > 0.01) {
        UpdateStars(tcsData);
        lastStarEpochCenturies = epochCenturies;
    }

    // Update planet positions if the day has changed
    if (MathUtils::Abs(epochCenturies - lastPlanetEpochCenturies) > (0.01 * (1.0 / 365.0))) {
        UpdatePlanets(tcsData);
        lastPlanetEpochCenturies = epochCenturies;
    }

    Vector4 vFog;
    vFog.x = fogParams.fogColor.r;
    vFog.y = fogParams.fogColor.g;
    vFog.z = fogParams.fogColor.b;
    vFog.w = fogParams.fogDensity;

    Vector4 vFogDistance;
    vFogDistance.x = MathUtils::Abs(fogParams.fogDistance);

    Renderer* renderer = tcsData->GetRenderer();

    if (renderer) {
        renderer->EnableBackfaceCulling(false);
        renderer->EnableTexture2D(false);
        renderer->EnableLighting(false);

        const ShaderHandle starShader = tcsData->GetStarShader()->GetShader();

        if (starShader) {
            renderer->EnableBlending(ONE, ONE);
        } else {
            renderer->EnableBlending(INVDSTCOLOR, ONE);
        }

        // Fade out stars with light pollution
        float lightPollutionFading = 1.0f;
        float normalizedLightPollution = lightPollution / maxLightPollution;
        if (normalizedLightPollution > 1.0f) normalizedLightPollution = 1.0f;
        lightPollutionFading -= normalizedLightPollution;
        vFogDistance.y = lightPollutionFading;

        // The basis is already baked into the modelview matrix here if not geocentric
        const Vector3 up = (geocentricMode) ? starCamera->GetUpVector() : Vector3(0, 1, 0);


        Matrix4 modelview = starCamera->GetModelViewMatrix();
        // Blow away any translation
        for (int row = 0; row < 3; row++) {
            modelview.elem[row][3] = 0;
        }
        modelview.elem[3][3] = 1;
        const Matrix4 modelviewProj = starCamera->GetProjectionMatrix() * modelview;

        static Matrix3 coordXform(0, 1, 0,
                                  0, 0, 1,
                                  1, 0, 0);

        Matrix3 eh;
        if (geocentricMode) {
            if (geoZUp) {
                eh = ephemeris->GetEquatorialToGeographicMatrix();
            } else {
                if (primeMeridianZ) {
                    static Matrix3 xform(0, 1, 0,
                                         0, 0, 1,
                                         renderer->GetIsRightHanded() ? 1 : -1, 0, 0);
                    eh = xform * ephemeris->GetEquatorialToGeographicMatrix();
                } else {
                    static Matrix3 xform(1, 0, 0,
                                         0, 0, 1,
                                         0, renderer->GetIsRightHanded() ? -1 : 1, 0);
                    eh = xform * ephemeris->GetEquatorialToGeographicMatrix();
                }
            }
        } else {
            eh = coordXform * ephemeris->GetEquatorialToHorizonMatrix();
        }

        const Matrix4 eqToHoriz(eh.elem[0][0], eh.elem[0][1], eh.elem[0][2], 0,
                                eh.elem[1][0], eh.elem[1][1], eh.elem[1][2], 0,
                                eh.elem[2][0], eh.elem[2][1], eh.elem[2][2], 0,
                                0, 0, 0, 1);

        Vector4 sl_pointSize(starPointSize, 0, 0, 0);

        ScopedConstantPrep prep(renderer, starShader, false);
        renderer->BindShader(starShader);

        Vector4 forceDepthVec;
        if (renderer->GetIsVulkan()) {
            if (forceDepth) {
                float zmin, zmax;
                renderer->GetDepthRange(zmin, zmax);
                forceDepthVec.x = 1.0;
                forceDepthVec.y = zmax;
            } else {
                forceDepthVec.x = 0.0;
            }
        }

        if (renderer->SupportsConstantLocations()) {
            const StarShaderConstantLocations& starShaderConstantLocations = tcsData->GetStarShader()->GetConstantLocations();
            renderer->SetConstantVectorAtLocation(starShader, starShaderConstantLocations.sl_outputScale, outputScale);
            renderer->SetConstantVector4AtLocation(starShader, starShaderConstantLocations.sl_fog, vFog);
            renderer->SetConstantVector4AtLocation(starShader, starShaderConstantLocations.sl_fogDistance, vFogDistance);
            renderer->SetConstantVectorAtLocation(starShader, starShaderConstantLocations.sl_up, up);
            renderer->SetConstantMatrix4AtLocation(starShader, starShaderConstantLocations.sl_modelViewProj, modelviewProj);
            renderer->SetConstantMatrix4AtLocation(starShader, starShaderConstantLocations.sl_modelView, modelview);
            renderer->SetConstantMatrix4AtLocation(starShader, starShaderConstantLocations.sl_invBasis, starCamera->GetInverseBasis4x4());
            renderer->SetConstantMatrix4AtLocation(starShader, starShaderConstantLocations.sl_equatorialToHorizon, eqToHoriz);
            renderer->SetConstantVector4AtLocation(starShader, starShaderConstantLocations.sl_pointSize, sl_pointSize);
            if (renderer->GetIsVulkan()) renderer->SetConstantVector4AtLocation(starShader, starShaderConstantLocations.sl_forceDepth, forceDepthVec);
        } else {
            renderer->SetConstantVector(starShader, "sl_outputScale", outputScale);
            renderer->SetConstantVector4(starShader, "sl_fog", vFog);
            renderer->SetConstantVector4(starShader, "sl_fogDistance", vFogDistance);
            renderer->SetConstantVector(starShader, "sl_up", up);
            renderer->SetConstantMatrix(starShader, "sl_modelViewProj", modelviewProj);
            renderer->SetConstantMatrix(starShader, "sl_modelView", modelview);
            renderer->SetConstantMatrix(starShader, "sl_invBasis", starCamera->GetInverseBasis4x4());
            renderer->SetConstantMatrix(starShader, "sl_equatorialToHorizon", eqToHoriz);
            renderer->SetConstantVector4(starShader, "sl_pointSize", sl_pointSize);
            if (renderer->GetIsVulkan()) renderer->SetConstantVector4(starShader, "sl_forceDepth", forceDepthVec);
        }

        if (!starShader) {
            renderer->SetModelviewMatrix((starCamera->GetModelViewMatrix()*eqToHoriz));
        }

        renderer->DrawPoints(starVerts->GetHandle(), starPointSize, nStars);

        renderer->DrawPoints(planetVerts->GetHandle(), starPointSize, NUM_PLANETS);

        renderer->UnbindShader();

        DrawGlares(eqToHoriz, outputScale, millis, starCamera, overriddenBillboardMatrix, tcsData, forceDepth);

        renderer->SetModelviewMatrix(starCamera->GetModelViewMatrix());

        //renderer->EnableBackfaceCulling(true);
        renderer->EnableLighting(false);
        renderer->DisableBlending();
    }

    return true;
}

void Stars::DrawGlares(const Matrix4& eqToHorizon, const Vector3& outputScale, unsigned long millis, const Camera* glareCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, ThreadCameraStreamData* tcsData, bool forceDepth)
{
    if (glareManager->EnableShaders(tcsData)) {
        SL_VECTOR(Glare *) ::iterator it;
        for (it = glares.begin(); it != glares.end(); it++) {
            Vector3 pos = (*it)->GetUserVector();
            pos = eqToHorizon * pos;

            (*it)->SetWorldPosition(pos);
            (*it)->Draw(0, outputScale, millis, glareCamera, overriddenBillboardMatrix, tcsData, forceDepth);
        }

        for (it = planetGlareVector.begin(); it != planetGlareVector.end(); it++) {
            Vector3 pos = (*it)->GetUserVector();
            pos = eqToHorizon * pos;

            (*it)->SetWorldPosition(pos);
            (*it)->Draw(0, outputScale, millis, glareCamera, overriddenBillboardMatrix, tcsData, forceDepth);
        }

        glareManager->DisableShaders(tcsData);
    }
}
