// Copyright 2006-2020 Sundog Software, LLC. All rights reserved worldwide.

/** \file CloudLayerTcsUserData.h
\brief A class for describing and managing data/rendering in a particular thread and associated camera, stream
(ThreadCamerStreamData)
*/

#pragma once

#include "ThreadCameraStreamData.h"
#include "Vector3.h"
#include "Matrix3.h"
#include "Matrix4.h"
#include "FadeModes.h"
#include "MutexContainer.h"
#include "TcsUserData.h"
#include "SLVector.h"
#include "CloudLayer.h"

namespace SilverLining
{
    class Cloud;
    class CloudLayerCullSet;

    /**
    This is an ***internal*** class interface. Do not use this directly!!
    */
    class CloudLayerTcsUserData : public TcsUserData
    {
    public:
        CloudLayerTcsUserData(ThreadCameraStreamData* tcsData, const CloudLayer* _cloudLayer);
        virtual ~CloudLayerTcsUserData();
    public:
        bool forceLightingUpdate;
        SL_VECTOR(Cloud *) drawList;
        double lastGamma;
        bool firstFrame;
        bool firstFrameDrawn;

        Vector3 localUp;
        Matrix3 localBasis;
        Matrix4 localBasis4;

        double baseAltitude;

        double layerX, layerZ;

        double fade, fadeBegin;
        unsigned long fadeTime, fadeStart;
        FadeModes fadeMode;

        double coverageMultiplier;
        bool densityDirty;

        CloudLayer::CurveModes curveMode;
        bool curveModeDirty;

        // whether enabled in tcs data
        bool enabledInTcsData;

        bool renderingEnabled;

        double geocentricAltitude;

        int lastCloudUpdated;

        SL_VECTOR(Cloud*) clouds;

        CloudLayerCullSet* cloudLayerCullSet;

        // back pointer to parent
        ThreadCameraStreamData* tcsData;

        // back pointer to parent cloud layer
        const CloudLayer* cloudLayer;

#define SILVERLINING_TRACK_CLOUDLAYERTCSUSERDATAS 0
#if SILVERLINING_TRACK_CLOUDLAYERTCSUSERDATAS
        static MutexContainer countMutexContainer;
        static int numCloudLayerTcsUserDatas; // total num  of cloud layer tcs user datas
        int id;// internal id
#endif
    };
}
