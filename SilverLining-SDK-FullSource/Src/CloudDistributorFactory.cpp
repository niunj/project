// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide.

#include "SilverLiningTypes.h"
#include "CloudDistributorFactory.h"
#include "CloudDistributorRandom.h"
#include "CloudDistributorRandomFast.h"
#include "Configuration.h"

using namespace SilverLining;
using namespace std;

CloudDistributor *CloudDistributorFactory::Create(const SL_STRING& configKeyBase)
{
    const char * type;
    if (Configuration::GetStringValue((configKeyBase + "-distribution-model").c_str(), type)) {
        string stype(type);
        if (stype == "random") {
            return SL_NEW CloudDistributorRandomFast();
        } else if (stype == "random-no-overlap") {
            return SL_NEW CloudDistributorRandom();
        }
    }

    return 0;
}
