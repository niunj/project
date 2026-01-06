// Copyright (c) 2006-2014 Sundog Software, LLC. All rights reserved worldwide.

/** \file CirroStratusLayer.h
   \brief A class to define a cirrus cloud "layer", which consists of a single large cirrus "cloud".
 */

#ifndef CIRROSTRATUS_CLOUD_LAYER_H
#define CIRROSTRATUS_CLOUD_LAYER_H

#include "CloudLayer.h"
#include "CirroStratusCloud.h"
#include <istream>

namespace SilverLining
{
/** A "layer" of cirrostratus clouds. A CirroStratusCloudLayer contains only a single
   CirroStratusCloud object. */
class CirroStratusCloudLayer : public CloudLayer
{
public:
/** Default constructor, just invokes the parent's CloudLayer() constructor. */
   CirroStratusCloudLayer(const Atmosphere& atmosphere);

/** Default destructor; does nothing. */
    virtual ~CirroStratusCloudLayer();

/** Creates a single CirroStratusCloud object to fill the CirroStratusCloudLayer's specified
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
