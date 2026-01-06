// Copyright (c) 2004-2018  Sundog Software, LLC. All rights reserved worldwide.

/** \file CloudLayer.h
\brief Methods for configuration and initializing cloud decks.
*/

#ifndef CLOUDLAYERSERIALIZATIONUTILS_H
#define CLOUDLAYERSERIALIZATIONUTILS_H

#include <iostream>

namespace SilverLining
{
    class CloudLayer;
    class ThreadCameraStreamData;
    class CloudLayerSerializationUtils
    {
    public:
        static void Serialize(std::ostream& s, const CloudLayer& cloudLayer, ThreadCameraStreamData* tcsData);
        static bool UnSerialize(std::istream& s, CloudLayer& cloudLayer, ThreadCameraStreamData* tcsData);
    };
}

#endif