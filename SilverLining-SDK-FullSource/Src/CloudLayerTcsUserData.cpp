// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.
#include "CloudLayerTcsUserData.h"
#include "CloudLayerCullSet.h"
#include "Configuration.h"
#include "Mutex.h"

namespace SilverLining
{

#if SILVERLINING_TRACK_CLOUDLAYERTCSUSERDATAS
MutexContainer CloudLayerTcsUserData::countMutexContainer;
int CloudLayerTcsUserData::numCloudLayerTcsUserDatas = 0;
#endif

CloudLayerTcsUserData::CloudLayerTcsUserData(ThreadCameraStreamData* _tcsData, const CloudLayer* _cloudLayer)
    : forceLightingUpdate(false)
    , lastGamma(0)
    , firstFrame(true)
    , firstFrameDrawn(false)
    , baseAltitude(1000)
    , layerX(0)
    , layerZ(0)
    , tcsData(_tcsData)
    , cloudLayer(_cloudLayer)
    , enabledInTcsData(true)
{
    Configuration::GetBoolValue("pre-init-clouds", firstFrame);

    renderingEnabled = true;
    fadeMode = NO_FADE;
    fade = fadeBegin = 1.0;
    fadeStart = fadeTime = 0;

    coverageMultiplier = 1.0;
    densityDirty = true;

    curveMode = CloudLayer::SQUARE;
    curveModeDirty = true;

#if SILVERLINING_TRACK_CLOUDLAYERTCSUSERDATAS
    ScopedMutex scopedMutex(countMutexContainer.mutex);
    id = numCloudLayerTcsUserDatas;
    ++numCloudLayerTcsUserDatas;
    std::cout << "num of CloudLayerTcsUserDatas: " << numCloudLayerTcsUserDatas << std::endl;
#endif

    cloudLayerCullSet = SL_NEW CloudLayerCullSet(cloudLayer);

    geocentricAltitude = 1000;

    lastCloudUpdated = 0;
}

CloudLayerTcsUserData::~CloudLayerTcsUserData()
{
    SL_DELETE cloudLayerCullSet;

#if SILVERLINING_TRACK_CLOUDLAYERTCSUSERDATAS
    ScopedMutex scopedMutex(countMutexContainer.mutex);
    --numCloudLayerTcsUserDatas;
    std::cout << "num of CloudLayerTcsUserDatas: " << numCloudLayerTcsUserDatas << std::endl;
#endif
}
}