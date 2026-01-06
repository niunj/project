// Copyright (c) 2014 Sundog Software, LLC. All rights reserved worldwide.

#include "CloudGeneratorRandom.h"
#include "MathUtils.h"
#include "SLAssert.h"
#include "Atmosphere.h"

#if defined(__INTEL_COMPILER)
#include <mathimf.h>
#else
#include <math.h>
#endif

using namespace SilverLining;

CloudGeneratorRandom::CloudGeneratorRandom() : beta(0), nu(1)
{
}

CloudGeneratorRandom::~CloudGeneratorRandom()
{
}

void CloudGeneratorRandom::StartGeneration()
{
    areaCovered = 0;
}

bool CloudGeneratorRandom::GetNextCloud(double& width, double& depth, double& height, const RandomNumberGenerator* rng)
{
    SL_ASSERT(GetMaximumSize() > 0);

    double range = GetMaximumSize() - GetMinimumSize();
    width = rng->UniformRandomDouble() * range + GetMinimumSize();
    depth = rng->UniformRandomDouble() * range + GetMinimumSize();

    double D = (width + depth) * 0.5;

    double hOverD = nu * pow(D / GetMaximumSize(), beta);
    height = D * hOverD;

    areaCovered += (width * 0.5) * (depth * 0.5) * MathUtils::Pi();

    double currentDensity = areaCovered / GetDesiredArea();
    if (currentDensity >= GetDesiredCoverage()) {
        return false;
    } else {
        return true;
    }
}
