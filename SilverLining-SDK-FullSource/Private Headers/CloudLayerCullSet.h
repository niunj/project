// Copyright (c) 2004-2018  Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#include "MemAlloc.h"
#include <vector>

namespace SilverLining
{
    class Atmosphere;
    class Camera;
    class CloudLayer;
    class ThreadCameraStreamData;

    class CloudLayerCullSet : public MemObject
    {
    public:
        CloudLayerCullSet(const CloudLayer* cloudLayer);
        virtual ~CloudLayerCullSet();

        void DoCulling(const Camera* camera, const Camera* cullCamera, ThreadCameraStreamData* tcsData);
        
        void ResizeAndInitCloudsCulledIfNecessary(ThreadCameraStreamData* tcsData);

        void ClearCulled();
        bool GetCulled(void) const;

        const SL_VECTOR(bool)& GetCloudsCulledVector(void) const;
        SL_VECTOR(bool)&       GetCloudsCulledVector(void);
    private:
        bool culled;
        SL_VECTOR(bool) cloudVisibility;

        const CloudLayer* cloudLayer;

        void SetCulled();

        SL_VECTOR(bool) cloudsCulled;
    };
}