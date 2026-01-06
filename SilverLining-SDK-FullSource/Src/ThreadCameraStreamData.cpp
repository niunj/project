// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.

#include "ThreadCameraStreamData.h"
#include "Camera.h"
#include "Renderer.h"
#include "BillboardRenderingParameters.h"
#include "MetaballRenderingParameters.h"
#include "BillboardRenderer.h"
#include "CloudRenderingParameters.h"
#include "CumulusCloudRenderingParameters.h"
#include "LuminanceMapper.h"
#include "BillboardShader.h"
#include "SkyShaders.h"
#include "StratocumulusShaders.h"
#include "FlareShader.h"
#include "CirrusShader.h"
#include "StratusShader.h"
#include "StarShader.h"
#include "GlareShader.h"
#include "AtmosphereFromSpaceShader.h"
#include "CrepuscularRays.h"
#include "BillboardBuffer.h"
#include "SLAssert.h"
#include "SLMap.h"
#include "Atmosphere.h"
#include "PrecipitationManager.h"
#include "PrecipitationShaders.h"
#include "ShadowMap.h"
#include "TextureManager.h"
#include "EnvironmentMap.h"
#include "LensFlare.h"
#include "DefaultRandomNumberGenerator.h"
#include "TcsUserData.h"
#include "mie.h"
#include "TGALoader.h"

namespace SilverLining
{
class MapUserDataImpl
{
public:
    MapUserDataImpl();
    virtual ~MapUserDataImpl();

    SL_MAP(const void*, TcsUserData*) mapUserPointerToUserData;
};

MapUserDataImpl::MapUserDataImpl()
{
    // nothing to do
}
MapUserDataImpl::~MapUserDataImpl()
{
    if (mapUserPointerToUserData.empty()==false) {
        std::cout << "potential leak elsewhere, deleting entries in: mapUserPointerToUserData" << std::endl;
    }
    for (SL_MAP(const void*, TcsUserData*)::iterator it = mapUserPointerToUserData.begin(); it != mapUserPointerToUserData.end(); ++it) {
        SL_DELETE(it->second);
    }
}

class MapCloudLayerUserDataImpl
{
public:
    MapCloudLayerUserDataImpl();
    virtual ~MapCloudLayerUserDataImpl();

    SL_MAP(const CloudLayer*, TcsUserData*) mapUserPointerToUserData;
};

MapCloudLayerUserDataImpl::MapCloudLayerUserDataImpl()
{
    // nothing to do
}
MapCloudLayerUserDataImpl::~MapCloudLayerUserDataImpl()
{
    if (mapUserPointerToUserData.empty()==false) {
        std::cout << "potential leak elsewhere, deleting entries in: mapUserPointerToUserData" << std::endl;
    }
    for (SL_MAP(const CloudLayer*, TcsUserData*)::iterator it = mapUserPointerToUserData.begin(); it != mapUserPointerToUserData.end(); ++it) {
        SL_DELETE(it->second);
    }
}

ThreadCameraStreamData::ThreadCameraStreamData(bool rightHanded, double unitScale, bool _deferred)
    : stream(0)
    , threadId(0)
    , drawingEnvMap(false)
    , drawingShadowMap(false)
    , sortScreenDepth(true)
    , context(0)
    , billboardShader(0)
    , billboardShaderInstanced(0)
    , skyShaders(0)
    , stratoCumulusShaders(0)
    , flareShader(0)
    , cirrusShader(0)
    , stratusShader(0)
    , starShader(0)
    , glareShader(0)
    , atmosphereFromSpaceShader(0)
    , renderer(0)
    , deferred(_deferred)
    , crepuscularRays(0)
    , sky(0)
    , billboardBufferProvider(0)
    , isDrawingSky(false)
    , isDrawingObjects(false)
    , forceImmediate(false)
    , precipitationManager(0)
    , precipitationShaders(0)
    , initialized(false)
    , shadowMap(0)
    , shadowMapDim(1024)
    , createTextures(true)
    , lensFlare(0)
    , infraRed(false)
    , timePassage(false)
    , baseTimeMS(0)
    , wantsLightingUpdate(true)
    , lastRelightPass(0), relightFrequency(-1)
    , mieLookupPixels(nullptr)
    , stratusTgaLoaded(false)
    , stratusTgaLoader(nullptr)
{
    camera = SL_NEW Camera(rightHanded);

    mapImpl = SL_NEW MapUserDataImpl();

    mapCloudLayerUserDataImpl = new MapCloudLayerUserDataImpl();

    billboardParams = SL_NEW BillboardRenderingParameters();
    metaballParams = SL_NEW MetaballRenderingParameters();

    billboardRenderer = SL_NEW BillboardRenderer(this);

    cloudParams = SL_NEW CloudRenderingParameters();
    cumulusCloudParams = SL_NEW CumulusCloudRenderingParameters(unitScale);

    luminanceMapper = SL_NEW LuminanceMapper();

    textureManager = SL_NEW TextureManager(this);

    defaultRandomNumberGenerator = SL_NEW DefaultRandomNumberGenerator();
    randomNumberGenerator = defaultRandomNumberGenerator;
}

ThreadCameraStreamData::~ThreadCameraStreamData()
{
    textureManager->ReleaseTextures();
    SL_DELETE textureManager;

    SL_DELETE camera;

    SL_DELETE mapImpl;

    SL_DELETE mapCloudLayerUserDataImpl;

    SL_DELETE billboardParams;
    SL_DELETE metaballParams;

    SL_DELETE billboardRenderer;

    SL_DELETE cloudParams;
    SL_DELETE cumulusCloudParams;

    SL_DELETE luminanceMapper;

    if (precipitationManager) {
        precipitationManager->DestroyPrecipitations();
        SL_DELETE precipitationManager;
    }

    if (crepuscularRays) {
        SL_DELETE crepuscularRays;
    }

    if (lensFlare) {
        SL_DELETE lensFlare;
    }

    if (sky) {
        SL_DELETE sky;
    }

    if (billboardBufferProvider) {
        SL_DELETE billboardBufferProvider;
    }

    if (shadowMap) {
        SL_DELETE shadowMap;
    }

    for (SL_MAP(CameraHandle, EnvironmentMap *)::iterator it = environmentMapMap.begin(); it != environmentMapMap.end(); it++) {
        SL_DELETE it->second;
    }

    Shutdown();

    SL_DELETE renderer;

    SL_DELETE defaultRandomNumberGenerator;
    defaultRandomNumberGenerator = 0;

    SL_DELETE mieLookupPixels;
    SL_DELETE stratusTgaLoader;
}

const Camera* ThreadCameraStreamData::GetCamera(void) const
{
    return camera;
}

Camera* ThreadCameraStreamData::GetCamera(void)
{
    return camera;
}

void ThreadCameraStreamData::SetUserData(const void* userPointer, TcsUserData* userData)
{
    SL_ASSERT(mapImpl->mapUserPointerToUserData.find(userPointer) == mapImpl->mapUserPointerToUserData.end());
    mapImpl->mapUserPointerToUserData.insert(std::make_pair(userPointer, userData));
}
TcsUserData* ThreadCameraStreamData::GetUserData(const void* userPointer)
{
    SL_MAP(const void*, TcsUserData*)::const_iterator it = mapImpl->mapUserPointerToUserData.find(userPointer);
    if (it != mapImpl->mapUserPointerToUserData.end()) {
        return it->second;
    }
    return 0;
}
bool ThreadCameraStreamData::UserDataExists(const void* userPointer) const
{
    return mapImpl->mapUserPointerToUserData.find(userPointer) != mapImpl->mapUserPointerToUserData.end();
}

void ThreadCameraStreamData::EraseUserData(const void* userPointer)
{
    mapImpl->mapUserPointerToUserData.erase(userPointer);
}

// Named Variants
//
void ThreadCameraStreamData::SetCloudLayerTcsUserData(const CloudLayer* cloudLayer, TcsUserData* userData)
{
    SL_ASSERT(mapCloudLayerUserDataImpl->mapUserPointerToUserData.find(cloudLayer) == mapCloudLayerUserDataImpl->mapUserPointerToUserData.end());
    mapCloudLayerUserDataImpl->mapUserPointerToUserData.insert(std::make_pair(cloudLayer, userData));
}

TcsUserData* ThreadCameraStreamData::GetCloudLayerTcsUserData(const CloudLayer* cloudLayer)
{
    SL_MAP(const CloudLayer*, TcsUserData*)::const_iterator it = mapCloudLayerUserDataImpl->mapUserPointerToUserData.find(cloudLayer);
    if (it != mapCloudLayerUserDataImpl->mapUserPointerToUserData.end()) {
        return it->second;
    }
    return 0;
}

bool ThreadCameraStreamData::CloudLayerTcsUserDataExists(const CloudLayer* cloudLayer)
{
    return mapCloudLayerUserDataImpl->mapUserPointerToUserData.find(cloudLayer) != mapCloudLayerUserDataImpl->mapUserPointerToUserData.end();
}

void ThreadCameraStreamData::EraseCloudLayerTcsUserData(const CloudLayer* cloudLayer)
{
    if (CloudLayerTcsUserDataExists(cloudLayer)) {
        mapCloudLayerUserDataImpl->mapUserPointerToUserData.erase(cloudLayer);
    }
}

Renderer* ThreadCameraStreamData::GetRenderer(void)
{
    if (renderer == 0) {
        renderer = SL_NEW Renderer(*this);
    }
    return renderer;
}
const Renderer* ThreadCameraStreamData::GetRenderer(void) const
{
    SL_ASSERT(renderer);
    return renderer;
}

const BillboardRenderingParameters* ThreadCameraStreamData::GetBillboardRenderingParams(void) const
{
    return billboardParams;
}

//! internal. Do not use directly. Get the parameters, non-const version
BillboardRenderingParameters* ThreadCameraStreamData::GetBillboardRenderingParams(void)
{
    return billboardParams;
}

const MetaballRenderingParameters* ThreadCameraStreamData::GetMetaballRenderingParams(void) const
{
    return metaballParams;
}

MetaballRenderingParameters* ThreadCameraStreamData::GetMetaballRenderingParams(void)
{
    return metaballParams;
}

const BillboardRenderer* ThreadCameraStreamData::GetBillboardRenderer(void) const
{
    return billboardRenderer;
}

BillboardRenderer* ThreadCameraStreamData::GetBillboardRenderer(void)
{
    return billboardRenderer;
}


const CloudRenderingParameters* ThreadCameraStreamData::GetCloudRenderingParams(void) const
{
    return cloudParams;
}

CloudRenderingParameters* ThreadCameraStreamData::GetCloudRenderingParams(void)
{
    return cloudParams;
}


const CumulusCloudRenderingParameters* ThreadCameraStreamData::GetCumulusCloudRenderingParams(void) const
{
    return cumulusCloudParams;
}

CumulusCloudRenderingParameters* ThreadCameraStreamData::GetCumulusCloudRenderingParams(void)
{
    return cumulusCloudParams;
}

const LuminanceMapper* ThreadCameraStreamData::GetLuminanceMappper(void) const
{
    return luminanceMapper;
}

LuminanceMapper* ThreadCameraStreamData::GetLuminanceMappper(void)
{
    return luminanceMapper;
}

bool ThreadCameraStreamData::GetIsDrawingEnvMap() const
{
    return drawingEnvMap;
}

void ThreadCameraStreamData::SetIsDrawingEnvMap(bool val)
{
    drawingEnvMap = val;
}

bool ThreadCameraStreamData::GetIsDrawingShadowMap() const
{
    return drawingShadowMap;
}

void ThreadCameraStreamData::SetIsDrawingShadowMap(bool val)
{
    drawingShadowMap = val;
}

bool ThreadCameraStreamData::GetSortByScreenDepth(void) const
{
    return sortScreenDepth;
}

void ThreadCameraStreamData::SetSortByScreenDepth(bool val)
{
    sortScreenDepth = val;
}

void ThreadCameraStreamData::ReloadBillboardShader(void)
{
    if (billboardShader) {
        SL_DELETE billboardShader;
        billboardShader = 0;
    }
    if (billboardShaderInstanced) {
        SL_DELETE billboardShaderInstanced;
        billboardShaderInstanced = 0;
    }
}

void ThreadCameraStreamData::ReloadShaders()
{
    ReloadBillboardShader();
    ReloadSkyShaders();
    ReloadStratocumulusShaders();
    ReloadCirrusShader();
    ReloadStratusShader();
    ReloadStarShader();
}

const BillboardShader* ThreadCameraStreamData::GetBillboardShader(void) const
{
    if (billboardShader == 0) {
        billboardShader = SL_NEW BillboardShader(const_cast<ThreadCameraStreamData*>(this), false);
    }
    return billboardShader;
}

const BillboardShader* ThreadCameraStreamData::GetBillboardShaderInstanced(void) const
{
    if (billboardShaderInstanced == 0) {
        billboardShaderInstanced = SL_NEW BillboardShader(const_cast<ThreadCameraStreamData*>(this), true);
    }
    return billboardShaderInstanced;
}

const SkyShaders* ThreadCameraStreamData::GetSkyShaders(bool simpleShader) const
{
    if (skyShaders == 0) {
        skyShaders = SL_NEW SkyShaders(const_cast<ThreadCameraStreamData*>(this), simpleShader);
    }
    return skyShaders;
}

const StratocumulusShaders* ThreadCameraStreamData::GetStratocumulusShaders(void) const
{
    if (stratoCumulusShaders == 0) {
        stratoCumulusShaders = SL_NEW StratocumulusShaders(const_cast<ThreadCameraStreamData*>(this));
    }
    return stratoCumulusShaders;
}

const FlareShader* ThreadCameraStreamData::GetFlareShader(void) const
{
    if (flareShader == 0) {
        flareShader = SL_NEW FlareShader(const_cast<ThreadCameraStreamData*>(this));
    }
    return flareShader;
}

void ThreadCameraStreamData::ReloadSkyShaders(void)
{
    SL_DELETE skyShaders;
    skyShaders = 0;
}

void ThreadCameraStreamData::ReloadStratocumulusShaders(void)
{
    SL_DELETE stratoCumulusShaders;
    stratoCumulusShaders = 0;
}

const CirrusShader* ThreadCameraStreamData::GetCirrusShader(void) const
{
    if (cirrusShader == 0) {
        cirrusShader = SL_NEW CirrusShader(const_cast<ThreadCameraStreamData*>(this));
    }
    return cirrusShader;
}

void ThreadCameraStreamData::ReloadCirrusShader(void)
{
    SL_DELETE cirrusShader;
    cirrusShader = 0;
}


const StratusShader* ThreadCameraStreamData::GetStratusShader(void) const
{
    if (stratusShader == 0) {
        stratusShader = SL_NEW StratusShader(const_cast<ThreadCameraStreamData*>(this));
    }
    return stratusShader;
}

void ThreadCameraStreamData::ReloadStratusShader(void)
{
    SL_DELETE stratusShader;
    stratusShader = 0;
}

const StarShader* ThreadCameraStreamData::GetStarShader(void) const
{
    if (starShader == 0) {
        starShader = SL_NEW StarShader(const_cast<ThreadCameraStreamData*>(this));
    }
    return starShader;
}

void ThreadCameraStreamData::ReloadStarShader(void)
{
    SL_DELETE starShader;
    starShader = 0;
}

const GlareShader* ThreadCameraStreamData::GetGlareShader(void) const
{
    if (glareShader == 0) {
        glareShader = SL_NEW GlareShader(const_cast<ThreadCameraStreamData*>(this));
    }
    return glareShader;
}

GlareShader* ThreadCameraStreamData::GetGlareShader(void)
{
    if (glareShader == 0) {
        glareShader = SL_NEW GlareShader(this);
    }
    return glareShader;
}

const AtmosphereFromSpaceShader* ThreadCameraStreamData::GetAtmosphereFromSpaceShader(bool doShadow) const
{
    if (atmosphereFromSpaceShader == 0) {
        atmosphereFromSpaceShader = SL_NEW AtmosphereFromSpaceShader(const_cast<ThreadCameraStreamData*>(this), doShadow);
    }
    return atmosphereFromSpaceShader;
}

bool ThreadCameraStreamData::GetDeferred(void) const
{
    return deferred;
}

CrepuscularRays* ThreadCameraStreamData::GetCrepuscularRays(void)
{
    return crepuscularRays;
}

const CrepuscularRays* ThreadCameraStreamData::GetCrepuscularRays(void) const
{
    return crepuscularRays;
}

Sky* ThreadCameraStreamData::GetSky(void)
{
    return sky;
}

const Sky* ThreadCameraStreamData::GetSky(void) const
{
    return sky;
}

LensFlare* ThreadCameraStreamData::GetLensFlare(void)
{
    return lensFlare;
}

const LensFlare* ThreadCameraStreamData::GetLensFlare(void) const
{
    return lensFlare;
}

const BillboardBufferProvider* ThreadCameraStreamData::GetBillboardBufferProvider(void) const
{
    return billboardBufferProvider;
}

int ThreadCameraStreamData::Initialize(int rendererType, bool rightHanded, void* environment, bool avoidStalls)
{
    SL_VECTOR(unsigned int) _userShaders;
    return Initialize(rendererType, rightHanded, environment, avoidStalls, _userShaders);
}

int ThreadCameraStreamData::Initialize(int rendererType, bool rightHanded, void *environment, bool avoidStalls, const SL_VECTOR(unsigned int)& userShaders)
{
    Renderer* renderer = GetRenderer();
    int result = renderer->Initialize(rendererType, rightHanded, environment, &context, avoidStalls, deferred, userShaders);
    initialized = (result == Atmosphere::E_NOERROR);
    return result;
}

void ThreadCameraStreamData::SetUpAndRightVector(const Vector3& upVector, const Vector3& rightVector)
{
    camera->SetUpVector(upVector);
    camera->SetRightVector(rightVector);
}

void ThreadCameraStreamData::Initialize(const Atmosphere& atmosphere)
{
    GetRenderer()->SetUserShaders(atmosphere.GetUserShaders());

    SetUpAndRightVector(atmosphere.GetDefaultTcsData()->GetCamera()->GetUpVector()
                        , atmosphere.GetDefaultTcsData()->GetCamera()->GetRightVector());

    SetForceImmediate(true);
    billboardBufferProvider = SL_NEW BillboardBufferProvider(this);

    if (!crepuscularRays) {
        crepuscularRays = SL_NEW CrepuscularRays(this);
    }

    sky = SL_NEW Sky(atmosphere, this);

    //lensFlare = SL_NEW LensFlare(atmosphere, GetSky()->GetEphemeris(), atmosphere.GetRandomNumberGenerator(), this);

    SetForceImmediate(false);

    //if (this != atmosphere.GetDefaultTcsData()) {
    InitializePrecipitationManager(atmosphere);
    precipitationManager->GetRain(0, this);
    precipitationManager->GetSleet(0, this);
    precipitationManager->GetSnow(0, this);
    //}

    Configuration::GetIntValue("shadow-map-texture-size", shadowMapDim);

    if (atmosphere.TexturesAreShared() == false && createTextures) {
        textureManager->CreateTextures(atmosphere.GetRandomNumberGenerator());
    }

    if (environmentMapFormat!=FORMAT_UNKNOWN && GetRenderer()->GetIsVulkan()) {
        auto environmentMap = SL_NEW EnvironmentMap(this, environmentMapFormat);
        environmentMapMap[0] = environmentMap;
    }

    if (useShadowMap) {
        GetOrCreateShadowMap(atmosphere);
    }
}

bool ThreadCameraStreamData::IsInitialized(void) const
{
    return initialized;
}

void ThreadCameraStreamData::Shutdown(void)
{
    if (billboardShader) {
        SL_DELETE billboardShader;
        billboardShader = 0;
    }
    if (billboardShaderInstanced) {
        SL_DELETE billboardShaderInstanced;
        billboardShaderInstanced = 0;
    }
    if (skyShaders) {
        SL_DELETE skyShaders;
        skyShaders = 0;
    }
    if (flareShader) {
        SL_DELETE flareShader;
        flareShader = 0;
    }
    if (cirrusShader) {
        SL_DELETE cirrusShader;
        cirrusShader = 0;
    }
    if (stratusShader) {
        SL_DELETE stratusShader;
        stratusShader = 0;
    }
    if (stratoCumulusShaders) {
        SL_DELETE stratoCumulusShaders;
        stratoCumulusShaders = 0;
    }
    if (starShader) {
        SL_DELETE starShader;
        starShader = 0;
    }
    if (glareShader) {
        SL_DELETE glareShader;
        glareShader = 0;
    }
    if (atmosphereFromSpaceShader) {
        SL_DELETE atmosphereFromSpaceShader;
        atmosphereFromSpaceShader = 0;
    }

    if (precipitationShaders) {
        SL_DELETE precipitationShaders;
        precipitationShaders = 0;
    }

    if (context != 0) {
        GetRenderer()->Shutdown(context);
        context = 0;
    }
}

void ThreadCameraStreamData::DeviceLost()
{
    GetRenderer()->DeviceLost(context);
}

void ThreadCameraStreamData::DeviceReset()
{
    GetRenderer()->DeviceReset(context);
}

void ThreadCameraStreamData::FrameStarted()
{
    GetRenderer()->FrameStarted(context);
}

void ThreadCameraStreamData::SetContext()
{
    GetRenderer()->SetContext(context);
}

bool ThreadCameraStreamData::GetIsDrawingSky(void) const
{
    return isDrawingSky;
}

bool ThreadCameraStreamData::GetIsDrawingObjects(void) const
{
    return isDrawingObjects;
}

void ThreadCameraStreamData::SetIsDrawingSky(bool val)
{
    isDrawingSky = val;
}

void ThreadCameraStreamData::SetIsDrawingObjects(bool val)
{
    isDrawingObjects = val;
}

void ThreadCameraStreamData::SetForceImmediate(bool val)
{
    forceImmediate = val;
    renderer->SetForceImmediate(val);
}
bool ThreadCameraStreamData::GetForceImmediate(void) const
{
    return forceImmediate;
}

void ThreadCameraStreamData::ExecuteStream(void)
{
    renderer->ExecuteStream();
}

void ThreadCameraStreamData::SetStream(void* stream, int frameIndex)
{
    renderer->SetStream(stream, frameIndex);
}

void ThreadCameraStreamData::DeInitialize(const Atmosphere& atmosphere)
{
    TcsUserData* userData = this->GetUserData(&atmosphere);
    if (userData) {
        this->EraseUserData(&atmosphere);
        SL_DELETE userData;
    }
}

const PrecipitationShaders* ThreadCameraStreamData::GetPrecipitationShaders(void) const
{
    if (precipitationShaders == 0) {
        precipitationShaders = SL_NEW PrecipitationShaders(const_cast<ThreadCameraStreamData*>(this));
    }
    return precipitationShaders;
}

PrecipitationShaders* ThreadCameraStreamData::GetPrecipitationShaders(void)
{
    if (precipitationShaders == 0) {
        precipitationShaders = SL_NEW PrecipitationShaders(const_cast<ThreadCameraStreamData*>(this));
    }
    return precipitationShaders;
}
void ThreadCameraStreamData::InitializePrecipitationManager(const Atmosphere& atmosphere)
{
    SL_ASSERT(precipitationManager == 0);

    precipitationManager = SL_NEW PrecipitationManager(atmosphere, this);
    precipitationManager->Initialize(&atmosphere);
}

const PrecipitationManager* ThreadCameraStreamData::GetPrecipitationManager(void) const
{
    return precipitationManager;
}

PrecipitationManager* ThreadCameraStreamData::GetPrecipitationManager(void)
{
    return precipitationManager;
}

bool ThreadCameraStreamData::GetPrecipitationEnabled(void) const
{
    static const bool precipitationEnabled = true;
    return precipitationEnabled;
}


const ShadowMap* ThreadCameraStreamData::GetOrCreateShadowMap(const Atmosphere& atmosphere) const
{
    if (!shadowMap) {
        shadowMap = SL_NEW ShadowMap(atmosphere, shadowMapDim, const_cast<ThreadCameraStreamData*>(this));
    }
    return shadowMap;
}

//! internal. do not use!
ShadowMap* ThreadCameraStreamData::GetOrCreateShadowMap(const Atmosphere& atmosphere)
{
    if (!shadowMap) {
        shadowMap = SL_NEW ShadowMap(atmosphere, shadowMapDim, this);
    }
    return shadowMap;
}
const ShadowMap* ThreadCameraStreamData::GetShadowMapObject(void) const
{
    return shadowMap;
}

void ThreadCameraStreamData::DeleteShadowMap(void)
{
    if (shadowMap) {
        SL_DELETE shadowMap;
        shadowMap = 0;
    }
}

void ThreadCameraStreamData::DeleteEnvironmentMaps(void)
{
    for (SL_MAP(CameraHandle, EnvironmentMap*)::iterator it = environmentMapMap.begin(); it != environmentMapMap.end(); it++) {
        SL_DELETE it->second;
    }
    environmentMapMap.clear();
}

const TextureManager* ThreadCameraStreamData::GetTextureManager(void) const
{
    return textureManager;
}
TextureManager* ThreadCameraStreamData::GetTextureManager(void)
{
    return textureManager;
}

void ThreadCameraStreamData::setCreateTextures(bool create)
{
    createTextures = create;
}

bool ThreadCameraStreamData::GetEnvironmentMap(EnvironmentMap*& environmentMap, Atmosphere& atmosphere, void *& texture, int facesToRender, bool floatingPoint, CameraHandle cameraID, bool drawClouds, bool drawSunAndMoon, bool geocentricMode)
{
    if (environmentMapMap.find(cameraID) != environmentMapMap.end()) {
        environmentMap = environmentMapMap[cameraID];
    }

    if (!environmentMap) {
        environmentMap = SL_NEW EnvironmentMap(this, FORMAT_UNKNOWN);
        environmentMapMap[cameraID] = environmentMap;
    }

    if (environmentMap) {
        bool ok = environmentMap->Create(facesToRender, floatingPoint, drawClouds, drawSunAndMoon, geocentricMode, atmosphere);
        texture = environmentMap->GetNativeTexture();
        return true;
    }
    return false;
}

bool ThreadCameraStreamData::DrawIndirect() const
{
    bool drawIndirect = false;
    Configuration::GetBoolValue("cumulus-draw-bindless-indirect", drawIndirect);
    drawIndirect = drawIndirect
                   && GetRenderer()->HasBindlessIndirectRendering()
                   && GetBillboardShaderInstanced()
                   && GetDeferred() == false;
    return drawIndirect;
}

void ThreadCameraStreamData::SetRandomNumberGenerator(RandomNumberGenerator* rng)
{
    if (rng) {
        randomNumberGenerator = rng;
    }
}

RandomNumberGenerator* ThreadCameraStreamData::GetRandomNumberGenerator() const
{
    return randomNumberGenerator;
}

void ThreadCameraStreamData::SetInfraRedMode(bool bInfraRed)
{
    infraRed = bInfraRed;
}

bool ThreadCameraStreamData::GetInfraRedMode() const
{
    return infraRed;
}

void ThreadCameraStreamData::SetTime(const LocalTime& pTime, unsigned long timerMilliseconds)
{
    localTime = pTime;
    wantsLightingUpdate = true;
    baseTimeMS = timerMilliseconds;
}

const LocalTime& ThreadCameraStreamData::GetTime(unsigned long timerMilliseconds)
{
    if (timePassage) {
        long dt = timerMilliseconds - baseTimeMS;
        long seconds = (long)((float(dt) * 0.001f));
        adjustedTime = localTime;
        adjustedTime.AddSeconds(seconds);
        return adjustedTime;
    }

    return localTime;
}

void ThreadCameraStreamData::SetLocation(const Location& _location)
{
    location = _location;
}

void ThreadCameraStreamData::EnableTimePassage(bool enabled, long relightFrequencyMS, unsigned long timerMilliseconds)
{
    timePassage = enabled;
    relightFrequency = relightFrequencyMS;
    lastRelightPass = baseTimeMS = timerMilliseconds;
}

bool ThreadCameraStreamData::WantsLightingUpdate(unsigned long timerMilliseconds)
{
    if (wantsLightingUpdate) {
        wantsLightingUpdate = false;
        return true;
    }

    if (!timePassage) {
        return true;
    }

    if (relightFrequency == -1) {
        return false;
    }

    long dt = timerMilliseconds - lastRelightPass;
    if (dt > relightFrequency) {
        lastRelightPass = timerMilliseconds;
        return true;
    }

    return false;
}

const Location& ThreadCameraStreamData::GetLocation() const
{
    return location;
}

Location& ThreadCameraStreamData::GetLocation()
{
    return location;
}

LocalTime& ThreadCameraStreamData::GetTime()
{
    return localTime;
}

LocalTime& ThreadCameraStreamData::GetAdjustedTime()
{
    return adjustedTime;
}

bool& ThreadCameraStreamData::GetTimePassage()
{
    return timePassage;
}

void ThreadCameraStreamData::SetBaseTimeMS(unsigned long val)
{
    baseTimeMS = val;
}
unsigned long ThreadCameraStreamData::GetBaseTimeMS(void) const
{
    return baseTimeMS;
}

bool ThreadCameraStreamData::GetHDREnabled() const
{
    if (sky) {
        return sky->GetAtmosphere().GetHDREnabled();
    }
    return false;
}
const float* ThreadCameraStreamData::GetStratusMieLookupPixels(int& width)
{
    if (mieLookupPixels == nullptr) {
        CreateStratusMieLookupPixels();
    }
    width = 2048;
    return mieLookupPixels;
}

void ThreadCameraStreamData::CreateStratusMieLookupPixels(void)
{
    int i;
    float* pixels = SL_NEW float[2048 * 3 * 2];
    memset((void*)pixels, 0, 2048 * 3 * 2 * sizeof(float));

    float* p = pixels;

    for (i = 0; i < 1801; i++) {
        *p++ = (float)mieRed[i];
        *p++ = (float)mieGreen[i];
        *p++ = (float)mieBlue[i];
    }

    p = pixels + 2048 * 3;
    for (i = 0; i < 1801; i++) {
        *p++ = (float)mieRedConv[i];
        *p++ = (float)mieGreenConv[i];
        *p++ = (float)mieBlueConv[i];
    }

    // Shove other constants in columns 2000+
    float blut[10] = { 1.1796f, 1.1293f, 1.1382f, 1.0953f, 0.9808f, 0.9077f, 0.7987f, 0.6629f, 0.5042f, 0.3021f };
    float clut[10] = { 0.0138f, 0.0154f, 0.0131f, 0.0049f, 0.0012f, 0.0047f, 0.0207f, 0.0133f, 0.0280f, 0.0783f };
    float kclut[10] = { 0.0265f, 0.0262f, 0.0272f, 0.0294f, 0.0326f, 0.0379f, 0.0471f, 0.0616f, 0.0700f, 0.0700f };
    float tlut[10] = { 0.8389f, 0.8412f, 0.8334f, 0.8208f, 0.8010f, 0.7774f, 0.7506f, 0.7165f, 0.7149f, 0.1000f };
    float rlut[10] = { 0.0547f, 0.0547f, 0.0552f, 0.0564f, 0.0603f, 0.0705f, 0.0984f, 0.1700f, 0.3554f, 0.9500f };

    p = pixels + 2000 * 3;
    for (i = 0; i < 10; i++) {
        *p++ = blut[i];
        *p++ = clut[i];
        *p++ = kclut[i];
    }

    p = pixels + 2048 * 3 + 2000 * 3;
    for (i = 0; i < 10; i++) {
        *p++ = tlut[i];
        *p++ = rlut[i];
        *p++ = 0;
    }
    mieLookupPixels = pixels;
}

const unsigned char* ThreadCameraStreamData::GetStratusNoisePixels(int& width, int& height)
{
    if (stratusTgaLoader == nullptr) {
        stratusTgaLoader = new TGALoader();
        stratusTgaLoaded = stratusTgaLoader->Load("stratus.tga", Atmosphere::GetResourceLoader());
    }
    if (stratusTgaLoaded) {
        width = stratusTgaLoader->GetWidth();
        height = stratusTgaLoader->GetHeight();
        return stratusTgaLoader->GetPixels();
    } else {
        width = height = -1;
        return nullptr;
    }
}

void ThreadCameraStreamData::SetEnvironmentMapFormat(ColorFormat format)
{
    environmentMapFormat = format;
}
void ThreadCameraStreamData::SetUseShadowMap(bool val)
{
    useShadowMap = val;
}

void ThreadCameraStreamData::SettingsChanged(void* environment)
{
    SL_ASSERT(GetRenderer()->GetIsVulkan());
    renderer->SettingsChanged(environment);
}

class NullStream : public std::ostream
{
    class NullBuffer : public std::streambuf
    {
    public:
        int overflow(int c)
        {
            return c;
        }
    } m_nb;
public:
    NullStream() : std::ostream(&m_nb) {}
};

std::ostream& ThreadCameraStreamData::log(LogLevel level)
{
    if (level >= currentLogLevel) {
        return std::cout;
    }
    static NullStream null;
    return null;
}

}
