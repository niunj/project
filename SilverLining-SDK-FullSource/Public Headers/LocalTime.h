// Copyright (c) 2004-2022 Sundog Software, LLC. All rights reserved worldwide.

/** \file LocalTime.h
   \brief An object to describe the local time, time zone, and daylight savings time observation.
 */

#ifndef LOCALTIME_H
#define LOCALTIME_H

#ifdef SWIG
%module SilverLiningLocalTime
#define SILVERLINING_API
%{
#include "LocalTime.h"
using namespace SilverLining;
%}
#endif

#include "MemAlloc.h"
#include <time.h>
#include <iostream>

/** An enumeration of defined time zones worldwide.
    Time zones are expressed as the hour correction (prior to daylight savings adjustments)
    from GMT. This enum provides names for the civilian time zones, and notes their military
    equivalents.
 */
 namespace SilverLining {
    enum TimeZones
    {
        GMT = 0,   /// ZULU         Greenwich Mean Time, UTC, Western European (WET)
        CET = 1,   /// ALPHA        Central European
        EET = 2,   /// BETA         Eastern European
        BT = 3,   /// CHARLIE      Baghdad Time 
        GET = 4,   /// DELTA        Georgia Standard 
        PKT = 5,   /// ECHO         Pakistan Standard 
        BST = 6,   /// FOXTROT      Bangladesh Standard
        THA = 7,   /// GOLF         Thailand Standard
        CCT = 8,   /// HOTEL        China Coast
        JST = 9,   /// INDIA        Japan Standard
        GST = 10,  /// KILO         Guam Standard
        SBT = 11,  /// LIMA         Solomon Islands
        IDLE = 12,  /// MIKE         International Date Line East, NZST (New Zealand Standard)
        WAT = -1,  /// NOVEMBER     West Africa
        AT = -2,  /// OSCAR        Azores
        BRT = -3,  /// PAPA         Brasilia
        AST = -4,  /// QUEBEC       Atlantic Standard
        EST = -5,  /// ROMEO        Eastern Standard
        CST = -6,  /// SIERRA       Central Standard
        MST = -7,  /// TANGO        Mountain Standard
        PST = -8,  /// UNIFORM      Pacific Standard
        YST = -9,  /// VICTOR       Yukon Standard
        AHST = -10, /// WHISKEY      Alaska-Hawaii Standard; Central Alaska (CAT), Hawaii Std (HST)
        NT = -11, /// X-RAY        Nome
        IDLW = -12  /// YANKEE       International Date Line West
    };
}

#pragma pack(push)
#pragma pack(8)

namespace SilverLining
{
/** A class to represent the simulated local time, time zone, and daylight savings time observation.
   LocalTime objects are passed into AtmosphericConditions::SetTime() to change the simulated
   time of day. Be sure that your Location object represents a location consistent with the
   time zone you have specified as well, unless you choose to use GMT or another non-local time zone
   consistently.
 */
class LocalTime : public MemObject
{
public:
/** Default constructor. Creates a local time with default settings
   (read from the SilverLining.config file if constructed after calling Atmosphere::Initialize()). */
    LocalTime();

/** Destructor. */
    virtual ~LocalTime() {
    }

/** Populates the LocalTime class based on epoch seconds (ie, as returned from
   \c time(NULL) ). Internally, it calls
   \c gmtime() on the value passed in, and populates the class based on the its results.
   The time zone will be set to GMT.

   \param time Seconds elapsed since midnight, January 1, 1970 UTC.
 */
    void SILVERLINING_API SetFromEpochSeconds(time_t time);

/** Set the calendar year to simulate. SilverLining can only handle Gregorian calendar years,
   which means that years before 1582 will not be accurately simulated.
   \sa GetYear()
 */
    void SILVERLINING_API SetYear(int year)                              {
        if (year >= 1582)
            localYear = year;
    }

/** Retrieves the Gregorian calendar year.
   \sa SetYear()
 */
    int SILVERLINING_API GetYear() const {
        return localYear;
    }

/** Sets the calendar month to simulate.
   \param month The month to simulate, ranging from 1 (January) to 12 (December).
   \sa GetMonth();
 */
    void SILVERLINING_API SetMonth(int month)                            {
        if (month > 0 && month <= 12)
            localMonth = month;
    }

/** Retrieves the calendar month (1-12).
   \sa SetMonth()
 */
    int SILVERLINING_API GetMonth() const {
        return localMonth;
    }

/** Sets the day of the month to simulate.
   \param day The day of month, ranging from 1-31.
   \sa GetDay()
 */
    void SILVERLINING_API SetDay(int day)                                {
        if (day > 0 && day <= 31)
            localDay = day;
    }

/** Retrieves the day of the month (1-31).
   \sa SetDay()
 */
    int SILVERLINING_API GetDay() const {
        return localDay;
    }

/** Sets the hour of day to simulate.
   \param hour The hour of day, ranging from 0-23 (0=midnight, 12=noon, 23=11PM)
   \sa GetHour()
 */
    void SILVERLINING_API SetHour(int hour)                              {
        if (hour >= 0 && hour < 24)
            localHours = hour;
    }

/** Retrieves the hour of day (0-23).
   \sa SetHour()
 */
    int SILVERLINING_API GetHour() const {
        return localHours;
    }

/** Sets the minute of the hour to simulate.
   \param minutes Minutes from 0-59
   \sa GetMinutes()
 */
    void SILVERLINING_API SetMinutes(int minutes)                        {
        if (minutes >= 0 && minutes < 60)
            localMinutes = minutes;
    }

/** Retrieves the minute of the hour (0-59).
   \sa SetMinutes()
 */
    int SILVERLINING_API GetMinutes() const {
        return localMinutes;
    }

/** Sets the second of the minute to simulate.
   \param seconds Seconds from 0-59.
   \sa GetSeconds()
 */
    void SILVERLINING_API SetSeconds(double seconds)                     {
        if (seconds >= 0 && seconds < 60)
            localSeconds = seconds;
    }

/** Retrieves the second of the minute (0-59).
   \sa SetSeconds()
 */
    double SILVERLINING_API GetSeconds() const {
        return localSeconds;
    }

/** Sets if Daylight Savings Time is currently observed in the simulation. Not required
   if using SetFromEpochSeconds(), but must be set otherwise.
   \sa GetObservingDaylightSavingsTime()
 */
    void SILVERLINING_API SetObservingDaylightSavingsTime(bool inDST)    {
        observingDST = inDST;
    }

/** Retrieves if daylight savings time is observed in the simulation.
   \sa SetObservingDaylightSavingsTime()
 */
    bool SILVERLINING_API GetObservingDaylightSavingsTime() const {
        return observingDST;
    }

/** Specifies the time zone being simulated. This is an hour offset from GMT, use the
 #TimeZones enumerated type as a convenient way to specify the hour offset for a known
   time zone. Be sure that this time zone is consistent with the Location you specify,
   unless you choose to specify all times in GMT consistently instead of using local time.

   \param zone Hour offset from GMT, ignoring daylight savings time. ie, PST is -8.
    Use the #TimeZones enumeration to obtain the hour offset for specific time
    zones.
   \sa GetTimeZone()
 */
    void SILVERLINING_API SetTimeZone(double zone)                       {
        if (zone >= -12.0 && zone <= 12.0)
            zoneCorrection = zone;
    }

/** Retrieves the currently simulated time zone.
   \return The hour offset from GMT, ignoring daylight savings time.
   \sa SetTimeZone()
 */
    double SILVERLINING_API GetTimeZone() const {
        return zoneCorrection;
    }

/** Retrieves the Julian Date that this LocalTime object represents. Julian Dates are used
   for astronomical calculations (such as our own ephemeris model) and represent
   days and fractions since noon Universal Time on January 1, 4713 BCE on the Julian calendar.
   Note that due to precision limitations of 64-bit doubles, the resolution of the date returned
   may be as low as within 8 hours.

   \param terrestrialTime Specifying terrestrial time means you want atomic clock time, not
         corrected by leap seconds to account for slowing of the Earth's
         rotation, as opposed to GMT which does account for leap seconds.

    \return the Julian date that this object represents.
 */
    double SILVERLINING_API GetJulianDate(bool terrestrialTime) const;

/** Obtains centuries and fraction since January 1, 2000 represented by this object. Used for
   internal astronomical calculations. Since this number is smaller than that returned by
   GetJulianDate(), it is of higher precision.

   \param terrestrialTime Specifying terrestrial time means you want atomic clock time, not
         corrected by leap seconds to account for slowing of the Earth's
         rotation, as opposed to GMT which does account for leap seconds.

    \return The fractional number of centuries elapsed since January 1, 2000.
 */
    double SILVERLINING_API GetEpoch2000Centuries(bool terrestrialTime) const;

/** Obtains days elapsed since January 1, 1990 represented by this object on the Julian
   calendar. Used for internal astronomical calculations. Since this number is smaller than
   that returned by GetJulianDate(), it is of higher precision.

   \param terrestrialTime Specifying terrestrial time means you want atomic clock time, not
         corrected by leap seconds to account for slowing of the Earth's
         rotation, as opposed to GMT which does account for leap seconds.

    \return The fractional number of days elapsed since January 1, 1990.
 */
    double SILVERLINING_API GetEpoch1990Days(bool terrestrialTime) const;

/** Populate the object based on the system's current local time settings. */
    void SILVERLINING_API SetFromSystemTime();

/** Add the given number of seconds to the time represented by this object. */
    void SILVERLINING_API AddSeconds(long seconds);

#ifndef SWIG
/** Flattens this object and everything in it to a stream buffer. */
    bool SILVERLINING_API Serialize(std::ostream& stream);

/** Restores this object from the stream created using Serialize() */
    bool SILVERLINING_API Unserialize(std::istream& stream);
#endif

    bool SILVERLINING_API operator == (const LocalTime& t) const
    {
        return (localSeconds == t.GetSeconds() && localMinutes == t.GetMinutes()
                && localYear == t.GetYear() && localMonth == t.GetMonth() && localDay == t.GetDay()
                && localHours == t.GetHour() && zoneCorrection == t.GetTimeZone()
                && observingDST == t.GetObservingDaylightSavingsTime());
    }

    bool SILVERLINING_API operator != (const LocalTime& t) const
    {
        return (localSeconds != t.GetSeconds() || localMinutes != t.GetMinutes()
                || localYear != t.GetYear() || localMonth != t.GetMonth() || localDay != t.GetDay()
                || localHours != t.GetHour() || zoneCorrection != t.GetTimeZone()
                || observingDST != t.GetObservingDaylightSavingsTime());
    }

    LocalTime& SILVERLINING_API operator = (const LocalTime& t)
    {
        localSeconds = t.GetSeconds();
        localMinutes = t.GetMinutes();
        localYear = t.GetYear();
        localMonth = t.GetMonth();
        localDay = t.GetDay();
        localHours = t.GetHour();
        zoneCorrection = t.GetTimeZone();
        observingDST = t.GetObservingDaylightSavingsTime();

        return *this;
    }

#ifndef SWIG
private:
    int localYear, localMonth, localDay, localHours, localMinutes;
    double zoneCorrection;
    double localSeconds;
    bool observingDST;
#endif
};

inline std::ostream& operator <<
(std::ostream& o, const LocalTime& localTime)
{
   o << localTime.GetHour() << ":" << localTime.GetMinutes() << ":" << localTime.GetSeconds() << ", "
      << localTime.GetMonth() << "/" << localTime.GetDay() << "/" << localTime.GetYear() << ", z: " << localTime.GetTimeZone();
   return o;
}
}

#pragma pack(pop)

#endif
