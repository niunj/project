// Copyright (c) 2006-2009 Sundog Software. All rights reserved worldwide.

#include "CumulusCongestusCloudLayer.h"
#include "CumulusCongestusCloud.h"

using namespace SilverLining;

CumulusCongestusCloudLayer::CumulusCongestusCloudLayer(const Atmosphere& atmosphere) : CumulusCloudLayer(atmosphere)
{
    ReadConfiguration("cumulus-congestus");
}

CumulusCongestusCloudLayer::~CumulusCongestusCloudLayer()
{

}

CumulusCloud *CumulusCongestusCloudLayer::CreateCloud(CloudLayer *layer, double coverageTreshold, bool voxelsUseShaderInstancing, bool _drawIndirect, ThreadCameraStreamData* tcsData)
{
    return SL_NEW CumulusCongestusCloud(layer, coverageTreshold, voxelsUseShaderInstancing, _drawIndirect, tcsData);
}
