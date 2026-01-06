// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file Sky.h
    \brief Simulates and renders the sky for a given time and location.
 */

#ifndef SKY_H
#define SKY_H

#include "Renderer.h"
#include "Ephemeris.h"
#include "Vector3.h"
#include "Glare.h"
#include "Configuration.h"
#include "SolarSpectrum.h"
#include "LunarSpectrum.h"
#include "SkyShaderConstantLocations.h"
#include "SkyShaders.h"
#include <map>

namespace SilverLining
{
class VertexBuffer;
class IndexBuffer;
class Stars;
class Billboard;
class BillboardBufferProvider;
class BillboardShader;
class Camera;
class SkyShaders;

class SkyDrawingParameters : public MemObject
{
public:
    SkyDrawingParameters();
    virtual ~SkyDrawingParameters();

    int pass;
    double skyboxSize;
    bool applicationFog;
    bool infraRed;
    bool drawStars;
    bool clearDepth;
    bool drawSunAndMoon;
    double horizonOffsetAngle;
    bool drawSky;

    float lightPollution;
    Vector3 outputScale;

    bool geocentricMode;
};


/** Simulates and renders the sky for a given time and condition. If available, vertex shaders are
   employed to offload the simulation to the GPU.

   For general background on the basic ideas behind this class, see:

   Preetham, A. J., Shirley, P., and Smits, B. 1999. A practical analytic model for daylight. In
   <i>Proceedings of the 26th Annual Conference on Computer Graphics and interactive Techniques</i>
   International Conference on Computer Graphics and Interactive Techniques.
   ACM Press/Addison-Wesley Publishing Co., New York, NY, 91-100. DOI= http://doi.acm.org/10.1145/311535.311545

   Jensen, H. W., Durand, F., Dorsey, J., Stark, M. M., Shirley, P., and Premože, S. 2001. A
   physically-based night sky model. In <i>Proceedings of the 28th Annual Conference on Computer
   Graphics and interactive Techniques</i> SIGGRAPH '01. ACM Press, New York, NY, 399-408.
   DOI= http://doi.acm.org/10.1145/383259.383306

   Hosek & Wilkie "An Analytic Model for Full Spectral Sky-Dome Radiance"
   ACM TOG 31(4) - Proceedings of ACM SIGGRAPH 2012

 */
class Sky : public MemObject
{
public:
/** Default constructor. */
    Sky(const Atmosphere& atmosphere, ThreadCameraStreamData* _tcsData);

/** Virtual destructor. */
    virtual ~Sky();

/** Associates an Ephemeris object with this sky, which computes the position of
   the sun, moon, and planets based on the simulated time and location. Must be called
   prior to any other method in the Sky class. Specify the altitude in meters MSL*/
    virtual void UpdateFromEphemeris(double altitude);

/** Overrides the default turbidity value of the simulated sky. You can think of
   this as a measure of "haziness." 1.0 would be a perfectly clear day and is
   the minimum value. Some guidelines for setting this value:

   1 = Pure air, range 256 km
   2 = very clear, range 50 km
   3 = clear, range 15 km
   7 = light haze, range 8 km
   20 = haze, range 3 km
   50 = thin fog, range 2 km

   \param turbidity The ratio of scattering due to haze to scattering due to molecules.
 */
    virtual void SetTurbidity(double turbidity) {
        T = turbidity;
    }

/** Selects usage of the Preetham or Hosek-Wilkie sky models. Hosek-Wilkie is a newer extension (2012)
    of the Preetham model (1999,) but is more complex. */
    virtual void SetSkyModel(SkyModel model) {
        skyModel = model;
    }

/** Retrieves the sky model currently in use. */
    virtual SkyModel GetSkyModel() const {
        return skyModel;
    }

/** Sets whether the sky is to simulate an overcast sky, in which case the model simply
   employs a simpler, monochromatic luminance distribution. */
    virtual void SetOvercast(bool overcast) {
        isOvercast = overcast;
    }

/** Retrieves whether the sky is overcast. */
	virtual bool GetOvercast() const {
		return isOvercast;
	}

/** The overcast parameters allow you to smoothly blend between simulating an overcast
   and a clear sky, if SetOvercast(true) has been called.
   \param blend The degree of overcastness to simulate, from 0 to 1.0.
   \param ambientTransmission The percentage of ambient sunlight transmitted through the cloud layer.
   \param directTransmission The percentage of direct sunlight transmitted through the cloud layer.
*/
    virtual void SetOvercastParams(double blend, double ambientTransmission, double directTransmission)
    {
        overcastBlend = blend;
        overcastTransmissionAmbient = ambientTransmission;
        overcastTransmissionDirect = directTransmission;
    }

/** Set the simulated light pollution which adds to the ambient night illumination.
    Specified in watts per square meter. Defaults to zero. */
    void SetLightPollution(double Wm2) {
        lightPollution = Wm2;
    }

/** Retrieves the simulated light pollution, in watts per square meter. */
    double GetLightPollution() const {
        return lightPollution;
    }

/** Sets the value used for gamma correction of the display. Defaults to the sky-box-gamma setting.
   1.8 works well. Higher values will yield lighter skies and natural light. */
    void SetGamma(double gamma);

/** Retrieves the value being used for display gamma correction.
   \sa SetGamma()
 */
    double GetGamma() const;

/** Simulates being inside a layer of fog, such that points on the sky near the horizon will
   be fogged, and points toward the zenith less so, assuming you aren't deep within a thick
   fog bank. The distance represents the vertical distance between the camera and the top or
   bottom of the simulated fog layer. */
    virtual void SetFogVolumeDistance(double dist) {
        fogVolumeDistance = dist;
    }

/** Causes the sky to blend toward a specified "haze color" toward the horizon. Although
   it does simulate a layer of colored fog, it's most practical application to allow for
   exact blending against a fog color used for terrain, in order to obscure the horizon line.
   For applications that do not render terrain all the way to the horizon, this is a must.
   GetAverageHorizonColor() may be used for an approximate match in the absence of an artificial
   layer of haze; it is more physically realistic.

   The haze color passed in is not lit; you must pre-multiply the color yourself. The skybox
   will blend toward the exact color passed in at the horizon.

   By default, hazeDepth is set to 0, thereby disabling the haze effects. If the viewpoint
   is within a cloud, the fog effects from the cloud will drawn in the sky in lieu of haze.

   \param hazeColor The color to blend toward at the horizon.
   \param hazeDepth The simulated height of the haze volume at ground level, in world units.
   \param hazeDensity The fog density parameter of the exponential fog equation.
 */
    virtual void SetHaze(const Color& hazeColor, double hazeDepth, double hazeDensity);

/** Retrieves the haze parameters set previously by SetHaze(). See SetHaze() for a
   description of the parameters. */
    virtual void GetHaze(Color& hazeColor, double& hazeDepth, double& hazeDensity) const;

/** Draws the sky. Set pass to 0 for the lighting pass, or 1 for the rendering pass.
   skyboxSize represents the dimension of a skybox face desired, or set to 0 to just use
   the default all the time. If applicationFog is specified, astronomical objects (sun, moon,
   stars) will not be drawn. If infraRed is specified, the buffer is cleared to black and only
   the sun is drawn. drawStars allows you to disable star rendering under all circumstances;
   the stars can mess up environment maps sometimes. */
	virtual bool Draw(const SkyDrawingParameters& params, const RandomNumberGenerator* rng, unsigned long millis, const Camera* camera);

/** Tests if the sky should be culled, which it never should be. */
    virtual bool IsCulled(const Frustum& f) const {
        return false;
    }

/** Returns a normalized direction vector toward the position of the dominant light source. */
    Vector3 GetSunOrMoonPosition() const;

/** Returns the dominant light source direction in geographic coordinates (the origin is at the center
   of the Earth, Z points through the North Pole, X points through the prime meridian) */
    Vector3 GetSunOrMoonPositionGeographic() const;

/** Returns the dominant light source direction in equatorial coordinates (the origin is at the center
   of the Earth, Z points through the North Pole, X points through the vernal equinox) */
    Vector3 GetSunOrMoonPositionEquatorial() const;

/** Returns a normalized direction vector toward the position of the sun (may be below the horizon
   if it's night) */
    Vector3 GetSunPosition() const;

/** Returns a normalized direction vector toward the position of the moon (which may be below
   the horizon depending on the time) */
    Vector3 GetMoonPosition() const;

/** Returns the sun direction in geographic coordinates (the origin is at the center
   of the Earth, Z points through the North Pole, X points through the prime meridian) */
    Vector3 GetSunPositionGeographic() const;

/** Returns the moon direction in geographic coordinates (the origin is at the center
   of the Earth, Z points through the North Pole, X points through the prime meridian) */
    Vector3 GetMoonPositionGeographic() const;

/** Returns the sun direction in equatorial coordinates (the origin is at the center
   of the Earth, Z points through the North Pole, X points through the vernal equinox) */
    Vector3 GetSunPositionEquatorial() const;

/** Returns the moon direction in equatorial coordinates (the origin is at the center
   of the Earth, Z points through the North Pole, X points through the vernal equinox) */
    Vector3 GetMoonPositionEquatorial() const;

/** Returns a matrix to convert horizon coordinates to equatorial coordinates,
   where Z points through the north pole and x points through the vernal equinox. */
    Matrix3 GetHorizonToEquatorialMatrix() const;

/** Returns a matrix to convert horizon coordinates to geocentric coordinates,
   where Z points through the north pole, and X points through the prime meridian. */
    Matrix3 GetHorizonToGeographicMatrix() const;

/** Returns the color of the dominant light source in the scene, taking the camera's altitude into account. */
    Color GetSunOrMoonColor() const;

/** Returns the color of the dominant light source in the scene, without taking the camera's altitude into account.*/
    Color GetSunOrMoonColorSeaLevel() const;

/** Returns the color of the direct sunlight transmitted through the atmosphere (Will be black
   at night) */
    Color GetSunColor() const;

/** Returns the color of the direct moonlight transmitted through the atmosphere. (Will be black
   if the moon isn't out, or during a new moon)*/
    Color GetMoonColor() const;

/** Retrieves the chromaticity and luminance of the sky at the zenith in xyY format. */
    void GetZenithLuminance(double *x, double *y, double *Y);

/** Retrieves the color of the ambient skylight, after tone mapping, taking the camera's altitude into account. */
    Color GetToneMappedSkyLight() const;

/** Retrieves the color of the ambient skylight, after tone mapping, not taking the camera's altitude into account. */
    Color GetToneMappedSkyLightSeaLevel();

/** Retrieves the average color of the sky at the horizon within the current field of view. */
    Color GetAverageHorizonColor(double pitchDegrees, const Camera* camera) const;

/** Retrieves the average color of the sky at the horizon within the field of view centered
   at the specified yaw angle, in degrees. */
    Color GetAverageHorizonColor(double yawDegrees, double pitchDegrees, const Camera* camera) const;

/** Gets the sky color in the given direction in horizon coordinates. */
    Color SkyColorAt(const Vector3& direction, double zenithAngleOffset) const;

/** Gets the sky color at the horizon when conditions are completely overcast. */
	Color GetOvercastHorizonColor() const;

    /** Retrieve the fog color and density used to fog the sky */
    void GetFog(Color& c, double& density, double& distance) const;

    /** Sets a transparency value for the sun billboard, useful for fading the sun out with haze.
        \param alpha - The transparency of the sun billboard; 0 = transparent, 1 = opaque.
    */
    void SetSunAlpha(double alpha) {
        sunAlpha = alpha;
    }

    /** Retrieves the alpha value of the sun, as set by Sky::SetSunAlpha(). Defaults to 1.0. */
    double GetSunAlpha() const {
        return sunAlpha;
    }

    /** Sets a transparency value for the moon billboard, useful for fading the moon out with haze.
        \param alpha - The transparency of the moon billboard; 0 = transparent, 1 = opaque.
    */
    void SetMoonAlpha(double alpha) {
        moonAlpha = alpha;
    }

    /** Retrieves the alpha value of the moon, as set by Sky::SetMoonAlpha(). Defaults to 1.0. */
    double GetMoonAlpha() const {
        return moonAlpha;
    }

    /** Retrieve the currently active sky shader program. */
    ShaderHandle GetSkyShader() const;

    /** Retrieve the star shader program. */
    ShaderHandle GetStarShader() const;

    /** Reload the shaders for the sky and stars (OpenGL only.) */
    void ReloadShaders();

    /** Whether it's dark enough to draw stars. */
    bool StarsVisible() const {
        return starsVisible;
    }

    const Ephemeris* GetEphemeris(void) const;

    Ephemeris* GetEphemeris(void);

    const Atmosphere& GetAtmosphere(void) const;
private:
    void UpdatePerezCoefficients();
    void UpdateHosekWilkieCoefficients();
    void UpdateZenith(double altitude);
    void InitTwilightZenithLuminanceLookup();
    void UpdateVertices(const Vector3& outputScale);
    void ColorVertices();
    void ComputeSun(double altitude);
    void ComputeMoon(double altitude);
    void DrawSun(bool geocentricMode, bool infraRed, const Vector3& outputScale, unsigned long millis, const Camera* sunCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, bool forceDepth);
    void DrawMoon(bool geocentricMode, const Vector3& outputScale, unsigned long millis, const Camera* camera, const Camera* moonCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, bool forceDepth);

    void PtToxyY(const Vector3& pt, double& x, double& y, double& Y, double zenithAngleOffset = 0.0) const;
    void PtToxyYMoon(const Vector3& pt, double& x, double& y, double& Y, double zenithAngleOffset = 0.0) const;

    double PerezY(double theta, double gamma) const;
    double Perezx(double theta, double gamma) const;
    double Perezy(double theta, double gamma) const;

    double HosekWilkie(int channel, double theta, double gamma) const;
    double HosekWilkieX(double theta, double gamma) const;
    double HosekWilkieY(double theta, double gamma) const;
    double HosekWilkieZ(double theta, double gamma) const;

    double AngleBetween(const Vector3& v1, const Vector3& v2) const;

    void ComputeLogAvg();

    double NightSkyLuminance(); // Returns kilo-candelas
    double MoonLuminance();
    void ComputeToneMappedSkyLight();
    void ApplyGamma(Vector3& v) const;
    Matrix4 ComputeMoonBillboard(bool geocentricMode, const Vector3& moonPos, const Camera* camera);

    VertexBuffer *cubeFace[6];
    Vector3f *localVerts[6];
    Vector3 faceDirs[6];

    IndexBuffer *indexBuffer;

    Stars *stars;

    double T;

    double lastSunT, lastMoonT;
    double lastSunZenith, lastMoonZenith, lastMoonPhase;
    bool lightingChanged;
    double lastFogDistance, lastFogDensity;
    bool lastIsOvercastSun, lastIsOvercastMoon;
	double lastOvercastBlendSun, lastOvercastBlendMoon;

    double fogVolumeDistance;
    double hazeVolumeDistance;
    Color hazeColor;
    double hazeDensity;

    double sunAlpha, moonAlpha;

    int nIndices;

    double AY, BY, CY, DY, EY; // Perez luminance coefficients
    double Ax, Bx, Cx, Dx, Ex; // Perez chromaticity coefficients
    double Ay, By, Cy, Dy, Ey;

    // Hosek-Wilkie coefficients [channel][constant A-I]
    double hosekWilkieCoeffs[3][9];
    double hosekWilkieRadiances[3];

    double thetaS; // Angle between sun and zenith
    double thetaM; // Angle between moon and zenith

    double xZenith, yZenith, YZenith, xMoon, yMoon, YMoon;

    double sunx, suny, sunY;
    double moonx, moony, moonY;

    TextureHandle sunTexture, moonTexture[30], glowTexture;
    Billboard *sunBillboard, *moonBillboard[30], *sunGlowBillboard, *moonGlowBillboard;
    Glare *sunGlare, *moonGlare;

    GlareManager *glareMgr;

    double rowInc, halfRowInc, colInc, halfDim, cubeDimension;
    int rows, cols;
    double sunWidth, moonWidth, maxSkylightLuminance;

    Color skyLight, skyLightSeaLevel;
    Vector3 sunTransmittedLuminance, moonTransmittedLuminance;
    Vector3 sunTransmittedLuminanceSeaLevel, moonTransmittedLuminanceSeaLevel;
    Vector3 sunScatteredLuminance, moonScatteredLuminance;
    Vector3 sunScatteredLuminanceSeaLevel, moonScatteredLuminanceSeaLevel;

    SolarSpectrum sunSpectrum, lunarSpectrum;

    bool isOvercast;
    double overcastBlend, overcastTransmissionAmbient, overcastTransmissionDirect;
    double lightPollution;
    double isothermalEffect;
    double oneOverGammaPreetham, oneOverGammaHosek;
    double yOffsetAngle, verticalOffsetAngle;
    float perezBlend;

    bool drawSunBelowHorizon, sunHasColor, moonHasColor;
    bool simpleShader, sunAboveHorizon;
    bool isInfraRed, isApplicationFog, disablePerCloudFog;

    bool useGlowSun, useGlowMoon;
    double glowSizeSun, glowSizeMoon, glowSizeSunInfrared;
    double glowIntensitySun, glowIntensityMoon;
	double lastGlowSize, lastSunDiscSize;
    double nightSkyScale;
    double moonBrightness, moonBrightnessHDR;

    bool monochromeAmbient;
	double sunAltitude;
	double daylightMoonDimming;
    bool starsVisible, blendSky, blendSunMoon;

	// Precomputed stuff for GetAverageHorizonColor
    double fovFromConfig;
	int nSamples;

    SkyModel skyModel;

    SL_MAP(int, double) twilightLuminance;

    // Pointer to the parent atmosphere
    const Atmosphere& atmosphere;

    ThreadCameraStreamData* tcsData;

    Ephemeris* ephemeris;

    double sunScale = 0.5;
    double moonScale = 0.5;

    bool monochromeMoon = false;
};
}

#endif
