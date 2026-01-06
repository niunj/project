// Copyright (c) 2004-2009 Sundog Software. All rights reserved worldwide.

#include "Location.h"
#include "Configuration.h"
#include "Atmosphere.h"

using namespace SilverLining;

Location::Location()
{
    latitude = 30;
    longitude = -122;
    altitude = 100;

    Configuration::GetDoubleValue("default-latitude", latitude);
    Configuration::GetDoubleValue("default-longitude", longitude);
    Configuration::GetDoubleValue("default-altitude", altitude);
    altitude *= Atmosphere::GetUnitScale();
}

bool Location::Serialize(std::ostream& s)
{
    s.write((char *)&latitude, sizeof(double));
    s.write((char *)&longitude, sizeof(double));
    s.write((char *)&altitude, sizeof(double));

    return true;
}

bool Location::Unserialize(std::istream& s)
{
    s.read((char *)&latitude, sizeof(double));
    s.read((char *)&longitude, sizeof(double));
    s.read((char *)&altitude, sizeof(double));

    return true;
}
