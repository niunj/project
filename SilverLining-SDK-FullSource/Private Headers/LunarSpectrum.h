// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file LunarSpectrum.h
    \brief Models the spectrum of light reflected from the moon.
 */

#ifndef LUNARSPECTRUM_H
#define LUNARSPECTRUM_H

#include "Spectrum.h"

namespace SilverLining
{
/** An implementation of Spectrum that models the spectrum of light emanating from
   the moon. It's a simple approximation that just ramps up linearly from 380-780 nm
   from 0.7 to 1.35 kcd/m2 */
class LunarSpectrum : public Spectrum
{
public:

/** Constructor; populates the array of spectral power based on a model
   of light reflected from the moon. */
    LunarSpectrum();
};
}

#endif
