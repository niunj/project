// Copyright (c) 2004-2014  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file Utils.h
    \brief General-purpose math and time macros and functions.
 */
#ifndef UTILS_H
#define UTILS_H

#include "Atmosphere.h"
#include "Billboard.h"

#if defined(WIN32) || defined(WIN64)
#include <windows.h>
#else
#include <sys/timeb.h>
#endif
#if defined(__INTEL_COMPILER)
#include <mathimf.h>
#else
#include <math.h>
#endif
#include <stdlib.h>

#include <sstream>
#include <locale>

// Convert watts/m2 to cd/m2
// wm2 * 683 lux / 1wm2 * 1 nit / 3.14 lux
#define NITS(irradiance) (irradiance * 683.0 / 3.14)

#define METERS(x)   ((x) * 0.3048)
#define FEET(x)     ((x) * 3.2808)

#define SLMAX(a,b)            (((a) > (b)) ? (a) : (b))
#define SLMIN(a,b)            (((a) < (b)) ? (a) : (b))


/** Returns the number of milliseconds elapsed. The time elapsed from may vary depending
   on the system, so this should only be used for relative time measurements. */
inline unsigned long getMilliseconds()
{
#if defined(WIN32) || defined(WIN64)
    unsigned long nowMS = timeGetTime();
    static unsigned long startTime = 0;
    if (startTime == 0) startTime = nowMS;
    return nowMS - startTime;
#else
    struct timeb tp;
    ftime(&tp);
    static time_t startTime = 0;
    if (startTime == 0) startTime = tp.time;
    return (((tp.time - startTime) * 1000) + tp.millitm);
#endif
}

/** Returns a very rough estimate (only one iteration) of the square root of a number.
   But it's really fast. */
inline double FastSquareRoot(double number) {
#if defined(WIN32) || defined(WIN64)
    long i;
    float x, y;
    const float f = 1.5F;

    float fnumber = (float)number;

    x = fnumber * 0.5F;
    y  = fnumber;
    i  = *( long * ) &y;
    i  = 0x5f3759df - ( i >> 1 );
    y  = *( float * ) &i;
    y  = y * ( f - ( x * y * y ) );
    // y  = y * ( f - ( x * y * y ) );
    return number * y;
#else
    // Sadly, GCC's strict aliasing rules don't allow these shenanigans.
    return sqrt(number);
#endif
}

inline double LocaleSafeAtoF(const char *str)
{
#if (_MSC_VER >= 1400)
    _locale_t l = _create_locale(LC_NUMERIC, "C");
    double result = _atof_l(str, l);
    _free_locale(l);
    return result;
#else
    double result;
    std::istringstream s(str);
    std::locale l("C");
    s.imbue(l);
    s >> result;
    return result;
#endif
}

/** 3D Perlin noise function. */
float Noise3(float vec[3]);

#endif
