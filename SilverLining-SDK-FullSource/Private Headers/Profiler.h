// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file Profiler.h
    \brief Provides simple methods for measuring the performance of blocks of code.
 */
#ifndef PROFILER_H
#define PROFILER_H

#if defined(WIN32) || defined(WIN64)
#pragma warning (disable: 4786)
#endif

#if defined(WIN32) || defined(WIN64)
#include <windows.h>
#else
#include <sys/timeb.h>
#endif

#include "MemAlloc.h"
#include <string>
#include <map>

namespace SilverLining
{
/** A named block of code whose performance is measured over one or more iterations. */
class ProfileEntry : public MemObject
{
public:
/** Default constructor, intializes the time spent and number of samples to zero. */
    ProfileEntry() : timeTotal(0), samples(0) {
    }

    double timeTotal;
    unsigned long samples;
};

/** Manages a collection of named ProfileEntry objects, and displays their results
   to stdout on demand. */
class Profiler : public MemObject
{
public:
/** The Profiler is a singleton, use this method to access its one instance. */
    static Profiler *GetProfiler();

/** Destroys the static singleton instance of the profiler. */
    static void Destroy();

/** Displays the current average timings of all named timers that have been
   submitted to this Profiler to stdout. */
    void Display();

/** Adds a timing sample for a given named block of code to the Profiler. Typically,
   this will come from the results of a Timer object.

   \param name The name of the sample, which is used to collate timings for the same block
   of code. The name must be unique.
   \param time The time spent in this block of code, in microseconds.
 */
    void AddSample(const SL_STRING& name, double time);

protected:
    Profiler() : displayFrequencyMS(5000), lastDisplayTime(0) {
    }

private:
    static Profiler *profiler;

    unsigned long displayFrequencyMS;
    unsigned long lastDisplayTime;
    time_t lastDisplaySeconds;
    unsigned short lastDisplayMilliseconds;
    SL_ORDERED_MAP(SL_STRING, ProfileEntry) results;
};

/** This class provides an easy way to time how many microseconds are spent
   in a given section of code, and submit the information to the Profiler. Just
   create a Timer object on the stack, constructing it with a unique name, and
   when it goes out of scope, it will submit its timing information to the Profiler.
 */
class Timer
{
public:
/** Must be constructed with a unique name. Will start the high-precision timer.*/
    Timer(const SL_STRING& name);

/** The destructor will stop the timer and submit the results to the Profiler. */
    ~Timer();

private:
#if defined(WIN32) || defined(WIN64)
    LARGE_INTEGER start, end, frequency;
#else
    time_t startSeconds;
    unsigned short startMillis;
#endif
    SL_STRING name;
};

}

#endif
