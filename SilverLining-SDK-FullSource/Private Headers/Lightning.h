// Copyright (c) 2006-2016 Sundog Software, LLC. All rights reserved worldwide.

/**
    \file Lightning.h
    \brief Models and renders lightning bolts.
 */

#ifndef LIGHTNING_H
#define LIGHTNING_H

#include "Renderable.h"
#include "Renderer.h"
#include "Atmosphere.h"
#include <vector>

namespace SilverLining
{
class Cloud;
class Billboard;
class ThreadCameraStreamData;

class LightningParameters;
class Lightning;

/** An individual branch of lightning. */
class LightningBranch : public MemObject
{
public:
/** Constructor; initializes the branch to not be the "main branch." */
    LightningBranch(ThreadCameraStreamData* _tcsData, const LightningParameters* _lightningParameters);

    virtual ~LightningBranch();

/** Renders this lightning branch.

   \return True if the operation succeeded.
 */
    bool Draw(bool hdr, double distanceFactor, const Lightning* lightning, ThreadCameraStreamData* tcsData) const;

/** Syncs the VBO with the branch points. */
    void SyncVertexBuffer();

/// A vector of points that define the 3D line that defines this branch.
    SL_VECTOR(Vector3) points;

/// A VBO for drawing the points.
    VertexBuffer* vertexBuffer;

    bool vertexBufferDirty;

/// The width of the lightning bolt, in pixels.
    double lineWidth;


/// Defines if this branch is the main lightning trunk.
    bool isMainBranch;

private:
    const LightningParameters* lightningParameters;
public:

    ThreadCameraStreamData* tcsData;
};

/** Models the growth of lightning and its branches as it reaches toward the ground
   from a cloud. */
class Lightning : public Renderable
{
public:
/** Constructor.

   \param parentCloud The cloud from which this lightning should originate from.
   \param height The distance, in world units, from the origin of the lightning bolt
   to the end of the bolt (at or below the ground), along the "up" axis.
   \param offsetPos A vector from the center of the parentCloud to the origin of the
   lightning bolt.
 */
    Lightning(Cloud *parentCloud, double height, const Vector3& offsetPos, const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData);

/** Virtual destructor. */
    virtual ~Lightning();
public:

/** Renders the lightning bolt, or schedules it for later rendering once all the
   scene's translucent objects have been sorted. \sa DrawBlendedObject()

   \param pass Set to 0 for the lighting pass, or 1 for the rendering pass.
   \param atm The current Atmosphere object
   \return True if the operation succeeded.
 */
    virtual bool Draw(int pass, const Atmosphere& atmosphere, ThreadCameraStreamData* tcsData);

/** A method called once per pass per frame, prior to the cull test. 
    \param pass The pass number, 0 for lighting, 1 for drawing. The lighting pass does not
                occur on every frame, but drawing does.
    \param atm The current Atmosphere object
*/
    virtual void Visit(int pass, const Atmosphere& atm, ThreadCameraStreamData* tcsData);

/** Retrieves the position, in world coordinates, of the origin of this lightning
   bolt. */
    virtual Vector3 GetWorldPosition(const ThreadCameraStreamData* tcsData) const;

/** Once the translucent objects in the scene have been sorted back-to-front, the framework
   will call each translucent object's DrawBlendedObject() method to perform the actual
   rendering of the object. */
    virtual void DrawBlendedObject(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
        , ThreadCameraStreamData* tcsData) const;

/** Returns the approximate distance from a given point to the closest point on
   the lightning bolt. */
    virtual double GetDistance(const Vector3& v);

    virtual Vector3 GetSortPosition(const Camera* camera, const ThreadCameraStreamData* tcsData) const;

/** Returns whether or not this lightning bolt is currently discharging. As an
   optimization, we re-use the same Lightning objects and just have them discharge
   sporadically. In our world, lightning does strike the same place twice...*/
    virtual bool GetIsDischarging() const {
        return discharging;
    }

/** Whether the lightning bolt was discharging on the previous frame. Used to
    detect state changes in conjunction with GetIsDischarging(). */
    virtual bool GetLightingApplied() const {
        return lightingApplied;
    }

    virtual void SetLightingApplied(bool b) {
        lightingApplied = b;
    }

/** Force a lightning strike starting in this frame. If the lightning mode is 
    FORCE_ON_OFF, the value parameter indicates whether lightning should be on or off.
    Otherwise, "value" is ignored. */
    virtual void ForceStrike(const Atmosphere& atm, bool value);

/** Returns the glow color of the glow surrounding the lightning. */
    virtual const Color& GetGlowColor() const;

    virtual void ResetDischargeTimer();

    //! accessors
    virtual double GetMainBranchCoeff(void) const;
    virtual double GetBranchCoeff(void) const;
    virtual float GetFogAlpha(void) const;

protected:
    void GenerateBranch(double theta, double phi, double thickness, const Vector3& startPos,
        int segmentsLeft, LightningBranch* branch, const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData);
    bool Generate(const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData);
    Vector3 GenerateSegment(const Vector3& lastPoint, double seedTheta, double seedPhi,
        double& theta, double& phi, const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData);
    double BranchProbability(double y);
    void GetBranchAngles(double seedTheta, double seedPhi, double& theta, double& phi, const RandomNumberGenerator* rng);
	void DeleteBranches(void);

private:
    /// Modulates the alpha component of the main branch and its glow.
    double mainBranchCoeff;

    /// Modulates the alpha component of child branches and their glow.
    double branchCoeff;

    /// Fading for visibility
    mutable float fogAlpha;

    double initialThickness;
    int maxSegmentsPerBranch;
    double maxSegmentLength;
    double segmentAngleVariance, segmentAngleMean;
    double maxBranchProbability;
    double minBranchThickness, maxBranchThickness;
    double thicknessReduction;
    double maxDischargePeriod;
    double perspectiveDistance;
    bool hdr;
    bool forceDischarge;

    unsigned long lastFireTime, nextFireTime, fireDuration;

    bool discharging, lightingApplied;

    double height;

    Vector3 offsetPos, startPos, endPos;
    Cloud *cloud;
    SL_VECTOR(LightningBranch*) branches;

    const LightningParameters* lightningParameters;

private:
    static LightningParameters* theLightningParameters;
    static MutexContainer internalMutexContainer;
    static int refCount;
};
}

#endif
