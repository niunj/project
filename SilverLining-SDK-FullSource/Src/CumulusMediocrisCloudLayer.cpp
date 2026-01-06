// Copyright (c) 2006-2009 Sundog Software. All rights reserved worldwide.

#include "CumulusMediocrisCloudLayer.h"
#include "CumulusMediocrisCloud.h"

using namespace SilverLining;

CumulusMediocrisCloudLayer::CumulusMediocrisCloudLayer(const Atmosphere& atmosphere) : CumulusCloudLayer(atmosphere)
{
    ReadConfiguration("cumulus-mediocris");
}

CumulusCloud *CumulusMediocrisCloudLayer::CreateCloud(CloudLayer *layer, double coverageTreshold, bool voxelsUseShaderInstancing, bool _drawIndirect, ThreadCameraStreamData* tcsData)
{
    return SL_NEW CumulusMediocrisCloud(layer, coverageTreshold, voxelsUseShaderInstancing, _drawIndirect, tcsData);
}
