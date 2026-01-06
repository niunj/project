// Copyright (c) 2006-2009 Sundog Software, LLC. All rights reserved worldwide.

/**
    \file CumulonimbusCloudLayer.h
    \brief A cloud layer that contains a single thunderhead.
 */

#ifndef CUMULONIMBUS_CLOUD_LAYER_H
#define CUMULONIMBUS_CLOUD_LAYER_H

#include "CumulusCloudLayer.h"

namespace SilverLining
{
/** A CloudLayer object that contains a single, large cumulonimbus cloud. */
class CumulonimbusCloudLayer : public CumulusCloudLayer
{
public:
/** Default constructor. */
    CumulonimbusCloudLayer(const Atmosphere& atmosphere);

/** Instantiates a single cumulonimbus cloud within the volume defined by
   this cloud layer. */
    bool SILVERLINING_API SeedClouds(const Atmosphere& atm, void* data = 0);

/** Forces a lightning strike starting with this frame on cloud layers that support lightning. */
    virtual bool SILVERLINING_API ForceLightning(bool value, void* data = 0);
    virtual void SILVERLINING_API SetLightningDischargeMode(LightningDischargeMode mode, void * data = 0);
    virtual LightningDischargeMode SILVERLINING_API GetLightningDischargeMode();

    // Don't allow infinite cumulonimbus clouds
    virtual void SILVERLINING_API SetIsInfinite(bool inf) {
        isInfinite = false;
    }

    virtual void SILVERLINING_API SetCloudWrapping(bool wrap) {
        cloudWrap = false;
    }

    virtual double GetSkyCoverage(const Camera* camera, ThreadCameraStreamData* tcsData) const { return 0; }
    virtual void SILVERLINING_API SetEnabled(bool enabled, unsigned long fadeTimeMS = 0, void* data = 0, bool _enabledInTcsData = true);
    virtual void SILVERLINING_API SetBaseAltitude(double meters, bool updateCloudPositions = true, void* data = 0) const;

protected:
    virtual CumulusCloud* CreateCloud(CloudLayer *layer, double coverageTreshold, bool voxelsUseShaderInstancing, bool _drawIndirect, ThreadCameraStreamData* tcsData);
    virtual void AdjustForCurvature(bool geocentric, const Atmosphere* atmosphere, const Camera* camera, CloudLayerTcsUserData* tcsUserData) {}
    LightningDischargeMode dischargeMode;
};
}

#endif
