// Copyright (c) 2009-2024 Sundog Software, LLC. All rights reserved worldwide.

#include "DefaultRandomNumberGenerator.h"
#include <stdlib.h>

using namespace SilverLining;


#ifndef MAC
#define MY_RAND_MAX ((1U << 31) - 1)

inline int myRand(unsigned int& seed)
{
    return seed = (seed * 1103515245 + 12345) & MY_RAND_MAX;
}
#endif

DefaultRandomNumberGenerator::DefaultRandomNumberGenerator()
{
    seed = 0;
}

float DefaultRandomNumberGenerator::UniformRandomFloat() const
{
#ifdef MAC
    return (float)(random() & 0xFFFFFF) / (float)0xFFFFFF;
#else
    double drand = (double)myRand(seed) / (double)MY_RAND_MAX;
    return (float)drand;
#endif
}

void DefaultRandomNumberGenerator::Reset()
{
    seed = 0;
}

double DefaultRandomNumberGenerator::UniformRandomDouble() const
{
#ifdef MAC
    return (double)(random() & 0xFFFFFF) / (double)0xFFFFFF;
#else
    return (double)myRand(seed) / (double)MY_RAND_MAX;
#endif
}

unsigned int DefaultRandomNumberGenerator::UniformRandomIntRange(unsigned int start, unsigned int end) const
{
    if (start > end) {
        int tmp = end;
        end = start;
        start = tmp;
    }

    int range = end - start;

#ifdef MAC
    return (random() % (range + 1)) + start;
#else
    return (myRand(seed) % (range + 1)) + start;
#endif
}

void DefaultRandomNumberGenerator::Seed(unsigned int seedVal)
{
#ifdef MAC
    srand(seedVal);
#else
    seed = seedVal;
#endif
}