// Copyright (c) 2004-2015  Sundog Software, LLC. All rights reserved worldwide.

/** \file Cloud.h
    \brief Defines a base class from which specific cloud types are derived.
 */

#ifndef CLOUD_H
#define CLOUD_H

#include "Vector3.h"
#include "Renderable.h"
#include "Color.h"
#include "MutexContainer.h"
#include <time.h>
#include <iostream>

namespace SilverLining
{
class Atmosphere;
class Frustum;
class BillboardBuffer;
class BillboardBufferProvider;
class Camera;
class CloudLayer;
class Color;
class Sky;
class RandomNumberGenerator;
class MillisecondTimer;

enum SortOrder
{
    ASCENDING,
    DESCENDING
};

/** A base class from which specific cloud types may derive from. It contains pure virtual
    functions, and so may not be directly instantiated. */
class Cloud : public Renderable
{
public:
/** Constructor; takes the cloud layer of which this cloud is a part of. */
    Cloud(CloudLayer *parentLayer, double coverageThreshold);

/** Virtual destructor. */
    virtual ~Cloud();

/** Sets the position of the center of the cloud at its base, in world units. */
    virtual void SetWorldPosition(const Vector3& center) {
        centerPos = center;
    }

/** Retrieves the position of the center of the cloud at its base, in world units. */
    virtual Vector3 GetWorldPosition(const ThreadCameraStreamData* tcsData) const {
        return centerPos;
    }

/** Retrieves the position of the center of the cloud at its base, in world units. */
	const Vector3& GetWorldPositionRef() const {
		return centerPos;
	}

/** Retrieve the CloudLayer this cloud is a part of (as specified in the constructor). */
    CloudLayer *GetParentCloudLayer() const {
        return parentCloudLayer;
    }

/** As an optimization, clouds do not recompute their lighting every frame - it's only
    recomputed if the lighting conditions have actually changed, or if cloud growth over
    time has been enabled via the disable-cloud-growth config setting. This method will
    compute if lighting needs to be recalculated for this frame, based on changes in the
    position of the cloud and the light source. */
    virtual bool LightingChanged(const Vector3& lightPos, ThreadCameraStreamData* tcsData) const;

/** The light position will be updated as the Draw() method specifies new light positions.
    If you need to update the light position (used to determine if the cloud needs to be
    re-lit) manually, calling this method will update the last light position and cloud
    position used to compare the current conditions against in LightingChanged(). */
    virtual void UpdateLightPos(const Vector3& lightPos);

/** An interface for frustum culling tests that any derived class must implement. */
    virtual bool IsCulled(const Camera* cullCamera, ThreadCameraStreamData* tcsData) const = 0;

/** A method called once per pass per frame, prior to the cull test. 
    \param pass The pass number, 0 for lighting, 1 for drawing. The lighting pass does not
                occur on every frame, but drawing does.
*/
    virtual void Visit(int pass, ThreadCameraStreamData* tcsData) {}

/** An interface for drawing. Clouds are typically rendered using
    the more specific Draw() method that takes in a light position and color. */
    virtual bool Draw(int pass, ThreadCameraStreamData* tcsData){
        return false;
    }

/** An interface for drawing a cloud given a light position and color that any derived
    class must implement.

    \param pass Set to 0 for the lighting pass, or 1 for the rendering pass.
    \param lightPos A vector pointing toward the infinitely distant dominant light source.
    \param lightDir A normalized direction vector pointing toward the infinitely distanct light source.
   \param lightColor The color of the dominant light source.
    \param invalid Forces the cloud to recompute its lighting with this call
    \param sky Pointer to the sky object, used to compute the fog color
	\param drawLightning whether lightning is drawn on clouds that support it
    \return true if the operation completed successfully.
 */
    virtual bool Draw(int pass, const Vector3& lightPos, const Vector3& lightDir,
                      const Color& lightColor, bool invalid, const Sky *sky,
                      bool drawLightning, const Camera* camera, ThreadCameraStreamData* tcsData) = 0;

/** An interface for modifying the cloud as a function of time, used for simulating cloud
    growth. Unless the disable-cloud-growth config setting is set to false, no cloud
    growth should be permitted. Since dynamic cloud growth requires re-lighting the clouds
    every frame, it's only appropriate for non-real-time applications.

    \param now The current time, in milliseconds. In Windows, this just comes from the
    timeGetTime() function. It's not important what this time is absolutely, only relative
    differences between each timestamp passed in.
    \param forceUpdate The cloud may attempt to make optimizations to avoid rebuilding the
    cloud unless necessary based on the time elapsed. To force the cloud to update itself,
    pass in true.
    \return True if an update of the cellular automata actually occurred for this cloud.
 */
    virtual bool Update(unsigned long now, bool forceUpdate, const RandomNumberGenerator* rng, const BillboardBufferProvider* provider, ThreadCameraStreamData* tcsData) = 0;

/** A cloud will not re-light itself unless it determines that the lighting conditions or
   viewing conditions have changed sufficiently to warrant it. If you wish to force this cloud
   to re-light itself in response to some change it doesn't know about, you can call Invalidate()
   to force it to re-light on the next frame. */
    void Invalidate(ThreadCameraStreamData* tcsData);

/** Accessor for the invalid flag documented in Invalidate() */
    bool IsInvalid(ThreadCameraStreamData* tcsData) const;

/** Set the initial position of this cloud relative to its cloud layer. */
    void SetLayerPosition(const Vector3& pos) {
        layerPosition = pos;
    }

/** Retrieve the initial position of this cloud relative to its cloud layer. */
    const Vector3& GetLayerPosition() const {
        return layerPosition;
    }

/** Sets the transparency of the cloud from 0 (transparent) to 1 (fully visible) */
    void SetFade(double pFade) {
        fade = pFade;
    }

/** Retrieves the transparency of the cloud, from 0 (transparent) to 1 (fully visible) */
    double GetFade() const {
        return fade;
    }

/** Sets explicit translucency for the cloud, for 0 (transparent) to 1 (fully visible) */
    void SetAlpha(double pAlpha) {
        alpha = pAlpha;
    }

/** Retrieves the explicit translucency for the cloud. */
    double GetAlpha() const {
        return alpha;
    }

/** Retrieve the dimensions of this cloud, in world units. This is a pure virtual function
   that all derived classes must implement.*/
    virtual void GetSize(double& width, double& depth, double& height) const = 0;

/** Some clouds may want to modify their initial shape based on the current wind speed and
   direction. Implementing the ApplyWindShear method will allow the cloud to modify its appearance
   based on the current wind velocity vector.

   \param wind The wind speed and direction, in world units per second. 
   \param tcsData The ThreadCameraStreamData in question.
   */
    virtual void ApplyWindShear(const Vector3& wind, ThreadCameraStreamData* tcsData) {
    }

/** The framework will call the CloudPlaced() method of every cloud once the cloud's position
    has been set. This is an opportune time for the cloud to initialize itself. */
    virtual void CloudPlaced(const Atmosphere& atm, ThreadCameraStreamData* tcsData) {
    }

/** Saves this cloud's information to a stream, in proprietary binary format. */
    virtual bool Serialize(std::ostream& s, ThreadCameraStreamData* tcsData) {
        return false;
    }

/** Restores this cloud from a stream, from our proprietary binary format. */
    virtual bool Unserialize(std::istream& s, ThreadCameraStreamData* tcsData) {
        return false;
    }

/** Exports this cloud as a collection of spheres to a VRML .WRL file. */
    virtual bool ExportToVRML(const char *filename) const {
        return false;
    }

/** Imports this cloud from a VRML '97 .WRL file. */
    virtual bool ImportFromVRML(const char *filename) {
        return false;
    }

/** Sets if this cloud needs to be re-lit on the next lighting pass. */
    void SetNeedsRelighting(bool relight) {
        needsRelight = relight;
    }

/** Retrieves if this cloud needs to be re-lit on the next lighting pass. */
    bool GetNeedsRelighting() const {
        return needsRelight;
    }

/** Sets if the altitude of this cloud needs recomputing relative to the
    center of the Earth. */
    void SetNeedsGeocentricPlacement(bool place) {
        needsPlacement = place;
    }

/** Retrieves if a new geocentric position for this cloud needs to be computed. */
    bool GetNeedsGeocentricPlacement() const {
        return needsPlacement;
    }

/** Returns the random value from 0-1 assigned in the constructor. */
    double GetCoverageThreshold() const {return coverageThreshold;}

/** Smoothly fade the cloud in from transparent to opaque */
    virtual void FadeIn(const MillisecondTimer* timer) {}

/** Smoothly fade the cloud out from opaque to transparent */
    virtual void FadeOut(const MillisecondTimer* timer) {}

    enum FadeStates
    {
        FADING_IN = 0,
        FADING_OUT,
        FADED_IN,
        FADED_OUT
    };

    virtual FadeStates GetFadeState() const {return fadeState;}

/** Intersect this cloud using a ray originating from a world space origin and direction.
    Return all hits in hit_ranges */
    virtual void Intersect( const Vector3& origin,
                            const Vector3& direction,
                            SL_VECTOR(double)& hit_ranges ) {}

protected:
    double fade, alpha, coverageThreshold;
    Vector3 lastLightPos, lastCloudPos, layerPosition;
    CloudLayer *parentCloudLayer;
    Color fogColor;
    FadeStates fadeState;

    bool needsRelight;
    bool needsPlacement;

    // clouds are tcs specific, so just make this 
    // mutable since DrawBlendedObject is const
    mutable bool invalid;

private:
    Vector3 centerPos;


#define SILVERLINING_TRACK_CLOUDS 0

#if SILVERLINING_TRACK_CLOUDS
    static MutexContainer countMutexContainer;
    static int numClouds; // total clouds
    int id;// internal id
#endif
};
}

#endif
