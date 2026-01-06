#include "CloudLayerCullSet.h"
#include "CloudLayer.h"
#include "Cloud.h"
#include "CloudLayerTcsUserData.h"

namespace SilverLining
{
CloudLayerCullSet::CloudLayerCullSet(const CloudLayer* _cloudLayer)
    : cloudLayer(_cloudLayer)
    , culled(false)
{

}

void CloudLayerCullSet::SetCulled()
{
    culled = true;
}
void CloudLayerCullSet::ClearCulled()
{
    culled = false;
}
bool CloudLayerCullSet::GetCulled(void) const
{
    return culled;
}

CloudLayerCullSet::~CloudLayerCullSet()
{

}

const SL_VECTOR(bool)& CloudLayerCullSet::GetCloudsCulledVector(void) const
{
    return cloudsCulled;
}

SL_VECTOR(bool)& CloudLayerCullSet::GetCloudsCulledVector(void)
{
    return cloudsCulled;
}

void CloudLayerCullSet::DoCulling(const Camera* camera, const Camera* cullCamera, ThreadCameraStreamData* tcsData)
{
    if (cloudLayer->CullLayerOnly()) {
        if (cloudLayer->IsRenderable(tcsData) == false || cloudLayer->IsCulled(camera, cullCamera, tcsData)) {
            SetCulled();
        } else {
            ClearCulled();
        }
        return;
    }

    if (cloudLayer->IsRenderable(tcsData) == false || (cloudLayer->enableCulling && cloudLayer->IsCulled(camera, cullCamera, tcsData))) {
        SetCulled();
    } else {
        ClearCulled();

        ResizeAndInitCloudsCulledIfNecessary(tcsData);

        CloudLayerTcsUserData* tcsUserData = cloudLayer->GetOrCreateCloudLayerTcsUserData(tcsData);
        const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

        int i = 0;

        for (SL_VECTOR(Cloud*)::const_iterator it = clouds.begin(); it != clouds.end(); ++it, ++i) {
            const Cloud *cloud = *it;
            cloudsCulled[i] = cloud->IsCulled(cullCamera, tcsData);
        }

#if 0
        static int frame = 0;
        if (frame < 60) {
            ++frame;
        } else {
            int numCulled = 0;
            int numNotCulled = 0;
            for (unsigned int i = 0; i < cloudsCulled.size(); ++i) {
                if (cloudsCulled[i]) ++numCulled;
                else ++numNotCulled;
            }
            std::cout << "c: " << numCulled << " nc: " << numNotCulled << std::endl;

            frame = 0;
        }
#endif
    }
}

void CloudLayerCullSet::ResizeAndInitCloudsCulledIfNecessary(ThreadCameraStreamData* tcsData)
{
    CloudLayerTcsUserData* tcsUserData = cloudLayer->GetOrCreateCloudLayerTcsUserData(tcsData);
    const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;

    if (cloudsCulled.size() == clouds.size()) {
        return;
    }

    cloudsCulled.resize(clouds.size());
    for (unsigned int i = 0; i < cloudsCulled.size(); ++i) {
        cloudsCulled[i] = false;
    }
}
}