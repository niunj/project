// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file CumulusMediocrisCloudLayer.h
    \brief A cloud layer that instantiates CumulusMediocrisCloud objects.
 */
#ifndef CUMULUS_MEDIOCRIS_CLOUD_LAYER_H
#define CUMULUS_MEDIOCRIS_CLOUD_LAYER_H

#include "CumulusCloudLayer.h"

namespace SilverLining
{
/** This implementation of CumulusCloudLayer just encapsulates the knowledge that
   it should create CumulusMediocrisCloud objects to fill the layer, and read its configuration
   from keys that begin with "cumulus-mediocris".
 */
class CumulusMediocrisCloudLayer : public CumulusCloudLayer
{
public:
/** Default constructor; reads configuration from cumulus-mediocris- keys. */
    CumulusMediocrisCloudLayer(const Atmosphere& atmosphere);

    virtual ~CumulusMediocrisCloudLayer() {
    }

protected:
/** Instantiates CumulusMediocrisCloud objects to fill the layer with. */
    CumulusCloud *CreateCloud(CloudLayer *layer, double coverageTreshold, bool voxelsUseShaderInstancing, bool _drawIndirect, ThreadCameraStreamData* tcsData);
};
}

#endif
