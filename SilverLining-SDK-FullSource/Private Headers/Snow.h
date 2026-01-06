// Copyright (c) 2009-2015 Sundog Software LLC, All rights reserved worldwide.

/** \file Rain.h
   \brief Renders rain effects.
 */

#ifndef SNOW_H
#define SNOW_H

#define SNOWFLAKE_MIN_DIAMETER 1.0
#define SNOWFLAKE_MAX_DIAMETER 6.0
#define SNOWFLAKE_DIAMETER_INCREMENT 1.0
#define NUM_SNOWFLAKE_DIAMETERS 6

#include "Vector3.h"
#include "Matrix4.h"
#include "Precipitation.h"
#include "Renderer.h"
#include <vector>

namespace SilverLining
{
class VertexBuffer;
class IndexBuffer;
class Buffer;
class Atmosphere;
class RandomNumberGenerator;

/** An individual flake of snow. */
class SnowParticle : public MemObject
{
public:
/** Default constructor, does nothing. */
    SnowParticle() {
    }

/** Randomly positions the particle within a radius surrounding the position
   passed in. */
    inline void InitializeParticlePosition(const Vector3& camPos, int idxOffset, const RandomNumberGenerator* rng);

/** Randomly orients the snowflake. */
    inline void InitializeParticleRotation(const RandomNumberGenerator* rng);

/** The flake's 4x4 translation/rotation matrix. */
    Matrix4 mat;
};

/** Manages the physical simulation of snow and rendering the snow effect. */
class Snow : public Precipitation
{
public:
/** Constructor, takes in the scene's Atmosphere object. */
    Snow(const Atmosphere* atmosphere, const Vector3& camPos, ThreadCameraStreamData* _tcsData);

/** Virtual destructor. */
    virtual ~Snow();

/** Sets the simulated snowfall intensity, in millimeters per hour (liquid
   equivalent). Reasonable values might range from 1.0 to 30.0. This will
   affect visibility, the number of particles, and the distribution of
   particle sizes. */
    virtual void SetIntensity(double R);

    /** Set the extents of the precipitation effect in terms of distance from the camera. The near clipping
    plane will be adjusted to encompass fNear while rendering the precipitation. */
    virtual void SetEffectRange(double fNear, double fFar, bool bUseDepthBuffer, const Camera* camera);

/** Renders the snow effect.

   \param dt The frame time, in seconds.
   \param f  The view frustum for culling.
   \param lightColor The dominant light color, used for lighting the snow particles.
 */
    virtual void Render(double dt, const Color& lightColor, const Camera* camera);

/** Wet snow falls twice as fast as dry snow, and has larger flakes. */
    void SetIsWet(bool wet) {
        isWet = wet;
    }

/** Retrieves whether the snow is "wet" or not.

   \sa SetIsWet()
 */
    bool GetIsWet() const {
        return isWet;
    }

private:
    void InitializeBuffers();
    void InitializeMatricesBuffer();
    void InitializeParticles(const Vector3& camPos);
    void UpdateParticles(const Vector3& motion, const Vector3& dp, int start, int end, const Camera* camera);
    void DrawParticles(const int startIdx, const int endIdx, const double s
        , const Frustum& adjustedFrustum
        , const Matrix4& billboard
        , const Matrix4& proj
        , const Matrix4& scale
        , const Camera* camera
        , ShaderHandle precipShader);
    void DrawParticlesInstanced(const int startIdx, const int endIdx, int diameterIndex, const double s
        , const Frustum& adjustedFrustum
        , const Matrix4& billboard
        , const Matrix4& proj
        , const Matrix4& scale
        , const Camera* camera
        , ShaderHandle precipShader);
    bool IsCulled(const Vector3& particlePosition, const double s, const Frustum& adjustedFrustum);

    IndexBuffer *indexBuffer;
    VertexBuffer *vertexBuffer;
    bool isWet;
    int maxParticles, maxRenderedParticles;
    double maxIntensity, visibilityMultiplier, lambda, sizeMultiplier;

    Buffer* matricesBuffer;

    SL_VECTOR(SnowParticle) particles;

    double nFlakes[NUM_SNOWFLAKE_DIAMETERS];
    double flakeDiameters[NUM_SNOWFLAKE_DIAMETERS]; // millimeters
    double terminalVelocity;
    double minParticlePixels;

    TextureHandle snowTexture;

    Vector3 previousCamPos;
    bool lastComputedIsWet;
    double lastComputedIntensity, lastComputedFogDensity;
    double wetSnowVelocity, drySnowVelocity;

    bool rotateParticles;

    Color snowColor;

    bool fastPathRendering;
};
}

#endif

