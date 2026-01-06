// Copyright (c) 2004-2016  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file Stars.h
    \brief Renders the stars, planets, and any associated glares.
 */

#ifndef STARS_H
#define STARS_H

#include "Renderer.h"
#include <vector>

namespace SilverLining
{
class VertexBuffer;
class Ephemeris;
class Glare;
class GlareManager;
class BillboardBufferProvider;
class BillboardShader;
class Camera;
struct Star;

class FogParameters
{
public:
    Color fogColor;
    double fogDensity, fogDistance;
};

/** Efficiently renders the stars, planets, and their glares - employing vertex
   and fragment shaders when available. */
class Stars : public MemObject
{
public:
/** Constructor.
   \param eph A pointer to an Ephemeris object you created, which represents the
   current time and location, and provides methods for determining astronomical
   positions based on it. This object will not be deleted by this class.
   \param gmgr A pointer to a GlareManager object you created, which creates
   Glare objects to draw bright planets or stars on dark nights with glare
   effects. This object will not be deleted by this class.
   \param turbidity The modeled atmospheric turbidity, which affects the color
   of the stars when using vertex shaders. For a description of how to
   interpret this value, see AtmosphericConditions::SetTurbidity().
 */
    Stars(const Ephemeris* _ephemeris, GlareManager *gmgr, double turbidity, bool billboardsUseShaderInstancing, const BillboardBufferProvider* provider, ThreadCameraStreamData* _tcsData);

/** Destructor; destroys any objects created internally within the class. */
    ~Stars();

/** Draws the stars; assumes the modelview matrix has already been set up by
   the Sky class's Sky::Draw() method. If available, vertex shaders will be used
   to simulate the visibile stars' brightness and colors. */
    bool Draw(const FogParameters& fogParams, bool geocentricMode, float lightPollution, const Vector3& outputScale, unsigned long millis, const Camera* starCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, ThreadCameraStreamData* tcsData, bool forceDepth);

/** Getter for the underlying shader. */
    ShaderHandle GetShader(ThreadCameraStreamData* tcsData) const;

/** Reload the underlying shader. */
    void ReloadShaders(ThreadCameraStreamData* tcsData);

private:
    void UpdatePlanets(ThreadCameraStreamData* tcsData);
    void UpdateStars(ThreadCameraStreamData* tcsData);
    void DrawGlares(const Matrix4& eqToHorizon, const Vector3& outputScale, unsigned long millis, const Camera* glareCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, ThreadCameraStreamData* tcsData, bool forceDepth);
    virtual bool ReadStarsFromFile(const char* strStarCatalogFile);

    VertexBuffer *starVerts, *planetVerts;
    Glare **planetGlares;
    const Ephemeris* ephemeris;

    int nStars;
    double turbidity;
    double starBrightness;
    bool noGlare;
    SL_VECTOR(Glare *) glares;
    SL_VECTOR(Glare *) planetGlareVector;
    GlareManager *glareManager;
    double glareThreshold;
    float fStarDistance;
    bool geoZUp;
	bool primeMeridianZ;
    float maxLightPollution;
    int totalStars;
    double minMagnitude;
    double lastStarEpochCenturies, lastPlanetEpochCenturies;

    bool billboardsUseShaderInstancing;
    const BillboardBufferProvider* billboardBufferProvider;

    ThreadCameraStreamData* tcsData;

    // This can be a pointer from strcat.h
    // or read from a file (in which case starsReadHere=true)
    const Star* stars;
    bool starsReadHere;

    double starPointSize = 1.0;
};
}

#endif
