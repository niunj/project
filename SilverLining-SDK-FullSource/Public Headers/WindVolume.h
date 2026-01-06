// Copyright (c) 2004-2012 Sundog Software, LLC. All rights reserved worldwide.

/** \file WindVolume.h
   \brief Defines an area of a given wind speed and direction.
 */

#ifndef WINDVOLUME_H
#define WINDVOLUME_H

#ifdef SWIG
%module SilverLiningWindVolume
#define SILVERLINING_API
%{
#include "WindVolume.h"
using namespace SilverLining;
%}
#endif

#include "MemAlloc.h"
#include <iostream>

#pragma pack(push)
#pragma pack(8)

namespace SilverLining
{
/** Defines an area of a given wind velocity and direction bounded by two altitudes.
   Passed into AtmosphericConditions::SetWind() to define wind that will affect cloud
   motion. */
class WindVolume : public MemObject
{
public:

/** Default constructor. Creates a WindVolume with default settings of no wind from
   zero to 100,000 meters above mean sea level. */
    WindVolume() : minAltitude(0), maxAltitude(100000), windSpeed(0), direction(0) {
    }

/** Destructor. */
    virtual ~WindVolume() {
    }

/** Set the minimum altitude affected by this object's wind settings.
   \param metersMSL The minimum altitude of this WindVolume in meters above mean sea level.
   \sa GetMinAltitude()
 */
    void SILVERLINING_API SetMinAltitude(double metersMSL)         {
        minAltitude = metersMSL;
    }

/** Retrieves the minimum altitude, in meters above mean sea level, affected by this object.
   \sa SetMinAltitude()
 */
    double SILVERLINING_API GetMinAltitude() const {
        return minAltitude;
    }

/** Set the maximum altitude affected by this object's wind settings.
   \param metersMSL The maximum altitude of this WindVolume in meters above mean sea level.
   \sa GetMaxAltitude()
 */
    void SILVERLINING_API SetMaxAltitude(double metersMSL)         {
        maxAltitude = metersMSL;
    }

/** Retrieves the maximum altitude, in meters above mean sea level, affected by this object.
   \sa SetMaxAltitude()
 */
    double SILVERLINING_API GetMaxAltitude() const {
        return maxAltitude;
    }

/** Set the wind velocity within this WindVolume, in meters per second.
   \sa GetWindSpeed()
 */
    void SILVERLINING_API SetWindSpeed(double metersPerSecond)     {
        windSpeed = metersPerSecond;
    }

/** Retrieves the wind velocity within this WindVolume, in meters per second.
   \sa SetWindSpeed()
 */
    double SILVERLINING_API GetWindSpeed() const {
        return windSpeed;
    }

/** Sets the wind direction, in degrees East from North. This is the direction the wind is
   coming from, not the direction it is blowing toward.
   \sa GetDirection()
 */
    void SILVERLINING_API SetDirection(double degreesFromNorth)    {
        direction = degreesFromNorth;
    }

/** Retrieves the wind direction, in degrees East from North. This is the direction the wind
   is blowing toward, not the direction it is coming from.
   \sa SetDirection()
 */
    double SILVERLINING_API GetDirection() const {
        return direction;
    }

/** Evaluates if a given altitude is affected by this WindVolume.
   \param metersMSL The altitude to query on, in meters above mean sea level.
   \return True if the given altitude is affected by this WindVolume object.
 */
    bool SILVERLINING_API Inside(double metersMSL) const
    {
        return (metersMSL >= minAltitude && metersMSL < maxAltitude);
    }

#ifndef SWIG
/** Flattens this object and everything in it to a stream buffer. */
    bool SILVERLINING_API Serialize(std::ostream& stream)
    {
        stream.write((char *)&minAltitude, sizeof(double));
        stream.write((char *)&maxAltitude, sizeof(double));
        stream.write((char *)&windSpeed, sizeof(double));
        stream.write((char *)&direction, sizeof(double));
        return true;
    }

/** Restores this object from the stream created using Serialize() */
    bool SILVERLINING_API Unserialize(std::istream& stream)
    {
        stream.read((char *)&minAltitude, sizeof(double));
        stream.read((char *)&maxAltitude, sizeof(double));
        stream.read((char *)&windSpeed, sizeof(double));
        stream.read((char *)&direction, sizeof(double));
        return true;
    }

private:
    double minAltitude, maxAltitude;
    double windSpeed, direction;
#endif
};
}

#pragma pack(pop)

#endif //WINDVOLUME_H
