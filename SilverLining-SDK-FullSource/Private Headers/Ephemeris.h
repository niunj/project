// Copyright (c) 2004-2021  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file Ephemeris.h
    \brief Computes the location of astronomical objects in the sky.
 */

#ifndef EPHEMERIS_H
#define EPHEMERIS_H

#include "MemAlloc.h"
#include "LocalTime.h"
#include "Location.h"
#include "Matrix3.h"

namespace SilverLining
{
/** An enumeration of planets that are visible to the naked eye. */
enum Planets
{
    MERCURY = 0,
    VENUS,
    EARTH,
    MARS,
    JUPITER,
    SATURN,
    NUM_PLANETS
};

/** A structure to store the calculated position and magnitude of a given planet. */
typedef struct Planet_S
{
    double lastEpochDaysCalculated;
    double rightAscension;
    double declination;
    double visualMagnitude;
} Planet;

class ThreadCameraStreamData;

/** This class computes the positions of the sun and moon in the sky, and the
   positions and magnitudes of visible planets as well. It also provides methods for
   converting between ecliptic, equatorial, and horizon coordinates. */
class Ephemeris : public MemObject
{
public:
/** Default constructor. */
    Ephemeris();

/** Recomputes the sun and moon positions based on the time and location
   passed in. Planet positions are cached, but will be recomputed if the time
   differs by more than one hour from the previous call to Update(). */
    void Update(const LocalTime& time, const Location& location, bool isRightHanded);

/** Force explicit override of the sun position. */
    void ForceSunPosition(double eclipticLongitude, double eclipticLatitude);

/** For explicit override of the moon position. */
    void ForceMoonPosition(double eclipticLongitude, double eclipticLatitude);

/** Force explicit override of the sun position. */
    void ForceSunPositionHoriz(double altitude, double azimuth);

/** For explicit override of the moon position and phase. */
    void ForceMoonPositionHoriz(double altitude, double azimuth);

/** For explicit override of the moon phase. */
    void ForceMoonPhase(double phase);

/** For explicit override of the moon phase angle (and phase.) */
    void ForceMoonPhaseAngle(double phaseAngle);

/** Clear explicit override of the moon phase and/or phase angle*/
    void ClearForcedMoonPhase();

/** Clear any forced sun position, and resume computing it based on time & location. */
    void ClearForcedSun();

/** Clear any forced moon position, and resume computing it based on time & location. */
    void ClearForcedMoon();

/** Returns the position of the sun in equatorial coordinates. Requires that
   Update() was previously called. */
    Vector3 GetSunPositionEquatorial() const {
        return sunEq;
    }

/** Returns the position of the moon in equatorial coordinates. Requires that
   Update() was previously called. */
    Vector3 GetMoonPositionEquatorial() const {
        return moonEq;
    }

/** Returns the position of the sun in geographic coordinates. Requires that
   Update() was previously called. */
    Vector3 GetSunPositionGeographic() const {
        return sunGeo;
    }

/** Returns the position of the moon in geographic coordinates. Requires that
   Update() was previously called. */
    Vector3 GetMoonPositionGeographic() const {
        return moonGeo;
    }


/** Returns the position of the sun in ecliptic coordinates. Requires that
   Update() was previously called. */
    Vector3 GetSunPositionEcliptic() const {
        return sunEcl;
    }

/** Returns the position of the moon in ecliptic coordinates. Requires that
   Update() was previously called. */
    Vector3 GetMoonPositionEcliptic() const {
        return moonEcl;
    }

/** Returns the position of the sun in horizon coordinates. Requires that
   Update() was previously called. */
    Vector3 GetSunPositionHorizon() const {
        return sunHoriz;
    }

/** Returns the position of the moon in horizon coordiantes. Requires that
   Update() was previously called. */
    Vector3 GetMoonPositionHorizon() const {
        return moonHoriz;
    }

/** Retrieves the "phase angle" of the moon. A full moon has a phase angle of 0
   degrees; a new moon has a phase angle of 180 degrees. Requires that Update()
   was previously called. */
    double GetMoonPhaseAngle() const {
        return moonPhaseAngle;
    }

/** Retrieves the phase of the moon in terms of percentage of the moon that is
   illuminated. Ranges from 0 (new moon) to 1.0 (full moon.) Requires that Update()
   was previously called. */
    double GetMoonPhase() const {
        return moonPhase;
    }

/** Retrieves the distance between the Earth and the Moon in kilometers. Useful
   for determining the brightness of the moon, in conjunction with the moon's phase.
   Requires that Update() was previously called. */
    double GetMoonDistanceKM() const {
        return moonDistance;
    }

/** Retrieves the horizon coordinates and visual magnitude of the visible planets.
   Requires that Update() was previously called.

   \param planet A member of the Planets enumeration specifying which planet you which to
   obtain the position of (excluding the Earth).
   \param ra The Right Ascension of the planet requested.
   \param dec The declination of the planet requested.
   \param visualMagnitude The visual magnitude of the planet requested (remember lower values
   are brighter)
 */
    void GetPlanetPosition(int planet, double& ra, double& dec, double& visualMagnitude) const;

/** Retrieves a 3x3 matrix that will transform ecliptic coordinates to horizon coordinates.
   Requires that Update() was previously called. */
    Matrix3 GetEclipticToHorizonMatrix() const {
        return eclipticToHorizon;
    }

/** Retrieves a 3x3 matrix that will transform equatorial coordinates (x through
   the vernal equinox) to geographic coordinates (x through the prime meridian.) */
    Matrix3 GetEquatorialToGeographicMatrix() const {
        return equatorialToGeographic;
    }

/** Retrieves a 3x3 matrix that will transform equatorial coordinates to horizon
   coordinates. Requires that Update() was previously called. */
    Matrix3 GetEquatorialToHorizonMatrix() const {
        return equatorialToHorizon;
    }

/** Retrieves a 3x3 matrix to transform horizon coordinates to equatorial
   coordinates. Requires that Update() was previously called. */
    Matrix3 GetHorizonToEquatorialMatrix() const {
        return horizonToEquatorial;
    }

/** Retrieves a 3x3 matrix to transform horizon coordinates to geocentric
   coordinates. Requires that Update() was previously called. */
    Matrix3 GetHorizonToGeographicMatrix() const {
        return horizonToGeographic;
    }

/** Retrieves a 3x3 matrix to transform geographic coordinates to horizon
   coordinates. Requires that Update() was previously called. */
    Matrix3 GetGeographicToHorizonMatrix() const {
        return geographicToHorizon;
    }

/** Returns the fractional number of centuries elapsed since January 1, 2000 GMT,
   terrestrial time (this is "atomic clock time," which does not account for leap seconds
   to correct for slowing of the Earth's rotation). */
    double GetEpochCenturies() const {
        return T;
    }

private:
/** Calculates the position of the sun; called by Update(). */
    void ComputeSunPosition(bool isRightHanded);

/** Calculates the position of the Earth; called by Update(). This must be called
   prior to calling ComputePlanetPosition(). */
    void ComputeEarthPosition(bool isRightHanded);

/** Calculates the position of the moon; called by Update(). */
    void ComputeMoonPosition(bool isRightHanded);

/** Computes the position of a given planet from the Planets enumeration. Called
   by Update(). Requires that ComputeEarthPosition() was previously called. */
    void ComputePlanetPosition(int planet);

/** Models atmospheric refraction for objects close to the horizon. This does
   not model variations in atmosphere pressure and temperature.

   \param elevation The elevation of the object above the horizon, in radians.
   \return The apparent elevation of the object above the horizon, in radians,
   after simulating atmospheric refraction.
 */
    double Refract(double elevation);

/** Converts polar coordinates to cartesian coordinates. */
    Vector3 ToCartesian(double r, double latitude, double longitude);

/** Converts to our local coordinate system. */
    Vector3 ConvertAxes(const Vector3& v, bool flipForLeftHanded, bool isRightHanded) const;

    Vector3 InverseConvertAxes(const Vector3& v, bool isRightHanded) const;

    Matrix3 equatorialToHorizon;
    Matrix3 eclipticToHorizon;
    Matrix3 eclipticToEquatorial;
    Matrix3 equatorialToGeographic;
    Matrix3 horizonToEquatorial;
    Matrix3 horizonToGeographic;
    Matrix3 geographicToEquatorial;
    Matrix3 geographicToHorizon;
    Matrix3 horizonToEcliptic;

    double sunEclipticLongitude;
    double moonPhase, moonPhaseAngle, moonDistance;
    double GMST, LMST;
    Matrix3 precession;

    double T, Tuncorr, epochDays;
    Vector3 moonEq, sunEq;
    Vector3 moonEcl, sunEcl;
    Vector3 sunHoriz, moonHoriz;
    Vector3 moonGeo, sunGeo;

    bool forceSun, forceMoon, forceMoonPhase, forceMoonPhaseAngle;
    bool forceSunHoriz, forceMoonHoriz;
    double forcedMoonLat, forcedMoonLon, forcedSunLat, forcedSunLon, forcedPhase, forcedPhaseAngle;
    double forcedMoonAltitude, forcedMoonAzimuth, forcedSunAltitude, forcedSunAzimuth;

    bool geoZUp;
	bool primeMeridianZ;
    bool flipZ;

    double R, L, e;

    LocalTime lastTime;
    Location lastLocation;
    bool forceUpdate;
    Planet planets[NUM_PLANETS];
};
}

#endif
