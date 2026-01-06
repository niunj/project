// Copyright (c) 2004-2018  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file CumulusCloud.h
    \brief A cloud that is composed of overlapping lit voxels, or metaballs.
 */

#ifndef CUMULUS_CLOUD_H
#define CUMULUS_CLOUD_H

#include "Cloud.h"
#include <vector>
#include <iostream>

#define CUMULUS_SMOOTH_WIDTH 1
#define CUMULUS_SMOOTH_DIM (CUMULUS_SMOOTH_WIDTH * 2 + 1)

namespace SilverLining
{
class Voxel;
class QuadBatches;
class TextureManager;

class CumulusCloudCompilationOperation;

/** This structure contains initialization parameters for a CumulusCloud that are read
   from the config file for a given cumulus cloud type. */
typedef struct CumulusCloudInit_S
{
    double width, height, depth;
    double vaporProbability, transitionProbability, extinctionProbability;
    double initialVaporProbability;
    double voxelSize;
    int initialEvolveSteps;
    int timeStepInterval;
    double dropletSize, waterContent, albedo, dropletsPerCubicCm;
    double spinRate;
    float ambientScattering, ambientScatteringHiRes;
    double attenuation, attenuationHiRes;
    double minimumDistanceScale;
    double verticalGradient;
} CumulusCloudInit;

/** This structure contains parameters for cloud puffs that can be read from either
    VRML formatted clouds or custom edited clouds. */
typedef struct puff_s {
    Vector3 pos;
    int textureId;
    float radius;
} Puff;

/** A Cloud that is composed of many lit voxels, which contain metaballs - or in simpler terms,
   a collection of puffs. Its creation and growth is driven by a cellular automata process.*/
class CumulusCloud : public Cloud
{
public:
/** Default constructor. */
    CumulusCloud(CloudLayer *layer, double coverageThreshold, bool voxelsUseShaderInstancing, bool drawIndirect, ThreadCameraStreamData* _tcsData);

/** Virtual destructor. */
    virtual ~CumulusCloud();

/** Configures the CumulusCloud object, instantiates and positions its voxels, and
   sets up the initial conditions for the cellular automata.

   \param init A CumulusCloudInit structure that contains initialization parameters read
   from the config file.
   \return true if the operation succeeded.
 */
    virtual bool Init(const CumulusCloudInit& init, const Atmosphere& atmosphere, ThreadCameraStreamData* tcsData);

/** Tests if this cloud is visible within the given Frustum.

   \param f The frustum to test visibility against.
   \return true if the cloud is not visible and should be culled; false if it is visible.
 */
    virtual bool IsCulled(const Camera* cullCamera, ThreadCameraStreamData* tcsData) const;

/** Draw the voxels that make up this cloud (or rather, schedule it for later drawing when
   it's sorted against the other translucent objects in the scene.)

   \param pass Set to 0 for the lighting pass, or 1 for the rendering pass.
   \param lightPos The normalized direction toward the dominant infinitely distant light source.
   \param lightColor The color of the dominant light source.
   \param invalid Forces the cloud to recompute its lighting
   \param sky Pointer to the sky object, used to compute the sky color in the cloud's direction for fog.
 */
    virtual bool Draw(int pass, const Vector3& lightPos, const Vector3& lightDir,
        const Color& lightColor, bool invalid, const Sky *sky, bool drawLightning, const Camera* camera
        , ThreadCameraStreamData* tcsData);

/** Performs the actual drawing of the cloud, once the framework has sorted all translucent
   objects in the scene. */
    virtual void DrawBlendedObject(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
        , ThreadCameraStreamData* tcsData) const;

/** Increment the time step of the cellular automata, allowing the cloud to grow. If
   inadequate time has passed to trigger a time increment, just update the voxel densities.
   Smooth growth over time could be implemented by blending the densities between time
   steps, but this is not yet implemented.

   \param now The current timestamp, in milliseconds.
   \param forceUpdate Force the automata to increment, regardless of other conditions.
 */
    virtual bool Update(unsigned long now, bool forceUpdate, const RandomNumberGenerator* rng, const BillboardBufferProvider* provider, ThreadCameraStreamData* tcsData);

/** Cloud growth is influenced by the current wind conditions. Call shear to offset the
   cloud voxels based on the current wind conditions.

   \param updraftSpeed The vertical updraft within the cloud, in meters per second.
   \param wind The current wind vector at the cloud's altitude, in meters per second.
   \param tcsData The ThreadCameraStreamData in question.
 */
    void Shear(double updraftSpeed, const Vector3& wind, ThreadCameraStreamData* tcsData);

/** Retrieves the dimensions of the bounding box this cloud fits within. */
    virtual void GetSize(double& width, double& depth, double& height) const;

/** Implements the cellular automata; called by the Update() method. */
    virtual void IncrementTimeStep(unsigned long now, const RandomNumberGenerator* rng, const BillboardBufferProvider* provider, ThreadCameraStreamData* tcsData) = 0;

/** Saves this cloud's information to a stream, in proprietary binary format. */
    virtual bool Serialize(std::ostream& s, ThreadCameraStreamData* tcsData);

/** Restores this cloud from disk, from our proprietary binary format. */
    virtual bool Unserialize(std::istream& s, ThreadCameraStreamData* tcsData, const RandomNumberGenerator* rng, const BillboardBufferProvider* provider);

/** Exports this cloud as a collection of spheres to a VRML .WRL file. */
    virtual bool ExportToVRML(const char *filename) const;

/** Imports this cloud from a VRML '97 .WRL file or a custom cloud format. Call instead of Init()*/
    virtual bool InitFromFile(const CumulusCloudInit& init, std::istream *s, std::istream *rotStream, bool useVrmlFormat, const Atmosphere* atmosphere, double scale, ThreadCameraStreamData* tcsData);

/** Smoothly fade the cloud in from transparent to opaque */
    virtual void FadeIn(const MillisecondTimer* timer);

/** Smoothly fade the cloud out from opaque to transparent */
    virtual void FadeOut(const MillisecondTimer* timer);

/** Intersect this cloud using a ray originating from a world space origin and direction.
    Return all hits in hit_ranges */
    virtual void Intersect( const Vector3& origin,
                            const Vector3& direction,
                            SL_VECTOR(double)& hit_ranges, ThreadCameraStreamData* tcsData);

/** Return a pointer to the 3D array of individual cloud voxels, and the dimensions of that array. */
    Voxel**** GetVoxels(int& voxelsWidth, int& voxelsDepth, int& voxelsHeight) const
    {
        voxelsWidth = width;
        voxelsDepth = depth;
        voxelsHeight = height;

        return voxels;
    }
    
protected:

/** Sets up the initial conditions for the cellular automata, based on the config-based
   initialization parameters. */
    virtual void InitializeVoxelState(const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData) = 0;

/** As a cloud is lit, adds any active voxels to a list of voxels that must be rendered. */
    void Submit(Voxel *v);

/** Performs the rendering of the cloud, by drawing its component voxels back-to-front
   from the camera. */
    void DrawCloud(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix
        , ThreadCameraStreamData* tcsData) const;

/** Performs a faster cloud shading operation by estimating the cloud to fill
   its bounding ellipsoid. The farther the distance from the point where the
   light source -> voxel ray intersects the ellipsoid, the darker it is.

   \param lightPos The normalized direction vector to the infinitely distant dominant light
   source.
   \param lightColor The color of the dominant light source.
 */
    void ShadeCloud(const Vector3& lightPos, const Color& lightColor, ThreadCameraStreamData* tcsData) const;


    void FreeVoxels();
   
    void MakeVoxel(Voxel *v, int k, const RandomNumberGenerator* rng, const BillboardBuffer& buffer, ThreadCameraStreamData* tcsData);

    int width, height, depth;

    unsigned long lastTimeStep, timeStepInterval;

    Voxel ****voxels;

    double voxelSize;
    Vector3 bounds;

    double vaporProbability, transitionProbability, extinctionProbability;
    double initialVaporProbability;
    double albedo, dropletsPerCubicCm, dropletSize, spinRate;
    float ambientScattering, ambientScatteringHiRes;
    double attenuation, attenuationHiRes;
    double verticalGradient;

    unsigned int fogRefreshSlice;

    unsigned long fadeStartTime;
    unsigned int frameCounter;

    bool lazyVoxelCreation;
    int minimumSpinVoxelHeight;

    bool voxelsUseShaderInstancing;

	float flattening;

	bool perCloudBillboards;
	double screenBlendFactor, boundScale;

    // clouds are tcs specific, so just make this 
    // mutable since DrawBlendedObject is const
    mutable SL_VECTOR(Voxel*) drawList;
    mutable Vector3 lastCameraPos;
    mutable QuadBatches* compiledObject;
    mutable bool voxelsDirty;

    friend class CumulusCloudCompilationOperation;
    mutable CumulusCloudCompilationOperation* compilationParameters;
	
	const bool drawIndirect;

    ThreadCameraStreamData* tcsData;

private:
    void InitFromVRMLFile(SL_VECTOR(Puff)* puffs, std::istream *stream, const Atmosphere* atmosphere, double scale = 1.0);
    void InitFromCloudEditorFile(SL_VECTOR(Puff)* puffs, std::istream *stream, const Atmosphere* atmosphere, double scale = 1.0);
};
}

#endif
