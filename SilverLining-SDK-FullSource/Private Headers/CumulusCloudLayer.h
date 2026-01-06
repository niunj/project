// Copyright (c) 2004-2017  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file CumulusCloudLayer.h
    \brief A CloudLayer that contains many individual cumulus clouds, and optionally
    generates a shadow map from these clouds.
 */

#ifndef CUMULUS_CLOUD_LAYER_H
#define CUMULUS_CLOUD_LAYER_H

#include "CloudLayer.h"
#include "CumulusCloud.h"
#include "Renderer.h"
#include <string>
#include <vector>
#include <iostream>

namespace SilverLining
{
class CloudGenerator;
class CloudDistributor;
class CumulusCloudLayerTcsUserData;

/** A collection of CumulusClouds that fill the CloudLayer's volume of space. */
class CumulusCloudLayer : public CloudLayer
{
public:
/** Default constructor. */
    CumulusCloudLayer(const Atmosphere& atmosphere);

/** Virtual destructor. */
    virtual ~CumulusCloudLayer();

/** Instantiates a CloudGenerator and CloudDistributor to fill the CloudLayer
   with clouds, up to the specified coverage amount. */
    virtual bool SILVERLINING_API SeedClouds(const Atmosphere& atm, void* data = 0);

  /** Controls cloud animation effects over time. This only affect cumulus cloud types.
        \param voxelSpinRate For cumulus clouds, the maximum rate at which individual cloud voxels rotate to
                             simulate convection in the cloud. Specified in radians per second. Set to 0 for
                             no animation.
        \param enableGrowth  Controls whether the shape of the cloud itself evolves over time, via cellular
                             automata techniques. This can cause clouds to "grow" over time, but comes with a
                             performance cost. Only affects cumulus congestus clouds.
        \param initialGrowthIterations If enableGrowth is true, this control the initial number of iterations
                             of the cullular automata to run to form each cloud prior to the first frame.
                             If you leave this set to zero, the default vaule for the specific cloud type will
                             be used, resulting in a fully-formed cloud at the outset. However, you could set this
                             as low as 1, resulting in clouds growing over time from very small clouds.
        \param timeStepInterval The time between cellular automata iterations, in seconds. Changes will be interpolated
                                across this time. Longer times will result in slower cloud formation. To use the
                                default settings for this cloud type, use a value of 0.
    */
    virtual void SILVERLINING_API SetCloudAnimationEffects(double voxelSpinRate, bool enableGrowth, 
        int initialGrowthIterations = 0, int timeStepInterval = 0);

    /** Retrieves the maximum rate at which individual cloud voxels rotate, in radians per second.
        \sa SetCloudAnimationEffects()
    */
    double SILVERLINING_API GetSpinRate() const {
        return spinRate;
    }

    /** Retrieves whether cloud growth over time is simulated at runtime.
        \sa SetCloudAnimationEffects()
    */
    bool SILVERLINING_API GetGrowthEnabled() const {
        return growth;
    }

    virtual bool SILVERLINING_API AddCloudAt(const Atmosphere& atm, const Vector3& relativePosition, const Vector3& dimensions, void* data = 0);
    virtual bool SILVERLINING_API SupportsAddCloudAt(void* data = 0) const { return true; }

    virtual double SILVERLINING_API GetMaxHeight() const { return maxHeight; }
	virtual double SILVERLINING_API GetMaxSize() const { return maxSize; }

    // Internal use only:
    virtual void SILVERLINING_API  MoveClouds(double x, double y, double z, ThreadCameraStreamData* tcsData);

    // Internal use only:
    virtual TextureHandle GetCustomAtlasTexture(ThreadCameraStreamData* tcsData) const;
    
    // Deprecated:
    virtual bool SILVERLINING_API SupportsShadowMaps();
    virtual void SILVERLINING_API GenerateShadowMaps(bool enable);
    virtual bool SILVERLINING_API BindShadowMap(int textureStage, double *m, ThreadCameraStreamData* tcsData) const;

protected:

/** Specific implementations of CumulusCloudLayer will need to create specific Cloud
   types, and must implement this method to create a cloud to put within the CloudLayer. */
    virtual CumulusCloud* CreateCloud(CloudLayer *layer, double coverageThreshold, bool voxelsUseShaderInstancing, bool _drawIndirect, ThreadCameraStreamData* tcsData) = 0;

/** Configures the CumulusCloudLayer based on config entries with the given prefix.
   By convention, this matches the cloud type in question (ie, "cumulus-congestus".) */
    void ReadConfiguration(const SL_STRING& configPrefix);

/** Sets up rendering states, projection matrices, and rendering contexts appropriate
   to the pass that are common to all clouds drawn in this layer. Must be balanced by a call
   to EndDraw() after the clouds are drawn.

   \param pass Set to 0 for the lighting pass, or 1 for the rendering pass.
   \param lightPos A normalized direction vector toward the infinitely distant dominant
   light source.
   \param lightColor The color of the dominant light source.
   \return true if the operation succeeded.
 */
    bool DrawSetup(int pass, const Vector3 *lightPos, const Color *lightColor, ThreadCameraStreamData* tcsData) const;

/** After all the clouds have been drawn, call EndDraw() to restore the rendering states
   prior to calling DrawSetup().

   \param pass Set to 0 for the lighting pass, or 1 for the rendering pass.
   \return true if the operation succeeded.
 */
    bool EndDraw(int pass) const;

/** Saves the individual clouds for a future run. */
    virtual bool SaveClouds(std::ostream& s, ThreadCameraStreamData* tcsData) const;

/** Restore the individual clouds from a previously saved run. */
    virtual bool RestoreClouds(const Atmosphere& atm, std::istream& s, ThreadCameraStreamData* tcsData);

/** Allocates the shadow texture. */
    void SetupSurfaces(ThreadCameraStreamData* tcsData);

// Catalogs the cloud files available to choose from (if any)
    void ScanCloudFiles(const Atmosphere& atm);

// Creates a cloud from a random cloud file. Assumes ScanCloudFiles was called previously.
    bool CreateCloudFromFile(const Atmosphere& atm, double scaleTo, ThreadCameraStreamData* tcsData);

    void InitCoverageMap();
    void DeleteCoverageMap();
    void AddToCoverage(Cloud *cloud, ThreadCameraStreamData* tcsData);
    float GetCoverage();
    void CreateCloud(const Atmosphere& atm, double cloudWidth, double cloudDepth, double cloudHeight, ThreadCameraStreamData* tcsData);
    CumulusCloudLayerTcsUserData* GetOrCreateCumulusCloudLayerTcsUserData(ThreadCameraStreamData* data) const;

    bool *coverageMap;
    int totalCoverageCells;
    int cellsCovered;
    int coverageW, coverageL;

    CumulusCloudInit parameters;

    CloudGenerator *generator;
    CloudDistributor *distributor;

    Vector3 shadowMapElapsedWind;

    bool makeShadowMaps;
    bool renderLightingPassOffscreen;
    
    Matrix4 lightProjView;

    Vector3 shadowMapPos;

    bool restoreBackBuffer;
    char *savedBackBuffer;

    SL_STRING cloudTypeStr;
    SL_VECTOR(SL_STRING) cloudFiles;

    int vpx, vpy, vpw, vph, shadowTexDim;

    double spinRate, maxHeight, maxSize;

    bool useVrmlCloudFormat;

    SL_STRING atlasName;
};
}

#endif
