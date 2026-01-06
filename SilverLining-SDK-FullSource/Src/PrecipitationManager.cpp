// Copyright (c) 2009-2016 Sundog Software LLC, All rights reserved worldwide.

#include "PrecipitationManager.h"
#include "Rain.h"
#include "Atmosphere.h"
#include "CloudLayer.h"
#include "Configuration.h"
#include "Snow.h"
#include "Sleet.h"
#include "Camera.h"
#include "SLAssert.h"
#include <map>


namespace SilverLining
{

PrecipitationManager::PrecipitationManager(const Atmosphere& _atmosphere, ThreadCameraStreamData* _tcsData)
    : atmosphere(_atmosphere), cameraHandle(0), enabled(true)
    , tcsData(_tcsData)
{
}

void PrecipitationManager::Initialize(const Atmosphere* atmosphere)
{
    rainSystems.clear();
    snowSystems.clear();
    sleetSystems.clear();

}

void PrecipitationManager::DestroyPrecipitations()
{
    KeyType key(cameraHandle, tcsData);
    SL_ORDERED_MAP(KeyType, Rain*)::iterator rainIt = rainSystems.find(key);
    if (rainIt != rainSystems.end()) {
        SL_DELETE rainIt->second;
        rainSystems.erase(rainIt);
    }

    SL_ORDERED_MAP(KeyType, Snow*)::iterator snowIt = snowSystems.find(key);
    if (snowIt != snowSystems.end()) {
        SL_DELETE snowIt->second;
        snowSystems.erase(snowIt);
    }

    SL_ORDERED_MAP(KeyType, Sleet*)::iterator sleeIt = sleetSystems.find(key);
    if (sleeIt != sleetSystems.end()) {
        SL_DELETE sleeIt->second;
        sleetSystems.erase(sleeIt);
    }
}

PrecipitationManager::~PrecipitationManager()
{
    SL_ORDERED_MAP(KeyType, Rain *)::iterator rainIt;
    for (rainIt = rainSystems.begin(); rainIt != rainSystems.end(); rainIt++) {
        SL_DELETE rainIt->second;
    }
    rainSystems.clear();

    SL_ORDERED_MAP(KeyType, Snow *)::iterator snowIt;
    for (snowIt = snowSystems.begin(); snowIt != snowSystems.end(); snowIt++) {
        SL_DELETE snowIt->second;
    }
    snowSystems.clear();

    SL_ORDERED_MAP(KeyType, Sleet *)::iterator sleetIt;
    for (sleetIt = sleetSystems.begin(); sleetIt != sleetSystems.end(); sleetIt++) {
        SL_DELETE sleetIt->second;
    }
    sleetSystems.clear();
}

Rain *PrecipitationManager::GetRain(CameraHandle cam, ThreadCameraStreamData* tcsData)
{
    KeyType key(cam, tcsData);
    SL_ORDERED_MAP(KeyType, Rain *) ::iterator it;
    it = rainSystems.find(key);
    if (it == rainSystems.end()) {
        rainSystems[key] = SL_NEW Rain(&atmosphere, tcsData->GetCamera()->GetPosition(), tcsData);
    }

    return rainSystems[key];
}

Snow *PrecipitationManager::GetSnow(CameraHandle cam, ThreadCameraStreamData* tcsData)
{
    KeyType key(cam, tcsData);
    SL_ORDERED_MAP(KeyType, Snow *) ::iterator it;
    it = snowSystems.find(key);
    if (it == snowSystems.end()) {
        snowSystems[key] = SL_NEW Snow(&atmosphere, tcsData->GetCamera()->GetPosition(), tcsData);
    }

    return snowSystems[key];
}

Sleet *PrecipitationManager::GetSleet(CameraHandle cam, ThreadCameraStreamData* tcsData)
{
    KeyType key(cam, tcsData);
    SL_ORDERED_MAP(KeyType, Sleet *) ::iterator it;
    it = sleetSystems.find(key);
    if (it == sleetSystems.end()) {
        sleetSystems[key] = SL_NEW Sleet(&atmosphere, tcsData->GetCamera()->GetPosition(), tcsData);
    }

    return sleetSystems[key];
}

void PrecipitationManager::SetCurrentCameraHandle(CameraHandle _cameraHandle)
{
    cameraHandle = _cameraHandle;
}

double PrecipitationManager::GetFogDensity() const
{
    double density = 0;

    bool doFog = false;
    Configuration::GetBoolValue("enable-precipitation-visibility-effects", doFog);
    if (doFog) {
        density += const_cast<PrecipitationManager*>(this)->GetRain(cameraHandle, tcsData)->GetFogDensity();
        density += const_cast<PrecipitationManager*>(this)->GetSnow(cameraHandle, tcsData)->GetFogDensity();
        density += const_cast<PrecipitationManager*>(this)->GetSleet(cameraHandle, tcsData)->GetFogDensity();
    }

    return density;
}

void PrecipitationManager::SetClipPlanes(int precipType, double nearClip, double farClip, bool useDepthBuffer)
{
    if (nearClip > 0 && farClip > 0) {
        switch (precipType) {
        case CloudLayer::RAIN:
            GetRain(cameraHandle, tcsData)->SetEffectRange(nearClip, farClip, useDepthBuffer, tcsData->GetCamera());
            break;
        case CloudLayer::WET_SNOW:
        case CloudLayer::DRY_SNOW:
            GetSnow(cameraHandle, tcsData)->SetEffectRange(nearClip, farClip, useDepthBuffer, tcsData->GetCamera());
            break;
        case CloudLayer::SLEET:
            GetSleet(cameraHandle, tcsData)->SetEffectRange(nearClip, farClip, useDepthBuffer, tcsData->GetCamera());
            break;
        }
    }
}

void PrecipitationManager::SetIntensity(int precipType, double mmPerHour)
{
    switch (precipType) {
    case CloudLayer::RAIN:
        GetRain(cameraHandle, tcsData)->SetIntensity(mmPerHour);
        break;
    case CloudLayer::WET_SNOW:
        GetSnow(cameraHandle, tcsData)->SetIsWet(true);
        GetSnow(cameraHandle, tcsData)->SetIntensity(mmPerHour);
        break;
    case CloudLayer::DRY_SNOW:
        GetSnow(cameraHandle, tcsData)->SetIsWet(false);
        GetSnow(cameraHandle, tcsData)->SetIntensity(mmPerHour);
        break;
    case CloudLayer::SLEET:
        GetSleet(cameraHandle, tcsData)->SetIntensity(mmPerHour);
        break;
    default:
        break;
    }
}

double PrecipitationManager::UpdateTime(CameraHandle cam)
{
    KeyType key(cam, tcsData);
    SL_ORDERED_MAP(KeyType, unsigned long) ::iterator it;
    it = lastTimes.find(key);
    if (it == lastTimes.end()) {
        lastTimes.insert(std::make_pair(key, 0));
    }
    unsigned long& lastTime = lastTimes[key];

    double dt = 0;
    unsigned long now = atmosphere.GetConditions().GetMillisecondTimer()->GetMilliseconds();
    if (lastTime != 0) {
        dt = (long)(now - lastTime)*0.001;
    }
    lastTime = now;

    return dt;
}

void PrecipitationManager::Render(const Color& lightColor)
{
    if (enabled) {

        bool hasRain = false, hasSnow = false, hasSleet = false;

        Rain *rain = GetRain(cameraHandle, tcsData);
        Snow *snow = GetSnow(cameraHandle, tcsData);
        Sleet *sleet = GetSleet(cameraHandle, tcsData);

        if (rain && rain->GetIntensity() > 0) {
            hasRain = true;
        }

        if (snow && snow->GetIntensity() > 0) {
            hasSnow = true;
        }

        if (sleet && sleet->GetIntensity() > 0) {
            hasSleet = true;
        }

        if (hasRain || hasSnow || hasSleet) {

            double dt = UpdateTime(cameraHandle);

            Renderer* renderer = tcsData->GetRenderer();
            renderer->PushAllState();
            renderer->SetDefaultState();

            const Camera* camera = tcsData->GetCamera();

            if (hasRain) {
                rain->Render(dt, lightColor, camera);
            }

            if (hasSnow) {
                snow->Render(dt, lightColor, camera);
            }

            if (hasSleet) {
                sleet->Render(dt, lightColor, camera);
            }

            renderer->PopAllState();
        }

    }
}

void PrecipitationManager::ClearPrecipitation()
{
    Rain *rain = GetRain(cameraHandle, tcsData);
    Snow *snow = GetSnow(cameraHandle, tcsData);
    Sleet *sleet = GetSleet(cameraHandle, tcsData);

    if (rain) rain->SetIntensity(0);
    if (snow) snow->SetIntensity(0);
    if (sleet) sleet->SetIntensity(0);
}

void PrecipitationManager::SetPrecipitation(int type, double intensity, double fNear, double fFar, bool bUseDepthBuffer)
{
    if (intensity < 0) intensity = 0;

    if (type < CloudLayer::NONE || type >= CloudLayer::NUM_PRECIP_TYPES) type = CloudLayer::NONE;

    if (type == CloudLayer::NONE) {
        precipitationEffects.clear();
        precipitationNearClips.clear();
        precipitationFarClips.clear();
        precipitationUseDepthBuffer.clear();
    } else {
        precipitationEffects[type] = intensity;
        precipitationNearClips[type] = fNear;
        precipitationFarClips[type] = fFar;
        precipitationUseDepthBuffer[type] = bUseDepthBuffer;
    }
}

void PrecipitationManager::ApplyPrecipitation()
{
    SL_MAP(int, double) ::iterator it;
    for (it = precipitationEffects.begin(); it != precipitationEffects.end(); it++) {
        int type = (*it).first;
        double intensity = (*it).second;
        SetIntensity(type, intensity);

        double nearClip = precipitationNearClips[type];
        double farClip = precipitationFarClips[type];
        bool   useDepthBuffer = precipitationUseDepthBuffer[type];
        SetClipPlanes(type, nearClip, farClip, useDepthBuffer);
    }
}

PrecipitationManager::KeyType::KeyType(CameraHandle _camHandle, ThreadCameraStreamData* _tcsData)
    : camHandle(_camHandle), tcsData(_tcsData)
{

}
bool PrecipitationManager::KeyType::operator<(const KeyType& rhs) const
{
    if (camHandle < rhs.camHandle) {
        return true;
    } else if (camHandle == rhs.camHandle) {
        if (tcsData < rhs.tcsData) {
            return true;
        }
    }
    return false;
}
}