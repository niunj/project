// Copyright (c) 2004-2009 Sundog Software, LLC. All rights reserved worldwide.

/** \file Location.h
   \brief Represents the geographic location being simulated.
 */

#ifndef LOCATION_H
#define LOCATION_H

#ifdef SWIG
%module SilverLiningLocation
#define SILVERLINING_API
%{
#include "Location.h"
using namespace SilverLining;
%}
#endif

#include "MemAlloc.h"
#include <iostream>

#pragma pack(push)
#pragma pack(8)

namespace SilverLining
{
/** An object representing a geographic location. A Location object must be passed to
   AtmosphericConditions::SetLocation() to specify the location you wish to simulate. This
   affects the position of the sun and moon in the sky, which in turn affects your scene's lighting.
   Be sure that your Location is consistent with the time zone specified in LocalTime::SetTimeZone(),
   unless you are choose to set all times as GMT/UTC rather than local time.
 */
class Location : public MemObject
{
public:
/** Default constructor. Creates a Location object with default settings. Defaults will be
   read from the SilverLining.config file if this is constructed after calling
   Atmosphere::Initialize()*/
    Location();

/** Set the simulated latitude.
   \param decimalLatitude The latitude in decimal degrees form (ie, 45.87). North is positive.
                          this value must be in the range of -90 to 90.
   \sa GetLatitude()
 */
    void  SILVERLINING_API SetLatitude(double decimalLatitude)          {
        if (decimalLatitude <= 90.0 && decimalLatitude >= -90.0)
            latitude = decimalLatitude;
    }

/** Retrieves the simulated latitude in decimal degrees form. North is positive.
   \sa SetLatitude()
 */
    double SILVERLINING_API GetLatitude() const {
        return latitude;
    }

/** Set the simulated longitude.
   \param decimalLongitude The longitude in decimal degrees form (ie, -122.78). East is
        positive. This value must be in the range of -180.0 to 180.0.
    \sa GetLongitude()
 */
    void SILVERLINING_API SetLongitude(double decimalLongitude)        {
        if (decimalLongitude >= -180.0 && decimalLongitude <= 180.0)
            longitude = decimalLongitude;
    }

/** Retrieves the simulated longitude in decimal degrees. East is positive.
   \sa SetLongitude()
 */
    double SILVERLINING_API GetLongitude() const {
        return longitude;
    }

/** Sets the simulated eyepoint altitude in meters above mean sea level. This altitude is only used
   for astronomical calculations and thereby doesn't have much of an affect on the simulation.
   Therefore it is not necessary to call SetAltitude() whenever your camera position changes,
   nor is it particularly important to set this to anything other than zero.
   \sa GetAltitude()
 */
    void SILVERLINING_API SetAltitude(double metersAboveMeanSeaLevel)  {
        altitude = metersAboveMeanSeaLevel;
    }

/** Retrives the currently simulated altitude above mean sea level, in meters.
   \sa SetAltitude()
 */
    double SILVERLINING_API GetAltitude() const {
        return altitude;
    }

    bool SILVERLINING_API operator == (const Location& l) const
    {
        return (altitude == l.GetAltitude() && longitude == l.GetLongitude() &&
                latitude == l.GetLatitude());
    }

    bool SILVERLINING_API operator != (const Location& l) const
    {
        return (altitude != l.GetAltitude() || longitude != l.GetLongitude() ||
                latitude != l.GetLatitude());
    }

#ifndef SWIG
/** Flattens this object and everything in it to a stream buffer. */
    bool SILVERLINING_API Serialize(std::ostream& stream);

/** Restores this object from the stream created using Serialize() */
    bool SILVERLINING_API Unserialize(std::istream& stream);

private:
    double latitude, longitude, altitude;
#endif
};
}

#pragma pack(pop)

#endif
