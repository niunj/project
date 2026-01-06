#include "CloudLayerSorter.h"
#include "CloudLayer.h"
#include "CloudLayerCompare.h"

#include <algorithm>

namespace SilverLining
{
CloudLayerSorter::CloudLayerSorter()
{
    // nothing else to do
}
CloudLayerSorter::~CloudLayerSorter()
{
    // nothing else to do
}

void CloudLayerSorter::Prime(const SL_MAP(int, CloudLayer*)& cloudLayers, ThreadCameraStreamData* tcsData)
{
    sortedCloudLayers.clear();

    for (SL_MAP(int, CloudLayer*)::const_iterator it = cloudLayers.begin(); it != cloudLayers.end(); it++) {
        if ((it->second)->IsRenderable(tcsData)) {
            sortedCloudLayers.push_back(it->second);
        }
    }
}

void CloudLayerSorter::Sort(const Camera* camera, bool enforceStrictWeak, ThreadCameraStreamData* tcsData)
{
    std::sort(sortedCloudLayers.begin(), sortedCloudLayers.end(), CloudLayerCompare(camera, enforceStrictWeak, tcsData));
}


const SL_VECTOR(CloudLayer*)& CloudLayerSorter::GetSortedCloudLayers(void) const
{
    return sortedCloudLayers;
}
}