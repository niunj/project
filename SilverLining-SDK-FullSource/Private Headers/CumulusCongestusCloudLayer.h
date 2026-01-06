// Copyright (c) 2006-2009 Sundog Software. All rights reserved worldwide.

/**
    \file CumulusCongestusCloudLayer.h
    \brief A CloudLayer that contains many Cumulus Congestus clouds.
 */

#ifndef CUMULUS_CONGESTUS_CLOUD_LAYER_H
#define CUMULUS_CONGESTUS_CLOUD_LAYER_H

#include "CumulusCloudLayer.h"

namespace SilverLining
{
/** This implementation of CumulusCloudLayer just exists to encapsulate the knowledge
   that it should create CumulusCongestusCloud objects and read configuration from
   keys that begin with "cumulus-congestus". */
class CumulusCongestusCloudLayer : public CumulusCloudLayer
{
public:
/** Default constructor. */
    CumulusCongestusCloudLayer(const Atmosphere& atmosphere);

    virtual ~CumulusCongestusCloudLayer();

protected:
/** Instantiates CumulusCongestusCloud objects to populate the CloudLayer with. */
    CumulusCloud *CreateCloud(CloudLayer *layer, double coverageThreshold, bool voxelsUseShaderInstancing, bool _drawIndirect, ThreadCameraStreamData* tcsData);
};
}

#endif
