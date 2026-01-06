// Copyright (c) 2006-2009 Sundog Software. All rights reserved worldwide.

/**
    \file CumulusMediocrisCloud.h
    \brief An implementation of CumulusCloud that knows how to create smaller,
    puffy cumulus mediocris clouds.
 */

#ifndef CUMULUS_MEDIOCRIS_CLOUD_H
#define CUMULUS_MEDIOCRIS_CLOUD_H

#include "CumulusCloud.h"

namespace SilverLining
{
/** Cumulus Mediocris clouds are smaller, puffy clouds found in fairer weather
   than cumulus congestus. This class embodies the knowledge of how to drive a
   cellular automata that generates clouds that look like this. */
class CumulusMediocrisCloud : public CumulusCloud
{
public:
/** Constructor */
    CumulusMediocrisCloud(CloudLayer *layer, double coverageThreshold, bool voxelsUseShaderInstancing, bool _drawIndirect, ThreadCameraStreamData* _tcsData) : CumulusCloud(layer, coverageThreshold, voxelsUseShaderInstancing, _drawIndirect, _tcsData) {
    }

/** Increments the cellular automata over a given time step, in such
   a manner such that the cloud will grow into something small and puffy. */
    virtual void IncrementTimeStep(unsigned long now, const RandomNumberGenerator* rng, const BillboardBufferProvider* provider, ThreadCameraStreamData* tcsData);

protected:
/** Sets the initial conditions of the cellular automata such that it
   encourages growth of clouds that look like cumulus mediocris clouds. */
    virtual void InitializeVoxelState(const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData);
};
}

#endif
