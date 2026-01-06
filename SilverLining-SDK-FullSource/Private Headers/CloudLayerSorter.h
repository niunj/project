// Copyright (c) 2004-2018  Sundog Software, LLC. All rights reserved worldwide.

/** \file CloudLayerSorter.h
\brief CloudLayerSorter class
*/

#include "SLVector.h"
#include "SLMap.h"

namespace SilverLining
{
    class CloudLayer;
    class Camera;
    class ThreadCameraStreamData;

    class CloudLayerSorter : public MemObject
    {
    public:
        CloudLayerSorter();
        virtual ~CloudLayerSorter();

        void Prime(const SL_MAP(int, CloudLayer*)& cloudLayers, ThreadCameraStreamData* tcsData);
        void Sort(const Camera* camera, bool enforceStrictWeak, ThreadCameraStreamData* tcsData);
        const SL_VECTOR(CloudLayer*)& GetSortedCloudLayers(void) const;
    private:
        SL_VECTOR(CloudLayer*) sortedCloudLayers;
    };
}