// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file Spectrum.h
    \brief A class that models a full spectrum of electromagnetic energy
 */
#ifndef SPECTRUM_H
#define SPECTRUM_H

#include "MemAlloc.h"
#include "Vector3.h"
#include "SilverLiningTypes.h"

namespace SilverLining
{

#define NSAMPLES 81
#define SAMPLEWIDTH 5

/** Models a full spectrum of electromagnetic energy. Derived classes must populate
   the powers[] array in their constructors. This class also models the passage
   of this energy through the atmosphere under given conditions, and converts the
   spectrum to XYZ color data. Limited to the visible range of 380-780 nm.

   For a description of some of the ideas behind this class, see

   Bird, Richard E., Riordan, Carol
   Simple Solar Spectral Model for Direct and Diffuse Irradiance on Horizontal and Tilted
   Planes at the Earth's Surface for Cloudless Atmospheres
   Journal of Applied Meteorology 1986 25: 87-97
 */
class Spectrum : public MemObject
{
public:
/** Default constructor; derived classes should populate the extraterrestrial
   spectral data here. */
    Spectrum() {
    }

/** Virtual destructor. */
    virtual ~Spectrum() {
    }

/** Converts the spectrum to XYZ color information. */
    Vector3 ToXYZ();

/** Simulates the passage of this spectrum through Earth's atmosphere, employing
   the National Renewable Energy Lab's "Bird model" (see the reference in the class description.)
   Two new spectra, representing the direct and scattered irradiance resulting from passage
   through the atmosphere, are returned.

   \param zenithAngle The angle between the zenith and the direction of the light source emitting
   the simulated spectrum.
   \param cosZenith The cosine of the zenith angle.
   \param T The simulated atmospheric turbidity (see AtmosphericConditions::SetTurbidity())
   \param altitude The simulated altitude in meters above mean sea level.
   \param directIrradiance A reference to a Spectrum object that will receive the spectral
   energy directly from the light source that survives transmission through the atmosphere.
   \param scatteredIrradiance A reference to a Spectrum object that will receive the spectral
   energy scattered by the atmosphere, which makes up "skylight."
 */
    void ApplyAtmosphericTransmittance(double zenithAngle, double cosZenith, double T,
                                       double altitude, Spectrum& directIrradiance, Spectrum& scatteredIrradiance, SkyModel skyModel);

/** Multiplies two Spectrums together, by multiplying the spectral powers at each
   wavelength sample. */
    Spectrum operator * (const Spectrum& s);

/** The array of spectral powers, from 380 - 780 nm sampled at 5 nm intervals. */
    double powers[NSAMPLES]; // 380 - 780 nm @ 5nm intervals
};
}

#endif
