// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide.

#include "LunarSpectrum.h"
#include "Utils.h"

using namespace SilverLining;

LunarSpectrum::LunarSpectrum()
{
    int i;

    double minLuminance = 0.7;
    double maxLuminance = 1.35;

    double total = 0;
    for (i = 0; i < NSAMPLES; i++) {
        double a = (double)i / (double)NSAMPLES;
        powers[i] = minLuminance * (1.0 - a) + maxLuminance * a;
        total += powers[i];
    }

    // Normalize
    for (i = 0; i < NSAMPLES; i++) {
        powers[i] /= total;
    }
}
