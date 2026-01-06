// Copyright (c) 2004-2009 Sundog Software. All rights reserved worldwide.

#include "LocalTime.h"
#include "Configuration.h"
#if defined(__INTEL_COMPILER)
#include <mathimf.h>
#else
#include <math.h>
#endif
#if defined(WIN32) || defined(WIN64)
#include <windows.h>
#endif
#include <time.h>

using namespace SilverLining;

LocalTime::LocalTime()
{
    localYear = 2011;
    localMonth = 8;
    localDay = 22;
    localHours = 12;
    localMinutes = 0;
    localSeconds = 0;
    observingDST = true;
    zoneCorrection = -8;

    Configuration::GetIntValue("default-year", localYear);
    Configuration::GetIntValue("default-month", localMonth);
    Configuration::GetIntValue("default-day", localDay);
    Configuration::GetIntValue("default-hour", localHours);
    Configuration::GetIntValue("default-minute", localMinutes);
    Configuration::GetDoubleValue("default-second", localSeconds);
    Configuration::GetBoolValue("default-dst", observingDST);
    Configuration::GetDoubleValue("default-timezone", zoneCorrection);
}

void LocalTime::AddSeconds(long seconds)
{
    struct tm oldDate;
    oldDate.tm_year = localYear - 1900;
    oldDate.tm_mon = localMonth - 1;
    oldDate.tm_mday = localDay;
    oldDate.tm_hour = localHours;
    oldDate.tm_min = localMinutes;
    oldDate.tm_sec = (int)localSeconds;
    oldDate.tm_isdst = -1;

    time_t time = mktime(&oldDate); //mktime assumes local time zone
    time += seconds;

    struct tm *date;
    date = localtime(&time);
    if (date) {
        localYear = date->tm_year + 1900;
        localMonth = date->tm_mon + 1;
        localDay = date->tm_mday;

        localHours = date->tm_hour;
        localMinutes = date->tm_min;
        localSeconds = date->tm_sec;
    }
}

void LocalTime::SetFromEpochSeconds(time_t time)
{
    struct tm *date;
    date = gmtime(&time);
    if (date) {
        localYear = date->tm_year + 1900;
        localMonth = date->tm_mon + 1;
        localDay = date->tm_mday;

        localHours = date->tm_hour;
        localMinutes = date->tm_min;
        localSeconds = date->tm_sec;

        observingDST = date->tm_isdst > 0;

        zoneCorrection = 0;
    }
}

double LocalTime::GetJulianDate(bool terrestrialTime) const
{
    // Convert to GMT
    long double hours = localHours + (localMinutes + localSeconds / 60.0) / 60.0;

    if (observingDST)
        hours -= 1;

    hours -= zoneCorrection;

    long double y, m;
    long double d;
    d = localDay + (hours / 24.0);

    if (localMonth < 3) {
        y = localYear - 1;
        m = localMonth + 12;
    } else {
        y = localYear;
        m = localMonth;
    }

    long double A = floorl(y / 100);
    long double B = 2 - A + floorl(A / 4);
    long double C = floorl(365.25 * y);
    long double D = floorl(30.6001 * (m + 1));

    long double JD = B + C + D + d + 1720994.5;

//    long double JD = 1720996.5 - floor(y / 100) + floor(y / 400) + (365.0 * y)
//                     + floor(30.6001 * (m + 1)) + d;

    if (terrestrialTime) {
        JD += 65.0 / 60.0 / 60.0 / 24.0;
    }

    return (double)JD;
}

double LocalTime::GetEpoch2000Centuries(bool terrestrialTime) const
{
    // Convert to GMT
    long double hours = localHours + (localMinutes + localSeconds / 60.0) / 60.0;

    if (observingDST)
        hours -= 1;

    hours -= zoneCorrection;

    long double y, m;
    long double d, mantissa;
    d = localDay + (hours / 24.0);

    if (localMonth < 3) {
        y = localYear - 1;
        m = localMonth + 12;
    } else {
        y = localYear;
        m = localMonth;
    }

    mantissa = 1720996.5 - floorl(y / 100.0) + floorl(y / 400.0) + floorl(365.25 * y)
               + floorl(30.6001 * (m + 1));
    mantissa -= 2451545.0;

    long double JD = mantissa + d;

    if (terrestrialTime) {
        JD += 65.0 / 60.0 / 60.0 / 24.0;
    }

    JD /= 36525.0;

    return (double)JD;
}

double LocalTime::GetEpoch1990Days(bool terrestrialTime) const
{
    // Convert to GMT
    double hours = localHours + (localMinutes + localSeconds / 60.0) / 60.0;

    if (observingDST)
        hours -= 1;

    hours -= zoneCorrection;

    double y, m;
    double mantissa;
    double d;
    d = localDay + (hours / 24.0);

    if (localMonth < 3) {
        y = localYear - 1;
        m = localMonth + 12;
    } else {
        y = localYear;
        m = localMonth;
    }

    mantissa = 1720996.5 - floor(y / 100.0) + floor(y / 400.0) + floor(365.25 * y)
               + floor(30.6001 * (m + 1));

    mantissa -= 2447891.5;

    double JD = mantissa + d;

    if (terrestrialTime) {
        JD += 65.0 / 60.0 / 60.0 / 24.0;
    }

    return JD;
}

void LocalTime::SetFromSystemTime()
{
#if defined(WIN32) || defined(WIN64)
    SYSTEMTIME sysTime;
    GetLocalTime(&sysTime);

    localYear = sysTime.wYear;
    localMonth = sysTime.wMonth;
    localDay = sysTime.wDay;
    localHours = sysTime.wHour;
    localMinutes = sysTime.wMinute;
    localSeconds = (double)sysTime.wSecond + ((double)sysTime.wMilliseconds / 1000.0);

    TIME_ZONE_INFORMATION tz;
    DWORD result = GetTimeZoneInformation(&tz);
    observingDST = (result == TIME_ZONE_ID_DAYLIGHT);

    zoneCorrection = -(double)tz.Bias / 60.0;
#else
    struct tm *sysTime;
    time_t now = time(NULL);
    sysTime = localtime(&now);

    localYear = sysTime->tm_year + 1900;
    localMonth = sysTime->tm_mon + 1;
    localDay = sysTime->tm_mday;
    localHours = sysTime->tm_hour;
    localMinutes = sysTime->tm_min;
    localSeconds = sysTime->tm_sec;

    zoneCorrection = -(double)sysTime->tm_gmtoff / 3600.0;
    observingDST = (sysTime->tm_isdst == 1);

#endif
}

bool LocalTime::Serialize(std::ostream& s)
{
    s.write((char *)&localYear, sizeof(int));
    s.write((char *)&localMonth, sizeof(int));
    s.write((char *)&localDay, sizeof(int));
    s.write((char *)&localHours, sizeof(int));
    s.write((char *)&localMinutes, sizeof(int));
    s.write((char *)&zoneCorrection, sizeof(double));
    s.write((char *)&localSeconds, sizeof(double));
    s.write((char *)&observingDST, sizeof(bool));

    return true;
}

bool LocalTime::Unserialize(std::istream& s)
{
    s.read((char *)&localYear, sizeof(int));
    s.read((char *)&localMonth, sizeof(int));
    s.read((char *)&localDay, sizeof(int));
    s.read((char *)&localHours, sizeof(int));
    s.read((char *)&localMinutes, sizeof(int));
    s.read((char *)&zoneCorrection, sizeof(double));
    s.read((char *)&localSeconds, sizeof(double));
    s.read((char *)&observingDST, sizeof(bool));

    return true;
}
