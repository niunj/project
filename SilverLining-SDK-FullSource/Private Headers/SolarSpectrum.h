// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file SolarSpectrum.h
    \brief Encodes the extraterrestrial solar spectrum
 */

#ifndef SOLARSPECTRUM_H
#define SOLARSPECTRUM_H

#include "Spectrum.h"

namespace SilverLining
{
/** Encodes the extraterrestrial solar spectrum, based on NASA data. */
class SolarSpectrum : public Spectrum
{
public:
/** Default constructor; populates the Spectrum object with data for the sun. */
    SolarSpectrum();
};
}

#endif
