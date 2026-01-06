// Copyright (c) 2004-2013  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file Color.h
    \brief A class that defines an RGBA color and operations on it.
 */

#ifndef COLOR_H
#define COLOR_H

#include "Vector3.h"
#include "Vector4.h"
#include <stdio.h>
#include <iostream>

#pragma pack(push)
#pragma pack(8)

namespace SilverLining
{
/** A RGBA color, where each component is represented as a float from 0-1. */
class Color : public MemObject
{
public:
/** Default constructor, initializes to black with no translucency. */
    Color() : r(0), g(0), b(0), a(1) {
    }

/** Constructor that takes in RGBA single-precision values that range from 0-1. */
    Color(float red, float green, float blue, float alpha) : r(red), g(green),
        b(blue), a(alpha) {
    }

/** Constructor that takes in RGB single-precision values that range from 0-1.
   The alpha component is assumed to be 1.0. */
    Color(float red, float green, float blue) : r(red), g(green), b(blue), a(1) {
    }

/** Constructor that takes in RGBA double-precision values that range from 0-1. */
    Color(double red, double green, double blue, double alpha) : r((float)red),
        g((float)green), b((float)blue), a((float)alpha) {
    }

/** Constructor that takes in RGB double-precision values that range from 0-1.
   The alpha component is assumed to be 1.0. */
    Color(double red, double green, double blue) : r((float)red), g((float)green),
        b((float)blue), a(1.0f) {
    }

/** Constructor that takes in RGBA integer values that range from 0-1. */
    Color(int red, int green, int blue, int alpha) : r((float)red),
        g((float)green), b((float)blue), a((float)alpha) {
    }

/** Constructor that takes in RGB integer values that range from 0-1.
   The alpha component is assumed to be 1.0. */
    Color(int red, int green, int blue) : r((float)red), g((float)green),
        b((float)blue), a(1.0f) {
    }

/** Multiplication operator; multiplies each rgb component of the Color by a constant. */
    Color SILVERLINING_API operator * (float f) const {
        return (Color(r*f, g*f, b*f, a));
    }

/** Multiplication operator; multiplies two Colors together on a per-component basis. */
    Color SILVERLINING_API operator * (const Color& c) const {
        return (Color(r*c.r, g*c.g, b*c.b, a*c.a));
    }

/** Addition operator; adds two colors together on a per-component basis. */
    Color SILVERLINING_API operator + (const Color& c) const {
        return (Color(r + c.r, g + c.g, b + c.b, a + c.a));
    }

/** Equality operator */
    bool SILVERLINING_API operator == (const Color& c) const {
        return (r == c.r && g == c.g && b == c.b);
    }

/** Inequality operator */
    bool SILVERLINING_API operator != (const Color& c) const {
        return (r != c.r || g != c.g || b != c.b);
    }

/** Convert Color to Vector4 */
    Vector4 SILVERLINING_API ToVector4() const {
        return Vector4(r, g, b, a);
    }

/** Convert Color to Vector4f */
    Vector4f SILVERLINING_API ToVector4f() const {
        return Vector4f((float)r, (float)g, (float)b, (float)a);
    }

/** Convert Color to Vector3 */
    Vector3 SILVERLINING_API ToVector3() const {
        return Vector3(r, g, b);
    }

/** Test if two colors are within a given distance of each other */ 
    bool SILVERLINING_API IsCloseTo(const Color& c, float distanceThreshold) const {
        float dr = r - c.r;
        float dg = g - c.g;
        float db = b - c.b;
        float d2 = dr*dr + dg*dg + db*db;
        return (d2 < (distanceThreshold * distanceThreshold));
    }

/** If any component of the Color exceeds 1.0, every component will be scaled down
   uniformly such that the maximum color component equals 1.0. */
    void SILVERLINING_API ScaleToUnitOrLess(bool hdr);

/** If any component of the Color exceeds the given value, every component will be scaled down
    uniformly such that the maximum color component equals it. */
    void SILVERLINING_API ScaleToValueOrLess(float value, bool hdr);

/** Clamps each color component to be within the range [0, 1.0]. */
    void SILVERLINING_API ClampToUnitOrLess(bool hdr);

/** Convert the RGB color to a grayscale value. */
    Color SILVERLINING_API ToGrayscale() const {
        float gray = r * 0.299f + g * 0.587f + b * 0.114f;
        return Color(gray, gray, gray);
    }

/** Save this color to disk. */
    void SILVERLINING_API Serialize(std::ostream& s) const
    {
        s.write((char *)&r, sizeof(float));
        s.write((char *)&g, sizeof(float));
        s.write((char *)&b, sizeof(float));
        s.write((char *)&a, sizeof(float));
    }

/** Restore this color from disk. */
    void SILVERLINING_API Unserialize(std::istream& s)
    {
        s.read((char *)&r, sizeof(float));
        s.read((char *)&g, sizeof(float));
        s.read((char *)&b, sizeof(float));
        s.read((char *)&a, sizeof(float));
    }

// data members public for convenience.
    float r, g, b, a;
};
}

#pragma pack(pop)

#endif
