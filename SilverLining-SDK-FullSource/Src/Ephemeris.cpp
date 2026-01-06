// Copyright (c) 2004-2021  Sundog Software, LLC. All rights reserved worldwide.

#include "Ephemeris.h"
#include "Utils.h"
#include "Renderer.h"
#include "Configuration.h"
#include "MathUtils.h"

using namespace SilverLining;

#define PLANET_DELTA_DAYS (1.0 / 24.0)

//#define PRINT_SUN_POSITION

static void InRange(double &in)
{
    while (in > 2.0 * MathUtils::Pi()) in -= 2.0 * MathUtils::Pi();
    while (in < 0) in += 2.0 * MathUtils::Pi();
}

Ephemeris::Ephemeris()
{
    lastTime.SetYear(0);

    geoZUp = true;
    Configuration::GetBoolValue("geocentric-z-is-up", geoZUp);

    primeMeridianZ = false;
    Configuration::GetBoolValue("z-is-on-prime-meridian", primeMeridianZ );

    flipZ = false;
    Configuration::GetBoolValue("flip-ephemeris-z", flipZ);

    forceUpdate = true;

    forceSun = forceMoon = forceMoonPhase = forceMoonPhaseAngle = false;
    forceSunHoriz = forceMoonHoriz = false;

    for (int i = 0; i < NUM_PLANETS; i++) {
        planets[i].lastEpochDaysCalculated = 0;
    }
}


void Ephemeris::ForceSunPosition(double eclipticLongitude, double eclipticLatitude)
{
    forceSun = true;
    forcedSunLat = eclipticLatitude;
    forcedSunLon = eclipticLongitude;
    forceUpdate = true;
}

void Ephemeris::ForceMoonPosition(double eclipticLongitude, double eclipticLatitude)
{
    forceMoon = true;
    forcedMoonLat = eclipticLatitude;
    forcedMoonLon = eclipticLongitude;
    forceUpdate = true;
}

void Ephemeris::ForceSunPositionHoriz(double altitude, double azimuth)
{
    forcedSunAltitude = altitude;
    forcedSunAzimuth = azimuth;

    forceSunHoriz = true;
    forceUpdate = true;
}

void Ephemeris::ForceMoonPositionHoriz(double altitude, double azimuth)
{
    forcedMoonAltitude = altitude;
    forcedMoonAzimuth = azimuth;

    forceMoonHoriz = true;
    forceUpdate = true;
}

void Ephemeris::ForceMoonPhase(double phase)
{
    forceMoonPhase = true;
    forceMoonPhaseAngle = false;
    forcedPhase = phase;
    forceUpdate = true;
}

void Ephemeris::ForceMoonPhaseAngle(double phaseAngle)
{
    forceMoonPhase = false;
    forceMoonPhaseAngle = true;
    forcedPhaseAngle = phaseAngle;
    forceUpdate = true;
}

void Ephemeris::ClearForcedMoonPhase()
{
    forceMoonPhase = false;
    forceMoonPhaseAngle = false;
}

void Ephemeris::ClearForcedSun()
{
    forceSun = false;
    forceSunHoriz = false;
    forceUpdate = true;
}

void Ephemeris::ClearForcedMoon()
{
    forceMoon = false;
    forceMoonHoriz = false;
    forceUpdate = true;
}

void Ephemeris::Update(const LocalTime& time, const Location& location, bool isRightHanded)
{
    bool timeChanged = false;
    bool locationChanged = false;

    if (time != lastTime) {
        timeChanged = true;
        lastTime = time;
    }

    if (location != lastLocation) {
        locationChanged = true;
        lastLocation = location;
    }

    if (timeChanged || locationChanged || forceUpdate) {
        forceUpdate = false;

        T = time.GetEpoch2000Centuries(true);
        Tuncorr = time.GetEpoch2000Centuries(false);
        epochDays = time.GetEpoch1990Days(false);

        Matrix3 Rx, Ry, Rz;
        Rx.FromRz(0.01118 * T);
        Ry.FromRy(-0.00972 * T);
        Rz.FromRz(0.01118 * T);
        precession = Rx * Ry * Rz;

        GMST = 4.894961 + 230121.675315 * Tuncorr;
        LMST = GMST + MathUtils::ToRadians(location.GetLongitude()); // radians

        double latitude = MathUtils::ToRadians(location.GetLatitude());

        e = 0.409093 - 0.000227 * T;

        Ry.FromRy((latitude - MathUtils::Pi() / 2.0)); // tilt
        Rz.FromRz(-LMST); // spin
        Rx.FromRx(e);  // ecliptic -> equatorial
        equatorialToHorizon = Ry * Rz * precession;
        eclipticToHorizon = Ry * Rz * Rx; // assumes precession already accounted for in eclipitic coords
        eclipticToEquatorial = Rx;

        equatorialToGeographic.FromRz(-GMST);

        geographicToEquatorial = equatorialToGeographic.Transpose();

        horizonToEquatorial = equatorialToHorizon.Transpose();
        horizonToGeographic = equatorialToGeographic * horizonToEquatorial;
        geographicToHorizon = equatorialToHorizon * geographicToEquatorial;
        horizonToEcliptic = eclipticToHorizon.Inverse();

        ComputeSunPosition(isRightHanded);
        ComputeMoonPosition(isRightHanded);
        ComputeEarthPosition(isRightHanded);

        for (int i = 0; i < NUM_PLANETS; i++) {
            if (i != EARTH) {
                ComputePlanetPosition(i);
            }
        }
    }
}

Vector3 Ephemeris::ToCartesian(double r, double latitude, double longitude)
{
    Vector3 v;
    v.x = r * cos(longitude) * cos(latitude); // s
    v.y = r * sin(longitude) * cos(latitude); // e
    v.z = r * sin(latitude); // up

    return v;
}

double Ephemeris::Refract(double elevation)
{
    /*
     *    Refraction correction, degrees
     *        Zimmerman, John C.  1981.  Sun-pointing programs and their
     *            accuracy.
     *            SAND81-0761, Experimental Systems Operation Division 4721,
     *            Sandia National Laboratories, Albuquerque, NM.
     */

    //float prestemp;    /* temporary pressure/temperature correction */
    double refcor;      /* temporary refraction correction */
    double tanelev;     /* tangent of the solar elevation angle */

    /* If the sun is near zenith, the algorithm bombs; refraction near 0 */
    if ( elevation > MathUtils::ToRadians(85.0) )
        refcor = 0.0;

    /* Otherwise, we have refraction */
    else {
        tanelev = tan ( elevation );
        if ( elevation >= MathUtils::ToRadians(5.0) ) {
            refcor  = 58.1 / tanelev -
                      0.07 / ( pow (tanelev,3) ) +
                      0.000086 / ( pow (tanelev,5) );
        } else if ( elevation >= MathUtils::ToRadians(-0.575) ) {
            double degElev = MathUtils::ToDegrees(elevation);
            refcor  = 1735.0 +
                      degElev * ( -518.2 + degElev * ( 103.4 +
                                  degElev * ( -12.79 + degElev * 0.711 ) ) );
        } else {
            refcor  = -20.774 / tanelev;
        }
        //prestemp    =
        //    ( pdat->press * 283.0 ) / ( 1013.0 * ( 273.0 + pdat->temp ) );
        //refcor     *= prestemp / 3600.0;
        refcor /= 3600.0;
    }

    /* Refracted solar elevation angle */
    double out = elevation + MathUtils::ToRadians(refcor);

    return out;
}

Vector3 Ephemeris::ConvertAxes(const Vector3& v, bool flipForLeftHanded, bool isRightHanded) const
{
    // Oriented -x=n z=up y=east
    Vector3 tmp;
    tmp.x = v.y; // x is east
    tmp.y = v.z; // y is up
    tmp.z = v.x; // -z is north

    if (flipZ) {
        tmp.z = -tmp.z;
    }

    if (flipForLeftHanded) {
        if (!isRightHanded) {
            tmp.z = -tmp.z;
        }
    }

    return tmp;
}

Vector3 Ephemeris::InverseConvertAxes(const Vector3& v, bool isRightHanded) const
{
    // In: x=e, y=up, -z=n. Out: -x=n, z=up, y=e
    Vector3 tmp;
    tmp.x = v.z;
    tmp.y = v.x;
    tmp.z = v.y;

    if (flipZ) {
        tmp.x = -tmp.x;
    }

    if (!isRightHanded) {
        tmp.x = -tmp.x;
    }

    return tmp;
}

#ifdef PRINT_SUN_POSITION
void printDMS(double deg)
{
    deg = MathUtils::Abs(deg);
    double degrees = (int)deg;
    double minutes = (int)((deg - degrees) * 60.0);
    double seconds = (deg - (degrees + minutes / 60.0)) * 60.0 * 60.0;

    printf("%f %f' %f''\n", degrees, minutes, seconds);
}
#endif

void Ephemeris::ComputeSunPosition(bool isRightHanded)
{
    double M = 6.24 + 628.302 * T;

    double longitude = 4.895048 + 628.331951 * T + (0.033417 - 0.000084 * T) * sin(M)
                       + 0.000351 * sin(2.0 * M);
    double latitude = 0;
    double geocentricDistance = 1.000140 - (0.016708 - 0.000042 * T) * cos(M) -
                                0.000141 * cos(2.0 * M); // AU's

    sunEclipticLongitude = longitude;

    if (forceSun) {
        latitude = forcedSunLat;
        longitude = forcedSunLon;
    }

    sunEcl = ToCartesian(geocentricDistance, latitude, longitude);

    sunEq = eclipticToEquatorial * sunEcl;

    Vector3 sunGeoTmp = equatorialToGeographic * sunEq;

    if (geoZUp) {
        sunGeo = sunGeoTmp;
    } else {
        if (primeMeridianZ) {
            sunGeo.x = sunGeoTmp.y;
            sunGeo.y = sunGeoTmp.z;
            sunGeo.z = sunGeoTmp.x;
        } else {
            sunGeo.x = sunGeoTmp.x;
            sunGeo.y = sunGeoTmp.z;
            sunGeo.z = -sunGeoTmp.y;
        }
    }

    if (!isRightHanded) {
        sunGeo.z = -sunGeo.z;
    }

#ifdef PRINT_SUN_POSITION
    Vector3 preConv = eclipticToHorizon * sunEcl;
    preConv.Normalize();
    double altRad = MathUtils::Acos(preConv.z);
    //double refraction = Refract(asin(preConv.z));
    //altRad = refraction;
    double altitude = MathUtils::ToDegrees(altRad);
    double azimuth = MathUtils::ToDegrees(atan(preConv.y / preConv.x));

    printDMS(altitude);
    printDMS(azimuth);
#endif

    if (forceSunHoriz) {
        sunHoriz.x = sin(forcedSunAzimuth) * cos(forcedSunAltitude);
        sunHoriz.z = -cos(forcedSunAzimuth) * cos(forcedSunAltitude);
        sunHoriz.y = sin(forcedSunAltitude);
        sunHoriz.Normalize();

        if (geoZUp) {
            sunGeo = horizonToGeographic * InverseConvertAxes(sunHoriz, isRightHanded);
        } else {
            sunGeo = ConvertAxes(horizonToGeographic * InverseConvertAxes(sunHoriz, isRightHanded), true, isRightHanded);
        }

        sunEcl = horizonToEcliptic * InverseConvertAxes(sunHoriz, isRightHanded);
        sunEq = horizonToEquatorial * InverseConvertAxes(sunHoriz, isRightHanded);
    } else {
        sunHoriz = ConvertAxes(eclipticToHorizon * sunEcl, false, isRightHanded);
    }

    bool doRefraction = true;
    Configuration::GetBoolValue("do-refraction", doRefraction);
    if (doRefraction) {
        // Account for atmospheric refraction.
        Vector3 tmp2 = sunHoriz;
        double R = tmp2.Length();
        tmp2.Normalize();
        double elev = asin(tmp2.y);
        if (!forceSun)
            elev = Refract(elev);

        sunHoriz.y = R * sin(elev);
    }
}

void Ephemeris::ComputeMoonPosition(bool isRightHanded)
{
    double lp = 3.8104 + 8399.7091 * T;
    double m = 6.2300 + 628.3019 * T;
    double f = 1.6280 + 8433.4663 * T;
    double mp = 2.3554 + 8328.6911 * T;
    double d = 5.1985 + 7771.3772 * T;

    double longitude =
        lp
        + 0.1098 * sin(mp)
        + 0.0222 * sin(2*d - mp)
        + 0.0115 * sin(2*d)
        + 0.0037 * sin(2*mp)
        - 0.0032 * sin(m)
        - 0.0020 * sin(2*f)
        + 0.0010 * sin(2*d - 2*mp)
        + 0.0010 * sin(2*d - m - mp)
        + 0.0009 * sin(2*d + mp)
        + 0.0008 * sin(2*d - m)
        + 0.0007 * sin(mp - m)
        - 0.0006 * sin(d)
        - 0.0005 * sin(m + mp);

    double latitude =
        +0.0895 * sin(f)
        + 0.0049 * sin(mp + f)
        + 0.0048 * sin(mp - f)
        + 0.0030 * sin(2*d  - f)
        + 0.0010 * sin(2*d + f - mp)
        + 0.0008 * sin(2*d - f - mp)
        + 0.0006 * sin(2*d + f);

    double pip =
        +0.016593
        + 0.000904 * cos(mp)
        + 0.000166 * cos(2*d - mp)
        + 0.000137 * cos(2*d)
        + 0.000049 * cos(2*mp)
        + 0.000015 * cos(2*d + mp)
        + 0.000009 * cos(2*d - m);

    double dMoon = 1.0 / pip; // earth radii

    if (forceMoon) {
        latitude = forcedMoonLat;
        longitude = forcedMoonLon;
    }

    moonEcl = ToCartesian(dMoon, latitude, longitude);

    moonEq = eclipticToEquatorial * moonEcl;

    Vector3 moonGeoTmp = equatorialToGeographic * moonEq;

    if (geoZUp) {
        moonGeo = moonGeoTmp;
    } else {
        if (primeMeridianZ) {
            moonGeo.x = moonGeo.y;
            moonGeo.y = moonGeo.z;
            moonGeo.z = moonGeo.x;
        } else {
            moonGeo.x = moonGeoTmp.x;
            moonGeo.y = moonGeoTmp.z;
            moonGeo.z = -moonGeoTmp.y;
        }
    }

    if (!isRightHanded) {
        moonGeo.z = -moonGeo.z;
    }

    if (forceMoonHoriz) {
        moonHoriz.x = sin(forcedMoonAzimuth) * cos(forcedMoonAltitude);
        moonHoriz.z = -cos(forcedMoonAzimuth) * cos(forcedMoonAltitude);
        moonHoriz.y = sin(forcedMoonAltitude);
        moonHoriz.Normalize();

        if (geoZUp) {
            moonGeo = horizonToGeographic * InverseConvertAxes(moonHoriz, isRightHanded);
        } else {
            moonGeo = ConvertAxes(horizonToGeographic * InverseConvertAxes(moonHoriz, isRightHanded), true, isRightHanded);
        }

        moonEcl = horizonToEcliptic * InverseConvertAxes(moonHoriz, isRightHanded);
        moonEq = horizonToEquatorial * InverseConvertAxes(moonHoriz, isRightHanded);
        moonDistance = 384400.0; // average earth-moon distance
    } else {
        moonHoriz = ConvertAxes(eclipticToHorizon * moonEcl, false, isRightHanded);
        moonDistance = (moonHoriz - Vector3(0, 0, 1)).Length() * 6378.137;
    }

    InRange(longitude);
    InRange(sunEclipticLongitude);
    moonPhaseAngle = longitude - sunEclipticLongitude;
    InRange(moonPhaseAngle);

    moonPhase = 0.5 * (1.0 - cos(moonPhaseAngle));

    if (forceMoonPhase) {
        moonPhase = forcedPhase; // 0 - 1 (new -> full)
        moonPhaseAngle = MathUtils::Acos(1.0 - 2.0 * moonPhase);  // 0-PI  (new -> full)
        moonPhaseAngle = MathUtils::TwoPi() - moonPhaseAngle; // Use back end of moon textures to include new moon
    }

    if (forceMoonPhaseAngle) {
        moonPhaseAngle = forcedPhaseAngle;
        moonPhase = cos(moonPhaseAngle);
    }
}

typedef struct OrbitalElements_S {
    double period;
    double epochLongitude;
    double perihelionLongitude;
    double eccentricity;
    double semiMajorAxis;
    double inclination;
    double longitudeAscendingNode;
    double angularDiameter;
    double visualMagnitude;
} OrbitalElements;

OrbitalElements planetElements[NUM_PLANETS] = {
    // Mercury
    {
        0.240852, MathUtils::ToRadians(60.750646), MathUtils::ToRadians(77.299833), 0.205633, 0.387099,
        MathUtils::ToRadians(7.004540), MathUtils::ToRadians(48.212740), 6.74, -0.42
    },

    // Venus
    {
        0.615211, MathUtils::ToRadians(88.455855), MathUtils::ToRadians(131.430236), 0.006778, 0.723332,
        MathUtils::ToRadians(3.394535), MathUtils::ToRadians(76.589820), 16.92, -4.40
    },

    // Earth
    {
        1.00004, MathUtils::ToRadians(99.403308), MathUtils::ToRadians(102.768413), 0.016713, 1.00000,
        0, 0, 0, 0
    },

    // Mars
    {
        1.880932, MathUtils::ToRadians(240.739474), MathUtils::ToRadians(335.874939), 0.093396, 1.523688,
        MathUtils::ToRadians(1.849736), MathUtils::ToRadians(49.480308), 9.36, -1.52
    },

    // Jupiter
    {
        11.863075, MathUtils::ToRadians(90.638185), MathUtils::ToRadians(14.170747), 0.048482, 5.202561,
        MathUtils::ToRadians(1.303613), MathUtils::ToRadians(100.353142), 196.74, -9.40
    },

    // Saturn
    {
        29.471362, MathUtils::ToRadians(287.690033), MathUtils::ToRadians(92.861407), 0.055581, 9.554747,
        MathUtils::ToRadians(2.488980), MathUtils::ToRadians(113.576139), 165.60, -8.88
    }
};

void Ephemeris::ComputeEarthPosition(bool isRightHanded)
{
    double Np = ( (2.0 * MathUtils::Pi()) / 365.242191 ) * (epochDays / planetElements[EARTH].period);
    InRange(Np);

    double Mp = Np + planetElements[EARTH].epochLongitude - planetElements[EARTH].perihelionLongitude;

    L = Np + 2.0 * planetElements[EARTH].eccentricity * sin(Mp) +
        planetElements[EARTH].epochLongitude;

    InRange(L);

    double vp = L - planetElements[EARTH].perihelionLongitude;

    R = (planetElements[EARTH].semiMajorAxis * (1.0 - planetElements[EARTH].eccentricity
            * planetElements[EARTH].eccentricity)) / (1.0 + planetElements[EARTH].eccentricity
                    * cos(vp));
}

void Ephemeris::GetPlanetPosition(int planet, double& ra, double& dec, double& visualMagnitude) const
{
    if (planet < NUM_PLANETS) {
        ra = planets[planet].rightAscension;
        dec = planets[planet].declination;
        visualMagnitude = planets[planet].visualMagnitude;
    }
}

void Ephemeris::ComputePlanetPosition(int planet)
{
    if ((epochDays - planets[planet].lastEpochDaysCalculated) < PLANET_DELTA_DAYS) {
        return;
    } else {
        planets[planet].lastEpochDaysCalculated = epochDays;
    }

    double Np = ( (2.0 * MathUtils::Pi()) / 365.242191 ) * (epochDays / planetElements[planet].period);
    InRange(Np);

    double Mp = Np + planetElements[planet].epochLongitude - planetElements[planet].perihelionLongitude;

    double heliocentricLongitude = Np + 2.0 * planetElements[planet].eccentricity * sin(Mp) +
                                   planetElements[planet].epochLongitude;

    InRange(heliocentricLongitude);

    double vp = heliocentricLongitude - planetElements[planet].perihelionLongitude;

    double r = (planetElements[planet].semiMajorAxis * (1.0 - planetElements[planet].eccentricity
                * planetElements[planet].eccentricity)) / (1.0 + planetElements[planet].eccentricity
                        * cos(vp));

    double heliocentricLatitude = asin(sin(heliocentricLongitude -
                                           planetElements[planet].longitudeAscendingNode) * sin(planetElements[planet].inclination));

    InRange(heliocentricLatitude);

    double y = sin(heliocentricLongitude - planetElements[planet].longitudeAscendingNode) *
               cos(planetElements[planet].inclination);

    double x = cos(heliocentricLongitude - planetElements[planet].longitudeAscendingNode);

    double projectedHeliocentricLongitude = atan2(y, x) + planetElements[planet].longitudeAscendingNode;

    double projectedRadius = r * cos(heliocentricLatitude);

    double eclipticLongitude;

    if (planet > EARTH) {
        eclipticLongitude = atan( (R * sin(projectedHeliocentricLongitude - L))
                                  / (projectedRadius - R * cos(projectedHeliocentricLongitude - L)) )
                            + projectedHeliocentricLongitude;
    } else {
        eclipticLongitude = MathUtils::Pi() + L + atan( (projectedRadius * sin(L - projectedHeliocentricLongitude))
                            / (R - projectedRadius * cos(L - projectedHeliocentricLongitude)) );
    }

    InRange(eclipticLongitude);

    double eclipticLatitude = atan( (projectedRadius * tan(heliocentricLatitude)
                                     * sin(eclipticLongitude - projectedHeliocentricLongitude))
                                    / (R * sin(projectedHeliocentricLongitude - L)));

    double ra = atan2( (sin(eclipticLongitude) * cos(e) - tan(eclipticLatitude) * sin(e))
                       , cos(eclipticLongitude));

    double dec = asin(sin(eclipticLatitude) * cos(e) + cos(eclipticLatitude) * sin(e)
                      * sin(eclipticLongitude));

    double dist2 = R * R + r * r - 2 * R * r * cos(heliocentricLongitude - L);
    double dist = sqrt(dist2);

    double d = eclipticLongitude - heliocentricLongitude;
    double phase = 0.5 * (1.0 + cos(d));

    double visualMagnitude;

    if (planet == VENUS) {
        d = MathUtils::ToDegrees(d);
        visualMagnitude = -4.34 + 5.0 * log10(r*dist) + 0.013 * d + 4.2E-7  * d*d*d;
    } else {
        visualMagnitude = 5.0 * log10( (r * dist) / sqrt(phase) ) + planetElements[planet].visualMagnitude;
    }

    planets[planet].rightAscension = ra;
    planets[planet].declination = dec;
    planets[planet].visualMagnitude = visualMagnitude;
}
