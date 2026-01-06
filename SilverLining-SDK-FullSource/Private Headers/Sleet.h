// Copyright (c) 2009-2016 Sundog Software, LLC. All rights reserved worldwide.

/** \file Sleet.h
   \brief Renders sleet effects.
 */

#ifndef SLEET_H
#define SLEET_H

#if defined(WIN32) || defined(WIN64)
#pragma warning (disable: 4786)
#endif

#include "Vector3.h"
#include "Frustum.h"
#include "Renderer.h"
#include "Atmosphere.h"
#include "Color.h"
#include "Precipitation.h"
#include <vector>

// Raindrop sizes sampled from 0.5 - 3mm at 0.5mm increments
#define NUM_PARTICLE_DIAMETERS 6
#define MIN_PARTICLE_DIAMETER 0.5
#define SLEET_DIAMETER_INCREMENT 0.5
#define NUM_SLEET_TEXTURE_OSCILLATIONS 10

namespace SilverLining
{
class VertexBuffer;
class IndexBuffer;
class Buffer;

/** An individual sleet particle. */
class SleetParticle : public MemObject
{
public:
/** Default constructor; does nothing. */
    SleetParticle() {
    }

/** Randomly positions the particle within a radius surrounding the position
   passed in. */
    inline void InitializeParticlePosition(const Vector3& camPos, int idxOffset, const RandomNumberGenerator* rng);

/** The current location of this raindrop (center of the streak). */
    Vector3 pos;
};

/** Manages the physical simulation of rain and its rendering. */
class Sleet : public Precipitation
{
public:
/** Constructor, takes in the scene's Atmosphere object. */
    Sleet(const Atmosphere* atmosphere, const Vector3& camPos, ThreadCameraStreamData* _tcsData);

/** Virtual destructor. */
    virtual ~Sleet();

/** Set the simulated sleetfall rate, in millimeters per hour. Reasonable values
   would range from 1.0 to 30.0. The intensity will affect visibility, particle
   size distribution, number of particles, and particle velocity.

   \param R sleetfall rate, in mm/hr.
 */
    virtual void SetIntensity(double R);

    /** Set the extents of the precipitation effect in terms of distance from the camera. The near clipping
    plane will be adjusted to encompass fNear while rendering the precipitation. */
    virtual void SetEffectRange(double fNear, double fFar, bool bUseDepthBuffer, const Camera* camera);

/** Renders the sleet effect.

   \param dt The frame time, in seconds
   \param f  The view frustum, used for culling.
   \param lightColor The dominant light color, used for lighting the rain particles.
 */
    virtual void Render(double dt, const Color& lightColor, const Camera* camera);

private:
    void UpdateParticles(const Vector3& motion, const Vector3& dp, int start, int end, const Camera* camera);
    void DrawParticlesInstanced(int particleIdx, int dropIndex, int numStreaksRendered, ShaderHandle precipShader, const Color& sleetColor
        , const Frustum& adjustedFrustum
        , const double halfStreak
        , const Matrix4& rotateTranslate
        , const Matrix4& proj
        , const Matrix4& scale
        , const Camera* camera) const;

    void DrawParticles(int particleIdx, int dropIndex, int numStreaksRendered, ShaderHandle precipShader, const Color& sleetColor
        , const Frustum& adjustedFrustum
        , const double halfStreak
        , const Matrix4& rotateTranslate
        , const Matrix4& proj
        , const Matrix4& scale
        , const Camera* camera);

    bool IsCulled(const Vector3& v, const Frustum& adjustedFrustme, double halfStreak) const;
    
    int GetSleetTextureIndex(int& oscCount, int& oscNum, const int dropsPerOscillation);
    void InitializeBuffers();
    void InitializeTexturesBuffer();
    void InitializeParticlesBuffer();
    void InitializePositions(const Vector3& camPos);
    void LoadStreakTextures();
    void ComputeOscillationTimes();

    int maxParticles;
    double maxIntensity;
    bool sleetStreakCameraCoords;

    bool fastPathRendering;

    VertexBuffer *vertexBuffer;
    IndexBuffer *indexBuffer;
    SL_VECTOR(SleetParticle) particles;

    Buffer* particlesBuffer;

    Buffer* texturesBuffer;

    Vector3 previousCamPos;

    int nDrops[NUM_PARTICLE_DIAMETERS];
    double dropDiameters[NUM_PARTICLE_DIAMETERS]; // millimeters
    double terminalVelocities[NUM_PARTICLE_DIAMETERS]; // m/s

    TextureHandle sleetTextures[NUM_SLEET_TEXTURE_OSCILLATIONS];

    double visibilityMultiplier, minParticlePixels;

    int maxRenderedParticles;

    float sleetWidthMultiplier;

    double lastComputedIntensity, lastComputedFogDensity;

    float sleetAlpha;

    double streakFrameTime;

    Vector3 cameraMotion;
    Vector3 cameraVelocities[CAMERA_VELOCITY_BUFFER_SIZE];
    int cameraVelocityBufferIndex;
    bool cameraVelocityBufferFilled;
};
}

#endif
