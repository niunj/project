// Copyright (c) 2010-2011 Sundog Software, LLC. All rights reserved worldwide.

/** \file StratocumulusCloud.h
    \brief A stub of a cloud, which only serves to keep track of bounding volumes for the individual
    clouds that make up the stratocumulus cloud layer at the time the layer was generated.
 */

#ifndef STRATOCUMULUS_CLOUD_H
#define STRATOCUMULUS_CLOUD_H

#include "Cloud.h"
#include "Renderer.h"

namespace SilverLining
{
/** A stub of a cloud, which only serves to keep track of bounding volumes for the individual
    clouds that make up the stratocumulus cloud layer at the time the layer was generated. While
    SilverLining doesn't actually make use of this data, it is useful to expose to some applications.
    You can use CloudLayer::GetClouds() to get a list of individual clouds, then Cloud::GetSize() and
    Cloud::GetWorldPosition() to construct bounding volumes for each one.
 */
class StratocumulusCloud : public Cloud
{
public:
/** Default constructor. */
    StratocumulusCloud(CloudLayer *layer, double coverageThreshold) : Cloud(layer, coverageThreshold) {}

/** Virtual destructor. */
    virtual ~StratocumulusCloud() {}

/** Initializes the cloud to take up a specified volume of space with the specified parameters.
   \param w The width of the cloud
   \param d The depth of the cloud
   \param h The height of the cloud
 */
    virtual void SetSize(double w, double d, double h) {
        width = w;
        depth = d;
        height = h;
    }

/** Determines if the cloud is within the specified viewing frustum. This always returns true since
    individual stratocumulus clouds don't actually do any rendering - the parent cloud layer does.
   \param f The Frustum to test visibility against.
   \return True if the cloud is outside the Frustum and should be culled; false otherwise.
 */
    virtual bool IsCulled(const Camera* cullCamera, ThreadCameraStreamData* tcsData) const
    {
        return true;
    }

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
        , ThreadCameraStreamData* tcsData)
    {
        return true;
    }

/** Runs an iteration of the cellular automata. */
    virtual bool Update(unsigned long now, bool forceUpdate, const RandomNumberGenerator* rng, const BillboardBufferProvider* provider, ThreadCameraStreamData* tcsData)
    {
        return false;
    }

/** Called by the framework to actually render the cloud deck, once all translucent
   objects in the scene have been sorted back-to-front. */
    virtual void DrawBlendedObject(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
        , ThreadCameraStreamData* tcsData) const
    {
        // nothing to do
    }

/** Retrieves the dimensions of the stratus cloud deck, as specified in the SetSize()
   method. */
    virtual void GetSize(double& w, double& d, double& h) const
    {
        w = width;
        d = depth;
        h = height;
    }

/** Saves this cloud's information to a stream, in proprietary binary format. */
    virtual bool Serialize(std::ostream& s, ThreadCameraStreamData* tcsData) {
        Vector3 pos = GetWorldPosition(tcsData);
        pos.Serialize(s);

        s.write((char *)&width, sizeof(double));
        s.write((char *)&height, sizeof(double));
        s.write((char *)&depth, sizeof(double));

        layerPosition.Serialize(s);    

        return true;
    }

/** Restores this cloud from a stream, from our proprietary binary format. */
    virtual bool Unserialize(std::istream& s, ThreadCameraStreamData* tcsData) {
        Vector3 pos;
        pos.Unserialize(s);
        SetWorldPosition(pos);

        s.read((char *)&width, sizeof(double));
        s.read((char *)&height, sizeof(double));
        s.read((char *)&depth, sizeof(double));

        layerPosition.Unserialize(s);

        return true;
    }

protected:

    double width, height, depth;
};
}

#endif
