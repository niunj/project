// Copyright (c) 2004-2018  Sundog Software, LLC. All rights reserved worldwide.

#include "Color.h"
#include "Atmosphere.h"

namespace SilverLining
{
void Color::ScaleToUnitOrLess(bool hdr)
{
    if (!hdr) {
        float minC = 0;
        if (r < minC) minC = r;
        if (g < minC) minC = g;
        if (b < minC) minC = b;
        minC = -minC;

        r += minC;
        g += minC;
        b += minC;

        float maxC = r;
        if (g > maxC) maxC = g;
        if (b > maxC) maxC = b;
        if (maxC > 1.0) {
            r /= maxC;
            g /= maxC;
            b /= maxC;
        }
    }

    if (r < 0.0) r = 0.0;
    if (g < 0.0) g = 0.0;
    if (b < 0.0) b = 0.0;
    if (a < 0.0) a = 0.0;
}

void Color::ScaleToValueOrLess(float value, bool hdr)
{
    if (!hdr) {
        float maxC = r;
        if (g > maxC) maxC = g;
        if (b > maxC) maxC = b;

        if (maxC > value) {
            float scale = value / maxC;
            r *= scale;
            g *= scale;
            b *= scale;
        }
    }
}

void Color::ClampToUnitOrLess(bool hdr)
{
    if (!hdr) {
        if (r > 1.0) r = 1.0;
        if (g > 1.0) g = 1.0;
        if (b > 1.0) b = 1.0;
    }
    if (a > 1.0) a = 1.0;

    if (r < 0.0) r = 0.0;
    if (g < 0.0) g = 0.0;
    if (b < 0.0) b = 0.0;
    if (a < 0.0) a = 0.0;
}
}