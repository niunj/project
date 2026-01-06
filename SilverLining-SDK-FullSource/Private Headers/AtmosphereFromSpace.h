// Copyright (c) 2007-2016 Sundog Software, LLC. All rights reserved worldwide.

/** \file AtmosphereFromSpace.h
   \brief Creates and maintains a blended ring surrounding the Earth that simulates
     the atmosphere viewed from space.
 */

#ifndef ATMOSPHERE_FROM_SPACE_H
#define ATMOSPHERE_FROM_SPACE_H

#include "MemAlloc.h"
#include "Renderer.h"
#include "Frustum.h"
#include "Vector3.h"
#include "AtmosphereSegment.h"
#include <vector>

namespace SilverLining
{
class Atmosphere;
class ThreadCameraStreamData;
class AtmosphereFromSpace : public MemObject
{
public:
    AtmosphereFromSpace(const Atmosphere& atmosphere, ThreadCameraStreamData* _tcsData);
    virtual ~AtmosphereFromSpace();

    void BuildGeometry();

    bool Draw(const Vector3& sunPositionGeocentric, const Vector3& earthCenter,
              double altitude, const Atmosphere* atmosphere, const Camera* camera
              , ThreadCameraStreamData* tcsData);

    float GetAtmosphereThickness() const {
        return atmosphereThickness;
    }

    ShaderHandle GetShader(ThreadCameraStreamData* tcsData) const;

private:
    float earthRadius, atmosphereThickness, atmosphereHeight, atmosphereBrightness;
    float atmosphereFadeDistance;
    int lateralSegments, verticalSegments, numSegments;
    SL_VECTOR(AtmosphereSegment *) segments;
    bool enableShader, doShadow;

    const Atmosphere& atmosphere;
    ThreadCameraStreamData* tcsData;
};
}

#endif
