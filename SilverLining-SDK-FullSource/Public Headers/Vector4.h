// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file Vector4.h
    \brief A simple 4D vector class.
 */

#ifndef VECTOR4_H
#define VECTOR4_H

#include "MemAlloc.h"
#include "Vector3.h"

#pragma pack(push)
#pragma pack(8)

namespace SilverLining
{
/** A simple double-precision 4D vector class with no operations defined.
   Essentially a struct with constructors. */
class Vector4 : public MemObject
{
public:
/** Constructs a Vector4 from the given x, y, z, and w values. */
    Vector4(double px, double py, double pz, double pw) : x(px), y(py), z(pz), w(pw) {
    }

/** Constructs a Vector4 from a Vector3, setting w to 1. */
    Vector4(const Vector3& v3) : x(v3.x), y(v3.y), z(v3.z), w(1.0) {
    }

/** Default constructor; initializes the Vector4 to (0, 0, 0, 1) */
    Vector4() : x(0), y(0), z(0), w(1.0) {
    }

/** Determines the dot product between this vector and another, and returns
   the result. */
    double SILVERLINING_API Dot (const Vector4& v) const
    {
        return x * v.x + y * v.y + z * v.z + w * v.w;
    }

/** Scales each x,y,z value of the vector by a constant n, and returns the result. */
    Vector4 SILVERLINING_API operator * (double n) const
    {
        return Vector4(x*n, y*n, z*n, w*n);
    }

/** The x, y, z, and w data members are public for convenience. */
    double x, y, z, w;
};

inline std::ostream& operator <<
(std::ostream& o, const Vector4& v)
{
	o << "Vector4(" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ")";
	return o;
}

/** A simple single-precision 4D vector class with no operations defined.*/
class Vector4f
{
public:
    /** Constructs a Vector4 from the given x, y, z, and w values. */
    Vector4f(float px, float py, float pz, float pw) : x(px), y(py), z(pz), w(pw) {
    }

    /** Constructs a Vector4 from a Vector3, setting w to 1. */
    Vector4f(const Vector3f& v3) : x(v3.x), y(v3.y), z(v3.z), w(1.0) {
    }

    /** Default constructor; initializes the Vector4 to (0, 0, 0, 1) */
    Vector4f() : x(0), y(0), z(0), w(1.0) {
    }

    const void* ptr(void) const {
        return &x;
    }
    const float* floatPtr(void) const {
        return &x;
    }
    float x, y, z, w;
};
}

#pragma pack(pop)

#endif
