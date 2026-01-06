// Copyright 2006-2922 Sundog Software, LLC. All rights reserved worldwide.

#include "StratusTextureData.h"
#include "StratusSections.h"

namespace SilverLining
{
StratusTextureData::StratusTextureData(int width, int height, bool isInfinite)
    : scudMapW(width)
    , scudMapH(height)
{
    texSize = scudMapW * scudMapH * 2;

    numTextures = isInfinite ? 1 : NUM_SECTIONS;

    for (int texNum = 0; texNum < numTextures; texNum++) {
        scudMap.push_back(SL_NEW unsigned char[scudMapW * scudMapH]);
        pixels.push_back(SL_NEW unsigned char[texSize]);
        repeatU.push_back(1);
        repeatV.push_back(1);
    }
}

StratusTextureData::~StratusTextureData()
{
    for (int i = 0; i < numTextures; ++i) {
        if (scudMap[i]) {
            SL_DELETE[] scudMap[i];
        }
        if (pixels[i]) {
            SL_DELETE[] pixels[i];
        }
    }
}
}
