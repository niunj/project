// Copyright (c) 2004-2013  Sundog Software, LLC. All rights reserved worldwide.

#include "Cloud.h"
#include "Configuration.h"
#include "MathUtils.h"
#include "Camera.h"
#include "CloudRenderingParameters.h"

#if SILVERLINING_TRACK_CLOUDS
#include "Mutex.h"
#endif

namespace SilverLining
{

#if SILVERLINING_TRACK_CLOUDS
MutexContainer Cloud::countMutexContainer;
int Cloud::numClouds = 0;
#endif

Cloud::Cloud(CloudLayer *parentLayer, double _coverageThreshold) : fade(1.0), alpha(1.0), parentCloudLayer(parentLayer),
    invalid(false), needsRelight(false), fadeState(FADED_IN), needsPlacement(true)
{
    coverageThreshold = _coverageThreshold;

#if SILVERLINING_TRACK_CLOUDS
    ScopedMutex scopedMutex(countMutexContainer.mutex);
    id = numClouds;
    ++numClouds;
    std::cout << "num of clouds: " << numClouds << std::endl;
#endif
}

Cloud::~Cloud()
{
#if SILVERLINING_TRACK_CLOUDS
    ScopedMutex scopedMutex(countMutexContainer.mutex);
    --numClouds;
    std::cout << "num of clouds: " << numClouds << std::endl;
#endif
}

void Cloud::UpdateLightPos(const Vector3& lightPos)
{
    lastLightPos = lightPos;

    lastCloudPos = centerPos;
}

bool Cloud::LightingChanged(const Vector3& lightPos, ThreadCameraStreamData* tcsData) const
{
    bool changed = lightPos.Dot(lastLightPos) < tcsData->GetCloudRenderingParams()->GetCosLightChangeAngle();
    return changed;
}

void Cloud::Invalidate(ThreadCameraStreamData* tcsData)
{
    invalid = true;
}

bool Cloud::IsInvalid(ThreadCameraStreamData* tcsData) const
{
    return invalid;
}
}

