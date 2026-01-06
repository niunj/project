// Copyright (c) 2004-2014  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file CloudDistributorRandomFast.h
    \brief A CloudDistributor that quickly places clouds within a given volume of space randomly.
 */

#ifndef CLOUD_DISTRIBUTOR_RANDOM_FAST_H
#define CLOUD_DISTRIBUTOR_RANDOM_FAST_H

#if defined(WIN32) || defined(WIN64)
#pragma warning (disable:4786)
#endif

#include "CloudDistributor.h"
#include <vector>

namespace SilverLining
{
class Cloud;

typedef struct CloudPlacement_S
{
    Vector3 cloudPos;
    double radius;
} CloudPlacement;

/** A CloudDistributor that places clouds within a given volume of space in a random fashion.
   It's ensured that clouds stay within the volume specified, but no effort is made to ensure
   that clouds do not intersect each other in the name of performance. If you need to avoid cloud
   intersections, use the CloudDistributorRandom class instead. */
class CloudDistributorRandomFast : public CloudDistributor
{
public:
    /** Constructor */
    CloudDistributorRandomFast();

    /** Destructor */
    ~CloudDistributorRandomFast();

    /** Defines the volume of space that clouds will be placed within.

       \param minCorner The world position of the corner of the volume of space with the minimum
       x, y, and z coordinates.
       \param maxCorner The world position of the corner of the volume of space with the maximum
       x, y, and z coordinates.
       \param voxelSize The size, in world units, of an individual "puff" or voxel that makes up
       a cloud.
     */
    bool Init(const Vector3& minCorner, const Vector3& maxCorner, double voxelSize);

    /** Randomly places a cloud within the given volume defined by the Init() method. */
    bool PlaceCloud(Cloud *cloud, double minimumDistanceScale, const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData);

private:
    Vector3 minCorner, maxCorner;
    SL_VECTOR(CloudPlacement) cloudPositionList;
};
}

#endif
