// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file CloudGeneratorFactory.h
    \brief A class factory to produce a CloudGenerator class, given a config entry.
 */

#ifndef CLOUD_GENERATOR_FACTORY_H
#define CLOUD_GENERATOR_FACTORY_H

#include "MemAlloc.h"
#include <string>

namespace SilverLining
{
class CloudGenerator;

/** A class factory to produce a specific CloudGenerator subclass, given a
   config entry that describes it. It will proceed to configure the CloudGenerator
   based on its other config file settings prior to returning it.*/
class CloudGeneratorFactory : public MemObject
{
public:
/** Instantiates and configures a specific CloudGenerator based on a base
   configuration cloud name. For example, for cumulus-congestus clouds, passing
   in "cumulus-congestus" would look up the config entry for "cumulus-congestus-generation-model".
   Currently the only supported generation model is "plank-exponential." It would
   then look up all the various config entries for cumulus-congestus-generation-*
   and configure the resulting CloudGeneratorExponential class after instantiating it.
 */
    static CloudGenerator *Create(const SL_STRING& configKeyBase);
};
}

#endif
