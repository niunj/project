// Copyright (c) 2004-2008  Sundog Software, LLC All rights reserved worldwide.

/**
    \file Frustum.h
    \brief A viewing frustum, modeled as a collection of six planes.
 */
#ifndef FRUSTUM_H
#define FRUSTUM_H

#include "Plane.h"

#include <iostream>

#pragma pack(push)
#pragma pack(8)

namespace SilverLining
{
/** A simple class that just collects six planes together and calls
   it a frustum. */
class Frustum : public MemObject
{
public:

/** Default constructor. */
    Frustum() : nCullingPlanes(6) {
    }

/** Destructor; does nothing. */
    ~Frustum() {
    }

/** Identifiers for each of the six planes that make up the viewing
   frustum. */
    enum Planes
    {

        PNEAR = 0,
        PLEFT,
        PRIGHT,
        PTOP,
        PBOTTOM,
        PBACK,
        NUM_PLANES
    };

/** Assigns a plane to one of the enumerated planes of the frustum.

   \param planeNum A member of the Planes enumeration, specifying if this plane
   represents the near, left, right, top, bottom, or back frustum plane.
   \param plane A Plane object representing the Frustum plane specified.
 */
    void SILVERLINING_API SetPlane(int planeNum, const Plane& plane) {
        planes[planeNum] = plane;
    }

/** Retrieves a specific Plane of the frustum.

   \param planeNum A member of the Planes enumeration, specifying if you
   wish to retrieve the near, left, right, top, bottom, or back frustum plane.
   \return A const reference to the Plane object requested.
 */
    const Plane& SILVERLINING_API GetPlane(int planeNum) const {
        return planes[planeNum];
    }

/** Enables culling against the far clip plane from this frustum (enabled by default.)
 */
    void SILVERLINING_API EnableFarClipCulling(bool bEnable) {
        bEnable ? nCullingPlanes = 6 : nCullingPlanes = 5;
    }

    int SILVERLINING_API GetNumCullingPlanes() const {
        return nCullingPlanes;
    }

    bool SILVERLINING_API operator==(const Frustum& rhs) const
    {
        for (int i = 0; i < 6; ++i)
        {
            if (planes[i] != rhs.planes[i])
            {
                return false;
            }
        }
        return true;
    }
    bool SILVERLINING_API operator!=(const Frustum& rhs) const
    {
        return !(this->operator==(rhs));
    }

    bool SILVERLINING_API Equals(const Frustum& rhs, double delta) const
    {
        for (int i = 0; i < 6; ++i)
        {
            if (planes[i].Equals(rhs.planes[i], delta) == false)
            {
                return false;
            }
        }
        return true;
    }

    void SILVERLINING_API InitializePlanesToZero(void)
    {
        for (int i = 0; i < 6; ++i)
        {
            planes[i].InitializeToZero();
        }
    }

private:
    Plane planes[6];
    int nCullingPlanes;
};

inline std::ostream& operator <<
(std::ostream& o, const Frustum& frustum)
{
    o << "Frustum:" << std::endl;
    for (int i = 0; i < 6; ++i)
    {
        o << "Plane " << i << ": " << frustum.GetPlane(i) << std::endl;
    }
    return o;
}

}

#pragma pack(pop)

#endif
