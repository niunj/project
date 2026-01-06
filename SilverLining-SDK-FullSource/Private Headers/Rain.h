// Copyright (c) 2009-2016 Sundog Software, LLC. All rights reserved worldwide.

/** \file Rain.h
   \brief Renders rain effects.
 */

#ifndef RAIN_H
#define RAIN_H

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

// Raindrop sizes sampled from 1-5mm at 0.5mm increments
#define NUM_RAINDROP_DIAMETERS 9
#define MIN_RAINDROP_DIAMETER 1.0
#define RAINDROP_DIAMETER_INCREMENT 0.5

#define NUM_RAIN_TEXTURE_ANGLES 5
#define RAIN_TEXTURE_ANGLE_INCREMENT 20
#define NUM_RAIN_TEXTURE_OSCILLATIONS 10

namespace SilverLining
{
class VertexBuffer;
class IndexBuffer;
class Buffer;

/** An individual rain streak. */
class RainParticle : public MemObject
{
public:
/** Default constructor; does nothing. */
    RainParticle() {
    }

/** Randomly positions the particle within a radius surrounding the position
   passed in. */
    inline void InitializeParticlePosition(const Vector3& camPos, int idxOffset, const RandomNumberGenerator* rng);

/** The current location of this raindrop (center of the streak). */
    Vector3 pos;
};

/** Manages the physical simulation of rain and its rendering. */
class Rain : public Precipitation
{
public:
/** Constructor, takes in the scene's Atmosphere object. */
    Rain(const Atmosphere* atmosphere, const Vector3& camPos, ThreadCameraStreamData* _tcsData);

/** Virtual destructor. */
    virtual ~Rain();

/** Set the simulated rainfall rate, in millimeters per hour. Reasonable values
   would range from 1.0 to 30.0. The intensity will affect visibility, particle
   size distribution, number of particles, and particle velocity.

   \param R rainfall rate, in mm/hr.
 */
    virtual void SetIntensity(double R);

/** Set the extents of the precipitation effect in terms of distance from the camera. The near clipping
    plane will be adjusted to encompass fNear while rendering the precipitation. */
    virtual void SetEffectRange(double fNear, double fFar, bool bUseDepthBuffer, const Camera* camera);

/** Renders the rain effect.

   \param dt The frame time, in seconds
   \param f  The view frustum, used for culling.
   \param lightColor The dominant light color, used for lighting the rain particles.
 */
    virtual void Render(double dt, const Color& lightColor, const Camera* camera);

private:
    void UpdateParticles(const Vector3& motion, const Vector3& dp, int start, int end, const Camera* camera);
    void DrawParticlesInstanced(int particleIdx
        , const int dropIndex
        , const int angleIndex
        , const Color& lightColor
        , const float rainAlpha
        , const double halfStreak
        , const Matrix4& _rotateTranslate
        , const Matrix4& scale
        , const Frustum& adjustedFrustum
        , const Matrix4& proj
        , const Camera* camera
        , ShaderHandle precipShader
        , Color& rainColor) const;

    void DrawParticles(int particleIdx
        , const int dropIndex
        , const int angleIndex
        , const Color& lightColor
        , const float rainAlpha
        , const double halfStreak
        , const Matrix4& _rotateTranslate
        , const Matrix4& scale
        , const Frustum& adjustedFrustum
        , const Matrix4& proj
        , const Camera* camera
        , ShaderHandle precipShader
        , Color& rainColor) const;

    bool IsCulled(const Vector3& particlePosition, const Frustum& adjustedFrustum, const double halfStreak) const;

    void GetRainTextureAndColor(Color& outRainColor, int& currentRainTextureIndex, bool& rainTextureSet, int& oscCount, int& oscNum, int dropsPerOscillation
        , const Color& lightColor, int angleIndex, float rainAlpha, ShaderHandle precipShader, Color& rainColor) const;
    void InitializeBuffers();
    void InitializePositions(const Vector3& camPos);
    void LoadStreakTextures();
    void InitializeTexturesBuffer();
    void InitializeParticlesBuffer();
    void LoadStreakColors();
    void ComputeOscillationTimes();

    int maxParticles;
    double maxIntensity;
    double rainConstantAlpha;
    double rainStreakFrameTime;
    bool rainStreakCameraCoords;

    VertexBuffer *vertexBuffer;
    IndexBuffer *indexBuffer;
    SL_VECTOR(RainParticle) particles;

    Buffer* texturesBuffer;
    Buffer* particlesBuffer;

    Vector3 previousCamPos;

    int nDrops[NUM_RAINDROP_DIAMETERS];
    double dropDiameters[NUM_RAINDROP_DIAMETERS]; // millimeters
    double terminalVelocities[NUM_RAINDROP_DIAMETERS]; // m/s
    double oscillationTimes[NUM_RAINDROP_DIAMETERS]; // s

    TextureHandle rainTextures[NUM_RAIN_TEXTURE_ANGLES * NUM_RAIN_TEXTURE_OSCILLATIONS];
    Color rainColors[NUM_RAIN_TEXTURE_ANGLES * NUM_RAIN_TEXTURE_OSCILLATIONS];
    double angleThresholds[NUM_RAIN_TEXTURE_ANGLES];

    double visibilityMultiplier, minParticlePixels;

    int maxRenderedParticles;

    float rainAlphaThreshold, rainWidthMultiplier;

    double lastComputedIntensity, lastComputedFogDensity;

    double rainVelocityFactor;

    Color rainColor;

    Vector3 cameraMotion;
    Vector3 cameraVelocities[CAMERA_VELOCITY_BUFFER_SIZE];
    int cameraVelocityBufferIndex;
    bool cameraVelocityBufferFilled;

    bool fastPathRendering;
};
}

#endif
