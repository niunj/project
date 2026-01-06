// Copyright (c) 2009-2018 Sundog Software LLC, All rights reserved worldwide.

#include "Precipitation.h"
#include "Configuration.h"
#include "Atmosphere.h"
#include "Renderer.h"



namespace SilverLining
{
Precipitation::Precipitation(const Atmosphere* _atmosphere, ThreadCameraStreamData* _tcsData) :
    atmosphere(_atmosphere),
    visibility(0), fogDensity(0), intensity(-1), nearClip(0.5), farClip(8.0), useDepthBuffer(false), writeDepth(false)
    , tcsData(_tcsData)
    , minDt(1.0 / 60.0)
    , maxDt(1.0 / 20.0)
{
    // cache it to avoid repeated gets
    renderer = tcsData->GetRenderer();
    Configuration::GetDoubleValue("precip-min-dt", minDt);
    Configuration::GetDoubleValue("precip-max-dt", maxDt);
}

double Precipitation::GetClampedDt(double dt) const
{
    double clampedDt = dt;
    if (dt > maxDt)
        clampedDt = maxDt;
    if (dt < minDt)
        clampedDt = minDt;
    return clampedDt;
}

}

