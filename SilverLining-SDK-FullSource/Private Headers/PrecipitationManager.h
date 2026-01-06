// Copyright (c) Sundog Software LLC 2008-2016, All rights reserved worldwide.

/**
\file PrecipitationManager.h
\brief Set and retrieve aggregate precipitation effects.
*/

#ifndef PRECIPITATION_MANAGER_H
#define PRECIPITATION_MANAGER_H

#include "Color.h"
#include "Frustum.h"
#include "SilverLiningTypes.h"
#include "SLMap.h"
#include "PrecipitationShaderConstantLocations.h"

namespace SilverLining
{
    class Atmosphere;
    class Rain;
    class Snow;
    class Sleet;
    class Renderer;
    class Camera;
    class ThreadCameraStreamData;


    /** Provides a common point of control for rain and snow effects. This is a singleton. */
    class PrecipitationManager : public MemObject
    {
    public:
        PrecipitationManager(const Atmosphere& _atmosphere, ThreadCameraStreamData* _tcsData);

        /** Destructor. */
        ~PrecipitationManager();

    public:

        /** Initializes rain and snow effects.

        \param atm A pointer to the Atmosphere object for the scene.
        */
        void Initialize(const Atmosphere* atmosphere);

        /** Stops all precipitation effects. */
        void ClearPrecipitation();

        void DestroyPrecipitations();

        /** Returns the total density term for exponential fog, based on the visibility reduction
        from precipitation. */
        double GetFogDensity() const;

        /** Sets the intensity, in millimeters per hour (liquid equivalent) of precipitation
        of a given type.

        \param precipType Either CloudLayer::RAIN, CloudLayer::DRY_SNOW, CloudLayer::WET_SNOW,
        or CloudLayer::SLEET.
        \param mmPerHour Liquid equivalent precipitation rate. Reasonable values
        range from 1.0 to 30.0.
        */
        void SetIntensity(int precipType, double mmPerHour);

        /** Sets the near and far extents of the precipitation effect as distances from the camera. */
        void SetClipPlanes(int precipType, double nearClip, double farClip, bool useDepthBuffer);

        void SetPrecipitation(int type, double intensity, double fNear, double fFar, bool bUseDepthBuffer);

        void ApplyPrecipitation();

        /** Renders all precipitation effects that have a non-zero intensity.
        \sa SetIntensity()

        \param dt The frame time, in seconds.
        \param f  The view frustum.
        \parma lightColor The dominant light color
        */
        void Render(const Color& lightColor);

        /** Sets the current camera in use, so we can have separate particle systems for each. */
        void SetCurrentCameraHandle(CameraHandle cameraHandle);

        void Enable(bool on) { enabled = on; }

        const SL_MAP(int, double)& GetPrecipitationEffects(void) const { return precipitationEffects; }

        SL_MAP(int, double)& GetPrecipitationEffects(void) { return precipitationEffects; }

        class KeyType
        {
        public:
            KeyType(CameraHandle camHandle, ThreadCameraStreamData* _tcsData);
            bool operator<(const KeyType& rhs) const;
        private:
            const CameraHandle camHandle;
            ThreadCameraStreamData* tcsData;
        };

        Rain *GetRain(CameraHandle cam, ThreadCameraStreamData* tcsData);
        Snow* GetSnow(CameraHandle cam, ThreadCameraStreamData* tcsData);
        Sleet* GetSleet(CameraHandle cam, ThreadCameraStreamData* tcsData);

        double UpdateTime(CameraHandle cam);


        SL_ORDERED_MAP(KeyType, Rain *) rainSystems;
        SL_ORDERED_MAP(KeyType, Snow *) snowSystems;
        SL_ORDERED_MAP(KeyType, Sleet *) sleetSystems;

        SL_MAP(int, double) precipitationEffects;
        SL_MAP(int, double) precipitationNearClips;
        SL_MAP(int, double) precipitationFarClips;
        SL_MAP(int, bool) precipitationUseDepthBuffer;

        SL_ORDERED_MAP(KeyType, unsigned long) lastTimes;

        CameraHandle cameraHandle;
        const Atmosphere& atmosphere;
        bool enabled;

        ThreadCameraStreamData* tcsData;
    };
}

#endif
