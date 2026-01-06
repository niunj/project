// Copyright (c) 2006-2017 Sundog Software, LLC. All rights reserved worldwide.

/**
    \file CumulonimbusCloud.h
    \brief A Cloud that implements the rendering of a thunderhead with lighting.
 */

#ifndef CUMULONIMBUS_CLOUD_H
#define CUMULONIMBUS_CLOUD_H

#include "CumulusCloud.h"
#include <vector>
#include <map>

namespace SilverLining
{
class Virga;
class Lightning;
class Voxel;

class LitVoxel
{
public:
    Voxel *voxel;
    Color origColor;
    Color lightningColor;
    bool hasOrigColor;
};

/** A derivative of CumulusCloud that models thunderheads, with their characteristic
   anvil-shape and lightning effects. */
class CumulonimbusCloud : public CumulusCloud
{
public:
/** Default constructor. */
    CumulonimbusCloud(CloudLayer *parentLayer, double coverageThreshold, bool voxelsUseShaderInstancing, bool _drawIndirect, ThreadCameraStreamData* _tcsData);

/** Virtual destructor. */
    virtual ~CumulonimbusCloud();

    /** Tests if this cloud is visible within the given Frustum.

    \param f The frustum to test visibility against.
    \return true if the cloud is not visible and should be culled; false if it is visible.
    */
    virtual bool IsCulled(const Camera* cullCamera, ThreadCameraStreamData* tcsData) const;

/** IncrementTimeStep() drives the cellular automata that constructs the cloud
   over time. It will activate cloud "voxels" or puffs in such a manner that encourages
   vertical growth at lower altitudes and a spreading anvil head at the top of the
   cloud.

   \param now Current timestamp, in milliseconds.*/
    virtual void IncrementTimeStep(unsigned long now, const RandomNumberGenerator* rng, const BillboardBufferProvider* provider, ThreadCameraStreamData* tcsData);

/** The voxel positions will be "blown" off-center by the current wind vector.
    \param wind The current wind vector, in world units per second.
    \param tcsData The ThreadCameraStreamData in question.
 */
    virtual void ApplyWindShear(const Vector3& wind, ThreadCameraStreamData* tcsData);

/** Draw the cloud, or rather, schedule it for drawing in the transulcent sort
   and draw pass. This will draw any attached rain and lightning effects, and then
   call up to the base CumulusCloud class to draw the cloud itself. It also manages
   lighting cloud voxels that are close to lightning bolts.

   \param pass Set to 0 for the lighting pass, or 1 for the rendering pass.
   \param lightPos The normalized direction to the infinitely distant dominant light source.
   \param lightColor The color of the dominant light source.
   \return true if the operation succeeded.
   \param invalid Forces the cloud to recompute its lighting
 */
    virtual bool Draw(int pass, const Vector3& lightPos, const Vector3& lightDir,
                      const Color& lightColor, bool invalid, const Sky *sky,
                      bool drawLightning, const Camera* camera
                      , ThreadCameraStreamData* tcsData);

/** Creates objects to support lightning and rain effects once the cloud's position
   has been set. */
    virtual void CloudPlaced(const Atmosphere& atm, ThreadCameraStreamData* tcsData);

/** Forces a random lightning strike in this cloud to fire starting with this frame. */
    bool ForceLightning(bool value);

    void ResetLightningTimer();
protected:
/** Sets up the initial conditions of the cellular automata to
   create the initial cloud shape. */
    virtual void InitializeVoxelState(const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData);

/** Each frame, this method will manage lighting cloud voxels that are close to
   currently active lightning bolts. */
    void ApplyLightningLighting(Lightning *l);

/** This puts the voxel colors back to normal after rendering lightning effects.*/
    void RestoreLightningLighting(Lightning *l);

/** Pre-computes how cloud voxels are affected by a given lightning bolt. */
    void ComputeLightningLighting(Lightning *l);

/** A method called once per pass per frame, prior to the cull test. 
    \param pass The pass number, 0 for lighting, 1 for drawing. The lighting pass does not
                occur on every frame, but drawing does.
*/
    virtual void Visit(int pass, ThreadCameraStreamData* tcsData);

private:
/** This method returns the probability of a cloud voxel attaining water vapor in
   the cellular automata model. It effectively promotes vertical growth within a
   shaft that spills into an expansive anvil-head at the top of the cloud.

   The i, j, k parameters indicate the position in the 3D array of voxels that make
   up the cloud for which the probability of attaining water vapor is desired in a
   given time step. */
    double GetVaporProbability(int i, int j, int k, const RandomNumberGenerator* rng);

    void CleanResources();

    double towerCenterX, towerCenterY;

    SL_VECTOR(Virga *) virgaList;
    SL_VECTOR(Lightning *) lightningList;
    SL_MAP( Lightning *, SL_VECTOR(LitVoxel) ) litVoxels;

    const Atmosphere *atmosphere;
};
}

#endif
