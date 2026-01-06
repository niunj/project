// Copyright 2006-2016 Sundog Software, LLC. All rights reserved worldwide.

/** \file CirroCumulusCloud.h
   \brief A class that knows how to draw cirrocumulus cloud decks.
 */

#ifndef CIRROCUMULUS_CLOUD_H
#define CIRROCUMULUS_CLOUD_H

#include "Cloud.h"
#include "Renderer.h"

namespace SilverLining
{

/** A class responsible for the rendering of a Cirrocumulus cloud. */
class CirroCumulusCloud : public Cloud
{
public:
/** Default constructor. */
    CirroCumulusCloud(CloudLayer *layer, double coverageThreshold);

/** The destructor deletes any vertex and index buffer objects required to
   draw this cloud. */
    virtual ~CirroCumulusCloud();

/** Prior to drawing this cloud, it must know how big it is so it can create the
   appropriate vertices for it. Call Init() prior to drawing to set up the resources
   required to draw a cirrocumulus cloud of a specified size. Note that cirrocumulus clouds are
   drawn as a 2D plane, they have no thickness, and so this method does not accept
   a thickness parameter.

   \param w The width of the cirrocumulus cloud in world units.
   \param h The depth of the cirrocumulus cloud in world units.
 */
    virtual void Init(double w, double h, ThreadCameraStreamData* tcsData);

/** Tests if this cirrocumulus cloud deck is visible within the specified frustum.

   \param f The Frustum to test visibility against.
   \return true if the cloud is not visible within the given frustum, and should be
   culled. Cirrocumulus decks are large enough and simple enough to draw that this just returns
   false no matter what, to save the CPU cost of computing visibility.
 */
    virtual bool IsCulled(const Camera* cullCamera, ThreadCameraStreamData* tcsData) const;

/** Draws this cirrocumulus cloud under the given lighting conditions. In fact, drawing may be
   deferred until the end of the frame so that this cloud may be sorted against other
   translucent objects in the scene.

   \param pass Pass in 0 for the lighting pass, and 1 for the drawing pass.
   \param lightPos A normalized vector pointing to the dominant infinitely-distanced
   light source for the scene.
   \param lightColor The color of the scene's dominant light source.
   \param invalid Forces the cloud to recompute its lighting
 */
    virtual bool Draw(int pass, const Vector3& lightPos, const Vector3& lightDir, const Color& lightColor
        , bool invalid, const Sky *sky, bool drawLightning, const Camera* camera
        , ThreadCameraStreamData* tcsData);

/** DrawBlendedObject() is called by the framework at the end of the scene, when sorted
   translucent objects have all been sorted against each other. Calling Draw() will cause
   a later call to DrawBlendedObject() to happen automagically. */
    virtual void DrawBlendedObject(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera
        , const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
        , ThreadCameraStreamData* tcsData) const;

/** Cirrocumulus clouds do not model growth over time, and so its Update() method is an
   empty implementation. */
    virtual bool Update(unsigned long now, bool forceUpdate, const RandomNumberGenerator* rng, const BillboardBufferProvider* provider, ThreadCameraStreamData* tcsData);

/** Returns the dimensions of this cirrocumulus cloud. */
    virtual void GetSize(double& width, double& depth, double& height) const;

/** Cirrocumulus clouds do not use dynamically-generated imposters, and so CreateImposter() does
   nothing for this class. */
    virtual void CreateImposter(bool billboardsUseShaderInstancing, const BillboardBuffer& buffer) {
    }

/** For sorting purposes, compute the screen depth. The view direction
   is considered in order to compute the point on the cirrus plane that intersects the
   view vector.

   \param from  The position of the position to compute the distance from, along the given
   view vector.
   \param direction A normalized vector that specifies the direction from the "from" point,
   defining the ray along which the intersection distance will be computed.
   \param mv The modelview  matrix.

   \return The screen depth of the intersection of the ray to the cirrus cloud's plane, or a very large number if
   there is no intersection.
 */
    virtual double GetDistance(const Vector3& from, const Vector3& direction, const Matrix4& mv, bool rightHanded, const Camera* camera, ThreadCameraStreamData* tcsData) const;

    Vector3 GetSortPosition(const Camera* camera, const ThreadCameraStreamData* tcsData) const;

protected:

    TextureHandle texture;
    VertexBuffer *vb;
    IndexBuffer *ib;
    Color cloudColor, appliedColor;

    int numVerts, nIndices, gridDim;

    double width, height;
    float extinction, albedo, fadeFalloff;
    bool animateCirrus;

    ThreadCameraStreamData* tcsData;
};
}

#endif
