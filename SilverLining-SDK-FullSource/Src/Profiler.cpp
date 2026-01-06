// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide.

#include "SilverLiningTypes.h"
#include "Profiler.h"
#include "Configuration.h"
#include <memory>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if (!defined(WIN32) && !defined(WIN64))
#include <sys/timeb.h>
#endif

static bool profile = false;

using namespace SilverLining;
using namespace std;

Profiler * Profiler::profiler = 0;

Profiler *Profiler::GetProfiler()
{
    if (!profiler) {
        profiler = SL_NEW Profiler();
    }

    return profiler;
}

void Profiler::Destroy()
{
    if (profiler) {
        SL_DELETE profiler;
    }

    profiler = 0;
}

void Profiler::Display()
{
    if (!profile) return;

    bool doDisplay = false;

#if defined(WIN32) || defined(WIN64)
    DWORD now = timeGetTime();
    doDisplay = (now - lastDisplayTime > displayFrequencyMS);
#else
    struct timeb tp;
    ftime(&tp);
    long elapsed = (1000 * (tp.time - lastDisplaySeconds)) + (tp.millitm - lastDisplayMilliseconds);
    doDisplay = elapsed > (long)displayFrequencyMS;
#endif

    if (doDisplay) {
        SL_ORDERED_MAP(SL_STRING, ProfileEntry) ::iterator it;
        ProfileEntry blank;

        printf("======================================\n");
        for (it = results.begin(); it != results.end(); it++) {
            ProfileEntry entry = (*it).second;
            double ms = (entry.timeTotal / (double)entry.samples) * 1000.0;
            printf("%20s: %5.5f ms\n", ((*it).first).c_str(), ms);
            (*it).second = blank;
        }
        printf("======================================\n");

#if defined(WIN32) || defined(WIN64)
        lastDisplayTime = now;
#else
        lastDisplaySeconds = tp.time;
        lastDisplayMilliseconds = tp.millitm;
#endif
    }
}

void Profiler::AddSample(const SL_STRING& name, double time)
{
    ProfileEntry entry = results[name];
    entry.timeTotal += time;
    entry.samples += 1;
    results[name] = entry;
}

Timer::Timer(const SL_STRING& pName)
{
    static bool configRead = false;
    if (!configRead) {
        Configuration::GetBoolValue("enable-profiling", profile);
        configRead = true;
    }

    if (profile) {
        name = pName;

#if defined(WIN32) || defined(WIN64)
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);
#else
        struct timeb tp;
        ftime(&tp);
        startSeconds = tp.time;
        startMillis = tp.millitm;
#endif
    }
}

Timer::~Timer()
{
    if (profile) {
        double seconds = 0;
#if defined(WIN32) || defined(WIN64)
        QueryPerformanceCounter(&end);
        seconds = (double)(end.QuadPart - start.QuadPart) / (double)frequency.QuadPart;
#else
        struct timeb tp;
        ftime(&tp);
        seconds = (tp.time - startSeconds) + (0.001 * (tp.millitm - startMillis));
#endif

        Profiler::GetProfiler()->AddSample(name, seconds);
    }
}
