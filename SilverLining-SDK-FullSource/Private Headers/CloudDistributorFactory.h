// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide.

/** \file CloudDistributorFactory.h
   \brief A class factory that creates specific CloudDistributor classes to place clouds within a volume
    of space, given a configuration key.
 */

#ifndef CLOUD_DISTRIBUTOR_FACTORY_H
#define CLOUD_DISTRIBUTOR_FACTORY_H

#include "MemAlloc.h"
#include <string>

namespace SilverLining
{
class CloudDistributor;

/** A class that creates a specific type of CloudDistributor, based on a given config entry. */
class CloudDistributorFactory : public MemObject
{
public:
/** Create a new CloudDistributor based on a given configuration entry.
    \param configKeyBase An STL string; currently supported values include "random"
    and "random-no-overlap".
    \return a CloudDistributor object appropriate for the config key passed in.
 */
    static CloudDistributor *Create(const SL_STRING& configKeyBase);
};
}

#endif
