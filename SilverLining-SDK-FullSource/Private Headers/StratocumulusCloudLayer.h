// Copyright (c) 2010-2020  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file StratocumulusCloudLayer.h
    \brief A CloudLayer that contains many individual low, puffy clouds that together make for a thick
    cloud layer with a few sun breaks. Unlike a broken stratus cloud layer, this layer is rendered volumetrically.
 */

#ifndef STRATOCUMULUS_CLOUD_LAYER_H
#define STRATOCUMULUS_CLOUD_LAYER_H

#include "CloudLayer.h"
#include "Renderable.h"
#include "Renderer.h"
#include "StratocumulusShaderConstantLocations.h"
#include <string>
#include <vector>
#include <iostream>

namespace SilverLining
{
class RandomNumberGenerator;
class StratocumulusCloudLayerTcsUserData;
class TextureManager;


class StratocumulusVoxel
{
public:
    StratocumulusVoxel() : states(0), extinctionProbability(1.0), phaseTransitionProbability(0), vaporProbability(0) {
    }

    unsigned char states;
    float extinctionProbability, phaseTransitionProbability, vaporProbability;
};

/** A collection of cellular-automata-grown puffy clouds within a volume rendered as volumetric slices. */
class StratocumulusCloudLayer : public CloudLayer, public Renderable
{
public:
/** Default constructor. */
    StratocumulusCloudLayer(const Atmosphere& atmosphere);

/** Virtual destructor. */
    virtual ~StratocumulusCloudLayer();

/** Instantiates a CloudGenerator and CloudDistributor to fill the CloudLayer
   with clouds, up to the specified coverage amount. */
    virtual bool SILVERLINING_API SeedClouds(const Atmosphere& atm, void* data = 0);

    virtual bool SILVERLINING_API Draw(int pass, const Vector3 *lightPos, const Color *lightColor,
        bool invalid, bool wantsLightingUpdate, unsigned long now, const Sky *sky, bool drawLightning, const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
        , ThreadCameraStreamData* tcsData) const;

    virtual void DrawBlendedObject(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
        , ThreadCameraStreamData* tcsData) const;

    virtual Vector3 GetWorldPosition(const ThreadCameraStreamData* tcsData) const;

/** Flattens this object and everything in it to a stream buffer. */
    virtual bool SILVERLINING_API Serialize(std::ostream& stream, void* data = 0);

/** Restores this object from the stream created using Serialize() */
    virtual bool SILVERLINING_API Unserialize(const Atmosphere& atm, std::istream& stream, void* data = 0);

/** Returns whether a precipitation type other than NONE will be simulated at the given camera position.
   If you're under a cloud and precipitation has been assigned to this cloud layer using
   SetPrecipitation(), this will return true. The specific effect may be retrieved with
   GetPrecipitation().

   \param camera The camera, and hence whose position, in world coordinates, for which you wish to
   test for precipitation effects.
 */
    virtual bool SILVERLINING_API HasPrecipitationAtPosition(const Camera* camera, void* data = 0) const;

/** Returns whether the given location is inside the bounding box of any of the clouds within this cloud
    layer.

   \param x camera The camera, and hence whose position, in world coordinates, for which you wish to
   test for cloud intersection.

   \param data. experimental. Do not use.
 */
    virtual bool SILVERLINING_API IsInsideCloud(const Camera* camera, void* data = 0, double* distanceInside = 0) const;

    /** Returns whether the given location is inside the bounding box of any of the clouds within this cloud
    layer.

    \param data. experimental. Do not use.
    */
    virtual bool SILVERLINING_API IsInsideCloud(double x, double y, double z, void* data = 0, double* distanceInside = 0) const;

    /** We implement our own SetBaseWidth so we can enforce the upper bound set in stratocumulus-max-size. */
    virtual void SILVERLINING_API SetBaseWidth(double meters);

    /** We implement our own SetBaseLength so we can enforce the upper bound set in stratocumulus-max-size. */
    virtual void SILVERLINING_API SetBaseLength(double meters);

    virtual ShaderHandle GetShader(ThreadCameraStreamData* tcsData) const;

    virtual void ReloadShaders(ThreadCameraStreamData* tcsData);

    virtual void ClearClouds(void* data = 0);

protected:

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

    virtual bool SaveClouds(std::ostream& s, ThreadCameraStreamData* tcsData) const {
        return true;
    }
    virtual bool RestoreClouds(const Atmosphere& atm, std::istream& s, ThreadCameraStreamData* tcsData) {
        return true;
    }

    virtual Vector3 GetSortPosition(const Camera* camera, const ThreadCameraStreamData* tcsData) const;

    virtual double GetDistance(const Vector3& from, const Vector3& to, const Matrix4& mv, bool rightHanded, const Camera* camera, ThreadCameraStreamData* tcsData) const;

    void ComputeBoundingBox(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenCameraPos& overriddenCameraPos, ThreadCameraStreamData* tcsData) const;
    void ComputeTexCoords(Vertex *v, const Atmosphere * atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenCameraPos& overriddenCameraPos, ThreadCameraStreamData* tcsData) const;
    void UploadVolumeTexture(const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData);
    void SubUploadVolumeTexture(ThreadCameraStreamData* tcsData) const;

	void UploadSDF(ThreadCameraStreamData* tcsData);
    void IncrementGrowth(const RandomNumberGenerator* rng);
    void FreeVoxels();
    void TrimExcessVoxels();
    void CreateCloud(double width, double depth, double height, const Vector3& layerWorldPosition, const Atmosphere& atm, ThreadCameraStreamData* tcsData);
	void InitCoverageMap();
	void DeleteCoverageMap();
    void AddToCoverage(Cloud *cloud, ThreadCameraStreamData* tcsData);
	float GetCoverage();
    Vector3 ApplyRoundedEdges(CurveModes curveMode) const;
    virtual void AdjustForCurvature(bool geocentric, const Atmosphere* atmosphere, const Camera* camera, CloudLayerTcsUserData* tcsUserData) {}
	virtual bool CullLayerOnly() const { return true; }

    // Compute lighting volume
    // Updates cloud tex data
    // returns slice min and slice max of what (sub) data was modified
    int ComputeVolumeLighting(int & updatedSliceMin, int & updatedSliceMax, ThreadCameraStreamData* tcsData) const;

    virtual void SILVERLINING_API MoveClouds(double x, double y, double z, ThreadCameraStreamData* tcsData);

    StratocumulusCloudLayerTcsUserData* GetOrCreateTcsUserData(ThreadCameraStreamData* data) const;

    double voxelDimension, lightSamplingDistance, multipleScatteringTerm;
    double dropletSize, ambientScattering, dropletsPerCubicCm, albedo, jitter, inscatteringTerm;
    double extinctionProbability, transitionProbability, vaporProbability;
    int initialEvolve;
    Vector3 cloudMotion, noiseOffset;
    double maxSize, fadeFalloff, lightExp;

    int voxelWidth, voxelDepth, voxelHeight;    

	bool *coverageMap;
	int totalCoverageCells;
	int cellsCovered;
	int coverageW, coverageL;

    StratocumulusVoxel ****voxels;
    float* sdfData;


    double  volumeLightingVectorFullRefreshTolerance;
    double  volumeLightingVectorSliceRefreshTolerance;

    bool updateCloudsWithWind;
};
}

#endif
