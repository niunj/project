// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file CloudGenerator.h
    \brief Defines a base class for classes that guide the creation of clouds within a given area
    for a given sky coverage.
 */

#ifndef CLOUD_GENERATOR_H
#define CLOUD_GENERATOR_H

#include "MemAlloc.h"

namespace SilverLining
{
class Cloud;
class RandomNumberGenerator;

/** A base class that defines an interface for classes that generate clouds for a given
    sky coverage amount and area. */
class CloudGenerator : public MemObject
{
public:
/** Constructor. */
    CloudGenerator() : coverage(0.5), area(0) {
    }

/** Virtual destructor */
    virtual ~CloudGenerator() {
    }

/** Set the desired coverage of the sky to be achieved within the area controlled by this
    class.

    \param pCoverage The percentage of the sky to attempt to cover; ie, 0.5 for 50%.
 */
    void SetDesiredCoverage(double pCoverage) {
        coverage = pCoverage;
    }

/** Set the desired area to fill, in world units squared. */
    void SetDesiredArea(double pArea) {
        area = pArea;
    }

/** Set the smallest diameter of a cloud permitted by this generator. */
    void SetMinimumSize(double meters) {
        minSize = meters;
    }

/** Set the largest diameter of a cloud permitted by this generator. */
    void SetMaximumSize(double meters) {
        maxSize = meters;
    }

/** Access the desired sky coverage amount as previously set by SetDesiredCoverage(). */
    double GetDesiredCoverage() const {
        return coverage;
    }

/** Access the desired cloud area as previously set by SetDesiredArea(). */
    double GetDesiredArea() const {
        return area;
    }

/** Access the minimum cloud diameter as previously set by SetMinimumSize(). */
    double GetMinimumSize() const {
        return minSize;
    }

/** Access the maximum cloud diameter as previously set by SetMaximumSize(). */
    double GetMaximumSize() const {
        return maxSize;
    }

/** StartGeneration() must be called after configuring the generator via SetDesiredCoverage(),
   SetDesiredArea(), SetMinimumSize(), and SetMaximumSize(), but before actually generating
   clouds via GetNextCloud(). This gives the cloud generator an opportunity to initialize
   itself. */
    virtual void StartGeneration() = 0;

/** Call GetNextCloud() repeatedly after calling StartGeneration() to generate clouds.
   Continue calling until it returns false, which signals that the desired coverage has been
   attained. With each call, a set of dimensions for a cloud will be returned based on the
   implementation's model of cloud growth. You'll want to pass the Cloud objects you create
   with this data on to a CloudDistributor in order to place the clouds within a volume of
   space.

   \param width The width of the cloud you'll create, in world units.
   \param depth The depth of the cloud you'll create.
   \param height The height of the cloud you'll create.
   \return true if more clouds must be generated in order to satisfy the desired coverage
   amount, or false if enough clouds have already been generated.
 */
    virtual bool GetNextCloud(double& width, double& depth, double& height, const RandomNumberGenerator* rng) = 0;

private:
    double coverage, area, minSize, maxSize;
};
}

#endif
