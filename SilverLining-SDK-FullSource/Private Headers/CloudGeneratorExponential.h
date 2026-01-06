// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide

/**
    \file CloudGeneratorExponential.h
    \brief An implementation of CloudGenerator that assumes an exponential distribution of
    cloud sizes within an area of space.
 */

#ifndef CLOUD_GENERATOR_EXPONENTIAL_H
#define CLOUD_GENERATOR_EXPONENTIAL_H

#if defined(WIN32) || defined(WIN64)
#pragma warning (disable:4786)
#endif

#include "CloudGenerator.h"

namespace SilverLining
{
/** An implementation of CloudGenerator that assumes an exponential distribution of cloud
   sizes in an area of space. For a full discussion of the model behind this class, see
   Plank, V. G., 1969: The size distribution of cumulus clouds in representative Florida populations.
   <i>J. Appl. Meteor.</i>, <b>8</b>, 46�67.
 */
class CloudGeneratorExponential : public CloudGenerator
{
public:
/** Constructor */
    CloudGeneratorExponential();

/** Destructor */
    ~CloudGeneratorExponential();

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
   alpha, beta, nu, and chi parameters that control this cloud size distribution model. */
    void SetChi(double pChi) {
        chi = pChi;
    }
/** Refer to the article cited in the class description for a description of the
   alpha, beta, nu, and chi parameters that control this cloud size distribution model. */
    double GetChi() const {
        return chi;
    }

/** Refer to the article cited in the class description for a description of the
   alpha, beta, nu, and chi parameters that control this cloud size distribution model. */
    void SetAlpha(double pAlpha) {
        alpha = pAlpha;
    }
/** Refer to the article cited in the class description for a description of the
   alpha, beta, nu, and chi parameters that control this cloud size distribution model. */
    double GetAlpha() const {
        return alpha;
    }

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

/** This cloud generator will model the distribution for a series of ranges of cloud
   sizes. The "bandwidth" defines the variance, in meters, of cloud sizes produced with
   a specific distribution. It should be between the minimum and maximum cloud sizes set.
   For example, if you set the bandwidth to 100 and the minimum cloud size is 200, it will
   place all of the clouds of size 200-300 at once using the same parameters, then all the
   clouds between 300 and 400 meters, etc. */
    void SetBandwidth(double meters) {
        epsilon = meters;
    }

/** Returns the bandwidth set previously by SetBandwidth(). */
    double GetBandwidth() const {
        return epsilon;
    }

private:
    double alpha, chi, epsilon, beta, nu;
    int currentN, targetN;
    double currentD;
};
}

#endif
