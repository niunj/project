// Copyright (c) 2004-2014  Sundog Software, LLC. All rights reserved worldwide.

/** \file CloudDistributor.h
   \brief Defines a base class from which specific classes that place clouds within a given
    volume of space are derived.
 */

#ifndef CLOUD_DISTRIBUTOR_H
#define CLOUD_DISTRIBUTOR_H

#include "MemAlloc.h"
#include "Vector3.h"

namespace SilverLining
{
class Cloud;
class RandomNumberGenerator;
class ThreadCameraStreamData;

/** This base class defines an interface for classes that place individual clouds within a
    given volume of space. Actual CloudDistributor's are instantiated using the
    CloudDistributorFactory class.*/
class CloudDistributor : public MemObject
{
public:
/** Default constructor */
    CloudDistributor() {
    }

/** Virtual destructor */
    virtual ~CloudDistributor() {
    }

/** Pure virtual interface for defining the volume of space that this CloudDistributor
   should place clouds within.

   \param minCorner The world position of the corner of the space with the volume's smallest
   x, y, and z coordinates.
   \param maxCorner The world position of the corner of the space with the volume's largest
   x, y, and z coordinates.
   \param voxelSize The diameter of an individual voxel (or "puff") that makes up a cloud
   of the type this volume will enclose. This information is needed to ensure that a puff
   won't be placed too close to the edge of the space, such that it extends out of it.
 */
    virtual bool Init(const Vector3& minCorner, const Vector3& maxCorner,
                      double voxelSize) = 0;

/** A pure virtual method used to place an individual cloud within the volume of space
   defined by an earlier call to Init(). The cloud's Cloud::SetWorldPosition() method will
   be called by this method when it determines an appropiate position for the cloud. */
    virtual bool PlaceCloud(Cloud *cloud, double minimumDistanceScale, const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData) = 0;

protected:
    double layerCenterX, layerCenterZ;
};
}

#endif
