// Copyright (c) 2006-2012 Sundog Software, LLC. All rights reserved worldwide.

/** \file LightningListener.h
   \brief A virtual base class for receiving lightning strike events in your application.
 */

#ifndef LIGHTNINGLISTENER_H
#define LIGHTNINGLISTENER_H

#ifdef SWIG
%module SilverLiningLightningListener
#define SILVERLINING_API
%{
#include "LightningListener.h"
using namespace SilverLining;
%}
#endif

#include "MemAlloc.h"

#pragma pack(push)
#pragma pack(8)

namespace SilverLining {
/** Extend this class in order to receive notifications of lightning strike events from
   cumulonimbus cloud layers. */
class LightningListener
{
public:
/** Virtual destructor. */
    virtual ~LightningListener() {
    };

/** This pure virtual method will be called any time a lightning strike begins within a CloudLayer.
   You must pass your LightningListener object's pointer into CloudLayer::AddLightningListener() in order
   to receive lightning events from a given CloudLayer. Presently, only CumulonimbusCloudLayer objects
   will fire these events. The position returned is the location of the lightning strike's origin, in
   world units. */
    virtual void SILVERLINING_API LightningStartedEvent(double lightningPosX, double lightningPosY, double lightningPosZ) = 0;
};
}

#pragma pack(pop)

#endif
