// Copyright (c) 2007-2009 Sundog Software, LLC. All rights reserved worldwide.

#include "MillisecondTimer.h"
#include "Utils.h"

using namespace SilverLining;

unsigned long MillisecondTimer::GetMilliseconds() const
{
    return getMilliseconds();
}
