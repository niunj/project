// Copyright (c) 2014  Sundog Software, LLC. All rights reserved worldwide

/**
    \file CloudGeneratorRandom.h
    \brief An implementation of CloudGenerator that generates random cloud sizes within a range.
 */

#ifndef CLOUD_GENERATOR_RANDOM_H
#define CLOUD_GENERATOR_RANDOM_H

#if defined(WIN32) || defined(WIN64)
#pragma warning (disable:4786)
#endif

#include "CloudGenerator.h"

namespace SilverLining
{
/** An implementation of CloudGenerator that creates random cloud sizes within a given range.
 */
class CloudGeneratorRandom : public CloudGenerator
{
public:
/** Constructor */
    CloudGeneratorRandom();

/** Destructor */
    ~CloudGeneratorRandom();

/** Call this after setting the desired cloud coverage and area using the CloudGenerator
   base class methods. */
    void StartGeneration();

/** After calling StartGeneration(), call GetNextCloud() repeatedly until it returns
   false. In this way, you'll receive a series of cloud dimensions which you can use to
   create cloud objects until the desired coverage is met.

   \param width The width, in world units, of the cloud you'll create.
   \param depth The depth of the cloud you'll create.
   \param height The height of the cloud you'll create.
   \return true if GetNextCloud() should be called again to get closer to the desired
   cloud coverage amount; false if the coverage has been met.
 */
    bool GetNextCloud(double& width, double& depth, double& height, const RandomNumberGenerator* rng);

/** Refer to the article cited in the class description for a description of the
   alpha, beta, and chi parameters that control this cloud size distribution model. */
    void SetBeta(double pBeta) {
        beta = pBeta;
    }
/** Refer to the article cited in the class description for a description of the
   alpha, beta, and chi parameters that control this cloud size distribution model. */
    double GetBeta() const {
        return beta;
    }

/** Refer to the article cited in the class description for a description of the
   alpha, beta, nu, and chi parameters that control this cloud size distribution model. */
    void SetNu(double pNu) {
        nu = pNu;
    }
/** Refer to the article cited in the class description for a description of the
   alpha, beta, nu, and chi parameters that control this cloud size distribution model. */
    double GetNu() const {
        return nu;
    }

private:
    double beta, nu, areaCovered;
};
}

#endif
