// Copyright (c) 2006-2014 Sundog Software, LLC. All rights reserved worldwide.

/** \file CirrusCloudLayer.h
   \brief A class to define a cirrus cloud "layer", which consists of a single large cirrus "cloud".
 */

#ifndef CIRRUS_CLOUD_LAYER_H
#define CIRRUS_CLOUD_LAYER_H

#include "CloudLayer.h"
#include "CirrusCloud.h"
#include <istream>

namespace SilverLining
{
/** A "layer" of cirrus clouds. A CirrusCloudLayer contains only a single
   CirrusCloud object. */
class CirrusCloudLayer : public CloudLayer
{
public:
/** Default constructor, just invokes the parent's CloudLayer() constructor. */
    CirrusCloudLayer(const Atmosphere& atmosphere);

/** Default destructor; does nothing. */
    virtual ~CirrusCloudLayer();

/** Creates a single CirrusCloud object to fill the CirrusCloudLayer's specified
   dimensions. */
    bool SILVERLINING_API SeedClouds(const Atmosphere& atm, void* data = 0);

/** Controls fog inside the cloud */
    void SILVERLINING_API ProcessAtmosphericEffects(Sky *sky, const Atmosphere* atmosphere, const Camera* camera, ThreadCameraStreamData* tcsData) const;

    virtual void SILVERLINING_API WrapClouds(bool geocentric, const Camera* camera, CloudLayerTcsUserData* tcsUserData);

    virtual double GetSkyCoverage(const Camera* camera, ThreadCameraStreamData* tcsData) const { return 0; }

    virtual ShaderHandle GetShader(ThreadCameraStreamData* tcsData) const;

    //! do not use directly
    virtual void ReloadShaders(ThreadCameraStreamData* tcsData);

protected:

    double SILVERLINING_API GetWrapFade(Cloud *c, const Vector3& anchor, bool fadeOverride, CloudLayerTcsUserData* tcsUserData)  const { return 1.0; }
    // over-ridden
    bool DrawSetup(int pass, const Vector3 *lightPos, const Color *lightColor, ThreadCameraStreamData* tcsData) const;
    bool EndDraw(int pass) const;
    virtual bool RestoreClouds(const Atmosphere& atm, std::istream& s, ThreadCameraStreamData* tcsData);
    virtual void AdjustForCurvature(bool geocentric, const Atmosphere* atmosphere, const Camera* camera, CloudLayerTcsUserData* tcsUserData) {}
};
}

#endif
