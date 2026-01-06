// Copyright (c) 2004-2022 Sundog Software, LLC All rights reserved worldwide.

// For leak checking
//#define VLD_FORCE_ENABLE 1
//#include <vld.h>

#include "Atmosphere.h"
#include "AtmosphereFromSpace.h"
#include "Renderer.h"
#include "Sky.h"
#include "Ephemeris.h"
#include "Profiler.h"
#include "Metaball.h"
#include "Utils.h"
#include "Cloud.h"
#include "CloudLayer.h"
#include "LicenseManager.h"
#include "CumulusCloud.h"
#include "PrecipitationManager.h"
#include "DefaultRandomNumberGenerator.h"
#include "LensFlare.h"
#include "ResourceLoader.h"
#include "CrepuscularRays.h"
#include "ShadowMap.h"
#include "CumulusCloudLayer.h"
#include "Mutex.h"
#include "Matrix4.h"
#include "MathUtils.h"
#include "Camera.h"
#include "SharedVertexBuffer.h"
#include "BillboardBuffer.h"
#include "CloudLayerSorter.h"
#include "OverriddenBillboardMatrix.h"
#include "BillboardRenderingParameters.h"
#include "BillboardRenderer.h"
#include "BillboardShader.h"
#include "MetaballRenderingParameters.h"
#include "OverriddenCameraPos.h"
#include "ThreadCameraStreamData.h"
#include "StratocumulusCloudLayer.h"
#include "TextureManager.h"
#include "CloudRenderingParameters.h"
#include "CumulusCloudRenderingParameters.h"
#include "PrecipitationShaders.h"
#include "TcsUserData.h"
#include "SLAssert.h"
#include <float.h>

#include <algorithm>

#if (!defined(WIN32) && !defined(WIN64))
#include <sys/timeb.h>
#endif

using namespace std;

namespace SilverLining
{
ResourceLoader *Atmosphere::resourceLoader = 0;
ResourceLoader *Atmosphere::defaultResourceLoader = 0;
RandomNumberGenerator *Atmosphere::defaultRandomNumberGenerator = 0;
bool Atmosphere::enableHDR = false;
double Atmosphere::unitScale = 1.0;

static Color transmittedLightFromTopOfAtmosphere(1.0, 0.96, 0.94, 1.0);
static int numAtmospheres = 0;

#ifdef MAC
static bool enforceStrictWeak = true;
#else
static bool enforceStrictWeak = false;
#endif

char* Atmosphere::userDefinedVertString = 0;
char* Atmosphere::userDefinedFragString = 0;

static void(*Debug_msg_cb)(const char *) = 0;


class AtmosphereTcsUserData : public TcsUserData
{
public:
    AtmosphereTcsUserData();
    virtual ~AtmosphereTcsUserData();
    CameraHandle cameraHandle;

    bool insideLoop;

    double framerateValue;

    bool fogOn;
    double fogDensity;
    SilverLining::Vector3 fogColor;

    CloudLayerSorter* cloudLayerSorter;

    unsigned long now;
    unsigned long lastTime;
    double dt;

    bool updated, culled;

    bool firstFrameCalled;

    bool invalid;
};

AtmosphereTcsUserData::AtmosphereTcsUserData()
{
    insideLoop = false;
    framerateValue = 0;

    fogOn = false;
    fogDensity = 1E-9;
    fogColor = Vector3(1.0, 1.0, 1.0);

    cloudLayerSorter = SL_NEW CloudLayerSorter();

    lastTime = 0;

    updated = culled = false;

    firstFrameCalled = false;

    invalid = false;

    cameraHandle = 0;
}
AtmosphereTcsUserData::~AtmosphereTcsUserData()
{
    // nothing else
    SL_DELETE cloudLayerSorter;
}

AtmosphereTcsUserData* Atmosphere::GetOrCreateTcsUserData(ThreadCameraStreamData* data) const
{
    if (data == 0) {
        return 0;
    }
    TcsUserData* userData = data->GetUserData(this);
    if (userData == 0) {
        userData = SL_NEW AtmosphereTcsUserData();
        data->SetUserData(this, userData);
    }
    return (AtmosphereTcsUserData*)userData;
}

Atmosphere::Atmosphere(const char *userName, const char *licenseKey)
{
    LicenseManager::ValidateCode(userName, licenseKey);

    conditions = SL_NEW AtmosphericConditions();
    conditions->SetAtmosphere(this);

    hasLightingOverride = false;
    ambientOverrideR = ambientOverrideG = ambientOverrideB = 0.0f;
    diffuseOverrideR = diffuseOverrideG = diffuseOverrideB = 0.0f;
    randomNumberGenerator = 0;

    debugMode = false;

    atmosphereFromSpace = 0;

    disableFarCulling = false;
    enableLensFlare = false;
    enableOcclusionQuery = true;

    outputScale = Vector3(1.0f, 1.0f, 1.0f);
    computeCloudBounds = true;
    configInited = false;

    if (!defaultRandomNumberGenerator) {
        defaultRandomNumberGenerator = SL_NEW DefaultRandomNumberGenerator;
    }
    randomNumberGenerator = defaultRandomNumberGenerator;

    if (!resourceLoader) {
        resourceLoader = defaultResourceLoader = SL_NEW ResourceLoader();
        resourceLoader->SetResourceDirPath(".\\Resources\\");
    }
    defaultTcsData = 0;

    numAtmospheres++;

    mutex = 0;
}

Atmosphere::~Atmosphere()
{
    if (atmosphereFromSpace) {
        SL_DELETE atmosphereFromSpace;
    }

    if (conditions) {
        SL_DELETE conditions;
    }


    if (userDefinedVertString) {
        SL_FREE(userDefinedVertString);
    }

    if (userDefinedFragString) {
        SL_FREE(userDefinedFragString);
    }

    if (configInited) {
        Configuration::Destroy();
    }

    numAtmospheres--;
    if (numAtmospheres <= 0) { // Delete static data when refcount is zero.
        if (defaultResourceLoader) {
            if (resourceLoader == defaultResourceLoader) {
                resourceLoader = 0;
            }
            SL_DELETE defaultResourceLoader;
            defaultResourceLoader = 0;
        }

        if (defaultRandomNumberGenerator) {
            if (randomNumberGenerator == defaultRandomNumberGenerator) {
                randomNumberGenerator = 0;
            }
            SL_DELETE defaultRandomNumberGenerator;
            defaultRandomNumberGenerator = 0;
        }
        Profiler::Destroy();
        LicenseManager::Shutdown();
    }

    defaultTcsData->DeInitialize(*this);
    SL_DELETE defaultTcsData;

    if (mutex) {
        SL_DELETE mutex;
    }
}

void Atmosphere::SetDebugMsgCB(void(*debug_msg_cb)(const char *))
{
    Debug_msg_cb = debug_msg_cb;
}

void Atmosphere::DebugMsg(const char *message)
{
    if (!message) return;

    SL_STRING msg("SILVERLINING: ");
    msg += message;
    msg += "\n";

    if (Debug_msg_cb)
        Debug_msg_cb(message);
}

void Atmosphere::SetSkyModel(SkyModel model, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    Sky* sky = tcsData->GetSky();
    if (sky) sky->SetSkyModel(model);
}

SkyModel Atmosphere::GetSkyModel(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (sky) return sky->GetSkyModel();
    return HOSEK_WILKIE;
}

void Atmosphere::SetSunAlpha(double alpha, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    Sky* sky = tcsData->GetSky();
    if (sky) sky->SetSunAlpha(alpha);
}

double Atmosphere::GetSunAlpha(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (sky) return sky->GetSunAlpha();
    return 1.0;
}

void Atmosphere::SetMoonAlpha(double alpha, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    Sky* sky = tcsData->GetSky();
    if (sky) sky->SetMoonAlpha(alpha);
}

double Atmosphere::GetMoonAlpha(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (sky) return sky->GetMoonAlpha();

    return 1.0;
}

void Atmosphere::OverrideCloudLighting(float ar, float ag, float ab, float dr, float dg, float db)
{
    ambientOverrideR = ar;
    ambientOverrideG = ag;
    ambientOverrideB = ab;
    diffuseOverrideR = dr;
    diffuseOverrideG = dg;
    diffuseOverrideB = db;
    hasLightingOverride = true;
}

void Atmosphere::ClearCloudLightingOverride()
{
    hasLightingOverride = false;
}

void Atmosphere::ContextBeingDeleted(void* context) const
{
    defaultTcsData->GetRenderer()->ContextBeingDeleted(context);
}

void Atmosphere::D3D9DeviceLost(void* data)
{
    // We'll lazily create new environment and shadow maps on demand.
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;

    if (tcsData) {
        tcsData->DeleteEnvironmentMaps();
        tcsData->DeleteShadowMap();
    }

    if (tcsData->GetLensFlare()) {
        tcsData->GetLensFlare()->DeviceLost();
    }

    defaultTcsData->DeviceLost();
}

void Atmosphere::D3D9DeviceReset()
{
    defaultTcsData->DeviceReset();
}

float Atmosphere::GetSunOcclusion(void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const LensFlare* lensFlare = tcsData->GetLensFlare();
    if (lensFlare) {
        return lensFlare->GetSunOcclusion();
    }

    return 0;
}

void Atmosphere::SetRandomNumberGenerator(RandomNumberGenerator *rng)
{
    if (rng) {
        randomNumberGenerator = rng;
    }
}

RandomNumberGenerator *Atmosphere::GetRandomNumberGenerator() const
{
    return randomNumberGenerator;
}

static bool matrixEquals(const double* m, const Matrix4& matrix)
{
    double m2[16];
    matrix.ToArray(m2);

    bool equals = true;
    for (int i = 0; i < 16; ++i) {
        if (m[i] != m2[i]) {
            equals = false;
            break;
        }
    }
    return equals;
}

//**!!To be Deprecated!!**
void Atmosphere::SetCameraMatrix(const double *m)
{
    if (mutex) mutex->Lock();

    if (m) {
        Matrix4 modelViewMatrix(m);
        modelViewMatrix.Transpose();

        SL_ASSERT(defaultTcsData);
        Camera* camera = defaultTcsData->GetCamera();

        camera->SetModelViewMatrix(modelViewMatrix);
        SL_ASSERT(matrixEquals(m, camera->GetModelViewMatrix()) == true);
    }
    if (mutex) mutex->Unlock();
}

//**!!To be Deprecated!!**
void Atmosphere::SetProjectionMatrix(const double *m)
{
    if (mutex) mutex->Lock();

    if (m) {
        Matrix4 projectionMatrix(m);
        projectionMatrix.Transpose();

        SL_ASSERT(defaultTcsData);
        Camera* camera = defaultTcsData->GetCamera();

        camera->SetProjectionMatrix(projectionMatrix);
        SL_ASSERT(matrixEquals(m, camera->GetProjectionMatrix()) == true);
    }

    if (mutex) mutex->Unlock();
}

//**!!To be Deprecated!!**
void Atmosphere::SetViewport(int x, int y, int w, int h)
{
    if (mutex) mutex->Lock();

    SL_ASSERT(defaultTcsData);
    Camera* camera = defaultTcsData->GetCamera();

    SL_ASSERT(camera);
    camera->SetViewport(x, y, w, h);

    if (mutex) mutex->Unlock();
}

//**!!To be Deprecated!!**
bool Atmosphere::GetViewport(int& x, int&y, int& w, int& h) const
{
    SL_ASSERT(defaultTcsData);
    const Camera* camera = defaultTcsData->GetCamera();

    SL_ASSERT(camera);
    return camera->GetViewport(x, y, w, h);
}

//**!!To be Deprecated!!**
void Atmosphere::SetUpVector(double x, double y, double z)
{
    Vector3 up(x, y, z);
    up.Normalize();

    SL_ASSERT(defaultTcsData);
    Camera* camera = defaultTcsData->GetCamera();

    SL_ASSERT(camera);
    camera->SetUpVector(up);
}

//**!!To be Deprecated!!**
void Atmosphere::GetUpVector(double &x, double &y, double &z)
{
    SL_ASSERT(defaultTcsData);
    Camera* camera = defaultTcsData->GetCamera();

    SL_ASSERT(camera);

    const Vector3& up = camera->GetUpVector();

    x = up.x;
    y = up.y;
    z = up.z;
}

//**!!To be Deprecated!!**
void Atmosphere::SetRightVector(double x, double y, double z)
{
    Vector3 rt(x, y, z);
    rt.Normalize();

    SL_ASSERT(defaultTcsData);
    Camera* camera = defaultTcsData->GetCamera();

    SL_ASSERT(camera);
    camera->SetRightVector(rt);
}

//**!!To be Deprecated!!**
void Atmosphere::GetRightVector(double &x, double &y, double &z)
{
    SL_ASSERT(defaultTcsData);
    Camera* camera = defaultTcsData->GetCamera();

    SL_ASSERT(camera);
    const Vector3& rt = camera->GetRightVector();

    x = rt.x;
    y = rt.y;
    z = rt.z;
}

//**!!To be Deprecated!!**
const double * Atmosphere::GetCameraMatrix() const
{
    SL_ASSERT(defaultTcsData);
    Camera* camera = defaultTcsData->GetCamera();

    SL_ASSERT(camera);

    static double m[16];
    camera->GetModelViewMatrix().ToArray(m);
    return m;
}

//**!!To be Deprecated!!**
const double * Atmosphere::GetProjectionMatrix() const
{
    SL_ASSERT(defaultTcsData);
    Camera* camera = defaultTcsData->GetCamera();

    SL_ASSERT(camera);

    static double m[16];
    camera->GetProjectionMatrix().ToArray(m);
    return m;
}

void Atmosphere::SetDepthRange(float n, float f)
{
    if (mutex) mutex->Lock();

    SL_ASSERT(defaultTcsData);
    Camera* camera = defaultTcsData->GetCamera();

    SL_ASSERT(camera);
    camera->SetDepthRange(n, f);

    if (mutex) mutex->Unlock();
}

bool Atmosphere::GetDepthRange(float& n, float& f) const
{
    SL_ASSERT(defaultTcsData);
    const Camera* camera = defaultTcsData->GetCamera();

    SL_ASSERT(camera);
    return camera->GetDepthRange(n, f);
}

void Atmosphere::SetConditions(const AtmosphericConditions& pConditions)
{
    if (conditions) {
        *conditions = pConditions;
    }
}

const AtmosphericConditions& Atmosphere::GetConditions() const
{
    return *conditions;
}

int Atmosphere::PreInit(const char *resourceDir, bool rightHanded)
{
    if (resourceDir) {
        resourceLoader->SetResourceDirPath(resourceDir);
        char *data;
        unsigned int dataLen;
        bool loaded = resourceLoader->LoadResource("SilverLining.config", data, dataLen, true);
        if (!loaded) {
            return E_RESOURCES_PATH_INVALID;
        }
        resourceLoader->FreeResource(data);
    }

    if (!configInited) {
        if (!Configuration::Initialize()) {
            return E_CONFIGURATION_FILE_NOT_FOUND;
        }
        configInited = true;
    }

    if (defaultTcsData == 0) {
        defaultTcsData = SL_NEW ThreadCameraStreamData(rightHanded, GetUnitScale(), false);
        defaultTcsData->SetEnvironmentMapFormat(environmentMapFormat);
        defaultTcsData->SetUseShadowMap(useShadowMap);
    }

    return E_NOERROR;
}

int Atmosphere::Initialize(int renderer, const char *resourceDir, bool rightHanded, void *environment)
{
    SL_VECTOR(unsigned int) _userShaders;
    return Initialize(renderer, resourceDir, rightHanded, environment, _userShaders);
}

bool Atmosphere::TexturesAreShared(void) const
{
    return texturesAreShared;
}

int Atmosphere::Initialize(int rendererType, const char *resourceDir, bool rightHanded, void *environment, const SL_VECTOR(unsigned int)& _userShaders)
{
    userShaders = _userShaders;

    int result = PreInit(resourceDir, rightHanded);
    if (result != E_NOERROR) {
        return result;
    }

    ThreadCameraStreamData* tcsData = defaultTcsData;

    if (!tcsData) {
        return E_CANT_INITIALIZE_RENDERER_SUBSYSTEM;
    }

#ifndef ANDROID
#ifndef OPENGLES
    bool threadSafe = true;
    Configuration::GetBoolValue("enable-thread-safety", threadSafe);
    if (threadSafe) mutex = SL_NEW Mutex;
#endif
#endif

    bool avoidStalls = false;
    Configuration::GetBoolValue("avoid-opengl-stalls", avoidStalls);

    Configuration::GetBoolValue("enforce-strict-weak-ordering", enforceStrictWeak);

    Configuration::GetDoubleValue("atmosphere-scale-height-meters", scaleHeightMeters);

    bool sortScreenDepth = false;
    Configuration::GetBoolValue("sort-by-screen-depth", sortScreenDepth);
    tcsData->SetSortByScreenDepth(sortScreenDepth);

    result = tcsData->Initialize(rendererType, rightHanded, environment, avoidStalls, userShaders);
    if (result == E_NOERROR) {

        tcsData->GetRenderer()->PushAllState();

// PPP: When textures are not shared and client is using only the tcs data interface, the default tcs
// data need not create textures. See SIL-57
#if 0
        if (TexturesAreShared() == false) {
            tcsData->setCreateTextures(false);
        }
#endif

        tcsData->Initialize(*this);


        TextureManager *textureManager = GetDefaultTcsData()->GetTextureManager();

        if (TexturesAreShared()) {
            bool loaded = false;
            if (textureManager) loaded = textureManager->CreateTextures(randomNumberGenerator);
            if (!loaded) {
                result = E_CANT_LOAD_METABALL_TEXTURE;
            }
        }

        bool doAtmosphereFromSpace = true;
        Configuration::GetBoolValue("enable-atmosphere-from-space", doAtmosphereFromSpace);

        if (doAtmosphereFromSpace) {
            atmosphereFromSpace = SL_NEW AtmosphereFromSpace(*this, tcsData);
            if (atmosphereFromSpace) {
                atmosphereFromSpace->BuildGeometry();
            }
        }

        double T = 2.0;
        Configuration::GetDoubleValue("default-turbidity", T);
        if (conditions) conditions->SetTurbidity(T);

        Configuration::GetBoolValue("compute-cloud-bounds", computeCloudBounds);

        //if (tcsData == defaultTcsData) {
        //    defaultTcsData->InitializePrecipitationManager(*this);
        //}

        atmosphereHeight = 100000;
        Configuration::GetDoubleValue("atmosphere-height", atmosphereHeight);
        atmosphereHeight *= unitScale;

        Configuration::GetFloatValue("atmosphere-color-at-top-red", transmittedLightFromTopOfAtmosphere.r);
        Configuration::GetFloatValue("atmosphere-color-at-top-green", transmittedLightFromTopOfAtmosphere.g);
        Configuration::GetFloatValue("atmosphere-color-at-top-blue", transmittedLightFromTopOfAtmosphere.b);
        transmittedLightFromTopOfAtmosphere.ScaleToUnitOrLess(tcsData->GetHDREnabled());

        applyFogFromCloudPrecip = false;
        Configuration::GetBoolValue("apply-fog-from-cloud-precipitation", applyFogFromCloudPrecip);
        precipMonochrome = false;
        Configuration::GetBoolValue("precipitation-fog-monochrome", precipMonochrome);

        Configuration::GetBoolValue("textures-are-shared", texturesAreShared);

        tcsData->GetRenderer()->PopAllState();
    }

    return result;
}

bool Atmosphere::IsInitialized(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    if (tcsData)
        return tcsData->IsInitialized();
    else
        return false;
}

void Atmosphere::SetSortByScreenDepth(bool sort, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    tcsData->SetSortByScreenDepth(sort);
}

bool Atmosphere::ReloadCumulusTextures(void* data )
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;

    TextureManager* textureManager = tcsData->GetTextureManager();
    textureManager->ReleaseTextures();
    if (tcsData->DrawIndirect()) {
        tcsData->GetBillboardRenderer()->ClearTexturesBuffer();
    }
    return textureManager->CreateTextures(randomNumberGenerator);
}

void Atmosphere::SetGamma(double gamma, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    Sky* sky = tcsData->GetSky();
    if (sky) {
        sky->SetGamma(gamma);
    }
}

double Atmosphere::GetGamma(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (sky) {
        return sky->GetGamma();
    }

    return 0;
}

void Atmosphere::GetSunOrMoonPosition(float *x, float *y, float *z, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    Sky* sky = tcsData->GetSky();
    if (!sky || !x || !y || !z) return;

    Vector3 pos = sky->GetSunOrMoonPosition();
    pos.Normalize();
    pos = pos * tcsData->GetCamera()->GetBasis3x3();

    *x = (float)(pos.x);
    *y = (float)(pos.y);
    *z = (float)(pos.z);
}

void Atmosphere::GetSunOrMoonPositionGeographic(float *x, float *y, float *z, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (!sky || !x || !y || !z) return;

    Vector3 pos = sky->GetSunOrMoonPositionGeographic();
    pos.Normalize();

    *x = (float)(pos.x);
    *y = (float)(pos.y);
    *z = (float)(pos.z);
}

void Atmosphere::GetSunOrMoonPositionEquatorial(float *x, float *y, float *z, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (!sky || !x || !y || !z) return;

    Vector3 pos = sky->GetSunOrMoonPositionEquatorial();
    pos.Normalize();

    *x = (float)(pos.x);
    *y = (float)(pos.y);
    *z = (float)(pos.z);
}

void Atmosphere::GetSunPosition(float *x, float *y, float *z, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (!sky || !x || !y || !z) return;

    Vector3 pos = sky->GetSunPosition();
    pos.Normalize();

    pos = pos * tcsData->GetCamera()->GetBasis3x3();

    *x = (float)(pos.x);
    *y = (float)(pos.y);
    *z = (float)(pos.z);
}

void Atmosphere::GetMoonPosition(float *x, float *y, float *z, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (!sky || !x || !y || !z) return;

    Vector3 pos = sky->GetMoonPosition();
    pos.Normalize();


    pos = pos * tcsData->GetCamera()->GetBasis3x3();

    *x = (float)(pos.x);
    *y = (float)(pos.y);
    *z = (float)(pos.z);
}

void Atmosphere::GetSunPositionGeographic(float *x, float *y, float *z, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (!sky || !x || !y || !z) return;

    Vector3 pos = sky->GetSunPositionGeographic();
    pos.Normalize();

    *x = (float)(pos.x);
    *y = (float)(pos.y);
    *z = (float)(pos.z);
}

void Atmosphere::GetMoonPositionGeographic(float *x, float *y, float *z, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (!sky || !x || !y || !z) return;

    Vector3 pos = sky->GetMoonPositionGeographic();
    pos.Normalize();

    *x = (float)(pos.x);
    *y = (float)(pos.y);
    *z = (float)(pos.z);
}

void Atmosphere::GetSunPositionEquatorial(float *x, float *y, float *z, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (!sky || !x || !y || !z) return;

    Vector3 pos = sky->GetSunPositionEquatorial();
    pos.Normalize();

    *x = (float)(pos.x);
    *y = (float)(pos.y);
    *z = (float)(pos.z);
}

void Atmosphere::GetMoonPositionEquatorial(float *x, float *y, float *z, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (!sky || !x || !y || !z) return;

    Vector3 pos = sky->GetMoonPositionEquatorial();
    pos.Normalize();

    *x = (float)(pos.x);
    *y = (float)(pos.y);
    *z = (float)(pos.z);
}

Color Atmosphere::Interpolate(const Color& c1, const Color& c2, ThreadCameraStreamData* tcsData) const
{
    static double spaceFade = 0.5;
    static bool readSpaceFade = false;

    if (!readSpaceFade) {
        Configuration::GetDoubleValue("space-lighting-fadeout", spaceFade);
        readSpaceFade = true;
    }

    double edge = atmosphereHeight + atmosphereHeight * spaceFade;
    double t = (edge - conditions->GetLocation(tcsData).GetAltitude()) / (atmosphereHeight * spaceFade);
    if (t > 1.0) t = 1.0;
    if (t < 0) t = 0;
    return (c2 * (float)t + c1 * (1.0f - (float)t));
}

void Atmosphere::GetSunOrMoonColor(float *r, float *g, float *b, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();

    if (sky && r && g && b) {
        Color c = sky->GetSunOrMoonColor();
        c.ScaleToUnitOrLess(tcsData->GetHDREnabled());

        if (conditions->GetLocation(tcsData).GetAltitude() > atmosphereHeight) {
            c = Interpolate(transmittedLightFromTopOfAtmosphere, c, tcsData);
        }

        *r = (float)(c.r);
        *g = (float)(c.g);
        *b = (float)(c.b);
    }
}

void Atmosphere::GetSunColor(float *r, float *g, float *b, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (sky && r && g && b) {
        Color c = sky->GetSunColor();
        c.ScaleToUnitOrLess(tcsData->GetHDREnabled());

        if (conditions->GetLocation(tcsData).GetAltitude() > atmosphereHeight) {
            c = Interpolate(transmittedLightFromTopOfAtmosphere, c, tcsData);
        }

        *r = (float)(c.r);
        *g = (float)(c.g);
        *b = (float)(c.b);
    }
}

void Atmosphere::GetMoonColor(float *r, float *g, float *b, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (sky && r && g && b) {
        Color c = sky->GetMoonColor();
        c.ScaleToUnitOrLess(tcsData->GetHDREnabled());

        if (conditions->GetLocation(tcsData).GetAltitude() > atmosphereHeight) {
            c = Interpolate(transmittedLightFromTopOfAtmosphere, c, tcsData);
        }

        *r = (float)(c.r);
        *g = (float)(c.g);
        *b = (float)(c.b);
    }
}
void Atmosphere::SetHaze(float hazeR, float hazeG, float hazeB, double hazeDepth, double hazeDensity, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    Sky* sky = tcsData->GetSky();
    if (hazeR >= 0.0f && hazeG >= 0.0f && hazeB >= 0.0f && hazeDepth >= 0.0 && hazeDensity >= 0.0) {
        if (sky) {
            Color hazeColor(hazeR, hazeG, hazeB);
            sky->SetHaze(hazeColor, hazeDepth, hazeDensity);
        }
    }
}

void Atmosphere::GetHaze(float& hazeR, float& hazeG, float& hazeB, double& hazeDepth, double& hazeDensity, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (sky) {
        Color hazeColor;
        sky->GetHaze(hazeColor, hazeDepth, hazeDensity);
        hazeR = (float)(hazeColor.r);
        hazeG = (float)(hazeColor.g);
        hazeB = (float)(hazeColor.b);
    }
}

void Atmosphere::GetAmbientColor(float *r, float *g, float *b, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (sky && r && g && b) {
        Color c = sky->GetToneMappedSkyLight();
        c.ScaleToUnitOrLess(tcsData->GetHDREnabled());

        if (conditions->GetLocation(tcsData).GetAltitude() > atmosphereHeight) {
            c = Interpolate(Color(0.0f, 0.0f, 0.0f, 1.0f), c, tcsData);
        }

        *r = (float)(c.r);
        *g = (float)(c.g);
        *b = (float)(c.b);
    }
}

void Atmosphere::GetHorizonColor(float pitchDegrees, float *r, float *g, float *b, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (sky && r && g && b) {

        ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
        SL_ASSERT(tcsData);

        Color c = sky->GetAverageHorizonColor(pitchDegrees, tcsData->GetCamera());
        c.ScaleToUnitOrLess(tcsData->GetHDREnabled());
        *r = (float)(c.r);
        *g = (float)(c.g);
        *b = (float)(c.b);
    }
}

void Atmosphere::GetHorizonColor(float yawDegrees, float pitchDegrees, float *r, float *g, float *b, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (sky && r && g && b) {

        ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
        SL_ASSERT(tcsData);

        Color c = sky->GetAverageHorizonColor(yawDegrees, pitchDegrees, tcsData->GetCamera());
        c.ScaleToUnitOrLess(tcsData->GetHDREnabled());
        *r = (float)(c.r);
        *g = (float)(c.g);
        *b = (float)(c.b);
    }
}

void Atmosphere::GetZenithColor(float *r, float *g, float *b, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (sky && r && g && b) {
        SL_ASSERT(tcsData);

        Color color = sky->SkyColorAt(Vector3(0.0, 1.0, 0.0), 0.0);
        *r = color.r;
        *g = color.g;
        *b = color.b;
    }
}

bool Atmosphere::GetFogEnabled(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    AtmosphereTcsUserData* tcsUserData = (AtmosphereTcsUserData*)GetOrCreateTcsUserData(tcsData);

    return tcsUserData->fogOn;
}

void Atmosphere::GetFogSettings(float *density, float *r, float *g, float *b, void* data) const
{
    if (density && r && g && b) {
        ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
        AtmosphereTcsUserData* tcsUserData = (AtmosphereTcsUserData*)GetOrCreateTcsUserData(tcsData);

        *density = (float)tcsUserData->fogDensity;

        *r = (float)tcsUserData->fogColor.x;
        *g = (float)tcsUserData->fogColor.y;
        *b = (float)tcsUserData->fogColor.z;
    }
}

void Atmosphere::ForceLightingRecompute(void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    AtmosphereTcsUserData* tcsUserData = (AtmosphereTcsUserData*)GetOrCreateTcsUserData(tcsData);

    bool& invalid = tcsUserData->invalid;
    invalid = true;
}

void Atmosphere::ShadeClouds(ThreadCameraStreamData* tcsData) const
{
    const Sky* sky = tcsData->GetSky();
    if (!sky) return;

    float sx, sy, sz;
    GetSunOrMoonPosition(&sx, &sy, &sz, tcsData);
    Vector3 sunPos(sx, sy, sz);
    Color sunColor = sky->GetSunOrMoonColorSeaLevel();

    if (hasLightingOverride) sunColor = Color(diffuseOverrideR, diffuseOverrideG, diffuseOverrideB);

    bool wantsUpdate = conditions->WantsLightingUpdate(tcsData);

    SL_MAP(int, CloudLayer*)& cloudLayer = conditions->GetCloudLayers();

    AtmosphereTcsUserData* tcsUserData = (AtmosphereTcsUserData*)GetOrCreateTcsUserData(tcsData);
    bool& invalid = tcsUserData->invalid;

    const bool infraRed = tcsData->GetInfraRedMode();
    for (SL_MAP(int, CloudLayer*)::const_iterator it = cloudLayer.begin(); it != cloudLayer.end(); it++) {
        Color black(0, 0, 0);
        (*it).second->Draw(0, &sunPos, infraRed ? &black : &sunColor, invalid,
                           wantsUpdate, conditions->GetMillisecondTimer()->GetMilliseconds(),
                           sky, false, this, tcsData->GetCamera(), tcsData->GetCamera(), OverriddenCameraPos(tcsData->GetCamera()), true, tcsData);
    }

    invalid = false;
}

void Atmosphere::UpdateEphemeris(void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    Sky* sky = tcsData->GetSky();
    Ephemeris* ephemeris = sky->GetEphemeris();

    if (ephemeris && conditions) {
        ephemeris->Update(conditions->GetTime(tcsData), conditions->GetLocation(tcsData), tcsData->GetRenderer()->GetIsRightHanded());
    }

    if (sky && conditions) {
        sky->SetLightPollution(conditions->GetLightPollution());
        sky->SetTurbidity(conditions->GetTurbidity());
        sky->UpdateFromEphemeris(conditions->GetLocation(tcsData).GetAltitude());

        const bool infraRed = tcsData->GetInfraRedMode();
        tcsData->GetMetaballRenderingParams()->SetAmbientColor(infraRed ? Color(0, 0, 0) : sky->GetToneMappedSkyLightSeaLevel());
        tcsData->GetCloudRenderingParams()->SetAmbientColor(infraRed ? Color(0, 0, 0) : sky->GetToneMappedSkyLightSeaLevel());

        bool overrideFog;
        double fogDensity, fogR, fogG, fogB;
        conditions->GetFog(overrideFog, fogDensity, fogR, fogG, fogB);
        if (!overrideFog) {
            BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();
            billboardParams->SetFogColor(infraRed ? Color(0, 0, 0) : sky->GetAverageHorizonColor(0.5, tcsData->GetCamera()));
        }
    }
}

void Atmosphere::UpdateSkyAndCloudsInternal(bool geocentricMode, ThreadCameraStreamData* tcsData) const
{
    AtmosphereTcsUserData* tcsUserData = (AtmosphereTcsUserData*)GetOrCreateTcsUserData(tcsData);

    const bool firstFrameCalled = tcsUserData->firstFrameCalled;

    if (!conditions || !firstFrameCalled) return;

    if (mutex) mutex->Lock();

    Ephemeris* ephemeris = tcsData->GetSky()->GetEphemeris();

    if (ephemeris && conditions) {
        ephemeris->Update(conditions->GetTime(tcsData), conditions->GetLocation(tcsData), tcsData->GetRenderer()->GetIsRightHanded());
    }

    unsigned long& now = tcsUserData->now;
    unsigned long& lastTime = tcsUserData->lastTime;
    double& dt = tcsUserData->dt;
    now = conditions->GetMillisecondTimer()->GetMilliseconds();

    dt = 0;
    if (lastTime != 0) {// && now > lastTime) {
        dt = (long)(now - lastTime) * 0.001;
    }
    lastTime = now;

    conditions->ApplyWind(dt, tcsData);

    SL_MAP(int, CloudLayer*)& cloudLayer = conditions->GetCloudLayers();
    SL_MAP(int, CloudLayer*)::const_iterator it;

    const Camera* camera = tcsData->GetCamera();

    for (it = cloudLayer.begin(); it != cloudLayer.end(); it++) {
        if (!(it->second->GetCulled(this, camera, camera, tcsData))) {
            it->second->DoUpdates(geocentricMode, this, now, camera, tcsData);
        }
    }

    bool& updated = tcsUserData->updated;
    updated = true;

    if (mutex) mutex->Unlock();
}

void Atmosphere::UpdateSkyAndClouds(bool geocentricMode, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    UpdateSkyAndCloudsInternal(geocentricMode, tcsData);
}

void Atmosphere::CullObjectsInternal(bool threadSafe, const Camera* sceneCamera, const Camera* cullCamera, ThreadCameraStreamData* tcsData) const
{
    AtmosphereTcsUserData* tcsUserData = (AtmosphereTcsUserData*)GetOrCreateTcsUserData(tcsData);

    const bool firstFrameCalled = tcsUserData->firstFrameCalled;

    if (!firstFrameCalled) return;

    if (mutex && threadSafe) mutex->Lock();

    const SL_MAP(int, CloudLayer*)& cloudLayers = conditions->GetCloudLayers();
    for (SL_MAP(int, CloudLayer *) ::const_iterator it = cloudLayers.begin(); it != cloudLayers.end(); it++) {
        it->second->DoCulling(sceneCamera, cullCamera, tcsData);
    }

    bool& culled = tcsUserData->culled;
    culled = true;

    if (mutex && threadSafe) mutex->Unlock();
}

void Atmosphere::CullObjects(bool threadSafe, const Camera* _camera, void* data)
{
    const Camera* camera = (_camera) ? _camera : defaultTcsData->GetCamera();
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    CullObjectsInternal(threadSafe, camera, camera, tcsData);
}

void Atmosphere::GetCloudBounds(double& minX, double& minY, double& minZ,
                                double& maxX, double& maxY, double& maxZ, void* data) const
{
    Vector3 minCloudBounds, maxCloudBounds;
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    if (tcsData == 0) {
        return;
    }

    GetCloudBounds(minCloudBounds, maxCloudBounds, tcsData);
    minX = minCloudBounds.x;
    minY = minCloudBounds.y;
    minZ = minCloudBounds.z;
    maxX = maxCloudBounds.x;
    maxY = maxCloudBounds.y;
    maxZ = maxCloudBounds.z;
}

double Atmosphere::GetSkyCoverage(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    return GetSkyCoverageInternal(tcsData->GetCamera(), tcsData);
}

double Atmosphere::GetSkyCoverageInternal(const Camera* camera, ThreadCameraStreamData* tcsData) const
{
    double skyCoverage = 0;
    SL_MAP(int, CloudLayer*)& cloudLayer = conditions->GetCloudLayers();
    SL_MAP(int, CloudLayer*)::const_iterator it;

    double sunniness = 1.0;
    for (it = cloudLayer.begin(); it != cloudLayer.end(); it++) {
        CloudLayer *layer = it->second;
        double coverage = layer->GetSkyCoverage(camera, tcsData);
        double percentSun = 1.0 - coverage;
        sunniness *= percentSun;
    }

    skyCoverage = 1.0 - sunniness;

    return skyCoverage;
}

void Atmosphere::GetCloudBounds(Vector3& minCloudBounds, Vector3& maxCloudBounds, ThreadCameraStreamData* tcsData) const
{
    if (!computeCloudBounds) return;

    maxCloudBounds = Vector3(-DBL_MAX, -DBL_MAX, -DBL_MAX);
    minCloudBounds = Vector3(DBL_MAX, DBL_MAX, DBL_MAX);

    SL_MAP(int, CloudLayer*)& cloudLayer = conditions->GetCloudLayers();
    SL_MAP(int, CloudLayer*)::const_iterator it;

    bool foundCloud = false;

    const Camera* camera = tcsData->GetCamera();

    for (it = cloudLayer.begin(); it != cloudLayer.end(); it++) {
        CloudLayer *layer = it->second;

        if (layer->IsRenderable(tcsData)) {

            if (layer->GetType() == STRATOCUMULUS || layer->GetType() == STRATOCUMULUS_PARTICLES) {
                double depth = layer->GetBaseLength();
                double width = layer->GetBaseWidth();
                double height = layer->GetThickness();

                Vector3 size(width, height, depth);
                size = size * camera->GetBasis3x3();
                size.x = MathUtils::Abs(size.x);
                size.y = MathUtils::Abs(size.y);
                size.z = MathUtils::Abs(size.z);

                Renderable *ren = dynamic_cast<Renderable*>(layer);
                if (ren) {
                    Vector3 pos = ren->GetWorldPosition(tcsData);
                    Vector3 min = pos - size;
                    Vector3 max = pos + size;

                    foundCloud = true;

                    if (min.x < minCloudBounds.x) minCloudBounds.x = min.x;
                    if (min.y < minCloudBounds.y) minCloudBounds.y = min.y;
                    if (min.z < minCloudBounds.z) minCloudBounds.z = min.z;
                    if (max.x > maxCloudBounds.x) maxCloudBounds.x = max.x;
                    if (max.y > maxCloudBounds.y) maxCloudBounds.y = max.y;
                    if (max.z > maxCloudBounds.z) maxCloudBounds.z = max.z;
                }
            } else {
                const SL_VECTOR(Cloud*)& clouds = layer->GetClouds(tcsData);
                for (SL_VECTOR(Cloud*)::const_iterator cit = clouds.begin(); cit != clouds.end(); cit++) {
                    foundCloud = true;

                    Cloud *cloud = *cit;
                    double width, depth, height;
                    cloud->GetSize(width, depth, height);
                    Vector3 size(width, height, depth);
                    size = size * camera->GetBasis3x3();
                    size.x = MathUtils::Abs(size.x);
                    size.y = MathUtils::Abs(size.y);
                    size.z = MathUtils::Abs(size.z);
                    Vector3 pos = cloud->GetWorldPosition(tcsData);
                    Vector3 min = pos - size;
                    Vector3 max = pos + size;

                    if (min.x < minCloudBounds.x) minCloudBounds.x = min.x;
                    if (min.y < minCloudBounds.y) minCloudBounds.y = min.y;
                    if (min.z < minCloudBounds.z) minCloudBounds.z = min.z;
                    if (max.x > maxCloudBounds.x) maxCloudBounds.x = max.x;
                    if (max.y > maxCloudBounds.y) maxCloudBounds.y = max.y;
                    if (max.z > maxCloudBounds.z) maxCloudBounds.z = max.z;
                }
            }
        }
    }

    if (!foundCloud) {
        maxCloudBounds = minCloudBounds = Vector3(0, 0, 0);
    }
}

static void UpdateRendererMVPMatrices(const Camera* camera, ThreadCameraStreamData* tcsData, bool setContext)
{
    Renderer* renderer = tcsData->GetRenderer();
    SL_ASSERT(renderer&&camera);

    if (camera->HasCameraMatrix() && camera->HasProjectionMatrix()) {
        if (setContext) {
            tcsData->SetContext();
        }
        renderer->SetModelviewMatrix(camera->GetModelViewMatrix());
        renderer->SetProjectionMatrix(camera->GetProjectionMatrix());
        renderer->SetHasExplicitMatrices(true);
    } else {
        renderer->SetHasExplicitMatrices(false);
    }
}

void Atmosphere::UpdateRendererDepthRange(Renderer* renderer, const Camera* camera) const
{
    if (camera->HasDepthRange()) {
        float nearDepth, farDepth;
        camera->GetDepthRange(nearDepth, farDepth);
        renderer->SetDepthRange(nearDepth, farDepth);
        renderer->SetHasExplicitDepthRange(true);
    } else {
        renderer->SetHasExplicitDepthRange(false);
    }
}

static void UpdateRendererViewport(Renderer* renderer, const Camera* camera)
{
    SL_ASSERT(renderer&&camera);

    if (camera->HasViewPort()) {
        int viewport[4];
        camera->GetViewport(viewport);
        renderer->SetViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        renderer->SetHasExplicitViewport(true);
    } else {
        renderer->SetHasExplicitViewport(false);
    }
}

bool Atmosphere::DrawSkyInternal(bool drawSky, bool geocentricMode, double skyboxSize,
                                 bool drawStars, bool clearDepth, bool drawSunAndMoon,
                                 double horizonOffsetAngle, double overcastBlend
                                 , ThreadCameraStreamData* tcsData) const
{
    tcsData->SetIsDrawingSky(true);
    AtmosphereTcsUserData* tcsUserData = (AtmosphereTcsUserData*)GetOrCreateTcsUserData(tcsData);

    tcsUserData->insideLoop = true;

    if (skyboxSize < 0) return false;

    LicenseManager::Heartbeat();

    if (tcsData == GetDefaultTcsData()) {
        tcsData->GetPrecipitationManager()->SetCurrentCameraHandle(tcsUserData->cameraHandle);
    }

    Renderer* renderer = tcsData->GetRenderer();

    renderer->HeartBeat();

    renderer->PushAllState();

    Camera* camera = tcsData->GetCamera();

    SL_ASSERT(renderer && camera);

    UpdateRendererMVPMatrices(camera, tcsData, true);
    UpdateRendererViewport(renderer, camera);
    UpdateRendererDepthRange(renderer, camera);

    tcsData->FrameStarted();

    bool& firstFrameCalled = tcsUserData->firstFrameCalled;

    firstFrameCalled = true;

    // Called after context is set.
    if (camera->HasDepthRange()) {
        float nearDepth, farDepth;
        camera->GetDepthRange(nearDepth, farDepth);
        renderer->SetReverseZ(nearDepth > farDepth);
    } else {
        renderer->SetReverseZ(false);
    }

    double H = scaleHeightMeters * unitScale;
    double isothermalEffect = exp(-(conditions->GetLocation(tcsData).GetAltitude() / H));
    if (isothermalEffect <= 0) isothermalEffect = 1E-9;
    if (isothermalEffect > 1.0) isothermalEffect = 1.0;

    BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();

    billboardParams->SetFogDensity(1.0 / conditions->GetVisibility() * isothermalEffect);

    // See if the application overrode our fog settings
    bool overrideFog;
    double fogDensity, fogR, fogG, fogB;
    conditions->GetFog(overrideFog, fogDensity, fogR, fogG, fogB);
    if (overrideFog) {

        const bool infraRed = tcsData->GetInfraRedMode();
        billboardParams->SetFogColor(infraRed ? Color(0, 0, 0) : Color(fogR, fogG, fogB));

        billboardParams->SetFogDensity(fogDensity);
    }

    bool& updated = tcsUserData->updated;
    bool& culled = tcsUserData->culled;
    if (!updated) {
        UpdateSkyAndCloudsInternal(geocentricMode, tcsData);
        updated = false;
    }

    if (!culled) {
        CullObjectsInternal(false, camera, camera, tcsData);
        culled = false;
    }

    if (mutex) mutex->Lock();

    // Turn off any shaders that were left on.
    renderer->UnbindShader();

    renderer->SetDefaultState();

    tcsData->GetCumulusCloudRenderingParams()->SetFrameStarted();

    Timer t("Render Atmosphere / Draw Sky");

    Sky* sky = tcsData->GetSky();

    if (sky && conditions) {

        if (overcastBlend > 0) {
            sky->SetOvercast(true);
            double ambientTransmission = 0.2, directTransmission = 0;
            Configuration::GetDoubleValue("stratus-light-transmission-ambient", ambientTransmission);
            Configuration::GetDoubleValue("stratus-light-transmission-direct", directTransmission);
            sky->SetOvercastParams(overcastBlend, ambientTransmission, directTransmission);
        } else {
            sky->SetOvercast(false);
        }

        SL_MAP(int, CloudLayer*)& cloudLayer = conditions->GetCloudLayers();
        SL_MAP(int, CloudLayer*)::const_iterator it;

        for (it = cloudLayer.begin(); it != cloudLayer.end(); it++) {
            it->second->UpdateOvercast(sky, camera, tcsData);
        }

        sky->SetLightPollution(conditions->GetLightPollution());
        sky->SetTurbidity(conditions->GetTurbidity());
        sky->UpdateFromEphemeris(conditions->GetLocation(tcsData).GetAltitude());
        Color ambient = hasLightingOverride ? Color(ambientOverrideR, ambientOverrideG, ambientOverrideB) : sky->GetToneMappedSkyLightSeaLevel();

        const bool infraRed = tcsData->GetInfraRedMode();
        tcsData->GetMetaballRenderingParams()->SetAmbientColor(infraRed ? Color(0, 0, 0) : ambient);
        tcsData->GetCloudRenderingParams()->SetAmbientColor(infraRed ? Color(0, 0, 0) : ambient);

        // See if the application overrode our fog settings
        bool overrideFog;
        double fogDensity, fogR, fogG, fogB;
        conditions->GetFog(overrideFog, fogDensity, fogR, fogG, fogB);
        if (overrideFog) {
            billboardParams->SetFogColor(infraRed ? Color(0, 0, 0) : Color(fogR, fogG, fogB));
            billboardParams->SetFogDensity(fogDensity);
        } else {
            billboardParams->SetFogColor(infraRed ? Color(0, 0, 0) : sky->GetAverageHorizonColor(0, camera));
        }
    }

    double savedDensity = billboardParams->GetFogDensity();
    billboardParams->SetFogDensity(1E-9);

    ShadeClouds(tcsData);

    billboardParams->SetFogDensity(savedDensity);

    if (!debugMode) {
        if (clearDepth) {
            renderer->ClearDepth();
        }

        //ren->PushAllState();

        if (sky) {
            if (drawSky || drawStars) {
                SkyDrawingParameters params;
                params.pass = 0;
                params.skyboxSize = skyboxSize;
                params.applicationFog = overrideFog;

                const bool infraRed = tcsData->GetInfraRedMode();
                params.infraRed = infraRed;

                params.drawStars = drawStars;
                params.clearDepth = clearDepth;
                params.drawSunAndMoon = drawSunAndMoon;
                params.horizonOffsetAngle = MathUtils::ToRadians(horizonOffsetAngle);
                params.drawSky = drawSky;

                params.lightPollution = (float)GetConditions().GetLightPollution();
                params.outputScale = GetOutputScale();

                params.geocentricMode = geocentricMode;

                unsigned long millis = GetConditions().GetMillisecondTimer()->GetMilliseconds();

                sky->Draw(params, GetRandomNumberGenerator(), millis, camera);
            }
        }

        //ren->PopAllState();

        const bool infraRed = tcsData->GetInfraRedMode();
        if (atmosphereFromSpace && !infraRed) {
            atmosphereFromSpace->Draw(sky->GetSunPositionGeographic(), Vector3(0, 0, 0),
                                      conditions->GetLocation(tcsData).GetAltitude(), this, camera, tcsData);
        }

        Profiler::GetProfiler()->Display();
    }

    // Turn off any shaders that were left on.
    renderer->UnbindShader();
    renderer->PopAllState();

    if (mutex) mutex->Unlock();

    tcsData->SetIsDrawingSky(false);

    return true;
}


bool Atmosphere::DrawSky(bool drawSky, bool geocentricMode, double skyboxSize, bool drawStars, bool clearDepth, bool drawSunAndMoon, CameraHandle cameraHandle, double horizonOffsetAngle, double overcastBlend, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    AtmosphereTcsUserData* tcsUserData = (AtmosphereTcsUserData*)GetOrCreateTcsUserData(tcsData);
    if (tcsData == defaultTcsData) {
        tcsUserData->cameraHandle = cameraHandle;
    } else {
        tcsUserData->cameraHandle = 0;
    }

    bool ok = DrawSkyInternal(drawSky, geocentricMode, skyboxSize, drawStars, clearDepth, drawSunAndMoon, horizonOffsetAngle, overcastBlend, tcsData);
    return ok;
}

static bool IsPlanar(const CloudLayer* cloudLayer)
{
    CloudTypes type = cloudLayer->GetType();
    bool isPlanar = (type == STRATUS || type == CIRRUS_FIBRATUS || type == CIRROCUMULUS || type == STRATOCUMULUS || type == STRATOCUMULUS_PARTICLES) && cloudLayer->GetIsInfinite();
    return isPlanar;
}

bool Atmosphere::GetShadowMapInternal(void *&texture, SilverLining::Matrix4 *lightViewProjMatrix,
                                      SilverLining::Matrix4 *worldToShadowMapTexCoord, bool wholeLayers, float shadowDarkness,
                                      bool moonShadows, double maxShadowObjectDistance, double shadowFactor, ThreadCameraStreamData* tcsData)
{
    ShadowMap* shadowMap = tcsData->GetOrCreateShadowMap(*this);

    Sky* sky = tcsData->GetSky();

    if (shadowMap && sky) {
        Renderer* renderer = tcsData->GetRenderer();
        renderer->PushAllState();

        // Turn off any shaders that were left on.
        renderer->UnbindShader();
        renderer->SetDefaultState();

        const Camera* camera = tcsData->GetCamera();

        SL_ASSERT(camera);
        UpdateRendererMVPMatrices(camera, tcsData, false);

        Vector3 sunPos = sky->GetSunPosition();
        sunPos.Normalize();
        Color sunColor = sky->GetSunColor();

        shadowMap->UpdateShadowCamera(sunPos, wholeLayers, maxShadowObjectDistance, camera, tcsData);

        const Camera* shadowCamera = shadowMap->GetShadowCamera();
        renderer->SetModelviewMatrix(shadowCamera->GetModelViewMatrix());
        renderer->SetProjectionMatrix(shadowCamera->GetProjectionMatrix());

        tcsData->FrameStarted();
        tcsData->GetCumulusCloudRenderingParams()->SetFrameStarted();

        BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();

        double savedDensity = billboardParams->GetFogDensity();
        billboardParams->SetFogDensity(1E-9);

        Color savedHazeColor;
        double savedHazeDepth, savedHazeDensity;
        sky->GetHaze(savedHazeColor, savedHazeDepth, savedHazeDensity);
        sky->SetHaze(savedHazeColor, 0, 1E-9);

        CullObjectsInternal(false, camera, shadowCamera, tcsData);

        AtmosphereTcsUserData* tcsUserData = (AtmosphereTcsUserData*)GetOrCreateTcsUserData(tcsData);
        bool& culled = tcsUserData->culled;

        culled = false;

        if (mutex) mutex->Lock();

        shadowMap->SortClouds(camera, enforceStrictWeak, tcsData);

        renderer->ClearBlendedObjects();

        tcsData->SetIsDrawingShadowMap(true);
        if (shadowMap->StartDrawing(sky->GetToneMappedSkyLightSeaLevel() * shadowDarkness, moonShadows, shadowFactor, camera, tcsData)) {

            const SL_VECTOR(CloudLayer*)& cloudLayers = shadowMap->GetSortedCloudLayers();

            for (SL_VECTOR(CloudLayer *)::const_iterator cloudLayerIter = cloudLayers.begin(); cloudLayerIter != cloudLayers.end(); cloudLayerIter++) {

                CloudLayer* cloudLayer = *cloudLayerIter;

                // don't do any fading?
                bool fadeOverride = false;
                Configuration::GetBoolValue("shadow-fade", fadeOverride);

                // If we encounter a planar cloud type, flush out everything behind it.
                bool isPlanar = IsPlanar(cloudLayer);

                if (isPlanar) {
                    shadowMap->DrawToRenderTexture(camera, shadowCamera, fadeOverride, tcsData);
                    renderer->ClearBlendedObjects();
                }

                cloudLayer->Draw(1, &sunPos, &sunColor, false, false, tcsUserData->now, sky, false, this, camera, shadowCamera, OverriddenCameraPos(shadowMap->GetLightWorldPos()), fadeOverride, tcsData);

                if (isPlanar) {
                    shadowMap->DrawToRenderTexture(camera, shadowCamera, fadeOverride, tcsData);
                    renderer->ClearBlendedObjects();
                }

            }

            shadowMap->DrawToRenderTexture(camera, shadowCamera, true, tcsData);
            renderer->ClearBlendedObjects();
        }

        tcsData->SetIsDrawingShadowMap(false);
        shadowMap->EndDrawing(tcsData);

        renderer->UnbindShader();

        billboardParams->SetFogDensity(savedDensity);
        sky->SetHaze(savedHazeColor, savedHazeDepth, savedHazeDensity);

        if (mutex) mutex->Unlock();

        renderer->SetModelviewMatrix(camera->GetModelViewMatrix());
        renderer->SetProjectionMatrix(camera->GetProjectionMatrix());
        renderer->PopAllState();

        texture = renderer->GetNativeTexture(shadowMap->GetTextureHandle(tcsData));
        *lightViewProjMatrix = shadowCamera->GetModelViewProjectionMatrix();
        *worldToShadowMapTexCoord = renderer->GetScaleAndBiasMatrix()*shadowCamera->GetModelViewProjectionMatrix();

        if (renderer->GetIsDirectX()) {
            lightViewProjMatrix->Transpose();
            worldToShadowMapTexCoord->Transpose();
        }

        if (tcsUserData->insideLoop) {
            tcsData->FrameStarted();
            tcsData->GetCumulusCloudRenderingParams()->SetFrameStarted();
            CullObjects(false, camera, tcsData);
            culled = false;
        }

        return true;
    }
    return false;
}

bool Atmosphere::GetShadowMap(void *&texture, Matrix4 *lightViewProjMatrix, Matrix4 *worldToShadowMapTexCoord,
                              bool wholeLayers, float shadowDarkness, bool moonShadows, double maxShadowObjectDistance, double shadowFactor, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? (ThreadCameraStreamData*)data : defaultTcsData;
    bool ok = GetShadowMapInternal(texture, lightViewProjMatrix, worldToShadowMapTexCoord, wholeLayers, shadowDarkness, moonShadows, maxShadowObjectDistance, shadowFactor, tcsData);
    return ok;
}

/*
void Atmosphere::ShadowMapReset(void)
{
if (shadowMap) {
shadowMap->Reset();
}
}
*/

bool Atmosphere::StarsVisible(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? (ThreadCameraStreamData*)data : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (sky) {
        return sky->StarsVisible();
    } else {
        return false;
    }
}

void Atmosphere::DrawLensFlare(bool geocentricMode, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? (ThreadCameraStreamData*)data : defaultTcsData;
    Renderer* renderer = tcsData->GetRenderer();

    renderer->PushAllState();

    // Turn off any shaders that were left on.
    renderer->UnbindShader();
    renderer->SetDefaultState();

    Camera* camera = tcsData->GetCamera();

    SL_ASSERT(camera);
    UpdateRendererMVPMatrices(camera, tcsData, false);
    UpdateRendererViewport(renderer, camera);

    unsigned long millis = GetConditions().GetMillisecondTimer()->GetMilliseconds();

    LensFlare* lensFlare = tcsData->GetLensFlare();
    if (lensFlare) {
        lensFlare->Update(geocentricMode, GetOutputScale(), millis, tcsData->GetCamera(), OverriddenBillboardMatrix(tcsData->GetCamera()));
        lensFlare->Draw(GetOutputScale(), tcsData->GetCamera());
    }

    // Turn off any shaders that were left on.
    renderer->UnbindShader();

    renderer->PopAllState();
}

bool Atmosphere::DrawObjectsInternal(bool drawClouds, bool drawPrecipitation,
                                     bool enableDepthTest, float crepuscularRaysIntensity, bool enableDepthWrites,
                                     bool backFaceCullClockWise,
                                     bool drawLightning, bool geocentricMode, ThreadCameraStreamData* tcsData) const
{
    tcsData->SetIsDrawingObjects(true);
    AtmosphereTcsUserData* tcsUserData = (AtmosphereTcsUserData*)GetOrCreateTcsUserData(tcsData);

    tcsUserData->insideLoop = false;

    if (debugMode) return true;

    Timer t("Render Atmosphere / Draw Objects");

    if (tcsData == GetDefaultTcsData()) {
        tcsData->GetPrecipitationManager()->SetCurrentCameraHandle(tcsUserData->cameraHandle);
    }

    Renderer* renderer = tcsData->GetRenderer();

    if (renderer == 0) {
        return true;
    }

    renderer->HeartBeat();

    const bool infraRed = tcsData->GetInfraRedMode();
    if (infraRed) crepuscularRaysIntensity = 0;

    if (conditions->GetCloudLayers().size() == 0) crepuscularRaysIntensity = 0;

    double skyCoverage = GetSkyCoverageInternal(tcsData->GetCamera(), tcsData);

    if (skyCoverage >= 1.0) crepuscularRaysIntensity = 0;

    renderer->PushAllState();

    // Turn off any shaders that were left on.
    renderer->UnbindShader();
    renderer->SetDefaultState();

    renderer->BackfaceCullClockwise(backFaceCullClockWise);

    const Camera* camera = tcsData->GetCamera();
    SL_ASSERT(camera);

    UpdateRendererMVPMatrices(camera, tcsData, false);
    UpdateRendererViewport(renderer, camera);
    UpdateRendererDepthRange(renderer, camera);

    Sky* sky = tcsData->GetSky();

    CrepuscularRays* crepuscularRays = tcsData->GetCrepuscularRays();

    const bool drawingCrepuscularRays = crepuscularRaysIntensity > 0 && crepuscularRays;
    if (drawingCrepuscularRays) {
        crepuscularRays->SetExposure(crepuscularRaysIntensity);
        crepuscularRays->UpdateLightSource(sky->GetEphemeris(), sky->GetSunColor(), geocentricMode, skyCoverage, camera, tcsData);
    }

    if (mutex) mutex->Lock();

    CloudLayerSorter* sorter = tcsUserData->cloudLayerSorter;
    sorter->Prime(conditions->GetCloudLayers(), tcsData);
    sorter->Sort(camera, enforceStrictWeak, tcsData);

    if (sky) {

        //sky->SetOvercast(false);
        sky->SetFogVolumeDistance(0);
        renderer->EnableFog(false);

        Vector3 sunPos = sky->GetSunOrMoonPosition();
        sunPos.Normalize();
        Color sunColor = sky->GetSunOrMoonColorSeaLevel();
        if (hasLightingOverride) {
            sunColor = Color(diffuseOverrideR, diffuseOverrideG, diffuseOverrideB);
        }

        if (tcsData->GetPrecipitationEnabled()) {
            tcsData->GetPrecipitationManager()->ClearPrecipitation();
        }

        tcsData->GetPrecipitationManager()->ApplyPrecipitation();

        const SL_VECTOR(CloudLayer*)& sortedCloudLayers = sorter->GetSortedCloudLayers();

        if (applyFogFromCloudPrecip) {
            for (SL_VECTOR(CloudLayer *)::const_iterator sit = sortedCloudLayers.begin(); sit != sortedCloudLayers.end(); sit++) {
                if ((*sit)->HasPrecipitationAtPosition(camera, tcsData)) {
                    (*sit)->ApplyPrecipitation(camera, tcsData);
                }
            }
        }

        // apply fog from precipitation
        BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();
        Color precipFogColor = billboardParams->GetFogColor();// sky->GetAverageHorizonColor(0);
        if (precipMonochrome) {
            precipFogColor.r = precipFogColor.b = precipFogColor.g;
        }
        double precipitationFogDensity = (tcsData->GetPrecipitationEnabled()) ? tcsData->GetPrecipitationManager()->GetFogDensity()
                                         : 0;

        if (precipitationFogDensity > 0) {
            renderer->EnableFog(true);
            renderer->ConfigureFog(precipitationFogDensity, 0, 0, infraRed ? Color(0, 0, 0) : precipFogColor);
            billboardParams->SetFogColor(infraRed ? Color(0, 0, 0) : precipFogColor);
            billboardParams->SetFogDensity(precipitationFogDensity);
            sky->SetFogVolumeDistance(1.0 / precipitationFogDensity);
        }

        for (SL_VECTOR(CloudLayer *)::const_iterator cloudLayerIter = sortedCloudLayers.begin(); cloudLayerIter != sortedCloudLayers.end(); cloudLayerIter++) {
            if (sky) {
                CloudLayer* cloudLayer = *cloudLayerIter;
                cloudLayer->ProcessAtmosphericEffects(sky, this, camera, tcsData);
            }
        }

        const bool fadeOverride = true;
        const Color black(0, 0, 0);

        bool sortAtLayerLevel = false;
        Configuration::GetBoolValue("sort-clouds-by-layer", sortAtLayerLevel);

        for (SL_VECTOR(CloudLayer *)::const_iterator cloudLayerIter = sortedCloudLayers.begin(); cloudLayerIter != sortedCloudLayers.end(); cloudLayerIter++) {

            CloudLayer* cloudLayer = *cloudLayerIter;

            // If we encounter a planar cloud type, flush out everything behind it.
            bool isPlanar = IsPlanar(cloudLayer);

            if (drawClouds && (isPlanar || sortAtLayerLevel)) {
                renderer->SortAndDrawBlendedObjects(enableDepthTest, enableDepthWrites, this, camera, camera, OverriddenBillboardMatrix(camera), OverriddenCameraPos(camera), fadeOverride, tcsData);
                // Skip this complication for vulkan.
                // This is because we can't do rtt (a separate begin/end render pass) easily
                if (drawingCrepuscularRays && renderer->GetIsVulkan()==false) {
                    crepuscularRays->DrawToRenderTexture(*this, camera, camera);
                }
                renderer->ClearBlendedObjects();
            }

            if (!applyFogFromCloudPrecip) {
                if (cloudLayer->HasPrecipitationAtPosition(camera, tcsData)) {
                    cloudLayer->ApplyPrecipitation(camera, tcsData);
                }
            }

            cloudLayer->ProcessFade(tcsUserData->now, tcsData);

            cloudLayer->Draw(1, &sunPos, infraRed ? &black : &sunColor, false, false, tcsUserData->now,
                             sky, drawLightning, this, camera, camera, OverriddenCameraPos(camera), fadeOverride, tcsData);

            if (drawClouds && (isPlanar || sortAtLayerLevel)) {
                renderer->SortAndDrawBlendedObjects(enableDepthTest, enableDepthWrites, this, camera, camera, OverriddenBillboardMatrix(camera), OverriddenCameraPos(camera), fadeOverride, tcsData);
                renderer->ClearBlendedObjects();
            }
        }

        if (drawClouds) {
            renderer->SortAndDrawBlendedObjects(enableDepthTest, enableDepthWrites, this, camera, camera, OverriddenBillboardMatrix(camera), OverriddenCameraPos(camera), fadeOverride, tcsData);
            // skip if vulkan. We will do it separately
            if (drawingCrepuscularRays && renderer->GetIsVulkan() == false) {
                crepuscularRays->DrawToRenderTexture(*this, camera, camera);
            }
        }

        if (tcsData->GetPrecipitationEnabled() && drawPrecipitation) {
            float directPrecipLight = 1.0f;
            Configuration::GetFloatValue("precipitation-direct-light", directPrecipLight);
            Color nLightColor = sunColor;
            nLightColor.ScaleToUnitOrLess(tcsData->GetHDREnabled());
            nLightColor = (nLightColor * directPrecipLight) + tcsData->GetMetaballRenderingParams()->GetAmbientColor();
            nLightColor.ClampToUnitOrLess(tcsData->GetHDREnabled());

            tcsData->GetPrecipitationManager()->Render(infraRed ? Color(0, 0, 0) : nLightColor);
        }
    }

    // skip if vulkan. We will do it separately
    if (drawingCrepuscularRays && renderer->GetIsVulkan()==false) {
        crepuscularRays->DrawRays(GetOutputScale(), tcsData);
    }

    LensFlare* lensFlare = tcsData->GetLensFlare();
    if (lensFlare) {
        if (enableLensFlare || enableOcclusionQuery) {

            const AtmosphericConditions& conditions = GetConditions();

            unsigned long millis = conditions.GetMillisecondTimer()->GetMilliseconds();
            lensFlare->Update(geocentricMode, GetOutputScale(), millis, camera, OverriddenBillboardMatrix(camera));
        }

        if (enableLensFlare) {
            lensFlare->Draw(GetOutputScale(), camera);
        }
    }

    // Turn off any shaders that were left on.
    renderer->UnbindShader();

    renderer->BackfaceCullClockwise(true);

    CalculateFramerate(tcsData);

    renderer->PopAllState();

    tcsUserData->fogOn = renderer->GetFogEnabled();
    tcsUserData->fogDensity = renderer->GetFogDensity();
    tcsUserData->fogColor = renderer->GetFogColor().ToVector3();

    if (mutex) mutex->Unlock();

    tcsData->SetIsDrawingObjects(false);

    return true;
}

bool Atmosphere::DrawObjects(bool drawClouds, bool drawPrecipitation, bool enableDepthTest, float crepuscularRaysIntensity,
                             bool enableDepthWrites, CameraHandle cameraHandle, bool cullClockWise, bool drawBackdrops,
                             bool drawLightning, bool geocentricMode,
                             void* data
                            )
{
    ThreadCameraStreamData* tcsData = (data) ? (ThreadCameraStreamData*)data : defaultTcsData;
    AtmosphereTcsUserData* tcsUserData = GetOrCreateTcsUserData(tcsData);
    if (tcsData == defaultTcsData) {
        tcsUserData->cameraHandle = cameraHandle;
    } else {
        tcsUserData->cameraHandle = 0;
    }

    bool ok = DrawObjectsInternal(drawClouds, drawPrecipitation, enableDepthTest, crepuscularRaysIntensity
                                  , enableDepthWrites, cullClockWise
                                  , drawLightning, geocentricMode, tcsData);
    return ok;
}

bool Atmosphere::GetEnvironmentMap(void *& texture, int facesToRender, bool floatingPoint, CameraHandle _cameraID, bool drawClouds, bool drawSunAndMoon, bool geocentricMode, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? (ThreadCameraStreamData*)data : defaultTcsData;

    Renderer* renderer = tcsData->GetRenderer();
    renderer->PushAllState();

    AtmosphereTcsUserData* tcsUserData = GetOrCreateTcsUserData(tcsData);

    bool savedFogOn = tcsUserData->fogOn;
    double savedFogDensity = tcsUserData->fogDensity;
    Vector3 savedFogColor = tcsUserData->fogColor;

    BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();
    Color savedBillboardColor = billboardParams->GetFogColor();
    double savedBillboardDensity = billboardParams->GetFogDensity();
    double savedRenderDensity, savedRenderStart, savedRenderEnd;
    Color savedRenderColor;
    renderer->GetFog(savedRenderDensity, savedRenderStart, savedRenderEnd, savedRenderColor);

    CameraHandle cameraID = 0;
    if (tcsData == defaultTcsData) {
        cameraID = _cameraID;
    }

    EnvironmentMap *environmentMap = 0;
    bool ok = tcsData->GetEnvironmentMap(environmentMap, *this, texture, facesToRender, floatingPoint, cameraID, drawClouds, drawSunAndMoon, geocentricMode);

    tcsUserData->fogColor = savedFogColor;
    tcsUserData->fogOn = savedFogOn;
    tcsUserData->fogDensity = savedFogDensity;
    billboardParams->SetFogColor(savedBillboardColor);
    billboardParams->SetFogDensity(savedBillboardDensity);
    renderer->ConfigureFog(savedRenderDensity, savedRenderStart, savedRenderEnd, savedRenderColor);

    renderer->PopAllState();

    return ok && environmentMap;
}

double SILVERLINING_API Atmosphere::GetFramerate(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? (ThreadCameraStreamData*)(data) : defaultTcsData;
    AtmosphereTcsUserData* tcsUserData = GetOrCreateTcsUserData(tcsData);
    return tcsUserData->framerateValue;
}

void Atmosphere::CalculateFramerate(ThreadCameraStreamData* tcsData) const
{
    AtmosphereTcsUserData* tcsUserData = GetOrCreateTcsUserData(tcsData);

#if defined(WIN32) || defined(WIN64)

    static LARGE_INTEGER frequency, start, end;
    static time_t lastSnapshot = 0;

    time_t tnow = time(NULL);

    if (frequency.QuadPart) {
        QueryPerformanceCounter(&end);

        if (tnow > lastSnapshot) {
            double seconds = (double)(end.QuadPart - start.QuadPart) / (double)frequency.QuadPart;

            tcsUserData->framerateValue = 1.0 / seconds;

            lastSnapshot = tnow;
        }
    }

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);

#else

    static time_t lastSeconds = 0;
    static unsigned short lastMillis = 0;
    static time_t lastSnapshot = 0;

    struct timeb tp;
    ftime(&tp);

    if (tp.time > lastSnapshot) {
        double elapsed = (tp.time - lastSeconds) + ((tp.millitm - lastMillis) * 0.001);
        tcsUserData->framerateValue = 1.0 / elapsed;
        lastSnapshot = tp.time;
    }

    lastSeconds = tp.time;
    lastMillis = tp.millitm;
#endif
}

void Atmosphere::GetObjects(SL_VECTOR(ObjectHandle)& destObjects, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? (ThreadCameraStreamData*)data : defaultTcsData;
    if (tcsData == 0) {
        return;
    }

    const SL_VECTOR(Renderable *)& objs = tcsData->GetRenderer()->GetBlendedObjects();

    destObjects.clear();

    for (SL_VECTOR(Renderable *) ::const_iterator it = objs.begin(); it != objs.end(); it++) {
        destObjects.push_back((ObjectHandle)*it);
    }
}

void Atmosphere::ClearObjects(void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? (ThreadCameraStreamData*)data : defaultTcsData;

    tcsData->GetRenderer()->ClearBlendedObjects();
}

float Atmosphere::GetObjectDistance(ObjectHandle obj, float ox, float oy, float oz, float sx, float sy, float sz, void* data)
{
    Renderable *r = (Renderable *)obj;

    Vector3 pos(ox, oy, oz);
    Vector3 sortPos(sx, sy, sz);

    ThreadCameraStreamData* tcsData = (data) ? (ThreadCameraStreamData*)data : defaultTcsData;

    Renderer* renderer = tcsData->GetRenderer();

    Matrix4 modelview;
    renderer->GetModelviewMatrix(&modelview);

    return (float)r->GetDistance(sortPos, pos, modelview, renderer->GetIsRightHanded(), defaultTcsData->GetCamera(), tcsData) + (float)r->GetDepthOffset();
}

float Atmosphere::GetObjectDistance(ObjectHandle obj, ObjectHandle obj2, float sx, float sy, float sz, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? (ThreadCameraStreamData*)data : defaultTcsData;

    Renderable *c1 = (Renderable*)obj;
    Renderable *c2 = (Renderable*)obj2;
    Vector3 p1, p2;
    if (c1) p1 = c1->GetSortPosition(tcsData->GetCamera(), tcsData);
    if (c2) p2 = c2->GetSortPosition(tcsData->GetCamera(), tcsData);

    Matrix4 modelview;;
    Renderer* renderer = tcsData->GetRenderer();
    renderer->GetModelviewMatrix(&modelview);

    Vector3 sortPos(sx, sy, sz);

    return c1 ? (float)(c1->GetDistance(sortPos, p2, modelview, renderer->GetIsRightHanded(), tcsData->GetCamera(), tcsData) + c1->GetDepthOffset()) : 0;
}

void Atmosphere::GetObjectPosition(ObjectHandle obj, float& x, float& y, float& z, void* data)
{
    if (obj) {
        Renderable *r = (Renderable *)obj;
        ThreadCameraStreamData* tcsData = (data) ? (ThreadCameraStreamData*)data : defaultTcsData;
        Vector3 pos = r->GetWorldPosition(tcsData);
        x = (float)pos.x;
        y = (float)pos.y;
        z = (float)pos.z;
    }
}

void Atmosphere::DrawObject(ObjectHandle obj, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? (ThreadCameraStreamData*)data : defaultTcsData;
    Renderable* renderable = (Renderable*)obj;

    renderable->DrawBlendedObject(this, defaultTcsData->GetCamera(), tcsData->GetCamera(), OverriddenBillboardMatrix(tcsData->GetCamera()), OverriddenCameraPos(tcsData->GetCamera()), true
                                  , tcsData);
}

void Atmosphere::ReloadConfigFile()
{
    Configuration::Clear();
    Configuration::Load("SilverLining.config");
    Configuration::Load("SilverLining.override");
}

void Atmosphere::SetConfigOption(const char *key, const char *value)
{
    if (key && value) {
        Configuration::Set(key, value);
    }
}

const char *Atmosphere::GetConfigOptionString(const char *key) const
{
    if (!key) return 0;

    const char * sval;
    if (Configuration::GetStringValue(key, sval)) {
        return sval;
    } else {
        return 0;
    }
}

double Atmosphere::GetConfigOptionDouble(const char *key) const
{
    if (!key) return 0;

    double val = 0;
    Configuration::GetDoubleValue(key, val);
    return val;
}

int SILVERLINING_API Atmosphere::GetConfigOptionInt(const char* key) const
{
    if (!key) return 0;

    int val = 0;
    Configuration::GetIntValue(key, val);
    return val;
}

bool Atmosphere::GetConfigOptionBoolean(const char *key) const
{
    if (!key) return false;

    bool val = 0;
    Configuration::GetBoolValue(key, val);
    return val;
}

bool Atmosphere::Serialize(std::ostream& s, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : GetDefaultTcsData();

    s.write((char *)&debugMode, sizeof(bool));

    const bool infraRed = tcsData->GetInfraRedMode();
    s.write((char *)&infraRed, sizeof(bool));

    s.write((char *)&disableFarCulling, sizeof(bool));
    s.write((char *)&enableLensFlare, sizeof(bool));
    s.write((char *)&enableOcclusionQuery, sizeof(bool));
    s.write((char *)&enableHDR, sizeof(bool));
    s.write((char *)&unitScale, sizeof(double));

    Vector3 up, right;
    const Camera* camera = tcsData->GetCamera();
    if (camera) {
        up = camera->GetUpVector();
        right = camera->GetRightVector();
    }
    up.Serialize(s);
    right.Serialize(s);

    conditions->Serialize(s, tcsData);

    return true;
}

bool Atmosphere::Unserialize(std::istream& s, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : GetDefaultTcsData();

    s.read((char *)&debugMode, sizeof(bool));

    bool infraRed = false;
    s.read((char *)&infraRed, sizeof(bool));
    tcsData->SetInfraRedMode(infraRed);

    s.read((char *)&disableFarCulling, sizeof(bool));
    s.read((char *)&enableLensFlare, sizeof(bool));
    s.read((char *)&enableOcclusionQuery, sizeof(bool));
    s.read((char *)&enableHDR, sizeof(bool));
    s.read((char *)&unitScale, sizeof(double));

    Vector3 up, right;
    up.Unserialize(s);
    right.Unserialize(s);
    SetUpVector(up.x, up.y, up.z);
    SetRightVector(right.x, right.y, right.z);

    conditions->Unserialize(this, s, tcsData);

    return true;
}

SL_VECTOR(unsigned int) Atmosphere::GetActivePlanarCloudShaders(void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;

    Renderer* renderer = tcsData->GetRenderer();
    SL_VECTOR(unsigned int) shaders;

    const SL_MAP(int, CloudLayer*)& cloudLayer = conditions->GetCloudLayers();
    for (SL_MAP(int, CloudLayer*)::const_iterator it = cloudLayer.begin(); it != cloudLayer.end(); it++) {
        ShaderHandle shader = it->second->GetShader(tcsData);
        if (shader) {
            if (renderer) shaders.push_back(renderer->GetShaderProgram(shader));
        }
    }

    return shaders;
}

unsigned int Atmosphere::GetBillboardShader(void *data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;

    const BillboardShader* billboardShader = tcsData->GetBillboardShader();
    if (billboardShader == 0) {
        return 0;
    }
    ShaderHandle handle = billboardShader->GetShaderHandle();

    return tcsData->GetRenderer()->GetShaderProgram(handle);
}

unsigned int Atmosphere::GetBillboardShaderInstanced(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;

    const BillboardShader* billboardShader = tcsData->GetBillboardShaderInstanced();
    if (billboardShader == 0) {
        return 0;
    }
    ShaderHandle handle = billboardShader->GetShaderHandle();

    return tcsData->GetRenderer()->GetShaderProgram(handle);
}

unsigned int Atmosphere::GetSkyShader(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (sky) {
        ShaderHandle handle = sky->GetSkyShader();
        return tcsData->GetRenderer()->GetShaderProgram(handle);
    }

    return 0;
}

unsigned int Atmosphere::GetStarShader(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    const Sky* sky = tcsData->GetSky();
    if (sky) {
        ShaderHandle handle = sky->GetStarShader();
        return tcsData->GetRenderer()->GetShaderProgram(handle);
    }

    return 0;
}

unsigned int Atmosphere::GetPrecipitationShader(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;

    ShaderHandle handle = tcsData->GetPrecipitationShaders()->GetShaderProgram();
    if (handle) {
        return tcsData->GetRenderer()->GetShaderProgram(handle);
    }

    return 0;
}

unsigned int Atmosphere::GetPrecipitationShader(int type, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;

    ShaderHandle handle = 0;
    if (type == CloudLayer::RAIN) {
        handle = tcsData->GetPrecipitationShaders()->GetShaderProgramInstancedRain();
    } else if (type == CloudLayer::DRY_SNOW || type == CloudLayer::WET_SNOW) {
        handle = tcsData->GetPrecipitationShaders()->GetShaderProgramInstancedSnow();
    } else if (type == CloudLayer::SLEET) {
        handle = tcsData->GetPrecipitationShaders()->GetShaderProgramInstancedSleet();
    }
    if (handle) {
        return tcsData->GetRenderer()->GetShaderProgram(handle);
    }

    return 0;
}

unsigned int Atmosphere::GetAtmosphericLimbShader(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    if (atmosphereFromSpace) {
        ShaderHandle handle = atmosphereFromSpace->GetShader(tcsData);
        if (handle) {
            return tcsData->GetRenderer()->GetShaderProgram(handle);
        }
    }

    return 0;
}

unsigned int Atmosphere::GetLineShader(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    return tcsData->GetRenderer()->GetLineShader();
}

void Atmosphere::ReloadShaders(const SL_VECTOR(unsigned int)& _userShaders, void* data)
{
    userShaders = _userShaders;
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;

    Renderer* renderer = tcsData->GetRenderer();

    renderer->SetUserShaders(userShaders);

    Sky* sky = tcsData->GetSky();
    if (sky) {
        sky->ReloadShaders();
    }

    tcsData->ReloadShaders();


    tcsData->GetPrecipitationShaders()->ReloadShaders();

    SL_MAP(int, CloudLayer*)& cloudLayer = conditions->GetCloudLayers();
    SL_MAP(int, CloudLayer*) ::iterator it;
    for (it = cloudLayer.begin(); it != cloudLayer.end(); it++) {
        it->second->ReloadShaders(tcsData);
    }
}

void Atmosphere::ForceSunAndMoon(double sunLat, double sunLon, double moonLat, double moonLon, double moonPhase, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    Ephemeris* ephemeris = tcsData->GetSky()->GetEphemeris();
    if (ephemeris) {
        ephemeris->ForceSunPosition(sunLon, sunLat);
        ephemeris->ForceMoonPosition(moonLon, moonLat);
        ephemeris->ForceMoonPhase(moonPhase);
    }
}

void Atmosphere::ForceSunAndMoon(double sunLat, double sunLon, double moonLat, double moonLon, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    Ephemeris* ephemeris = tcsData->GetSky()->GetEphemeris();
    if (ephemeris) {
        ephemeris->ForceSunPosition(sunLon, sunLat);
        ephemeris->ForceMoonPosition(moonLon, moonLat);
    }
}

void Atmosphere::ForceSunAndMoonHorizon(double sunAltitude, double sunAzimuth, double moonAltitude, double moonAzimuth, double moonPhase, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    Ephemeris* ephemeris = tcsData->GetSky()->GetEphemeris();
    if (ephemeris) {
        ephemeris->ForceSunPositionHoriz(sunAltitude, sunAzimuth);
        ephemeris->ForceMoonPositionHoriz(moonAltitude, moonAzimuth);
        ephemeris->ForceMoonPhase(moonPhase);
    }
}

void Atmosphere::ForceSunAndMoonHorizon(double sunAltitude, double sunAzimuth, double moonAltitude, double moonAzimuth, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    Ephemeris* ephemeris = tcsData->GetSky()->GetEphemeris();
    if (ephemeris) {
        ephemeris->ForceSunPositionHoriz(sunAltitude, sunAzimuth);
        ephemeris->ForceMoonPositionHoriz(moonAltitude, moonAzimuth);
    }
}

void Atmosphere::ForceMoonPhase(double moonPhase, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    Ephemeris* ephemeris = tcsData->GetSky()->GetEphemeris();
    if (ephemeris) {
        ephemeris->ForceMoonPhase(moonPhase);
    }
}

void Atmosphere::ForceMoonPhaseAngle(double moonPhaseAngle, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    Ephemeris* ephemeris = tcsData->GetSky()->GetEphemeris();
    if (ephemeris) {
        ephemeris->ForceMoonPhaseAngle(moonPhaseAngle);
    }
}

double Atmosphere::GetMoonPhase(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    Ephemeris* ephemeris = tcsData->GetSky()->GetEphemeris();
    if (ephemeris) {
        return ephemeris->GetMoonPhase();
    }

    return 0;
}

void Atmosphere::ClearForcedSunAndMoon(void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    Ephemeris* ephemeris = tcsData->GetSky()->GetEphemeris();
    if (ephemeris) {
        ephemeris->ClearForcedMoon();
        ephemeris->ClearForcedSun();
    }
}

void Atmosphere::ClearForcedMoonPhase(void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    Ephemeris* ephemeris = tcsData->GetSky()->GetEphemeris();
    if (ephemeris) {
        ephemeris->ClearForcedMoonPhase();
    }
}

void Atmosphere::SetUserDefinedVertString(const char *userString)
{
    if (userDefinedVertString) SL_FREE(userDefinedVertString);

    userDefinedVertString = (char *)SL_MALLOC(strlen(userString) + 1);

#ifdef _WIN32
#if _MSC_VER < 1400
    strncpy(userDefinedVertString, userString, strlen(userString) + 1);
#else
    strcpy_s(userDefinedVertString, strlen(userString) + 1, userString);
#endif
#else
    strncpy(userDefinedVertString, userString, strlen(userString) + 1);
#endif
}

void Atmosphere::SetUserDefinedFragString(const char *userString)
{
    if (userDefinedFragString) SL_FREE(userDefinedFragString);

    userDefinedFragString = (char *)SL_MALLOC(strlen(userString) + 1);

#ifdef _WIN32
#if _MSC_VER < 1400
    strncpy(userDefinedFragString, userString, strlen(userString) + 1);
#else
    strcpy_s(userDefinedFragString, strlen(userString) + 1, userString);
#endif
#else
    strncpy(userDefinedFragString, userString, strlen(userString) + 1);
#endif
}

const char * Atmosphere::GetUserDefinedVertString()
{
    return userDefinedVertString;
}

const char * Atmosphere::GetUserDefinedFragString()
{
    return userDefinedFragString;
}

ThreadCameraStreamData* Atmosphere::GetDefaultTcsData() const
{
    return defaultTcsData;
}

const SL_VECTOR(unsigned int)& Atmosphere::GetUserShaders(void) const
{
    return userShaders;
}

void Atmosphere::SetInfraRedMode(bool bInfraRed, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    if (tcsData->GetInfraRedMode() != bInfraRed) ForceLightingRecompute(tcsData);
    tcsData->SetInfraRedMode(bInfraRed);
}

bool Atmosphere::GetInfraRedMode(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : defaultTcsData;
    return tcsData->GetInfraRedMode();
}

void Atmosphere::SetOutputScale(const Vector3& scale)
{
    outputScale = scale;
}

void Atmosphere::SetOutputScale(float scale)
{
    outputScale = Vector3(scale, scale, scale);
}
Vector3 Atmosphere::GetOutputScale() const
{
    return outputScale;
}

void Atmosphere::GenerateCrepuscularRays(void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? (ThreadCameraStreamData*)data : defaultTcsData;

    SL_ASSERT(tcsData->GetRenderer()->GetIsVulkan());

    CrepuscularRays* crepuscularRays = tcsData->GetCrepuscularRays();
    const Camera* camera = tcsData->GetCamera();
    crepuscularRays->DrawToRenderTexture(*this, camera, camera);
}

void Atmosphere::DrawCrepuscularRays(void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? (ThreadCameraStreamData*)data : defaultTcsData;

    SL_ASSERT(tcsData->GetRenderer()->GetIsVulkan());

    CrepuscularRays* crepuscularRays = tcsData->GetCrepuscularRays();
    crepuscularRays->DrawRays(GetOutputScale(), tcsData);
}

void Atmosphere::SetEnvironmentMapFormat(ColorFormat format)
{
    environmentMapFormat = format;
}

void Atmosphere::SetUseShadowMap(bool val)
{
    useShadowMap = val;
}

void Atmosphere::SettingsChanged(void* environment, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? (ThreadCameraStreamData*)data : defaultTcsData;
    SL_ASSERT(tcsData->GetRenderer()->GetIsVulkan());
    tcsData->SettingsChanged(environment);
}
}
