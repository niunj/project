// Copyright (c) 2004-2022 Sundog Software, LLC
// All rights reserved worldwide.

/** \file AtmosphericConditions.h
   \brief Configures the time, location, atmosphere, and cloud cover.
 */

#ifndef ATMOSPHERIC_CONDITIONS_H
#define ATMOSPHERIC_CONDITIONS_H

#ifdef SWIG
#define SILVERLINING_API
#define SL_MAP(a, b) std::map<a, b>
#define SL_VECTOR(a) std::vector<a>
%module SilverLiningAtmosphericConditions
%include "CloudLayer.h"
%include "LocalTime.h"
%include "WindVolume.h"
%include "Location.h"
%{
#include "AtmosphericConditions.h"
using namespace SilverLining;
%}
#endif

#if defined(WIN32) || defined(WIN64)
#pragma warning(disable: 4786)
#endif

#include "MemAlloc.h"
#include "WindVolume.h"
#include "Location.h"
#include "LocalTime.h"
#include "MillisecondTimer.h"

#include <vector>
#include <map>
#include <iostream>

#pragma pack(push)
#pragma pack(8)

namespace SilverLining
{
class CloudLayer;
class Atmosphere;
class Camera;
class ThreadCameraStreamData;

/** A class to set the current simulation conditions for the sky.

   This class allows you to specify the simulated time, location, wind, visibility,
   haze, and cloud cover for your scene. You do so by instantiating your own
   AtmosphericConditions object, configuring it by calling its methods, and then passing
   it into your Atmosphere object via Atmosphere::SetConditions().

   To make changes to the AtmosphericConditions after it has been passed into
   Atmosphere::SetConditions(), call Atmosphere::GetConditions() and call methods on the
   AtmosphericConditions reference that it returns. Atmosphere::SetConditions() copies your
   AtmosphericConditions into the Atmosphere class, so changes made to the AtmosphericConditions
   used to initialize the Atmosphere will have no effect.
 */
class AtmosphericConditions : public MemObject
{
public:
/** Default constructor. Creates an AtmosphericConditions object with default settings. */
    AtmosphericConditions();

/** Destructor. Destroys all members, but does not destroy the CloudLayer objects passed
   into AddCloudLayer(). */
    virtual ~AtmosphericConditions();

/** Sets the simulated geographic location. This affects the sun and moon positions
   relative to the horizon, and thereby also affects the lighting of the scene. Be
   sure that the location specified is consistent with the time zone specified in the
   LocalTime passed to SetTime(), or you will experience confusing results.

    \param location A reference to a Location object that embodies the simulated latitude
           and longitude.
    \param data An optional ThreadCameraStreamData object that this calls pertains to.
 */
    void SILVERLINING_API SetLocation(const Location& location, void* data = 0);

/** Gets the current simulated location.

   \param data An optional ThreadCameraStreamData object that this calls pertains to.
   \return A const reference to a Location object that may be queried for the current
      simulated geographic position.
 */
    const Location& SILVERLINING_API GetLocation(void* data = 0) const;

/** Sets the simulated time of day in local time. You must specify if daylight savings time
    is currently being observed or not. Be sure that the time zone specified is consistent
   with the simulated Location passed to SetLocation(), or you will experience confusing
   results.

   \param time A reference to a LocalTime object that embodies the simulated local time,
   time zone, and daylight savings time observation.
   \param data An optional ThreadCameraStreamData object that this calls pertains to.
 */
    void SILVERLINING_API SetTime(const LocalTime& time, void* data = 0);

/** Retrieves the currently simulated time of day. Use this to obtain the local time, time
   zone, and daylight savings time observance being simulated.

   This will also reflect the passage of time simulated by activating
   EnableTimePassage(). As such, it may differ from what was originally
   passed into SetTime() if time passage is activated.

   \param data An optional ThreadCameraStreamData object that this calls pertains to.
   \return A const reference to a LocalTime object, which may be queried for the local time,
   time zone, and DST information being simulated.*/
    const LocalTime& SILVERLINING_API GetTime(void* data = 0);

    enum ConditionPresets
    {
        FAIR = 0,
        PARTLY_CLOUDY,
        MOSTLY_CLOUDY,
        OVERCAST
    };

/** A quick way to set up typical weather conditions. This method will create "infinite" cloud layers that remain centered at
    the camera, so there's no need to worry about positioning them. Typical, realistic values for cloud altitudes will be
    used. Finer control is available by setting up your own CloudLayer objects and passing them into AtmosphericConditions::AddCloudLayer().

    To simulate storms, set up MOSTLY_CLOUDY conditions, and then position a few Cumulonimbus clouds near the camera in addition.

    Any existing cloud layers in the scene will be removed. You'll still need to set up the simulated time, location, visibility, and any 
    precipitation and wind you might want.

    \param preset The cloud condition preset you wish to use; one of AtmosphericConditions::FAIR, PARTLY_CLOUDY, MOSTLY_CLOUDY,
    or OVERCAST.

    \param atm The Atmosphere object to associate the clouds with.

    \param data An optional ThreadCameraStreamData object that this calls pertains to.
*/
    void SILVERLINING_API SetPresetConditions(ConditionPresets preset, Atmosphere& atm, void* data = 0);

/** Sets a volume of wind for moving clouds. You may call this multiple times to
    define different wind velocities and directions at different altitudes. Wind also
   has some influence on cloud formation and the rendering of virga effects.

    \param windVolume A WindVolume to add to the simulation.
    \return A handle to the wind volume
 */
    int SILVERLINING_API SetWind(const WindVolume& windVolume);

/** Removes a specific wind volume from the simulation.
    \param windVolumeHandle The handle returned from AtmosphericConditions::SetWind().
 */
    bool SILVERLINING_API RemoveWindVolume(int windVolumeHandle);

/** Clears all wind volumes previously set via SetWind. */
    void SILVERLINING_API ClearWindVolumes();

/** Retrieves the simulated wind at a given altitude.
    \param velocity Receives the simulated wind velocity in meters per second
    \param heading Receives the simulated wind direction in degrees from North
    \param altitude The altitude, in meters, for which you want wind information.
 */
    void SILVERLINING_API GetWind(double& velocity, double& heading, double altitude) const;

/** Apply wind to cloud layers for given time step. This is called by the Atmosphere class;
   you do not need to call this directly!

   \param dt Length of time to simulate wind over, in seconds.

   \param data An optional ThreadCameraStreamData object that this calls pertains to.
 */
    void SILVERLINING_API ApplyWind(double dt, void* data = 0);

/** Get a vector of all wind volumes being simulated.

   \return A const reference to the STL vector of wind volumes currently being simulated.
 */
    const SL_MAP(int, WindVolume)& SILVERLINING_API GetWindVolumes() const {
        return windVolumes;
    }

/** Adds a cloud layer to the simulated atmospheric conditions. Cloud layers are created via
   the CloudLayerFactory::Create() method, and won't become part of your scene until they
   are added to a AtmosphericConditions class that is then passed into
   Atmosphere::SetConditions(). The CloudLayer object will be destroyed along with this
   AtmosphericConditions class; there's no need to keep this object around for cleanup.

   \param layer A CloudLayer created via CloudLayerFactory::Create(). This CloudLayer should
     be configured and seeded (see CloudLayer::SeedClouds()) prior to being passed in.
   \returns An integer handle to the cloud layer that may be used in calls to
    RemoveCloudLayer() and GetCloudLayer().
 */
    int SILVERLINING_API AddCloudLayer(CloudLayer *layer);

/** Whether a cloud layer exists in the atmospheric conditions
   \param layer A CloudLayer that is being searched for
*/
    bool SILVERLINING_API HasCloudLayer(const CloudLayer *layer) const;

/** Removes a previously added cloud deck from the scene. The specified CloudLayer handle,
   if found, will be removed from the simulation and its CloudLayer object deleted. Be sure
   your GL context or DirectX device is still active when calling this, as it will attempt
   to free the cloud layer's underlying graphic resources.

    \param layerHandle The integer handle previously returned from AddCloudLayer().
    \param data An optional ThreadCameraStreamData object that this calls pertains to.
    \param destroy. internal. Do not use.
    \param removeEntry. internal. Do not use.
    \return true if the cloud deck was removed, false if the handle specified was not found.
 */
    bool SILVERLINING_API RemoveCloudLayer(int layerHandle, void* data = 0, bool destroy = true, bool removeEntry = true);

/** Removes all cloud layers from the scene. All CloudLayer objects currently contained by
   this class will be removed and deleted. Be sure
   your GL context or DirectX device is still active when calling this, as it will attempt
   to free the cloud layer's underlying graphic resources.
   \param data An optional ThreadCameraStreamData object that this calls pertains to.
   \param destroy. internal. Do not use.
   \param removeEntry. internal. Do not use.
   */
    void SILVERLINING_API RemoveAllCloudLayers(void* data = 0, bool destroy = true, bool removeEntry = true);

/** Obtain a pointer to a CloudLayer object. Given a cloud layer handle, obtain its underlying
   CloudLayer object.

   \param layerHandle The integer handle to a cloud layer previously returned by AddCloudLayer().
   \param layer A pointer to a pointer to receive the CloudLayer object pointer requested.
   \return false if the layerHandle does not specify a CloudLayer being managed by this class.
 */
    bool SILVERLINING_API GetCloudLayer(int layerHandle, CloudLayer **layer);

/** Obtain the stl map of CloudLayer objects. An accessor for a reference to the STL map of
   CloudLayer objects managed by this AtmosphericConditions class. It is not const, and so
   it may be directly manipulated. Use with care.*/
    SL_MAP(int, CloudLayer*)& SILVERLINING_API GetCloudLayers() {
        return cloudLayers;
    }

	const SL_MAP(int, CloudLayer*)& SILVERLINING_API GetCloudLayers() const {
		return cloudLayers;
	}

/** Sets the "turbidity" of the simulated atmosphere. You can think of this as a measure
    of "haziness."  Some guidelines for setting this value:

    2 = very clear, range 50 km
    3 = clear, range 15 km
    7 = light haze, range 8 km

   Setting turbidity is not an appropriate way to simulate fog; see Atmosphere::SetHaze()
   for one method, or for dense fog simply clear the backbuffer to the fog color and pass
   false to Atmosphere::DrawSky() to suppress the sky rendering altogether.

   Turbidity just lets you simulate the number of particles in the air. In practice it
   will vary the color of the sky from a pure light blue to a hazy, darker, yellowish
   color. Turbidity values below 1.8 or above 8.0 lead to unpredictable results and are clamped.

    \param \pTurbidity The ratio of scattering due to haze to scattering due to molecules.
 */
    void SILVERLINING_API SetTurbidity(double pTurbidity) {
        if (pTurbidity < 1.8) pTurbidity = 1.8;
        if (pTurbidity > 8.0) pTurbidity = 8.0;
        turbidity = pTurbidity;
    }

/** Get the simulated atmospheric turbidity. See SetTurbidity() for an explanation of
   turbidity values. */
    double SILVERLINING_API GetTurbidity() const {
        return turbidity;
    }

/** Sets the simulated visibility in meters; this will affect the appearance of clouds in the
   distance. Defaults to 30km. This is intended only for light haze, and serves only to blend
    clouds into the sky in the distance. It does not fog the sky itself. For thicker fog, see
    AtmosphericConditions::SetFog() and Atmosphere::SetHaze(). */
    void SILVERLINING_API SetVisibility(double range) {
        visibility = range;
    }

/** Retrieves the currently simulated visibility, in meters. */
    double SILVERLINING_API GetVisibility() const {
        return visibility;
    }

/** Explicitly sets exponential fog color and density, if you need to match an existing scene.
   If set, this will override the visibility effects from SetVisibility(). Color components are
   specified in the range 0-1. Density should be 1.0 / simulated visibility. This affects the
   appearance of the clouds, but not the sky.
   This is intended for real, thick fog, as opposed to SetVisibility(), which simulates extinction
   from atmospheric scattering. When using this, you'll usually want to call DrawSky() with
   false in the first parameter to suppress drawing the sky box, and just clear your back buffer
   to the fog color instead. If you do draw the sky, you may want to consider disabling the drawing
   of the stars, sun, and moon when calling DrawSky() if thick fog is present. */
    void SILVERLINING_API SetFog(double density, double r, double g, double b);

/** Clears explicitly set fog from SetFog(), and reverts the fog to simulating the visibility
   specified in SetVisibility() instead. */
    void SILVERLINING_API ClearFog();

/** Retrieves the explicit fog (if any) set by SetFog().
   \param fogIsSet Returns true if SetFog has been called and ClearFog has not, meaning we are using fog
   with an explicitly defined application setting instead of simulating visibility.
   \param density The exponential fog density term set in SetFog()
   \param r The red component of the fog color specified in SetFog(). Ranges from 0-1.
   \param g The green component of the fog color specified in SetFog(). Ranges from 0-1.
   \param b The blue component of the fog color specified in SetFog(). Ranges from 0-1. */
    void SILVERLINING_API GetFog(bool& fogIsSet, double& density, double &r, double &g, double &b);

/** Sets the simulated amount of nighttime light pollution, in watts per square meter.
    Defaults to zero. A reasonable value would be in the order of 0.01 */
    void SILVERLINING_API SetLightPollution(double Wm2) {
        lightPollution = Wm2;
    }

/** Retrieves the currently simulated light pollution, in watts per square meter. */
    double SILVERLINING_API GetLightPollution() const {
        return lightPollution;
    }

/** Simulates global precipitation of a specified type. Precipitation effects will display if the
    type is set to something other than NONE.

    Note, you may also set precipitation effects associated with a CloudLayer that only render when
    the camera is underneath a rain cloud with the similar CloudLayer::SetPrecipitation() method.

    For mixed precipitation, you may call SetPrecipitation multiple times with different precipitation
    types. To clear all precipitation, call SetPrecipitation with a type of CloudLayer::NONE. If you
    call this method multiple times for the same precipitation type, the intensity specified will
    overwrite the intensity previously specified for that type.

   \param precipitationType The type of precipitation to simulate under this cloud layer - CloudLayer::NONE,
                CloudLayer::RAIN, CloudLayer::WET_SNOW, CloudLayer::DRY_SNOW, or CloudLayer::SLEET.
   \param precipitationRate The simulated rate of precipitation, in millimeters per hour. Reasonable ranges
   might be between 1 for light rain or 20 for heavier rain. This value will be clamped to the value
   specified by rain-max-intensity, snow-max-intensity, or sleet-max-intensity in
    resources/SilverLining.config, which is 30 by default.

   \param nearClip How close to the camera the closest precipitation particles will be rendered. The
   near clipping plane will be adjusted to include this distance while the precipitation is being 
   rendered. Values less than zero will result in the default value being applied.

   \param farClip The farthest distance from the camera that precipitation particles will be rendered.
   Since there is an upper bound on the number of particles rendered per frame, changing the difference
   between nearClip and farClip may result in changes to the intensity of the precipitation. By default
   this difference is 7.5 world units. Values less than zero will result in the default value being
   applied.
 
   \param bUseDepthBuffer Set this to true if you want to enable depth buffer testing of precipitation 

   \param data An optional ThreadCameraStreamData object that this calls pertains to.
   particles against your scene.
*/
    void SILVERLINING_API SetPrecipitation(int precipitationType, double precipitationRate, double nearClip = -1, double farClip = -1, bool bUseDepthBuffer = false, void* data = 0);

/** Used to temporarily enable or disable all precipitation effects. 
   \param data An optional ThreadCameraStreamData object that this calls pertains to.
*/
    void SILVERLINING_API EnablePrecipitation(bool enabled, void* data = 0);

/** Sets a precipitation specific wind vector which is additive to any global wind set at 
   the AtmosphericConditions level. The vector itself specifies the wind direction and the 
   length of the vector provides the wind velocity.

   \param windX The east vector of the precipitation specific wind, in meters/s.
   \param windZ The south vector of the precipitation specific wind, in meters/s.
   \sa GetWind()
 */
    void SILVERLINING_API SetPrecipitationWind(double windX, double windZ) {
        precipitationWindX = windX;
        precipitationWindZ = windZ;
    }

/** Retrieves the precipitation specific wind vector.
   \sa SetWind()
 */
    void SILVERLINING_API GetPrecipitationWind(double& windX, double& windZ) const {
        windX = precipitationWindX;
        windZ = precipitationWindZ;
    }

/** By default, SilverLining will "freeze" time at the time specified by
   AtmosphericConditions::SetTime(). If you want to simulate the passage of
   time, call EnableTimePassage with the enabled parameter set to true.

   Relighting the clouds is a relatively expensive operation, so for real
   time applications you probably won't want to relight the clouds every
   frame. The relightFrequencyMS parameter allows you to specify the interval,
   in milliseconds, between cloud relighting passes. The sky will continue
   to update in real time, along with the position of the sun, moon, and
   stars, between these intervals. If you have specified your own
   MillisecondTimer with SetMillisecondTimer(), the interval will be computed
   based on its concept of time.

   If the enabled parameter is false (the default,) or the relightFrequencyMS
   parameter is set to -1, cloud relighting will only happen in response to
   calls to AtmosphericConditions::SetTime().

   Clouds will move with respect to the simulated wind irregardless of calling
   this method.

   \param enabled True if you want to simulate the passage of time between
   calls to SetTime(); false if the simulated time should remain static.
   \param relightFrequencyMS The interval, in milliseconds, between cloud
   relighting passes. Set to -1 to prevent cloud relighting outside of calls
   \param data An optional ThreadCameraStreamData object that this calls pertains to.
   to SetTime().
 */
    void SILVERLINING_API EnableTimePassage(bool enabled, long relightFrequencyMS, void* data = 0);

#ifndef SWIG
/** By default, SilverLining will simulate the motion of clouds in the wind
   and the motion of astronomical objects (when EnableTimePassage() is active)
   by calling the system's millisecond timer. If you want to accelerate, slow,
   or reverse the passage of time, you may instead provide your own
   MillisecondTimer implementation, and pass it in here. See the documentation
   for the MillisecondTimer class for more details.

   \param timer The MillisecondTimer object to use for moving clouds and
   astronomical objects over time, relative to the LocalTime passed into
   SetTime(). Pass NULL to restore the default timer.

   \param data An optional ThreadCameraStreamData object that this calls pertains to.
 */
    void SILVERLINING_API SetMillisecondTimer(const MillisecondTimer *timer, void* data = 0);

/** Retrieves the MillisecondTimer previously set by SetMillisecondTimer(),
   or the default timer if SetMillisecondTimer() is unset or set to NULL. */
    const MillisecondTimer * SILVERLINING_API GetMillisecondTimer() const {
        return timer;
    }

/** Flattens this object and everything in it to a stream buffer. */
    bool SILVERLINING_API Serialize(std::ostream& stream, void* data = 0);

/** Restores this object from the stream created using Serialize() */
    bool SILVERLINING_API Unserialize(const Atmosphere *atm, std::istream& stream, void* data = 0);

// Methods for internal use only:
    bool SILVERLINING_API WantsLightingUpdate(ThreadCameraStreamData* tcsData);
    void SILVERLINING_API SetAtmosphere(Atmosphere* atmosphere);

#endif

#ifndef SWIG
private:
    double turbidity, visibility, lightPollution;

    const MillisecondTimer *timer;
    MillisecondTimer *defaultTimer;

    Atmosphere* atmosphere;

    SL_MAP(int, WindVolume) windVolumes;
    SL_MAP(int, CloudLayer*) cloudLayers;

    double precipitationWindX, precipitationWindZ;

    bool overrideFog;
    double fogDensity, fogR, fogG, fogB;

    static int runningCloudHandle;
#endif
};
}

#pragma pack(pop)

#endif

