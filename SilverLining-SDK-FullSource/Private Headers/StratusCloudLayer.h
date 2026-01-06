// Copyright (c) 2006-2019 Sundog Software, LLC. All rights reserved worldwide.

/**
    \file StratusCloudLayer.h
    \brief Models a CloudLayer that contains a single StratusCloud.
 */

#ifndef STRATUS_CLOUD_LAYER_H
#define STRATUS_CLOUD_LAYER_H

#include "CloudLayer.h"
#include "StratusCloud.h"
#include <istream>

namespace SilverLining
{
class StratusTextureData;

/** Models a CloudLayer that contains a single StratusCloud. */
class StratusCloudLayer : public CloudLayer
{
public:
/** Default constructor. */
    StratusCloudLayer(const Atmosphere& atmosphere);

/** Virtual destructor. */
    virtual ~StratusCloudLayer();

/** Populates the StratusCloudLayer with its single StratusCloud. */
    bool SILVERLINING_API SeedClouds(const Atmosphere& atm, void* data = 0);

/** Gives the stratus cloud layer an opportunity to tell the Sky object
   that it should render itself with overcast conditions. */
    void SILVERLINING_API ProcessAtmosphericEffects(Sky *sky, const Atmosphere* atmosphere, const Camera* camera, ThreadCameraStreamData* tcsData) const;

/** Returns whether a precipitation type other than NONE will be simulated at the given camera position.
   If you're under a cloud and precipitation has been assigned to this cloud layer using
   SetPrecipitation(), this will return true. The specific effect may be retrieved with
   GetPrecipitation().

   \param x The position, in world coordinates, for which you wish to test for precipitation effects.
   \param y The position, in world coordinates, for which you wish to test for precipitation effects.
   \param z The position, in world coordinates, for which you wish to test for precipitation effects.
 */
    virtual bool SILVERLINING_API HasPrecipitationAtPosition(double x, double y, double z, void* data = 0) const;

    /** Returns whether the given location is inside the bounding box of any of the clouds within this cloud
    layer.

    \param x camera The camera, and hence whose position, in world coordinates, for which you wish to
    test for cloud intersection.

    \param data. experimental. Do not use.
    */
    virtual bool SILVERLINING_API IsInsideCloud(const Camera* camera, void* data = 0, double* distanceInside = 0) const;

/** Returns whether the given location is inside the bounding box of any of the clouds within this cloud
     layer.

   \param x The position, in world coordinates, for which you wish to test for a cloud intersection.
   \param y The position, in world coordinates, for which you wish to test for a cloud intersection.
   \param z The position, in world coordinates, for which you wish to test for a cloud intersection.
 */
    virtual bool SILVERLINING_API IsInsideCloud(double x, double y, double z, void* data = 0, double* distanceInside = 0) const;

    virtual void SILVERLINING_API WrapClouds(bool geocentric, const Camera* camera, CloudLayerTcsUserData* tcsUserData);

    virtual ShaderHandle GetShader(ThreadCameraStreamData* tcsData) const;

    virtual void ReloadShaders(ThreadCameraStreamData* tcsData);
    
    virtual void UpdateOvercast(Sky *s, const Camera* camera, ThreadCameraStreamData* tcsData);
    virtual void GetBounds(Vector3 bounds[8], const Camera* camera, ThreadCameraStreamData* tcsData) const;
	virtual void ClearClouds(void* data = 0);

    virtual StratusTextureData* GetOrCreateTextureData(double density, double edgeTexSizeX, double edgeTexSizeY);
    virtual StratusTextureData* GetTextureData(void);
protected:

    double SILVERLINING_API GetWrapFade(Cloud *c, const Vector3& anchor, bool fadeOverride, CloudLayerTcsUserData* tcsUserData)  const { return 1.0; }
    bool DrawSetup(int pass, const Vector3 *lightPos, const Color *lightColor, ThreadCameraStreamData* tcsData) const;
    bool EndDraw(int pass) const;
    virtual bool RestoreClouds(const Atmosphere& atm, std::istream& s, ThreadCameraStreamData* tcsData);
    virtual void AdjustForCurvature(bool geocentric, const Atmosphere* atmosphere, const Camera* camera, CloudLayerTcsUserData* tcsUserData) {}
    StratusCloud *myCloud;

    StratusTextureData* stratusTextureData;
    double bulginess;
};
}

#endif
