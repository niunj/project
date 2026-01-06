// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide.

#include "CloudGeneratorExponential.h"
#include "Atmosphere.h"
#include "MathUtils.h"
#include "SLAssert.h"

#if defined(__INTEL_COMPILER)
#include <mathimf.h>
#else
#include <math.h>
#endif

using namespace SilverLining;

CloudGeneratorExponential::CloudGeneratorExponential() : alpha(0), chi(1), epsilon(0), beta(0), nu(1)
{
}

CloudGeneratorExponential::~CloudGeneratorExponential()
{
}

void CloudGeneratorExponential::StartGeneration()
{
    currentN = 0;
    targetN = 0;
    currentD = GetMaximumSize() + epsilon * 0.5;
}

bool CloudGeneratorExponential::GetNextCloud(double& width, double& depth, double& height, const RandomNumberGenerator* rng)
{
    SL_ASSERT(chi > 0);
    SL_ASSERT(GetMaximumSize() > 0);

    while (currentN >= targetN) {
        currentD -= epsilon;

        if (currentD <= GetMinimumSize()) {
            return false;
        }

        currentN = 0;

        double dTargetN = (((2.0 * GetDesiredArea() * epsilon * alpha * alpha * alpha *
                             GetDesiredCoverage()) / (MathUtils::Pi() * chi)) * exp(-alpha * (currentD)));
        targetN = (int)(dTargetN);

        //printf("Creating %d clouds from %5.2f to %5.2f m diameter.\n", targetN,
        //     currentD - epsilon * 0.5, currentD + epsilon * 0.5);
    }

    if (currentD <= GetMinimumSize()) {
        return false;
    }

    // select random diameter within currentD += bandwidth
    double variationW = rng->UniformRandomDouble() * (epsilon);
    double variationH = rng->UniformRandomDouble() * (epsilon);

    width = currentD - epsilon * 0.5 + variationW;
    depth = currentD - epsilon * 0.5 + variationH;

    double D = (width + depth) * 0.5;

    double hOverD = nu * pow(D / GetMaximumSize(), beta);
    height = D * hOverD;

    currentN++;

    return true;
}
