// Copyright (c) 2014 Sundog Software. All rights reserved worldwide.

#include "ToweringCumulusCloudLayer.h"
#include "ToweringCumulusCloud.h"

using namespace SilverLining;

ToweringCumulusCloudLayer::ToweringCumulusCloudLayer(const Atmosphere& atmosphere) : CumulusCloudLayer(atmosphere)
{
    ReadConfiguration("towering-cumulus");
}

CumulusCloud *ToweringCumulusCloudLayer::CreateCloud(CloudLayer *layer, double coverageTreshold, bool voxelsUseShaderInstancing, bool _drawIndirect, ThreadCameraStreamData* tcsData)
{
    return SL_NEW ToweringCumulusCloud(layer, coverageTreshold, voxelsUseShaderInstancing, _drawIndirect, tcsData);
}
