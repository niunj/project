// Copyright (c) 2004-2008  Sundog Software, LLC All rights reserved worldwide.

/**
    \file Plane.h
    \brief A class that models a geometric plane and its operations.
 */
#ifndef PLANE_H
#define PLANE_H

#include "MemAlloc.h"
#include "Vector3.h"

#include <iostream>

#pragma pack(push)
#pragma pack(8)

namespace SilverLining
{
/** A geometric plane, modelled as a normal vector and a distance to the plane. */
class Plane : public MemObject
{
public:
/** Default constructor, performs no intializations */
    Plane() {
    }

/** Constructor that initializes the plane based on coefficients of the
   plane equation, ax + by + cz + d = 0. */
    Plane(double a, double b, double c, double d)
    {
        normal = Vector3(a, b, c);
        dist = -d;
    }

/** Constructor that initializes the plane based on a normal vector and
   a distance to the plane, which is how the plane is represented internally. */
    Plane(Vector3 pNormal, double pDistance) : normal(pNormal), dist(pDistance) {
    }

/** Retrieves the normal vector for the plane. */
    const Vector3& SILVERLINING_API GetNormal() const {
        return normal;
    }

/** Retrieves the distance to the plane. */
    double SILVERLINING_API GetDistance() const {
        return dist;
    }

/** Set the normal vector for the plane. */
	void SILVERLINING_API SetNormal(const Vector3& v) {
		normal = v;
	}

/** Set the distance to the plane. */
	void SILVERLINING_API SetDistance(double d) {
		dist = d;
	}

/** Normalizes the plane normal vector and adjusts the distance accordingly. */
    void SILVERLINING_API Normalize() {
        dist /= normal.Length();
        normal.Normalize();
    }

    bool SILVERLINING_API operator==(const Plane& rhs) const
    {
        return (normal == rhs.normal && dist == rhs.dist);
    }
    bool SILVERLINING_API operator!=(const Plane& rhs) const
    {
        return !(this->operator ==(rhs));
    }

    bool SILVERLINING_API Equals(const Plane& rhs, double delta) const
    {
        double distDelta = dist - rhs.dist;
        if (distDelta<0) distDelta = -distDelta;

        return normal.Equals(rhs.normal, delta) == false || distDelta>delta;
    }

    void SILVERLINING_API InitializeToZero(void)
    {
        normal = Vector3(0, 0, 0);
        dist = 0;
    }

private:
    Vector3 normal;
    double dist;
};

inline std::ostream& operator <<
(std::ostream& o, const Plane& plane)
{
    o << "plane(normal: "<<plane.GetNormal()<<" distance: "<<plane.GetDistance()<<")";
    return o;
}

}

#pragma pack(pop)

#endif
