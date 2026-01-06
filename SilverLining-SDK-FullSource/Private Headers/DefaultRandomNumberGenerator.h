// Copyright (c) 2024 Sundog Software, LLC. All rights reserved worldwide.

/** \file DefaultRandomNumberGenerator.h
   \brief A simple random number generator based on stdlib's rand().
 */

#ifndef DEFAULT_RANDOM_NUMBER_GENERATOR_H
#define DEFAULT_RANDOM_NUMBER_GENERATOR_H

#include "RandomNumberGenerator.h"

namespace SilverLining {
/** The default implementation of SilverLining's random number generator. This may be overridden
    by extending RandomNumberGenerator and passing in your class to Atmosphere::SetRandomNumberGenerator()*/
class DefaultRandomNumberGenerator : public RandomNumberGenerator
{
public:
    /** constructor. */
    DefaultRandomNumberGenerator();

/** Virtual destructor. */
    virtual ~DefaultRandomNumberGenerator() {
    };

/** Reset Function. Override to reset the seed for example. */
    virtual void Reset();

/** Return a uniformly distributed random float between 0.0 and 1.0*/
    virtual float SILVERLINING_API UniformRandomFloat() const;

/** Return a uniformly distributed random double between 0.0 and 1.0*/
    virtual double SILVERLINING_API UniformRandomDouble() const;

/** Return a uniformly distributed random integer between the integers specified (inclusive.)*/
    virtual unsigned int SILVERLINING_API UniformRandomIntRange(unsigned int start, unsigned int end) const;

/** Set a seed value for the random number generator. */
    virtual void SILVERLINING_API Seed(unsigned int seedValue);
protected:
    mutable unsigned int seed;
};
}

#endif
