// Copyright (c) 2004-2022  Sundog Software, LLC. All rights reserved worldwide.

/** \file StratusTextureData.h
 */

#ifndef STRATUS_TEXTURE_DATA_H
#define STRATUS_TEXTURE_DATA_H

#include "SLVector.h"

namespace SilverLining
{
    class StratusTextureData
    {
    public:
        StratusTextureData(int width, int height, bool isInfinite);
        ~StratusTextureData();
    public:
        SL_VECTOR(unsigned char*) scudMap;
        SL_VECTOR(unsigned char*) pixels;
        SL_VECTOR(int) repeatU;
        SL_VECTOR(int) repeatV;
        int scudMapW, scudMapH;
        int texSize;
        int numTextures;
    };
}

#endif