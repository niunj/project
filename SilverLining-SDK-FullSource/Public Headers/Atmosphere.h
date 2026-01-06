// Copyright (c) 2004-2022  Sundog Software, LLC. All rights reserved worldwide.

#ifndef ATMOSPHERE_H
#define ATMOSPHERE_H

/** \file Atmosphere.h
   \brief The main interface to SilverLining.
 */
#ifdef SWIG
#define SILVERLINING_API
#define SL_VECTOR(a) std::vector<a>
%module SilverLiningAtmosphere
%include "carrays.i"
%include "AtmosphericConditions.h"
%array_functions( double, double_array )
%{
#include "Atmosphere.h"
using namespace SilverLining;
%}
#endif

#include "SilverLiningTypes.h"
#include "MemAlloc.h"
#include "AtmosphericConditions.h"
#include "RandomNumberGenerator.h"
#include "Matrix4.h"
#include "Vector3.h"
#include "Frustum.h"
#include <map>

#include <iostream>

#pragma pack(push)
#pragma pack(8)

namespace SilverLining
{
// Forward declarations of internal classes.
class Sky;
class Ephemeris;
class AtmosphereFromSpace;
class LensFlare;
class Color;
class ResourceLoader;
class Mutex;
class CrepuscularRays;
class ShadowMap;
class EnvironmentMap;
class Camera;
class BillboardBuffer;
class BillboardBufferProvider;
class CameraParameters;
class Renderer;
class CloudLayerSorterProvider;
class ThreadCameraStreamData;
class AtmosphereTcsUserData;

typedef void * ObjectHandle;

/** This class is the main interface to SilverLining.  The Atmosphere simulates the sky,
    clouds, and weather.

   To use an Atmosphere class, simply instantiate one using the default constructor, and
   then call the Initialize() method to set it up for either the OpenGL or DirectX rendering
   subsystems.

   You may initialize the atmospheric conditions (cloud decks, wind, time, location, etc.)
   by populating an AtmosphericConditions class and passing this to the SetConditions method
   prior to rendering.

   Then, within your main rendering loop, as soon as you have set the view and projection
   matrices for your scene to reflect the current camera position, call the DrawSky() method.
   This will draw the background of the sky and perform any lighting computations required for
   the atmosphere.

   At this point, you may query GetSunOrMoonPosition() and GetSunOrMoonColor() to obtain the
   tone-mapped directional lighting information for your outdoor scene, and GetAmbientColor() to
   get the tone-mapped ambient skylight for your scene.

   Finally, at the end of your rendering loop, call DrawObjects(). This will draw all of the clouds
   within the scene in back-to-front order.
 */
class Atmosphere : public MemObject
{
public:
/** Constructor. Creates an atmosphere object with default settings. You must specify
   the user name and license key provided with your SilverLining license purchase in
   order to instantiate an Atmosphere. An invalid name / key combination will result in
   a dialog box informing the user that this software is unlicensed, and in the application
   terminating after two minutes of use.

    Warning! Displaying a dialog box while in full-screen mode will cause some DirectX
    applications to crash. To prevent the licensing dialog box on unlicensed SDK's from
    causing a crash, instantiate your Atmosphere object before entering full-screen mode.

   \param userName The user name, exactly as provided to you in the licensing information
        received with your SilverLining license purchase. A null-terminated C
     string.
   \param licenseKey The license key code, exactly as provided to you with your license
       purchase. Null-terminated C string.

 */
    Atmosphere(const char *userName, const char *licenseKey);

/** Destructor. This will clean up all cloud objects owned by the atmospheric conditions. */
    ~Atmosphere();

/** Configures the simulated cloud, wind, time, and location. The cloud, wind, time, and
   location settings are all contained inside an AtmosphericConditions object. Pass in
   an AtmosphericConditions class configured the way you like it, after first calling Initialize
   on the Atmosphere class, and before rendering your scene. Any previous conditions will
   be overwritten by this call.

   Note, this passes in a const reference to your AtmosphericConditions class. It is copied
   internally, and you're free to dispose of your AtmosphericConditions object once you
   have passed it into SetConditions. */
    void SILVERLINING_API SetConditions(const AtmosphericConditions& conditions);

/** Accessor to the current cloud, wind, time, and location settings. Returns a const
   reference to an AtmosphericConditions class that contains the settings for the current
   simulation. */
    const AtmosphericConditions& SILVERLINING_API GetConditions() const;

/** Sets the physical model used for simulating sky colors. The "Preetham model" is simple
    and fast, but has some inaccuracies near the horizon. The newer "Hosek-Wilkie" model
    extends the Preetham model for more accurate sky colors, especially at very high
    and low solar angles. If "Hosek-Wilkie" is selected, it's actually only used for daytime
    lighting form the sun - twilight and moonlit conditions still use the Preetham model,
    since Hosek-Wilkie can only simulate sky colors from sunlight at positive solar angles. 
    \param skyModel One of PREETHAM or HOSEK_WILKIE, as described above.
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
*/
    void SILVERLINING_API SetSkyModel(SkyModel skyModel, void* data = 0);

/** Retrieves the current sky model in use. \sa SetSkyModel().
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    \return One of PREETHAM or HOSEK_WILKIE.
*/
    SkyModel SILVERLINING_API GetSkyModel(void* data = 0) const;

/** Retrieve the normalized direction of the dominant directional light source.
   This method will return a normalized direction vector pointing to the dominant light source,
   which may be the moon after sunset.
   In left-handed coordinate systems where positive Z is "North", you  may need to 
   negate the Z value returned to match your conventions.
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
   */
    void SILVERLINING_API GetSunOrMoonPosition(float *x, float *y, float *z, void* data = 0) const;

/** Retrieve the normalized direction of the dominant directional light source.
    This method will return a normalized direction vector pointing to the dominant light source,
    which may be the moon after sunset. The direction is relative to the center of the Earth in a
    geocentric coordinate system, where the z axis points from the center of the
    Earth to the North Pole, and x points toward the prime meridian.
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    void SILVERLINING_API GetSunOrMoonPositionGeographic(float *x, float *y, float *z, void* data = 0) const;

/** Retrieve the normalized direction of the dominant directional light source.
    This method will return a normalized direction vector pointing to the dominant light source,
    which may be the moon after sunset. The direction is in equatorial coordinates; where x points
    toward the vernal equinox (where the equator and ecliptic intersect), z points
    through the north pole.
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    void SILVERLINING_API GetSunOrMoonPositionEquatorial(float *x, float *y, float *z, void* data = 0) const;

/** Retrieve the normalized direction of the sun's light source. This method will return a
   normalized direction vector pointing to the sun, which may be below the horizon.
   In left-handed coordinate systems where positive Z is "North", you  may need to 
   negate the Z value returned to match your conventions. 
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
   */
    void SILVERLINING_API GetSunPosition(float *x, float *y, float *z, void* data = 0) const;

/** Retrieve the normalized direction of the moon's light source. This method will return a
   normalized direction vector pointing to the moon, which may be below the horizon. 
   In left-handed coordinate systems where positive Z is "North", you  may need to
   negate the Z value returned to match your conventions. 
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
   */
    void SILVERLINING_API GetMoonPosition(float *x, float *y, float *z, void* data = 0) const;

/** Retrieve the direction of the sun, relative to the center of the Earth in a
   geocentric coordinate system, where the z axis points from the center of the
   Earth to the North Pole, and x points toward the prime meridian. 
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
   */
    void SILVERLINING_API GetSunPositionGeographic(float *x, float *y, float *z, void* data = 0) const;

/** Retrieve the direction of the moon, relative to the center of the Earth in a
   geocentric coordinate system, where the z axis points from the center of the
   Earth to the North Pole, and x points toward the prime meridian. 
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
   */
    void SILVERLINING_API GetMoonPositionGeographic(float *x, float *y, float *z, void* data = 0) const;

/** Retrieve the direction of the sun in equatorial coordinates; where x points
   toward the vernal equinox (where the equator and ecliptic intersect), z points
   through the north pole. 
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
   */
    void SILVERLINING_API GetSunPositionEquatorial(float *x, float *y, float *z, void* data = 0) const;

/** Retrieve the direction of the moon in equatorial coordinates; where x points
   toward the vernal equinox (where the equator and ecliptic intersect), z points
   through the north pole. 
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
   */
    void SILVERLINING_API GetMoonPositionEquatorial(float *x, float *y, float *z, void* data = 0) const;

/** Returns the color of the dominant directional light source. This method will return a color
   suitable for lighting your scene based on the natural lighting conditions. The value is
   tone-mapped, so the high dynamic range between night and day and they way the human eye
   perceives it is modeled. For example, at noon this value is likely white, but at night
   it may be surprisingly bright if a full moon is out. At sunset or sunrise, there may be an
   orange, pink, or reddish hue due to the scattering of sunlight through the atmosphere -
   which is also simulated. This scattering is sensitive to the turbidity setting specified
   in the AtmosphericConditions class.

   This method only simulates natural light sources passing through the atmosphere: the sun,
   moon, starlight, planetary light, airglow, galactic light, and zodiacal light. At night,
   it may be appropriate to add in a little extra to simulate city lights for urban or suburban
   scenes.

   If your camera position is below a thick, infinite stratus cloud deck with coverage of 1.0, 
   the effects on lighting will be simulated as well.

   \param r A pointer to a float that will be populated with the red directional light color,
    ranging from 0 - 1.0.
   \param g A pointer to a float that will be populated with the green directional light color,
    ranging from 0 - 1.0.
   \param b A pointer to a float that will be populated with the blue directional light color,
    ranging from 0 - 1.0.
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
 */
    void SILVERLINING_API GetSunOrMoonColor(float *r, float *g, float *b, void* data = 0) const;


/** Returns the color of the sun's light source. This method will return a color
   suitable for lighting your scene based on the natural lighting conditions. The value is
   tone-mapped, so the high dynamic range between night and day and they way the human eye
   perceives it is modeled. At sunset or sunrise, there may be an
   orange, pink, or reddish hue due to the scattering of sunlight through the atmosphere -
   which is also simulated. This scattering is sensitive to the turbidity setting specified
   in the AtmosphericConditions class.

   If your camera position is below a thick, infinite stratus cloud deck with coverage of 1.0, 
   the effects on lighting will be simulated as well.

   \param r A pointer to a float that will be populated with the red directional light color,
    ranging from 0 - 1.0.
   \param g A pointer to a float that will be populated with the green directional light color,
    ranging from 0 - 1.0.
   \param b A pointer to a float that will be populated with the blue directional light color,
    ranging from 0 - 1.0.
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
 */
    void SILVERLINING_API GetSunColor(float *r, float *g, float *b, void* data = 0) const;

/** Returns the color of the moon's light source. This method will return a color
   suitable for lighting your scene based on the natural lighting conditions. The value is
   tone-mapped, so the high dynamic range between night and day and they way the human eye
   perceives it is modeled. For example, at night it may be surprisingly bright if a full moon
   is out. This scattering is sensitive to the turbidity setting specified in the
   AtmosphericConditions class.

   This method only simulates natural light sources passing through the atmosphere: the sun,
   moon, starlight, planetary light, airglow, galactic light, and zodiacal light. At night,
   it may be appropriate to add in a little extra to simulate city lights for urban or suburban
   scenes.

   If your camera position is below a thick, infinite stratus cloud deck with coverage of 1.0, 
   the effects on lighting will be simulated as well.

   \param r A pointer to a float that will be populated with the red directional light color,
    ranging from 0 - 1.0.
   \param g A pointer to a float that will be populated with the green directional light color,
    ranging from 0 - 1.0.
   \param b A pointer to a float that will be populated with the blue directional light color,
    ranging from 0 - 1.0.
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
 */
    void SILVERLINING_API GetMoonColor(float *r, float *g, float *b, void* data = 0) const;

/** Returns the color of the ambient "skylight". This color is suitable for use as an ambient
   light color for your scene. When used together with the directional light position and color
   returned by GetSunOrMoonPosition and GetSunOrMoonColor, an accurate simulated model of
   natural lighting conditions may be obtained.

   The color returned by this method is arrived at by simulating the light scattered over the
   sky above your simulated location at your simulated time, commonly referred to as "skylight."
   This light is then tone-mapped to account for high dynamic range in the same manner as
   the directional light. This color is also affected by the presence of thick cloud decks
   above the camera position.

   \param r A pointer to a float to receive the red channel of the ambient color, from 0 -1.0.
   \param g A pointer to a float to receive the green channel of the ambient color, from 0 -1.0.
   \param b A pointer to a float to receive the blue channel of the ambient color, from 0 -1.0.
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
 */
    void SILVERLINING_API GetAmbientColor(float *r, float *g, float *b, void* data = 0) const;

/** Returns the average color of the sky at the horizon. Based on the specified view direction
   and field of view, this method will return the average color of the sky in the scene at
   the horizon. This is often an effective choice of a fog color for your scenes, since it
   will blend well with the sky in the distance - thereby covering up outdoor scenes that lack
   sufficient terrain to extend to the horizon. Even for scenes that do render to the horizon,
   this is a good color for fog just for simulating haze and atmospheric perspective.

   Note that during sunrise and sunset when a red glow surround the sun, this color may
   vary widely depending on the yawDegrees parameter.

   \param yawDegrees The camera's yaw value, in degrees east from north. The horizon color
       will be calculated by averaging the field of view's horizon color about
       this direction.
   \param pitchDegrees The number of degrees above the horizon to sample the color from.
   \param r A pointer to a float to receive the red channel of the horizon color, from 0 - 1.0
   \param g A pointer to a float to receive the red channel of the horizon color, from 0 - 1.0
   \param b A pointer to a float to receive the red channel of the horizon color, from 0 - 1.0
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
 */
    void SILVERLINING_API GetHorizonColor(float yawDegrees, float pitchDegrees, float *r, float *g, float *b, void* data = 0) const;

/** Returns the average color of the sky at the horizon. Based on the current camera orientation
   and field of view, this method will return the average color of the sky in the scene at
   the horizon. This is often an effective choice of a fog color for your scenes, since it
   will blend well with the sky in the distance - thereby covering up outdoor scenes that lack
   sufficient terrain to extend to the horizon. Even for scenes that do render to the horizon,
   this is a good color for fog just for simulating haze and atmospheric perspective.

   Note that during sunrise and sunset when a red glow surround the sun, this color may
   vary widely depending on the orientation of the camera.

   \param pitchDegrees The number of degrees above the horizon to sample the color from.
   \param r A pointer to a float to receive the red channel of the horizon color, from 0 - 1.0
   \param g A pointer to a float to receive the red channel of the horizon color, from 0 - 1.0
   \param b A pointer to a float to receive the red channel of the horizon color, from 0 - 1.0
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
 */
    void SILVERLINING_API GetHorizonColor(float pitchDegrees, float *r, float *g, float *b, void* data = 0) const;

/** Returns the color of the sky at the zenith.
   \param r A pointer to a float to receive the red channel of the zenith color, from 0 - 1.0
   \param g A pointer to a float to receive the red channel of the zenith color, from 0 - 1.0
   \param b A pointer to a float to receive the red channel of the zenith color, from 0 - 1.0
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
 */
    void SILVERLINING_API GetZenithColor(float *r, float *g, float *b, void* data = 0) const;

/** Returns whether SilverLining would like to suggest fog settings. SilverLining depends on
   fog effects to simulate being inside a stratus or broken stratus cloud deck, or to simulate
   reduced visibility due to the presence of rain or snow at the camera's location. If this method
   returns true, then you should call GetFogSettings in order to help determine the appropriate
   fog configuration for your scene in order to preserve these atmospheric effects.

   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.

   */
    bool SILVERLINING_API GetFogEnabled(void* data = 0) const;

/** Returns exponential fog settings appropriate for your scene. If GetFogEnabled() returns
   true, then call GetFogSettings() to obtain suggested fog values required to preserve
   in-cloud effects and precipitation effects that depend on fog. The r, g, and b colors
   returned are the color of the fog itself modulated by the directional light color.

   \param density A fog density appropriate for use in GL_FOG_DENSITY in exponential fog
       mode. ie, \f$f = e^{-(density \cdot z)}\f$
   \param r A pointer to a float to receive the red component of the fog color, from 0 - 1.0.
   \param g A pointer to a float to receive the green component of the fog color, from 0 - 1.0.
   \param b A pointer to a float to receive the blue component of the fog color, from 0 - 1.0.

   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
 */
    void SILVERLINING_API GetFogSettings(float *density, float *r, float *g, float *b, void* data = 0) const;

/** Causes the sky to blend toward a specified "haze color" toward the horizon. Although
   it does simulate a layer of colored fog, it's most practical application to allow for
   exact blending against a fog color used for terrain, in order to obscure the horizon line.
   For applications that do not render terrain all the way to the horizon, this is a must.
   GetHorizonColor() may be used for an approximate match in the absence of an artificial
   layer of haze; it is more physically accurate.

   The haze color passed in is not lit; you must pre-multiply the color yourself. The skybox
   will blend toward the exact color passed in at the horizon, night or day. You could abuse
   this to create a glow effect at the horizon from city lights, for example.

   By default, hazeDepth is set to 0, thereby disabling the haze effects. If the viewpoint
   is within a cloud, the fog effects from the cloud will drawn in the sky in lieu of haze.

   All parameters must be greater than or equal to zero. If any parameter is less than zero
   then the method will do nothing.

   \param hazeR The red component of the color to blend toward at the horizon.
   \param hazeG The red component of the color to blend toward at the horizon.
   \param hazeB The red component of the color to blend toward at the horizon.
   \param hazeDepth The simulated height of the haze volume at ground level, in world units.
   \param hazeDensity The fog density parameter of the exponential fog equation.
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
 */
    void SILVERLINING_API SetHaze(float hazeR, float hazeG, float hazeB, double hazeDepth,
                 double hazeDensity, void* data = 0);

/** Retrieves the haze parameters set previously by SetHaze(). See SetHaze() for a
   description of the parameters. 
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
   */
    void SILVERLINING_API GetHaze(float& hazeR, float& hazeG, float& hazeB, double& hazeDepth,
                 double& hazeDensity, void* data = 0);

/** Sets the value used for gamma correction of the display. Defaults to the sky-box-gamma setting.
   1.8 works well. Higher values will yield lighter skies and natural light. 
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
   */
    void SILVERLINING_API SetGamma(double gamma, void* data = 0);

/** Retrieves the value being used for display gamma correction.
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
   \sa SetGamma()
 */
    double SILVERLINING_API GetGamma(void* data = 0) const;

/** Force SilverLining to recompute all cloud lighting. Under normal operation, SilverLining
   only recomputes cloud lighting when the simulated time, location, or cloud cover changes,
   or when the camera moves a significant amount relative to each cloud. If you wish to force
   a lighting computation for all clouds to take place on the next frame, call this method.

   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.

   There is a performance impact for calling this. Under normal circumstances, there is no
   reason to call this method. */
    void SILVERLINING_API ForceLightingRecompute(void* data = 0);

/** Return a reference to the current simulated conditions. Use this to change the current
   simulated time, location, etc., by manipulating the AtmosphericConditions object owned
   by the Atmosphere class directly. */
    AtmosphericConditions * SILVERLINING_API GetConditions() {
        return conditions;
    }

    enum
    {
        OPENGL = 0,
        DIRECTX9,
        DIRECTX11,
        DIRECTX11_1,
        CUSTOM_RENDERER,
        OPENGL32CORE, // Really this is OpenGL 3.2 and above
        OPENGLES2, // No longer supported
        VULKAN // In development; do not use yet!
    };

    enum InitializeErrors
    {
        E_NOERROR = 0,
        E_RESOURCES_PATH_INVALID,
        E_CONFIGURATION_FILE_NOT_FOUND,
        E_CANT_LOAD_RENDERER_DLL,
        E_CANT_INITIALIZE_RENDERER_SUBSYSTEM,
        E_CANT_LOAD_METABALL_TEXTURE
    };

/** Call this immediately after constructing your scene's Atmosphere and initializing your
   graphics subsystem (OpenGL, OpenGL32, DirectX9, DirectX11, or DirectX11.1). This method will configure SilverLining to use
   OpenGL or DirectX, and in the case of DirectX, allows you to pass in a required pointer
   to your IDirect3DDevice9, ID3D10Device, or ID3D11Device object.

   This method should be called from the same thread and context you intend to render the Atmosphere from.

    If you are tying SilverLining directly into your own rendering engine, pass
    Atmosphere::CUSTOM_RENDERER as the renderer, provide your own implementation of the functions in
    SilverLiningDLLCommon.h, and link against the "static" versions of the SilverLining libraries.

   The OPENGL32CORE renderer will only initialize successfully on graphics hardware and drivers that
   support the OpenGL 3.2 specification, with contexts created explicitly for OpenGL 3.2 or newer.
   It is compatible with forward-compatible OpenGL 3.2 core profile contexts that do not
   have backward-compatibilty enabled; no deprecated OpenGL functions are used in this renderer.

   OpenGL users may pass 0 for the environment parameter. It is ignored for OpenGL.

   ** Note ** This method is not thread safe.

   You must also pass in a path to the Resources directory, which contains the art resources,
   data files, and shaders required for SilverLining to run. You may name and redistribute
   this directory however you wish, but SilverLining needs to know where it is and what
   it's called.

   \param renderer Pass the enumerated constant \c SilverLining::OPENGL or \c SilverLining::OPENGL32CORE or
     \c SilverLining::DIRECTX9 or
     \c SilverLining::DIRECTX11  or \c SilverLining::DIRECTX11_1 or \c SilverLining::CUSTOM_RENDERER

   \param resourceDirectoryPath A null-terminated string that specifies a path to
   the application's redistributed "Resources" directory, including the directory name
   itself. For example, "..\\Resources\\". 

   \param rightHanded Pass true if you're using a right-handed coordinate system, false for
   a left-handed coordinate system. OpenGL typically uses right-handed; DirectX can use either.

   \param environment When using DIRECTX9, DIRECTX11, or DIRECTX11_1 renderers, this must contain
   a pointer to your IDirect3DDevice9, ID3D10Device, or ID3D11Device. When using VULKAN, this
   must contain a pointer to a fully populated VulkanInitInfo structure, as defined in the
   VulkanInitInfo.h header.

   \return An error code from the Atmosphere::InitializeErrors enumeration, or E_NOERROR. See
    the troubleshooting section of the documentation for further guidance if you encounter an
    error.
 */
    int SILVERLINING_API Initialize(int renderer, const char *resourceDirectoryPath, bool rightHanded, void *environment);

/** OpenGL-only variant of Atmosphere::Initialize() that takes in a list of user-compiled shader objects that will be 
    linked into all subsequently linked shader program objects. This allows you to inject your own shader functions into 
    our shaders without copying the source.

    ** Note ** This method is not thread safe.
*/
    int SILVERLINING_API Initialize(int renderer, const char *resourceDirectoryPath, bool rightHanded, void *environment, 
        const SL_VECTOR(unsigned int)& userShaders);

/** Sets the assumption of what direction is "up". If the vector (x, y, z)
   is not a unit vector, it is normalized before being stored. Cannot be called prior to
   Atmosphere::Initialize(). Must be called in conjunction with SetRightVector().

    Be sure to call this prior to positioning any clouds.

    In a geocentric / ECEF coordinate system, this should be the normalized camera position vector pointing
    from the center of the Earth, not the local normal vector of the Earth's ellipsoid. There is a 
    subtle difference between the two that can lead to cloud positions being a bit off.

   \sa SetRightVector()
   \sa GetUpVector()
 */
    void SILVERLINING_API SetUpVector(double x, double y, double z);

/** Returns the direction that SilverLining assumes is "up" as a unit vector. */
    void SILVERLINING_API GetUpVector(double& x, double& y, double& z);

/** Sets the assumption of what direction is "right". If the vector (x, y, z)
   is not a unit vector, it is normalized before being stored. Cannot be called prior to
   Atmosphere::Initialize(). Must be called in conjunction with SetUpVector().

    Be sure to call this prior to positioning any clouds.

   \sa SetUpVector()
   \sa GetRightVector()
 */
    void SILVERLINING_API SetRightVector(double x, double y, double z);

/** Returns the direction that SilverLining assumes is "right" as a unit vector. */
    void SILVERLINING_API GetRightVector(double& x, double& y, double& z);

/** Call this at the beginning of each rendering loop, prior to calling DrawSky(). The
   matrix passed in should represent the transform for your camera, as a 4x4 matrix of
   doubles. Be sure to also call SetProjectionMatrix() */
    void SILVERLINING_API SetCameraMatrix(const double *cameraMatrix);

/** Call this at the beginning of each rendering loop, prior to calling DrawSky(). The
   matrix passed in should represent the projection matrix for your scene, as a 4x4 matrix
   of doubles. Be sure to also call SetCameraMatrix() */
    void SILVERLINING_API SetProjectionMatrix(const double *projectionMatrix);

/** Call this at the beginning of each rendering loop, prior to calling DrawSky().
    \param x The x position of the viewport origin.
    \param y The y position of the viewport origin.
    \param w The width of the viewport.
    \param h The height of the viewport. */
    void SILVERLINING_API SetViewport(int x, int y, int w, int h);

/** Call this at the beginning of each rendering loop, prior to calling DrawSky(), indicating
    the range of depth buffer values in use. Calling this is optional, but may result in
    avoiding a stall each frame. Normally, the near depth value is 0.0 and the far depth
    value is 1.0, unless you're doing something like implementing reversed floating point
    depth buffers.

	In the case of reverse depth buffers, OpenGL will call glDepthRangedNV with the values 
	you pass in, which will have the effect of flipping depth values as SilverLining draws
	its own objects. In DirectX, the viewport near and far ranges will not be modified
	if the far depth value is less than the near depth value, so you will need to ensure the
	projection matrix passed into SilverLining reflects your reversed depth scheme instead.

    \param nearDepth The z value that is mapped to the near clipping plane. (Usually 0)
    \param farDepth The z value that is mapped to the far clipping plane. (Usually 1)
*/
    void SILVERLINING_API SetDepthRange(float nearDepth, float farDepth);


/** Returns an array of 16 doubles representing the view matrix last set via SetCameraMatrix(). */
    const double * SILVERLINING_API GetCameraMatrix() const;

/** Returns an array of 16 doubles representing the projection matrix last set via SetProjectionMatrix(). */
    const double * SILVERLINING_API GetProjectionMatrix() const;

/** Returns the viewport size and position last set via SetViewport(). */
    bool SILVERLINING_API GetViewport(int& x, int& y, int& w, int& h) const;

/*** Returns the depth range last set via SetDepthRange(). */
    bool SILVERLINING_API GetDepthRange(float& nearDepth, float& farDepth) const;
    
/** Explicitly updates the ephemeris model and cloud shapes and positions. Calling this method
    is optional; if it is not called prior to Atmosphere::DrawSky(), it will be called automatically.
    But, once you start calling it, it must be called every frame. This exposes the ability to perform 
    updates in a separate pass or thread from the actual drawing of the sky and clouds. However, you
    must ensure that a valid and correct GL or device context is active prior to calling this. For example,
    this is not the case with OpenSceneGraph's update traversal - so don't use this method with OSG.

    \param geocentricMode Whether we are updating in geocentric mode. See DrawSky().

    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    void SILVERLINING_API UpdateSkyAndClouds(bool geocentricMode = false, void* data = 0);

/** Explicitly performs culling tests against the view frustum for the clouds in the scene. Calling
    this method is optional; if it is not called prior to Atmosphere::DrawSky(), it will be called
    automatically. However, this exposes the ability to perform culling in a separate pass or thread
    from the actual drawing of the clouds. Make sure Atmosphere::SetCameraMatrix() and 
    Atmosphere::SetProjectionMatrix() have been called prior to calling this method, so its view
    frustum is accurate. 
    
    If you are calling Atmosphere::DrawObjects() more than once per frame, be sure to call 
    Atmosphere::CullObjects() prior to each DrawObjects() call. 
    
    \param threadSafe whether a mutex will be enforced to protect against culling from occurring
    simultaneously with drawing. Needed only in multi-threaded applications.

    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    void SILVERLINING_API CullObjects(bool threadSafe = true, const Camera* camera = 0, void* data = 0);

/** Call this at the beginning of your rendering loop. At the start of each frame in your scene,
   first call SetCameraMatrix() and SetProjectionMatrix(). Then, call DrawSky(). This will
   draw the skybox for the simulated time and location, including the sun, moon, stars, and planets,
   and perform any necessary lighting calculations.

   DrawSky() should be called from the same rendering context and thread that was active when
   Atmosphere::Initialize() was called. Otherwise, you may experience a crash inside this method.

   Alternately, DrawSky() may be called instead at the end of your rendering loop (prior to drawing
   translucent objects and prior to calling DrawObjects() ) if you set the clearDepth parameter
   to false. This may result in better performance, as it reduces overdraw against the sky box
   by rendering the sky box last instead of first.

   It is important that the camera and projection matrices are set to reflect your scene's current
   camera prior to calling DrawSky(). Versions of SilverLining prior to 1.7 would extract
   these matrices automatically, but beginning with 1.7 you MUST call SetCameraMatrix() and
   SetProjectionMatrix(). We made this change in order to support DirectX 10, which has no
   fixed function pipeline for us to query for these matrices.

   When using the VULKAN renderer, be sure to associate SilverLining with the current command
   buffer using ThreadCameraStreamData::SetStream() prior to calling this, or any other method
   that issues draw commands.

   Every call to DrawSky() should be balanced with a later call to DrawObjects() or GetObjects().

   \param drawSky Pass false to suppress drawing of the skybox, but still perform any lighting
       calculations required.
    \param geocentricMode If set to true, the stars, planets, sun, and moon will be
   drawn in a geocentric coordinate frame, where the X axis points through the
   prime meridian, Z points through the North Pole, and the origin is at the center
   of the Earth. This is generally only useful for space-based viewpoints. Normally,
   you'll leave this set to false so that astronomical objects will be drawn in local
   horizon coordinates.
    \param skyBoxDimension Sets an explicit dimension in world units for the length of
    a face of the sky box. Useful to prevent clipping of the skybox in systems that
    dynamically adjust the near and far clip planes; you can specify a value each frame
    that fits within them. Most applications can just leave this set to 0, in which case
    it will draw the sky box with the default size specified in SilverLining.config
    (1000)
    \param drawStars Set true to draw the stars when the sky is dark, and conditions are not
    overcast - or false to explicitly disable the drawing of the stars under all circumstances, such as
    in the presence of thick fog.
    \param clearDepth Set true to clear depth buffer contents with sky, 
    false to not clear depth and draw sky with Z test (useful for front to back sorting). Note that under
	DirectX, the depth buffer will always be cleared to 1.0 if clearDepth is true. If you are using a 
	reverse depth buffer, you'll want to keep clearDepth set to false and clear the depth buffer manually
	to your preferred far depth value.
    \param drawSunAndMoon Set true to enable drawing of the sun and moon, or false to disable
    them (for example, if you're drawing your own sun and moon billboards, or you're in heavy
    fog and the sun and moon should not be visible at all.)
    \param camera An optional pointer to your own camera object, or some sort of identifier for the camera
    currently being drawn. This is useful if you are using the same Atmosphere objects to draw the sky on
    multiple viewports or windows that share the same graphics context. By specifying a camera, we can associate
    precipitation particle positions with each one, allowing precipitation effects to work properly across
    multiple views with the same Atmosphere. If you are using separate Atmosphere objects per view, have only
    one view, or are not using precipitation effects, there is no need to set this.
    \param horizonOffset An offset, in degrees, of the horizon line. You can use this for example to move the
    horizon line down to compensate for terrain that does not extend infinitely, to force the sky's horizon
    to match up to the edge of your terrain. For example, at a camera height of h and terrain extent t,
    you might compute this as atan2(-h, t) and then converting to degrees.
    \param overcastBlend If you wish to force some degree of overcast conditions on the sky (rendering it as
    grey) irrespective of the presence of stratus clouds, pass in a non-zero value for overcastBlend. 0
    indicates no overcast effects on the sky box, while 1.0 indicates full overcast.
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    \return true if the skybox was successfully drawn.
 */
    bool SILVERLINING_API DrawSky(bool drawSky, bool geocentricMode = false, double skyBoxDimension = 0,
                    bool drawStars = true, bool clearDepth = true, bool drawSunAndMoon = true,
                    CameraHandle camera = 0, double horizonOffsetAngle = 0, double overcastBlend = 0,
                    void* data = 0);

/** Call this at the end of your rendering loop. After drawing all of your scene's objects, call
   the DrawObjects() method. This will draw all of the scene's clouds from back to front, if the
   drawClouds parameter is set to true. If there are translucent objects in your scene that should
   be drawn in front of the clouds, you'll want to draw those after calling DrawObjects().

   Alternately, you may obtain handles to each cloud object independently and sort them with the
   other translucent objects in your scene. If you set drawClouds to false, DrawObjects() will build
   up a list of translucent objects to render for this frame, but not actually render them. You
   may then access each individual cloud object with the GetObjects() method, sort them against your
   other translucent objects using the GetObjectDistance() method to obtain their sort distances, and
   then actually draw them using DrawObject().

   For applications where the scene is generally beneath the clouds, setting drawClouds to true
   and then drawing your own translucent objects after DrawObjects() is generally adequate.

   In cases where you call DrawObjects() more than once per frame, be sure to explicitly call
   Atmosphere::CullObjects() prior to each call.

   DrawObjects() must be called from the same thread and render context that called
   Atmosphere::Initialize(). It should be balanced with a call to DrawSky() prior to calling DrawObjects().

   When using the VULKAN renderer, be sure to associate SilverLining with the current command
   buffer using ThreadCameraStreamData::SetStream() prior to calling this, or any other method
   that issues draw commands.

   \param drawClouds Set to true if you want DrawObjects to actually draw all of the clouds, sorted
   from back to front. Set to false if you just want to build up a list of clouds to draw but not
   actually draw them; this list may be obtained with GetObjects().

    \param drawPrecipitation Set to false in order to disable rendering of precipitation effects.

    \param enableDepthTest By default, translucent objects in the scene will read from the depth buffer
    but not write to them. If you have objects behind your clouds, this ensures they are drawn properly.
    However, in some situations, you may have a ground-based viewpoint where nothing is behind the clouds,
    and you may not have a depth buffer at all, or a depth buffer with undefined contents. In these situations,
    you may want to disable depth reads by setting enableDepthTest to false.

    \param crepuscularRays Set to a value greater than 0 to draw crepuscular rays (AKA "God rays") after the clouds, 
    rendering shafts of light through the clouds. This results in an extra rendering pass, so does come at some
    performance cost. The value controls the intensity of the effect; 1.0 will result in the default intensity
    (as configured in SilverLining.config under crepuscular-rays-exposure), while values less than 1.0 will
    scale the effect down. To minimize the performance cost of crepuscular rays, disable lens flare effects 
    using the disable-lens-flare setting in the resource/silverlining.config file - the two effects are costly
    when used together. This parameter is ignored when using the VULKAN renderer; for crepuscular rays with
    Vulkan, please see the Atmosphere::GenerateCrepuscularRays() and Atmosphere::DrawCrepuscularRays()
    methods, and refer to the Vulkan sample application for an example of usage.

    \param enableDepthWrites Normally you would not want transparent objects such as clouds to write to the
    depth buffer, but there are some specialized situations, such as writing to a depth buffer for shadows,
    where this might be needed. Setting this to true will cause the clouds to write to the depth buffer; it is
    false by default.

    \param camera An optional pointer to your own camera object, or some sort of identifier for the camera
    currently being drawn. This is useful if you are using the same Atmosphere objects to draw the sky on
    multiple viewports or windows that share the same graphics context. By specifying a camera, we can associate
    precipitation particle positions with each one, allowing precipitation effects to work properly across
    multiple views with the same Atmosphere. If you are using separate Atmosphere objects per view, have only
    one view, or are not using precipitation effects, there is no need to set this.

    \param backFaceCullClockWise Determines whether back-face culling should cull out clockwise-winding
    polygons, which is normally the default. If you are doing something like rendering a reflection texture
    where the up axis is flipped, then you also need to flip the winding order for backface culling, and
    setting this to false allows you to do that.

    \param drawBackdrops If 2D dynamically-updated imposter backdrops are in use for representing distant
    clouds, this parameter allows you to skip drawing them. Useful for preserving performance and consistency
    when drawing views where distant clouds don't matter much, such as reflection passes.
    **!!Deprecated!!**

	\param drawLightning Whether lightning strikes from cumulonimbus clouds are rendered or not.

	\param geocentricMode Whether we are drawing in geocentric mode. See DrawSky().

    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.

   \return true if the clouds were successfully drawn.
 */
    bool SILVERLINING_API DrawObjects(bool drawClouds = true, bool drawPrecipitation = true, 
        bool enableDepthTest = true, float crepuscularRays = 0.0f, bool enableDepthWrites = false,
        CameraHandle camera = 0, bool backFaceCullClockWise = true, bool drawBackdrops = true,
        bool drawLightning = true, bool geocentricMode = false,
        void* data = 0
        );

/** Creates and retrieves a shadow map containing the clouds in the scene, the view / projection matrix 
    used to create it, and the matrix you'll need to map it to your terrain.

    The texture returned will be an RGBA texture that may be multiplied with your terrain textures to simulate
    shadows case from the clouds. The shadow map represents an orthographic view from the sun or moon's direction,
    with clouds rendered as the current ambient color against a white background. The color in the shadow map 
    represents the opacity of the cloud as seen from the ground to the sun or moon with ambient light added in, 
    resulting in soft shadow edges. Unlike other shadow maps, this is not a depth texture - it's meant to be
    multiplied into your terrain as-is.

    Cirrus and cirrocumulus clouds will not cast shadows, but any cumulus or stratus layer will.

    The shadow map resolution may be configured with the config setting shadow-map-texture-size, and defaults
    to 1024x1024.

    Calling GetShadowMap() in between calls to DrawSky() and DrawObjects() is not recommended, as it will trigger
    an additional culling pass you wouldn't otherwise need.

    Note that this method may restore the active view and projection matrices to those passed in via
    SetCameraMatrix() and SetProjectionMatrix(). OpenGL and DirectX9 users may want to push and pop
    the active matrices surrounding this call to be safe.

    \param texture A pointer to the platform-specific texture handle containing the 2D shadow map. On OpenGL this is
    a GLuint; on DirectX9 it is a IDirect3DTexture9 *; on
    DirectX11 it is a ID3D11ShaderResourceView *.

    \param lightViewProjMatrix The view x projection matrix used when rendering the shadow map.

    \param worldToShadowMapTexCoord The matrix to transform world coordinates into texture map
    coordinates of the shadow map texture. This is the lightViewProjMatrix multiplied by a matrix to
    scale and translate from clip space into texture coordinate space for this Atmosphere's rendering 
    system.

    \param wholeLayers If set to true, the shadow map will encompass the entirety of any cloud layers above
    the camera's view frustum. This will allow you to re-use this shadow map from frame to frame, by
    multiplying in a translation matrix to account for any wind movement, until the lighting or cloud
    conditions in your scene changes. This makes this call more expensive since it must draw every cloud
    in the layer, but allows you to potentially avoid calling it every frame. If set to false, the shadow map 
    will only contain clouds that affect the current view frustum, and must be regenerated whenever the camera moves.
    In this mode, the shadow map may make better use of its available resolution if the far clip plane is relatively
    close to the camera, but it may not work well with distant far clip planes. If you are simulating real-time 
    cloud growth using CloudLayer::SetCloudAnimationEffects(), you will want to create a new shadow map each frame 
    regardless for accurate shadows. If you're not sure, set this to true.

    \param shadowDarkness This controls how dark the shadows in the shadow map are. If set to the default
    of 1.0, shadows will take on the color of the tone-mapped ambient light in the scene. If set to zero, the
    shadows will be black.

    \param moonShadows If false (the default value,) shadows will only be generated from the sun,. If true, shadows
    will be generated from either the sun or the moon depending on which is brighter at the time. However,
    shadows may suddenly switch direction when the moon overtakes the sun's brightness. If the sun or moon
    is too close to the horizon, shadows will be faded out to prevent anomalies.

    \param maxShadowObjectDistance If set to a positive value, no objects farther than this distance from the camera
    will cast shadows. This may be used to tighten up the shadow map's resolution by eliminating distant shadows. If
    you are using infinite cloud layers that don't extend to the far clip plane, and you're using "false" for the
    "wholeLayers" parameter, you may need to set this to the maximum distance clouds will extend from the viewpoint in
    order to ensure the shadow map covers the area underneath the viewpoint. Otherwise, shadows will be centered about
    the centroid of the view frustum.

    \param shadowFactor Sets the overall intensity of the shadow effect, allowing you to potentially fade it out entirely
    under certain conditions. 1.0 means shadows will be generated naturally, and 0.0 will fade them out completely.

    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.

    \return true if a shadow map was successfully returned, false if an error was encountered or you
    tried to call GetShadowMap() in between calls to DrawSky() and DrawObjects().
*/
    bool SILVERLINING_API GetShadowMap(void *&texture, SilverLining::Matrix4 *lightViewProjMatrix, 
        SilverLining::Matrix4 *worldToShadowMapTexCoord, bool wholeLayers = true, float shadowDarkness = 1.0f,
        bool moonShadows = false, double maxShadowObjectDistance = -1.0, double shadowFactor = 1.0, void* data = 0);

/**
    Must call when cloud layers are added/removed/modified/visibility changed
    Will probably not be needed in future
    void SILVERLINING_API ShadowMapReset(void);
*/

/** Generates and retrieves a cube map texture representing the sky as viewed from the current camera position (as specified
    by the last call to Atmosphere::SetCameraMatrix().) This can be useful for rendering reflections, for example
    with Sundog Software's Triton Ocean SDK. The size of each cube map face texture is determined by the 
    SilverLining.config setting "environment-map-size", and defaults to 256x256 per face.

    This method ends up rendering the sky and clouds six times, so avoid calling this every frame.
    It will impact performance if you do.

    \param texture A pointer to the platform-specific texture handle containing the cube map. On OpenGL this is
    a GLuint; on DirectX9 it is a LPDIRECT3DCUBETEXTURE9; on
    DirectX11 it is a ID3D11ShaderResourceView *.

    \param facesToRender The number of cube map faces to render in this call. Normally you'll leave this at the default of
    6 to generate a complete cube map, but if you want to spread out the load of generating the cube map over multiple frames,
    you could set this to something lower. For example, you could call GetEnvironmentMap() with facesToRender set to 1 and
    call it six times to update the cube map over six calls. We keep track of the last face rendered, and loop through them
    sequentially each time GetEnvironmentMap is called.

    \param floatingPoint If true, the environment will be rendered to a 16-bit-per-channel floating point texture on each
    cube map face. If false, an 8-bit-per-channel RGBA texture will be created instead. Note this only affects the first
	call to GetEnvironmentMap() for this Atmosphere, which is when the actual texture is created. Once you've called
	GetEnvironmentMap() with a given floatingPoint parameter, you can't change it later unless you recreate the Atmosphere.
    This parameter is ignored if Renderer is Vulkan.
    In Vulkan we need to declare environment map usage up front, before initialization. See Atmosphere::SetEnvironmentMapFormat.

    \param cameraID An optional pointer to an object representing your application's camera that you want to create an
    environment map for. Use of this parameter allows you to maintain separate environment maps for individual cameras.

    \param drawClouds Whether clouds should be drawn into the environment map. If you need to speed up this call and don't
    care about cloud reflections, this could be a useful option for you.

	\param drawSunAndMoon Whether the sun and moon should be drawn into the environment map. If you're using this environment
	map for use on reflections, sometimes the sun or moon reflection can interfere with specular reflections and you might
	want this option on.

	\param geocentricMode Whether we are updating/rendering in geocentric mode. See DrawSky().

    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.

    \return True if the environment map was successfully generated.
*/
    bool SILVERLINING_API GetEnvironmentMap(void *&texture, int facesToRender = 6, bool floatingPoint = false, CameraHandle cameraID = 0, 
		bool drawClouds = true, bool drawSunAndMoon = true, bool geocentricMode = false, void* data = 0);

/** Use this to use specific colors for lighting the clouds, instead of the sun, moon, and sky colors. See also the
    CloudLayer::OverrideCloudColor() method for overriding the diffuse light on a per cloud layer basis.
    \sa ClearCloudLightingOverride()
    \param ambientR The red component of the explicit color to use for ambient lighting of the clouds.
    \param ambientG The green component of the explicit color to use for ambient lighting of the clouds.
    \param ambientB The blue component of the explicit color to use for ambient lighting of the clouds.
    \param diffuseR The red component of the explicit color to use for diffuse (directional) lighting of the clouds.
    \param diffuseG The green component of the explicit color to use for diffuse (directional) lighting of the clouds.
    \param diffuseB The blue component of the explicit color to use for diffuse (directional) lighting of the clouds.
*/
    void SILVERLINING_API OverrideCloudLighting(float ambientR, float ambientG, float ambientB,
                                                float diffuseR, float diffuseG, float diffuseB);

/** Clears any manual overrides of the cloud light color that were previously set with Atmosphere::OverrideCloudLighting(). */
    void SILVERLINING_API ClearCloudLightingOverride();

/** Return the current framerate, based on the rendering time of the previous frame, in
   frames per second. 
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
   */
    double SILVERLINING_API GetFramerate(void* data = 0) const;

/** If you called DrawObjects() with the drawClouds parameter set to false, no clouds will actually be
   drawn. It's then your responsibility to obtain a list of cloud objects to draw, and draw them yourself.
   GetObjects() provides you with this list. This list will be empty until DrawObjects() has been called for
   the current frame. If you call DrawObjects() with the drawClouds parameter set to true, this list will
   also be empty, since the objects have already been drawn.

   \sa DrawObjects()
   \sa GetObjectDistance()
   \sa DrawObject()
   \sa objects A vector of ObjectHandles. If its not empty, it will be cleared before being filled with the drawn objects.
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
 */
    void SILVERLINING_API GetObjects(SL_VECTOR(ObjectHandle)& objects, void* data = 0) const;

/** Clears the internal list of objects retrieved by GetObjects(). 
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
*/
    void SILVERLINING_API ClearObjects(void* data = 0);

/** If you are managing your own cloud drawing, you'll need to sort them from back to front along
   with the other objects in your scene. This method will return the screen depth for sorting purposes.

   \param obj An object handle obtained from the list returned by GetObjects(), after calling DrawObjects(false).
   \param obj2X The X coordinate of the object you are currently comparing this one against.
   \param obj2Y The Y coordinate of the object you are currently comparing this one against.
   \param obj2Z The Z coordinate of the object you are currently comparing this one against.
   \param sortPosX The X coordinate of the viewpoint location you are sorting translucent objects against.
   \param sortPosY The Y coordinate of the viewpoint location you are sorting translucent objects against.
   \param sortPosZ The Z coordinate of the viewpoint location you are sorting translucent objects against.
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
   \return The screen depth in eye coordinates at the intersection point to this object, taking into account internal depth
   biases.

   \sa DrawObjects()
   \sa DrawObject()
   \sa GetObjects()
 */
    float SILVERLINING_API GetObjectDistance(ObjectHandle obj, float obj2X, float obj2Y, float obj2Z,
        float sortPosX, float sortPosY, float sortPosZ, void* data = 0);

/** If you are managing your own cloud drawing, you'll need to sort them from back to front along
   with the other objects in your scene. This method will return the screen depth for sorting purposes.

   \param obj An object handle obtained from the list returned by GetObjects(), after calling DrawObjects(false).
   \param obj2 The object you are comparing this object against.
   \param sortPosX The X coordinate of the viewpoint location you are sorting translucent objects against.
   \param sortPosY The Y coordinate of the viewpoint location you are sorting translucent objects against.
   \param sortPosZ The Z coordinate of the viewpoint location you are sorting translucent objects against.
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
   \return The screen depth in eye coordinates, taking into account internal depth biases

   \sa DrawObjects()
   \sa DrawObject()
   \sa GetObjects()
 */
    float SILVERLINING_API GetObjectDistance(ObjectHandle obj, ObjectHandle obj2, float sortPosX, float sortPosY, float sortPosZ, void* data = 0);

/** Obtains the position, in world coordinates, of a given ObjectHandle obtained from the list returned
   from GetObjects(). */
    void SILVERLINING_API GetObjectPosition(ObjectHandle obj, float& x, float& y, float& z, void* data = 0);

/** If you are managing your own cloud object drawing by calling DrawObjects(false), then your sorted list
   of objects may drawn by calling DrawObject() on each object.

   Your rendering state must be set for rendering translucent objects. Specifically, you must enable
   blending with a blend equation of ONE, INVSRCALPHA. Lighting must be off, depth reads enabled,
   depth writes disabled, fog must be off, and 2D texturing enabled.

   \param obj An object handle obtained from the list returned by GetObjects(), after calling DrawObjects(false).

   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.

   \sa DrawObjects()
   \sa GetObjects()
   \sa GetObjectDistance()
 */
    void SILVERLINING_API DrawObject(ObjectHandle obj, void* data = 0);

/** Reloads the SilverLining.config file in the resources directory. This won't
   immediately affect existing clouds in the scene, but will affect new clouds
   created going forward. Mostly this is useful for advanced users who want to
   tweak the default config file settings without having to shut down and restart
   their application every time. Requires that Atmosphere::Initialize() was called
   previously. */
    void SILVERLINING_API ReloadConfigFile();

/** Sets a specific SilverLining.config value. Will overwrite whatever was specified.
   Subject to the same caveats listed for ReloadConfigFile(). Many settings are only
   read when Atmosphere::Initialize() is called; not everything may be adjusted at
   runtime. Contact support for guidance. */
    void SILVERLINING_API SetConfigOption(const char *key, const char *value);

/** Retrieves a specific SilverLining.config value as a string. */
    const char * SILVERLINING_API GetConfigOptionString(const char *key) const;

/** Retrieves a specific SilverLining.config value as a double. */
    double SILVERLINING_API GetConfigOptionDouble(const char *key) const;

/** Retrieves a specific SilverLining.config value as an int. */
    int SILVERLINING_API GetConfigOptionInt(const char* key) const;

/** Retrieves a specific SilverLining.config value as a double. */
    bool SILVERLINING_API GetConfigOptionBoolean(const char *key) const;

/** Forces the ephemeris model to recalculate the position of the sun, moon, and stars for the
   currently simulated time and location. This happens automatically when DrawSky() or UpdateSkyAndClouds()
   is called; this is provided only if you need to compute the astronomical positions in response to a 
   change in the simulated time and location prior to calling DrawSky() or UpdateSkyAndClouds(). 
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
   */
    void SILVERLINING_API UpdateEphemeris(void* data = 0);

/** Sets infrared sensor simulator mode. Just renders everything as black except the sun. 
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
*/
    void SILVERLINING_API SetInfraRedMode(bool bInfraRed, void* data = 0);

/** Gets whether infrared sensor mode is enabled. 
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
*/
    bool SILVERLINING_API GetInfraRedMode(void* data = 0) const;

/** Allows you to disable culling of clouds against the far clip plane of your projection matrix.
   This might be useful if you have a very close far clip plane and cannot push it out to encompass the
   clouds due to depth buffer precision issues. Note, this call will disable culling, not clipping. You'll
   need to disable clipping explicitly prior to calling Atmosphere::DrawObjects() for this call to have an
   effect. In OpenGL, you can do this using glEnable(GL_DEPTH_CLAMP_NV) in conjuction with
   glDepthFunc(GL_LEQUAL). */
    void SILVERLINING_API DisableFarCulling(bool bDisable) {
        disableFarCulling = bDisable;
    }

/** Retrieves whether culling against the far clip plane for clouds is disabled.
   \sa DisableFarCulling()
 */
    bool SILVERLINING_API GetFarCullingDisabled() const {
        return disableFarCulling;
    }

/** Retrieves the bounds of any clouds currently in the scene. 
    \param minX The minimum x coordinate of the bounding box enclosing all of the clouds.
    \param minY The minimum y coordinate of the bounding box enclosing all of the clouds.
    \param minZ The minimum z coordinate of the bounding box enclosing all of the clouds.
    \param maxX The maximum x  coordinate of the bounding box enclosing all of the clouds.
    \param maxY The maximum y  coordinate of the bounding box enclosing all of the clouds.
    \param maxZ The maximum z  coordinate of the bounding box enclosing all of the clouds.
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
*/
    void SILVERLINING_API GetCloudBounds(double& minX, double& minY, double& minZ, double& maxX, double& maxY, double& maxZ, void* data = 0) const;

/** Enable or disable a big, flashy lens flare effect when the sun is visible in the scene. */
    void SILVERLINING_API EnableLensFlare(bool bEnabled) {
        enableLensFlare = bEnabled;
    }

/** Returns if lens flare is currently enabled. \sa EnableLensFlare() */
    bool SILVERLINING_API GetLensFlareEnabled() const {
        return enableLensFlare;
    }

    /** Enable or disable occlusion testing of the sun, required when using GetSunOcclusion().
        Disable this if you will never use GetSunOcclusion() to regain performance when the sun
        is in view. */
    void SILVERLINING_API EnableSunOcclusion(bool bEnabled) {
        enableOcclusionQuery = bEnabled;
    }

/** Returns if sun occlusion testing is currently enabled. \sa EnableSunOcclusion() */
    bool SILVERLINING_API GetSunOcclusionEnabled() const {
        return enableOcclusionQuery;
    }

/** Returns the percentage of the sun that is currently visible in the scene, as a float from
   0 to 1.0. This can be used for determining if you're in a shadow, or for implementing your own
   lens flare effects. If you will never use this method, call Atmosphere::EnableSunOcclusion(false)
   to regain some performance. 
   \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
   */
    float SILVERLINING_API GetSunOcclusion(void* data = 0);

#ifndef SWIG
/** If you wish to override SilverLining's default random number generator,  you may do so by extending 
   the RandomNumberGenerator base class, instantiating your extended class, and passing in a pointer to 
   your class here. You are responsible for deleting the RandomNumberGenerator you passed in at shutdown. */
    void SILVERLINING_API SetRandomNumberGenerator(RandomNumberGenerator* rng);

/** Retrieves the random number generator currently in use by SilverLining. */
    RandomNumberGenerator * SILVERLINING_API GetRandomNumberGenerator() const;
#endif

/** Enabling High Dynamic Range (HDR) mode will disable all tone mapping, clamping, and gamma
   correction in SilverLining. This is only useful if you are rendering to a floating point
   framebuffer and applying a tone mapping operator of your own as a postprocessing step - otherwise,
   it'll just have the effect of making the sky and clouds pure white during the day.

   The values written to the framebuffer in HDR mode will represent kilo-candelas per square meter,
   which may or may not be on a luminance scale comparable to the rest of your scene. The config settings
   sun-transmission-scale, sun-scattered-scale, moon-transmission-scale, and moon-scattered-scale can
   be used to scale our luminance values into a range comparable with your own. (You'll find these settings
   in the file resources/silverlining.config).

   HDR is a static state; its current setting will apply to all Atmosphere objects at once. Under DirectX,
   HDR requires shader model 3.0 support.

   \sa GetHDREnabled()

   \param hdr True to enable high dynamic range mode and disable all tone mapping and gamma correction.
           False to tone-map the sky to the displayable range of [0,1.0]. HDR is disabled by default.
 */
    static void SILVERLINING_API EnableHDR(bool hdr) {
        enableHDR = hdr;
    }

/** Retrieve whether high dynamic range rendering mode is currently enabled. \sa EnableHDR() */
    static bool SILVERLINING_API GetHDREnabled() {
        return enableHDR;
    }

#ifndef SWIG
/** Flatten the Atmosphere and everything in it to a stream - useful for capturing the state of everything
   to save to disk or for recreating the Atmosphere in response to a device reset in DX9. */
    bool SILVERLINING_API Serialize(std::ostream& stream, void* data = 0);

/** Restore the Atmosphere from a stream generated from Atmosphere::Serialize. The Atmosphere must be
   constructed and initialized before calling Unserialize(). */
    bool SILVERLINING_API Unserialize(std::istream& stream, void* data = 0);

/** Overrides the default FILE* - based resource loader with a customer-supplied resource loader object.
   See the ResourceLoader documentation for more information. This allows you to include SilverLining's
   textures, models, and shaders within your own resource management scheme. This should be called after
   initializing your first Atmosphere object, but before calling Atmosphere::Initialize(). You are responsible
   for deleting the ResourceLoader you passed in at shutdown. */
    static void SILVERLINING_API SetResourceLoader(ResourceLoader *loader) {
        resourceLoader = loader;
    }

/** Gets the ResourceLoader object being used to load resources, which may be the default FILE * based loader
   or a customer-supplied loader. */
    static ResourceLoader * SILVERLINING_API GetResourceLoader() {
        return resourceLoader;
    }
#endif

/** By default, SilverLining assumes that one world unit is equal to one meter - that is, all of the dimensions for
    cloud layers, altitudes, etc. are in meters. If you are using a different scale, you may specify the size of
    one unit in meters here. This should be set prior to initializing the Atmosphere object. */
    static void SILVERLINING_API SetWorldUnits(double meters) {
        unitScale = 1.0 / meters;
    }

/** Retrieves the scale factor for world units, as set by SetWorldUnits(). Defaults to 1.0. */
    static double SILVERLINING_API GetUnitScale() {
        return unitScale;
    }

/** Returns whether the Initialize() method has been successfully called on this Atmosphere yet, and it is safe
    to use for rendering tasks. 
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    bool SILVERLINING_API IsInitialized(void* data = 0) const;

    /** Sets a transparency value for the sun billboard, useful for fading the sun out with haze.
        \param alpha - The transparency of the sun billboard; 0 = transparent, 1 = opaque.
        \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    void SILVERLINING_API SetSunAlpha(double alpha, void* data = 0);

    /** Retrieves the alpha value of the sun, as set by Atmosphere::SetSunAlpha(). Defaults to 1.0. 
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    double SILVERLINING_API GetSunAlpha(void* data = 0) const;

    /** Sets a transparency value for the moon billboard, useful for fading the moon out with haze.
        \param alpha - The transparency of the moon billboard; 0 = transparent, 1 = opaque.
        \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    void SILVERLINING_API SetMoonAlpha(double alpha, void* data = 0);

    /** Retrieves the alpha value of the moon, as set by Atmosphere::SetMoonAlpha(). Defaults to 1.0. 
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    double SILVERLINING_API GetMoonAlpha(void* data = 0) const;

    /** Retrieve the percentage of sky occluded by clouds, above the current camera position. 
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    double SILVERLINING_API GetSkyCoverage(void* data = 0) const;

    /** OpenGL only: retrieve a list of OpenGL shader program objects for all currently active planar cloud shaders. This
        may be used for attaching your own shader uniform parameters. 
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    SL_VECTOR(unsigned int) SILVERLINING_API GetActivePlanarCloudShaders(void* data = 0);

    /** OpenGL only: retrieve the OpenGL shader program object for drawing instanced billboards, 
        which generally consists of cumulus cloud puffs.
        \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
        */
    unsigned int SILVERLINING_API GetBillboardShaderInstanced(void* data = 0) const;

    /** OpenGL only: retrieve the OpenGL shader program object for drawing non-instanced billboards, 
        such as the sun and the moon. 
        \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
        */
    unsigned int SILVERLINING_API GetBillboardShader(void* data = 0) const;

    /** OpenGL only: retrieve the OpenGL shader program object for drawing the sky box. 
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    unsigned int SILVERLINING_API GetSkyShader(void* data = 0) const;

    /** OpenGL only: retrieve the OpenGL shader program object for drawing the stars. 
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    unsigned int SILVERLINING_API GetStarShader(void* data = 0) const;

    /** OpenGL only: retrieve the OpenGL shader program object for drawing precipitation particles. 
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    unsigned int SILVERLINING_API GetPrecipitationShader(void* data = 0) const;

    /** OpenGL only: retrieve the OpenGL shader program object for drawing precipitation particles.
        \param type The precipitation type whose shader handle to return. Each precipitation type
        has its own specialized shader.
        Values are CloudLayer::RAIN, CloudLayer::WET_SNOW, CloudLayer::DRY_SNOW, or CloudLayer::SLEET.
        Note: CloudLayer::WET_SNOW & CloudLayer::DRY_SNOW now use the same shader, so return the same handle
        \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    unsigned int SILVERLINING_API GetPrecipitationShader(int type, void* data = 0) const;
    
    /** OpenGL only: retrieve the OpenGL shader program object for the atmospheric limb effect. 
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    unsigned int SILVERLINING_API GetAtmosphericLimbShader(void* data = 0) const;

    /** OpenGL only: retrieve the OpenGL shader program object for the line shader. 
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    unsigned int SILVERLINING_API GetLineShader(void* data = 0) const;

    /** OpenGL only: recompile and link all shader programs on demand.
        \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    void SILVERLINING_API ReloadShaders(const SL_VECTOR(unsigned int)& userShaders, void* data = 0);

    /** Force ecliptic (not horizon) latitude and longitude of the sun & moon to specified positions. 
        Note latitudes and longitudes are in radians. Call Atmosphere::ClearForcedSunAndMoon() to return
        to automatic computation based on the time and location. 
        \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
        */
    void SILVERLINING_API ForceSunAndMoon(double sunLat, double sunLon, double moonLat, double moonLon, void* data = 0);

    /** Force ecliptic (not horizon) latitude and longitude of the sun & moon to specified positions, and force a moon phase. 
        Note latitudes and longitudes are in radians, and the moon phase ranges from 0 (new moon) to 1.0 (full moon.) 
        Call Atmosphere::ClearForcedSunAndMoon() and Atmosphere::ClearForcedMoonPhase() to return to automatic 
        computation based on the time and location. 
        \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
        */
    void SILVERLINING_API ForceSunAndMoon(double sunLat, double sunLon, double moonLat, double moonLon, double moonPhase, void* data = 0);

    /** Force the sun and moon positions to given horizon coordinates. Call Atmosphere::ClearForcedSunAndMoon() to return
        to automatic computation based on time and location. Altitudes and azimuths are all in radians. 
        \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
        */
    void SILVERLINING_API ForceSunAndMoonHorizon(double sunAltitude, double sunAzimuth, double moonAltitude, double moonAzimuth, void* data = 0);
    
    /** Force the sun and moon positions to given horizon coordinates, and force a moon phase. Call Atmosphere::ClearForcedSunAndMoon()
        and Atmosphere::ClearForcedMoonPhase() to go back to automatic computation based on time and location. Altitudes and
        azimuths are in radians, and the moon phase ranges from 0 (new moon) to 1.0 (full moon.) 
        \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
        */
    void SILVERLINING_API ForceSunAndMoonHorizon(double sunAltitude, double sunAzimuth, double moonAltitude, double moonAzimuth, double moonPhase, void* data = 0);

    /** Force a specific moon phase regardless of the simulated date. Moon phase ranges from 0 (new moon) to 1.0
        (full moon.) Call Atmosphere::ClearForcedMoonPhase() to return to automatic computation of the phase. 
        \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
        */
    void SILVERLINING_API ForceMoonPhase(double moonPhase, void* data = 0);

    /** Force a specific moon phase angle regardless of the simulated date. The phase angle ranges from -PI to +PI, 
        with 0 representing a full moon. This method allows you to force specifically waning or waxing moons and not
        just a given phase, as Atmosphere::ForceMoonPhase does. Call Atmosphere::ClearForcedMoonPhase to return to
        the automatic computation of the phase angle. You cannot force both a moon phase and a phase angle at the
        same time; whichever is forced most recently will take precedence. 
        \param data An optional pointer to a ThreadCameraStreamData object that this call pertains to.
        */
    void SILVERLINING_API ForceMoonPhaseAngle(double moonPhaseAngle, void* data = 0);

    /** Retrieve the currently simulated moon phase (0 - 1.0) 
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    double SILVERLINING_API GetMoonPhase(void* data = 0) const;

    /** Clear any forced moon phase set with ForceSunAndMoon(), ForceSunAndMoonHorizon(), or ForceMoonPhase() 
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    void SILVERLINING_API ClearForcedMoonPhase(void* data = 0);

    /** Clear any forced sun & moon position set with ForceSunAndMoon() 
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    void SILVERLINING_API ClearForcedSunAndMoon(void* data = 0);
    
    /** Reloads the textures used to represent cumulus cloud puffs. When using CUMULUS_CONGESTUS_HI_RES for example, you
    might switch to a different texture set by first using Atmosphere::SetConfigOption("cloud-atlas-texture-name",
    [new texture name]) and then calling Atmosphere::ReloadCumulusTextures(). Returns true if the textures successfully
    loaded. 
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    bool SILVERLINING_API ReloadCumulusTextures(void* data = 0);

    /** Scales all final color values by some factor; useful if you need to match SilverLining to 
    some other tone mapping scheme or just want a quick way to darken or lighten everything. 
    \param scale The scaling factor for the final output in all fragment shaders. 
    */
    void SILVERLINING_API SetOutputScale(const Vector3& scale);

    /** Same as above. Shortcut to set all components of the output scale to a single float value */
    void SILVERLINING_API SetOutputScale(float scale);

    /** Retrieve the output scale previously set via SetOutputScale, or 1.0 by default. */
    Vector3 SILVERLINING_API GetOutputScale() const;

    /** Vulkan only.
    This must be called *after* Atmosphere::DrawObjects, and followed by a call to 
    Atmosphere::DrawCrepuscularRays() within a new begin/end render pass. 
    You may refer to the Vulkan sample application for an example of usage.
    For crepuscular rays in non-Vulkan renderers, simply use the crepuscular rays parameter
    in Atmosphere::DrawObjects().
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    void SILVERLINING_API GenerateCrepuscularRays(void* data = 0);

    /** Vulkan only.
    This must be called *after* Atmosphere::GenerateCrepuscularRays(), inside another begin/end render pass.
    You may refer to the Vulkan sample application for an example of usage.
    For crepuscular rays in non-Vulkan renderers, simply use the crepuscular rays parameter
    in Atmosphere::DrawObjects().
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    void SILVERLINING_API DrawCrepuscularRays(void* data = 0);

    /** Vulkan only.
    * If you intend to use environment maps via Atmosphere::GetEnvironmentMap, this needs to be specified
    * before initializing Atmosphere.
    */
    void SILVERLINING_API SetEnvironmentMapFormat(ColorFormat format);

    /** Vulkan only.
    * If you intend to use shadow maps via Atmosphere::GetShadowMap, this needs to be specified
    * before initializing Atmosphere.
    */
    void SILVERLINING_API SetUseShadowMap(bool val);

    /** Vulkan only.
    \param environment This must contain a pointer to a VulkanInitInfo structure, as defined in the VulkanInitInfo.h header.
    Only the parameters: renderPass, sampleCount, colorFormat and depthFormat are considered for the change.
    Other parameters are ignored.
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    void SILVERLINING_API SettingsChanged(void* environment, void* data = 0);

    /** Does nothing but set the resource directory path and load the configuration settings within it.
        This provides enough setup to create (but not seed) CloudLayer objects before you have a graphics
        context in place, and before you call Atmosphere::Initialize(). You only need to call this if you
        intend to call CloudLayerFactory::Create() prior to Atmosphere::Initialize() (which requires a
        graphics context to already be active. 
    
        \param resourceDirectoryPath A null-terminated string that specifies a path to
        the application's redistributed "Resources" directory, including the directory name
        itself. For example, "..\\Resources\\".

		\param rightHanded Pass true if you're using a right-handed coordinate system, false for
		a left-handed coordinate system. OpenGL typically uses right-handed; DirectX can use either.

        \return An error code from the Atmosphere::InitializeErrors enumeration, or E_NOERROR. See
        the troubleshooting section of the documentation for further guidance if you encounter an
        error.
    */
    int SILVERLINING_API PreInit(const char *resourceDirectoryPath, bool rightHanded);

    void SILVERLINING_API SetSortByScreenDepth(bool sort, void* data = 0);

    static void SILVERLINING_API SetUserDefinedVertString( const char *userString );

    static void SILVERLINING_API SetUserDefinedFragString( const char *userString );

    static const char * SILVERLINING_API GetUserDefinedVertString();

    static const char * SILVERLINING_API GetUserDefinedFragString();

    /** Call when a context is being deleted (all renderers) */
    void SILVERLINING_API ContextBeingDeleted(void* context) const;

#ifndef SWIG
    /// Call when a Direct3D9 device is lost. 
    //\param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    void SILVERLINING_API D3D9DeviceLost(void* data = 0);

    /// Call when a Direct3D9 device is reset.
    void SILVERLINING_API D3D9DeviceReset();

    // The following methods are intended for internal or special purpose use:

    static void SILVERLINING_API SetDebugMsgCB(void(*debug_msg_cb)(const char *));
    
    static void SILVERLINING_API DebugMsg(const char *debugMessage);

    void SILVERLINING_API ShowOffscreenBuffer(bool debugOn) {
        debugMode = debugOn;
    }

    /**
    \param geocentricMode Whether geocentric mode.
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    void SILVERLINING_API DrawLensFlare(bool geocentricMode, void* data = 0) const;

    /**
    \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.
    */
    bool SILVERLINING_API StarsVisible(void* data = 0) const;


    /** \param data An optional pointer to a ThreadCameraStreamData object that this calls pertains to.*/
    void SILVERLINING_API CullObjectsInternal(bool threadSafe, const Camera* sceneCamera, const Camera* cullCamera, ThreadCameraStreamData* tcsData) const;

    //! internal
    void UpdateRendererDepthRange(Renderer* renderer, const Camera* camera) const;

    //! internal. do not use!
    ThreadCameraStreamData* SILVERLINING_API GetDefaultTcsData() const;

    //! internal. do not use!
    const SL_VECTOR(unsigned int)& GetUserShaders(void) const;

    //! internal. do not use!
    bool TexturesAreShared(void) const;
#endif

#ifndef SWIG
private:

    RandomNumberGenerator *randomNumberGenerator;
    static RandomNumberGenerator *defaultRandomNumberGenerator;

    AtmosphereFromSpace *atmosphereFromSpace;
    AtmosphericConditions *conditions;
    bool debugMode;

    double atmosphereHeight;
    
    
    static ResourceLoader *resourceLoader;
    static ResourceLoader *defaultResourceLoader;
    static double unitScale;

    ThreadCameraStreamData* defaultTcsData;

    bool disableFarCulling;
    bool enableLensFlare, enableOcclusionQuery;

    static bool enableHDR;
    float ambientOverrideR, ambientOverrideG, ambientOverrideB;
    float diffuseOverrideR, diffuseOverrideG, diffuseOverrideB;
    bool hasLightingOverride;

    Mutex *mutex;
    
    Vector3 outputScale;
    bool computeCloudBounds;

    void GetCloudBounds(Vector3& minCloudBounds, Vector3& maxCloudBounds, ThreadCameraStreamData* tcsData) const;
    void DrawClouds();
    void ShadeClouds(ThreadCameraStreamData* tcsData) const;
    void CalculateFramerate(ThreadCameraStreamData* tcsData) const;
    void DisplayFramerate();
    SilverLining::Color Interpolate(const Color& over, const Color& under, ThreadCameraStreamData* tcsData) const;
    bool DrawObjectsInternal(bool drawClouds, bool drawPrecipitation,
        bool enableDepthTest, float crepuscularRays, bool enableDepthWrites,
        bool backFaceCullClockWise,
        bool drawLightning, bool geocentricMode, ThreadCameraStreamData* tcsData) const;
    bool DrawSkyInternal(bool drawSky, bool geocentricMode, double skyBoxDimension,
        bool drawStars, bool clearDepth, bool drawSunAndMoon,
        double horizonOffsetAngle, double overcastBlend, ThreadCameraStreamData* tcsData) const;
    bool GetShadowMapInternal(void *&texture, SilverLining::Matrix4 *lightViewProjMatrix,
        SilverLining::Matrix4 *worldToShadowMapTexCoord, bool wholeLayers, float shadowDarkness,
        bool moonShadows, double maxShadowObjectDistance, double shadowFactor, ThreadCameraStreamData* tcsData);
    double GetSkyCoverageInternal(const Camera* camera, ThreadCameraStreamData* tcsData) const;
    void UpdateSkyAndCloudsInternal(bool geocentricMode, ThreadCameraStreamData* tcsData) const;

    AtmosphereTcsUserData* GetOrCreateTcsUserData(ThreadCameraStreamData* data) const;

    bool applyFogFromCloudPrecip;
    bool precipMonochrome;

    static char* userDefinedVertString;
    static char* userDefinedFragString;

    bool configInited;

    SL_VECTOR(unsigned int) userShaders;

    ColorFormat environmentMapFormat = FORMAT_UNKNOWN;
    
    bool useShadowMap = false;

    double scaleHeightMeters = 0;

    bool texturesAreShared = true;
#endif
};
}

#pragma pack(pop)

#endif
