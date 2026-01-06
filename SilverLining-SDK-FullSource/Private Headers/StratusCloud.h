// Copyright (c) 2004-2016  Sundog Software, LLC. All rights reserved worldwide.

/** \file StratusCloud.h
    \brief Renders decks of stratus clouds, which may be solid or "broken."
 */

#ifndef STRATUS_CLOUD_H
#define STRATUS_CLOUD_H

#include "Cloud.h"
#include "Renderer.h"
#include "StratusShader.h"
#include "StratusSections.h"
#include "StratusTextureData.h"

namespace SilverLining
{

/** Renders a deck of stratus clouds, modeled by two textured planes with fog between them.
   The cloud texture is procedurally generated and will use alpha to simulate "broken cloud
   decks" if the specified coverage is less than 1.0. "Scud" effects are also supported, meaning
   fog effects extend above and below the modeled deck in an irregular manner to better simulate
   the experience of flying just above or below the clouds.

   Stratus clouds are smart enough to not do fog effects if you're flying through a "hole" in
   a broken cloud deck.

   Stratus clouds are always drawn relative to the camera position. Tricks with texture mapping
   are employed to provide the illusion of motion relative to the cloud, but you'll never reach
   the edge of a stratus cloud.
 */
class StratusCloud : public Cloud
{
public:
/** Default constructor. */
    StratusCloud(CloudLayer *layer, double coverageThreshold);

/** Virtual destructor. */
    virtual ~StratusCloud();

/** Initializes the cloud to take up a specified volume of space with the specified parameters.
   \param w The width of the cloud deck. Since the cloud is always drawn relative to the camera
   position, this is how far toward the horizon the cloud deck will extend along the x axis.
   \param h The height of the cloud deck. Since the cloud is always drawn relative to the camera
   position, this is how far toward the horizon the cloud deck will extend along the z axis.
   \param thickness The thickness of the cloud deck; that is, the vertical distance between
   the lower and upper planes that make up the cloud.
   \param density The coverage of the cloud deck; 1.0 specifies a solid stratus cloud deck,
   0.5 would give you a broken cloud deck with 50% coverage of the sky.
 */
    virtual void Init(double w, double h, double thickness, double density, ThreadCameraStreamData* tcsData);

/** Determines if the cloud is within the specified viewing frustum.
   \param f The Frustum to test visibility against.
   \return True if the cloud is outside the Frustum and should be culled; false otherwise.
 */
    virtual bool IsCulled(const Camera* cullCamera, ThreadCameraStreamData* tcsData) const;

/** Draws the stratus cloud - or more accurately, schedules it for later rendering with the
   other translucent objects via DrawBlendedObject().
   \param pass Set to 0 for the lighting pass, or 1 for the rendering pass.
   \param lightPos The normalized direction vector toward the dominant infinitely-distance light
   source.
   \param lightColor The color of the dominant light source.
   \param invalid Forces the cloud to recompute its lighting
   \return True if the operation completed successfully.
 */
    virtual bool Draw(int pass, const Vector3& lightPos, const Vector3& lightDir,
        const Color& lightColor, bool invalid, const Sky *sky, bool drawLightning, const Camera* camera
        , ThreadCameraStreamData* tcsData);

/** Runs an iteration of the cellular automata. Stratus clouds do not evolve over time,
   so this is basically a no-op. */
    virtual bool Update(unsigned long now, bool forceUpdate, const RandomNumberGenerator* rng, const BillboardBufferProvider* provider, ThreadCameraStreamData* tcsData);

/** Called by the framework to actually render the cloud deck, once all translucent
   objects in the scene have been sorted back-to-front. */
    virtual void DrawBlendedObject(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
        , ThreadCameraStreamData* tcsData) const;

/** Retrieves the dimensions of the stratus cloud deck, as specified in the Init()
   method. */
    virtual void GetSize(double& width, double& depth, double& height) const;

/** Create a 2D imposter of the cloud; not supported for stratus decks. */
    virtual void CreateImposter(bool billboardsUseShaderInstancing, const BillboardBuffer& buffer) {
    }

/** For a given world position, determine the "thickness" of the cloud which may be
   used to set fog effects. It's based on the texture map generated for the cloud, so you'll
   generally get scud on the bright parts of the cloud. For broken clouds, you won't get
   scud under the "holes" in the cloud. */
    float LookupScud(const Vector3& cameraPos, ThreadCameraStreamData* tcsData);

/** For sorting purposes, compute the screen depth. The view direction
   is considered in order to compute the point on the cirrus plane that intersects the
   view vector.

   \param from  The position of the position to compute the distance from, along the given
   view vector.
   \param direction A normalized vector that specifies the direction from the "from" point,
   defining the ray along which the intersection distance will be computed.
   \param mv The modelview matrix.

   \return The screen depth of the intersection of the ray to the cirrus cloud's plane, or a very large number if
   there is no intersection.
 */
    virtual double GetDistance(const Vector3& from, const Vector3& to, const Matrix4& mvp, bool rightHanded, const Camera* camera, ThreadCameraStreamData* tcsData) const;

    virtual Vector3 GetSortPosition(const Camera* camera, const ThreadCameraStreamData* tcsData) const;
protected:
    void CreateTexture(double density, double edgeTexSizeX, double edgeTexSizeY, ThreadCameraStreamData* tcsData);
    void CreateMieLookupTexture(ThreadCameraStreamData* tcsData);

    VertexBuffer* CreateVertices(int x1, int x2, int y1, int y2, float u1, float u2, float v1, float v2, double baseWidth, double baseLength, ThreadCameraStreamData* tcsData);
    IndexBuffer* CreateIndices(int w, int h, bool top, ThreadCameraStreamData* tcsData);

	double DistanceToPlane(const Vector3& from, const Vector3& to, double D, const Matrix4& mv, const Camera* camera, ThreadCameraStreamData* tcsData) const;

    TextureHandle textures[9];
    VertexBuffer *vb[9];
    IndexBuffer *ibTop[9], *ibBottom[9];
    TextureHandle mieTexture;

    float incX, incY, hw, hh, fadeFalloff;
    int dim;
    int edgeVertsX, edgeVertsY;

    double width, height, depth, segmentWidth, segmentHeight, textureDensity;

    Vector3 skyColorScale, groundColorScale;

    float lightScale;
    Vector3 lightDirection, sunColor;

	double fogCutoff;

    bool doBrokenVisibility;

    ThreadCameraStreamData* tcsData;
};
}

#endif
