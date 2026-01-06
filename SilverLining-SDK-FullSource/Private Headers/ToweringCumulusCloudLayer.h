// Copyright (c) 2014 Sundog Software. All rights reserved worldwide.

/**
    \file ToweringCumulusCloudLayer.h
    \brief A CloudLayer that contains many Towering Cumulus clouds.
 */

#ifndef TOWERING_CUMULUS_CLOUD_LAYER_H
#define TOWERING_CUMULUS_CLOUD_LAYER_H

#include "CumulusCloudLayer.h"

namespace SilverLining
{
/** This implementation of CumulusCloudLayer just exists to encapsulate the knowledge
   that it should create ToweringCumulusCloud objects and read configuration from
   keys that begin with "towering-cumulus". */
class ToweringCumulusCloudLayer : public CumulusCloudLayer
{
public:
/** Default constructor. */
    ToweringCumulusCloudLayer(const Atmosphere& atmosphere);

protected:
/** Instantiates ToweringCumulusCloud objects to populate the CloudLayer with. */
    CumulusCloud *CreateCloud(CloudLayer *layer, double coverageTreshold, bool voxelsUseShaderInstancing, bool _drawIndirect, ThreadCameraStreamData* tcsData);
};
}

#endif
