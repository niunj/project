// Copyright (c) 2004-2014  Sundog Software, LLC. All rights reserved worldwide.

/** \file CloudDistributorRandom.h
   \brief A CloudDistributor that randomly places clouds within a given volume
    of space, without allowing clouds to intersect each other.
 */

#ifndef CLOUD_DISTRIBUTOR_RANDOM_H
#define CLOUD_DISTRIBUTOR_RANDOM_H

#if defined(WIN32) || defined(WIN64)
#pragma warning (disable:4786)
#endif

#include "CloudDistributor.h"
#include <map>
#include <vector>

namespace SilverLining
{
class Cloud;
class ThreadCameraStreamData;

/** A simple class representing a 2D integer-based vector. */
class Vector2i
{
public:
/** Constructs a Vector2i given an integer x and y position. */
    Vector2i(int px, int py) : x(px), y(py) {
    }

// Data members public for convenience.
    int x, y;
};

/** A CloudDistributor that places clouds randomly within its volume. A cloud is guaranteed
   to not go outside the bounds of the CloudDistributor's volume. Clouds are not allowed to intersect
   each other with this distributor, which makes it slower than using a CloudDistributorRandomFast
   CloudDistributor instead. */
class CloudDistributorRandom : public CloudDistributor
{
public:
/** Default constructor. */
    CloudDistributorRandom();

/** Destructor. */
    ~CloudDistributorRandom();

/** Defines the volume of space that this object will place clouds within. The class will
   precalculate valid positions for clouds of various sizes in this method, ensuring that no
   clouds intersect each other.

   \param minCorner The world position of the corner of the volume of space with the minimal
   x, y, and z coordinates.
   \param maxCorner The world position of the corner of the volume of space with the maximum
   z, y, and z coordinates.
   \param voxelSize The size of an individual voxel, or "puff" that makes up a cloud.
   \return true if the operation completed successfully.
 */
    bool Init(const Vector3& minCorner, const Vector3& maxCorner, double voxelSize);

/** Places a cloud within the volume previously defined via the Init() method, by calling
   its Cloud::SetWorldPosition() method. */
    bool PlaceCloud(Cloud *cloud, double minimumDistanceScale, const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData);

private:
    void Occupy(int x, int y, int diameter);
    SL_MAP(int, SL_VECTOR(Vector2i) ) slots; // map cloud diameter to available positions
    Vector3 minCorner;
    int width, depth, maxDiameter;
    double height, voxelSize;
};
}

#endif
