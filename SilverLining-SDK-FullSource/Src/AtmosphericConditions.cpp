// Copyright 2004-2022 Sundog Software, LLC. All rights reserved worldwide.

#include "AtmosphericConditions.h"
#include "CloudLayer.h"
#include "Vector3.h"
#include "Renderer.h"
#include "Camera.h"
#include "PrecipitationManager.h"
#include "Billboard.h"
#include "CloudLayerFactory.h"
#include "SLAssert.h"

#if defined(__INTEL_COMPILER)
#include <mathimf.h>
#else
#include <math.h>
#endif

using namespace SilverLining;
using namespace std;

AtmosphericConditions::AtmosphericConditions() : turbidity(2.0), visibility(30000), lightPollution(0),
    precipitationWindX(0.0), precipitationWindZ(0.0),
    overrideFog(false),
    fogDensity(0.001), fogR(1), fogG(1), fogB(1)
    , atmosphere(0)
{
    defaultTimer = SL_NEW MillisecondTimer();
    timer = defaultTimer;
}

AtmosphericConditions::~AtmosphericConditions()
{
    RemoveAllCloudLayers();

    if (defaultTimer) {
        SL_DELETE defaultTimer;
    }
}

void AtmosphericConditions::SetFog(double density, double r, double g, double b)
{
    if (density > 0 && r >= 0.0 && r <= 1.0 && g >= 0.0 && g <= 1.0 && b >= 0.0 && b <= 1.0) {
        overrideFog = true;
        fogR = r;
        fogG = g;
        fogB = b;
        fogDensity = density;
    }
}

void AtmosphericConditions::ClearFog()
{
    overrideFog = false;
}

void AtmosphericConditions::GetFog(bool& fogIsSet, double& density, double& r, double& g, double& b)
{
    fogIsSet = overrideFog;
    density = fogDensity;
    r = fogR;
    g = fogG;
    b = fogB;
}

void AtmosphericConditions::SetLocation(const Location& pLocation, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : (atmosphere)?
                                      atmosphere->GetDefaultTcsData() : nullptr;

    if (tcsData) {
        tcsData->SetLocation(pLocation);
    }
}

const Location& AtmosphericConditions::GetLocation(void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : (atmosphere) ?
                                      atmosphere->GetDefaultTcsData() : nullptr;
    if (tcsData) {
        return tcsData->GetLocation();
    }
    static const Location invalid;
    return invalid;
}

void AtmosphericConditions::SetTime(const LocalTime& pTime, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : (atmosphere) ?
                                      atmosphere->GetDefaultTcsData() : nullptr;
    if (tcsData) {
        tcsData->SetTime(pTime, timer->GetMilliseconds());
    }

}

const LocalTime& AtmosphericConditions::GetTime(void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : (atmosphere) ?
                                      atmosphere->GetDefaultTcsData() : nullptr;
    if (tcsData) {
        return tcsData->GetTime(timer->GetMilliseconds());
    }
    static const LocalTime invalid;
    return invalid;
}

void AtmosphericConditions::ClearWindVolumes()
{
    windVolumes.clear();
}

int AtmosphericConditions::SetWind(const WindVolume& windVolume)
{
    static int newWindVolumeHandle = 0;

    newWindVolumeHandle++;

    windVolumes[newWindVolumeHandle] = windVolume;

    return newWindVolumeHandle;
}

bool AtmosphericConditions::RemoveWindVolume(int windVolumeHandle)
{
    SL_MAP(int, WindVolume) ::iterator it;
    it = windVolumes.find(windVolumeHandle);
    if (it != windVolumes.end()) {
        return (windVolumes.erase(windVolumeHandle) > 0);
    }

    return false;
}

void AtmosphericConditions::GetWind(double& velocity, double& heading, double altitude) const
{
    SL_MAP(int, WindVolume) ::const_iterator it;
    for (it = windVolumes.begin(); it != windVolumes.end(); it++) {
        if (it->second.Inside(altitude)) {
            velocity = it->second.GetWindSpeed();
            heading = it->second.GetDirection();
            return;
        }
    }

    velocity = 0;
    heading = 0;
}

void AtmosphericConditions::SetPresetConditions(ConditionPresets preset, Atmosphere& atm, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atm.GetDefaultTcsData();
    SL_ASSERT(tcsData);

    RemoveAllCloudLayers();

    // All conditions have cirrus
    CloudLayer *cirrusCloudLayer;

    cirrusCloudLayer = CloudLayerFactory::Create(CIRRUS_FIBRATUS, atm);
    cirrusCloudLayer->SetBaseAltitude(8000 * Atmosphere::GetUnitScale(), true, tcsData);
    cirrusCloudLayer->SetThickness(0);
    cirrusCloudLayer->SetBaseLength(100000 * Atmosphere::GetUnitScale());
    cirrusCloudLayer->SetBaseWidth(100000 * Atmosphere::GetUnitScale());
    cirrusCloudLayer->SetLayerPosition(0, 0);
    cirrusCloudLayer->SeedClouds(atm);
    cirrusCloudLayer->SetIsInfinite(true);
    AddCloudLayer(cirrusCloudLayer);

    // If overcast, add stratus
    if (preset == OVERCAST) {
        CloudLayer *stratusLayer;

        stratusLayer = CloudLayerFactory::Create(STRATUS, atm);
        stratusLayer->SetIsInfinite(true);
        stratusLayer->SetBaseAltitude(1500 * Atmosphere::GetUnitScale(), true, tcsData);
        stratusLayer->SetThickness(600 * Atmosphere::GetUnitScale());
        stratusLayer->SetDensity(1.0);
        stratusLayer->SetLayerPosition(0, 0);
        stratusLayer->SeedClouds(atm);
        AddCloudLayer(stratusLayer);
    } else if (preset == PARTLY_CLOUDY || preset == MOSTLY_CLOUDY) {
        // Otherwise, add in some cumulus congestus
        CloudLayer *cumulusCongestusLayer;

        cumulusCongestusLayer = CloudLayerFactory::Create(CUMULUS_CONGESTUS, atm);
        cumulusCongestusLayer->SetIsInfinite(true);
        cumulusCongestusLayer->SetBaseAltitude(3000 * Atmosphere::GetUnitScale(), true, tcsData);
        cumulusCongestusLayer->SetThickness(100 * Atmosphere::GetUnitScale());
        cumulusCongestusLayer->SetBaseLength(60000 * Atmosphere::GetUnitScale());
        cumulusCongestusLayer->SetBaseWidth(60000 * Atmosphere::GetUnitScale());
        cumulusCongestusLayer->SetDensity(preset == PARTLY_CLOUDY ? 0.2 : 0.8);
        cumulusCongestusLayer->SetLayerPosition(0, 0);
        cumulusCongestusLayer->SetCloudAnimationEffects(0.1, false);
        cumulusCongestusLayer->SeedClouds(atm);
        cumulusCongestusLayer->SetAlpha(0.8);
        cumulusCongestusLayer->SetFadeTowardEdges(true);

        AddCloudLayer(cumulusCongestusLayer);
    }
}

int AtmosphericConditions::runningCloudHandle = 0;

int AtmosphericConditions::AddCloudLayer(CloudLayer *layer)
{
    if (!layer) return 0;

    runningCloudHandle++;

    cloudLayers[runningCloudHandle] = layer;

    layer->SetHandle(runningCloudHandle);

    return runningCloudHandle;
}

bool AtmosphericConditions::HasCloudLayer(const CloudLayer *layer) const
{
    for (SL_MAP(int, CloudLayer *)::const_iterator it = cloudLayers.begin(); it != cloudLayers.end(); ++it) {
        if (it->second == layer) {
            return true;
        }
    }
    return false;
}

bool AtmosphericConditions::RemoveCloudLayer(int layerHandle, void* data, bool destroy, bool removeEntry)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere->GetDefaultTcsData();

    SL_MAP(int, CloudLayer *)::const_iterator it = cloudLayers.find(layerHandle);
    if (it != cloudLayers.end()) {

        CloudLayer* cloudLayer = it->second;
        cloudLayer->ClearClouds(tcsData);
        if (removeEntry) {
            cloudLayer->DeleteCloudLayerTcsUserData(tcsData);
            cloudLayer->DeleteTcsUserData(tcsData);
        }
        if (destroy) {
            SL_DELETE cloudLayer;
            return (cloudLayers.erase(layerHandle) > 0);
        }
    }

    return false;
}

void AtmosphericConditions::RemoveAllCloudLayers(void* data, bool destroy, bool removeEntry)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere->GetDefaultTcsData();
    for (SL_MAP(int, CloudLayer *)::const_iterator it = cloudLayers.begin(); it != cloudLayers.end(); it++) {
        CloudLayer* cloudLayer = it->second;
        cloudLayer->ClearClouds(tcsData);
        if (removeEntry) {
            cloudLayer->DeleteCloudLayerTcsUserData(tcsData);
            cloudLayer->DeleteTcsUserData(tcsData);
        }
        if (destroy) {
            SL_DELETE cloudLayer;
        }
    }
    if (destroy) {
        cloudLayers.clear();
    }
}

bool AtmosphericConditions::GetCloudLayer(int handle, CloudLayer **layer)
{
    if (layer) {
        SL_MAP(int, CloudLayer*) ::iterator it;
        it = cloudLayers.find(handle);
        if (it != cloudLayers.end()) {
            *layer = (*it).second;
            return true;
        }
    }

    return false;
}

static inline double toRadians(double x)
{
    return x * (3.14159265 / 180.0);
}

void AtmosphericConditions::ApplyWind(double dt, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere->GetDefaultTcsData();

    const Camera* camera = tcsData->GetCamera();

    SL_MAP(int, CloudLayer *) ::iterator it;
    for (it = cloudLayers.begin(); it != cloudLayers.end(); it++) {
        CloudLayer *cl = (*it).second;
        if (cl) {
            double alt = cl->GetBaseAltitude(tcsData);
            double velocity, heading;
            GetWind(velocity, heading, alt);

            double windX;
            double windZ;
            cl->GetWind(windX, windZ);

            if (windX != 0 || windZ != 0 || velocity != 0) {

                double dx = (windX + sin(toRadians(heading)) * velocity) * dt;
                double dz = (windZ + cos(toRadians(heading)) * velocity) * dt;

                Vector3 vWind(-dx, 0, dz);
                vWind = vWind * camera->GetBasis3x3();
                cl->MoveClouds(vWind.x, vWind.y, vWind.z, tcsData);
            }
        }
    }
}

void AtmosphericConditions::SetPrecipitation(int type, double intensity, double fNear, double fFar, bool bUseDepthBuffer, void* data)
{
    SL_ASSERT(atmosphere);
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere->GetDefaultTcsData();
    if (tcsData)
        tcsData->GetPrecipitationManager()->SetPrecipitation(type, intensity, fNear, fFar, bUseDepthBuffer);
}


void AtmosphericConditions::EnablePrecipitation(bool on, void* data)
{
    SL_ASSERT(atmosphere);
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere->GetDefaultTcsData();
    if (tcsData) {
        PrecipitationManager* pm = tcsData->GetPrecipitationManager();
        if (pm) {
            pm->Enable(on);
        }
    }
}

void AtmosphericConditions::EnableTimePassage(bool enabled, long relightFrequencyMS, void* data)
{
    SL_ASSERT(atmosphere);
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere->GetDefaultTcsData();
    if (tcsData)
        tcsData->EnableTimePassage(enabled, relightFrequencyMS, timer->GetMilliseconds());
}

bool AtmosphericConditions::WantsLightingUpdate(ThreadCameraStreamData* tcsData)
{
    return tcsData->WantsLightingUpdate(timer->GetMilliseconds());
}

void AtmosphericConditions::SetAtmosphere(Atmosphere* _atmosphere)
{
    atmosphere = _atmosphere;
}

void AtmosphericConditions::SetMillisecondTimer(const MillisecondTimer *pTimer, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : (atmosphere) ?
                                      atmosphere->GetDefaultTcsData() : nullptr;
    if (pTimer) {
        tcsData->SetBaseTimeMS(pTimer->GetMilliseconds());
        timer = pTimer;
    } else {
        timer = defaultTimer;
    }
}

bool AtmosphericConditions::Serialize(std::ostream& s, void* data)
{
    SL_ASSERT(atmosphere);
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere->GetDefaultTcsData();

    tcsData->GetLocation().Serialize(s);
    tcsData->GetTime().Serialize(s);
    tcsData->GetAdjustedTime().Serialize(s);

    s.write((char *)&turbidity, sizeof(double));
    s.write((char *)&visibility, sizeof(double));
    s.write((char *)&lightPollution, sizeof(double));

    // wind volumes
    {
        int nWindVolumes = (int)windVolumes.size();
        s.write((char *)&nWindVolumes, sizeof(int));
        SL_MAP(int, WindVolume) ::iterator it;
        for (it = windVolumes.begin(); it != windVolumes.end(); it++) {
            int handle = it->first;
            s.write((char *)&handle, sizeof(int));
            it->second.Serialize(s);
        }
    }

    // cloud layers
    {
        int nCloudLayers = (int)cloudLayers.size();
        s.write((char *)&nCloudLayers, sizeof(int));
        SL_MAP(int, CloudLayer *) ::iterator it;
        for (it = cloudLayers.begin(); it != cloudLayers.end(); it++) {
            int handle = it->first;
            s.write((char *)&handle, sizeof(int));
            CloudLayer* layer = it->second;
            CloudLayerFactory::Serialize(layer, s, tcsData);
        }
    }

    // precipitation
    const SL_MAP(int, double)& precipitationEffects = tcsData->GetPrecipitationManager()->GetPrecipitationEffects();
    int nPrecipEffects = (int)precipitationEffects.size();
    s.write((char *)&nPrecipEffects, sizeof(int));

    for (SL_MAP(int, double) ::const_iterator it2 = precipitationEffects.begin(); it2 != precipitationEffects.end(); it2++) {
        int type = it2->first;
        double intensity = it2->second;
        s.write((char *)&type, sizeof(int));
        s.write((char *)&intensity, sizeof(double));
    }

    s.write((char *)&precipitationWindX, sizeof(double));
    s.write((char *)&precipitationWindZ, sizeof(double));

    s.write((char *)&tcsData->GetTimePassage(), sizeof(bool));
    s.write((char *)&fogDensity, sizeof(double));
    s.write((char *)&fogR, sizeof(double));
    s.write((char *)&fogG, sizeof(double));
    s.write((char *)&fogB, sizeof(double));

    return true;
}

bool AtmosphericConditions::Unserialize(const Atmosphere* atm, std::istream& s, void* data)
{
    SL_ASSERT(atmosphere && atmosphere==atm);
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere->GetDefaultTcsData();

    tcsData->GetLocation().Unserialize(s);
    tcsData->GetTime().Unserialize(s);
    tcsData->GetAdjustedTime().Unserialize(s);

    s.read((char *)&turbidity, sizeof(double));
    s.read((char *)&visibility, sizeof(double));
    s.read((char *)&lightPollution, sizeof(double));

    // wind volumes
    windVolumes.clear();
    int nWindVolumes;
    s.read((char *)&nWindVolumes, sizeof(int));
    for (int i = 0; i < nWindVolumes; i++) {
        int handle;
        s.read((char *)&handle, sizeof(int));
        WindVolume wv;
        wv.Unserialize(s);
        windVolumes[handle] = wv;
    }

    // cloud layers
    cloudLayers.clear();
    int nCloudLayers;
    s.read((char *)&nCloudLayers, sizeof(int));
    for (int i = 0; i < nCloudLayers; i++) {
        int handle;
        s.read((char *)&handle, sizeof(int));
        CloudLayer *layer = CloudLayerFactory::Unserialize(*atm, s, tcsData);
        cloudLayers[handle] = layer;
    }

    // precipitation
    SL_MAP(int, double)& precipitationEffects = tcsData->GetPrecipitationManager()->GetPrecipitationEffects();
    precipitationEffects.clear();
    int nPrecipEffects;
    s.read((char *)&nPrecipEffects, sizeof(int));
    for (int i = 0; i < nPrecipEffects; i++) {
        int type;
        double intensity;
        s.read((char *)&type, sizeof(int));
        s.read((char *)&intensity, sizeof(double));
        precipitationEffects[type] = intensity;
    }

    s.read((char *)&precipitationWindX, sizeof(double));
    s.read((char *)&precipitationWindZ, sizeof(double));

    s.read((char *)&tcsData->GetTimePassage(), sizeof(bool));
    s.read((char *)&fogDensity, sizeof(double));
    s.read((char *)&fogR, sizeof(double));
    s.read((char *)&fogG, sizeof(double));
    s.read((char *)&fogB, sizeof(double));

    return true;
}

