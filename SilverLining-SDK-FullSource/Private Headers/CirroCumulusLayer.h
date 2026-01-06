// Copyright (c) 2006-2014 Sundog Software, LLC. All rights reserved worldwide.

/** \file CirroCumulusLayer.h
   \brief A class to define a cirrus cloud "layer", which consists of a single large cirrus "cloud".
 */

#ifndef CIRROCUMULUS_CLOUD_LAYER_H
#define CIRROCUMULUS_CLOUD_LAYER_H

#include "CloudLayer.h"
#include "CirroCumulusCloud.h"
#include <istream>

namespace SilverLining
{
/** A "layer" of cirrocumulus clouds. A CirrCumulusCloudLayer contains only a single
   CirroCumulusCloud object. */
class CirroCumulusCloudLayer : public CloudLayer
{
public:
/** Default constructor, just invokes the parent's CloudLayer() constructor. */
    CirroCumulusCloudLayer(const Atmosphere& atmosphere);

/** Default destructor; does nothing. */
    virtual ~CirroCumulusCloudLayer();

/** Creates a single CirroCumulusCloud object to fill the CirroCumulusCloudLayer's specified
   dimensions. */
    bool SILVERLINING_API SeedClouds(const Atmosphere& atm, void* data = 0);

/** Controls fog inside the cloud */
    void SILVERLINING_API ProcessAtmosphericEffects(Sky *sky, const Atmosphere* atmosphere, const Camera* camera, ThreadCameraStreamData* tcsData) const;

    virtual void SILVERLINING_API WrapClouds(bool geocentric, const Camera* camera, CloudLayerTcsUserData* tcsUserData);

    virtual double GetSkyCoverage(const Camera* camera, ThreadCameraStreamData* tcsData) const { return 0; }

    virtual ShaderHandle GetShader(ThreadCameraStreamData* tcsData) const;

    virtual void ReloadShaders(ThreadCameraStreamData* tcsData);

protected:

    double SILVERLINING_API GetWrapFade(Cloud *c, const Vector3& anchor, bool fadeOverride, CloudLayerTcsUserData* tcsUserData)  const { return 1.0; }
    bool DrawSetup(int pass, const Vector3 *lightPos, const Color *lightColor, ThreadCameraStreamData* tcsData) const;
    bool EndDraw(int pass) const;
    virtual bool RestoreClouds(const Atmosphere& atm, std::istream& s, ThreadCameraStreamData* tcsData);
    virtual void AdjustForCurvature(bool geocentric, const Atmosphere* atmosphere, const Camera* camera, CloudLayerTcsUserData* tcsUserData) {}
};
}

#endif
