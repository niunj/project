#pragma once
#include "CloudLightCache.h"
namespace rockyenv {
// Display-only, per-object bulk optical-depth atlas. View density and all
// sensor/physics queries remain canonical. Reuses the same-frame bake barrier.
class CloudLightField {
public:
    CloudLightField(vsg::Device*,vsg::ref_ptr<vsg::vec4Array> source,
                    vsg::ref_ptr<vsg::vec4Array> weather,CloudLightCache&);
    void update(const VoxelWeatherFrame&);
    vsg::ref_ptr<vsg::ImageInfo> image;
    vsg::ref_ptr<vsg::vec4Array> state;
    vsg::ref_ptr<vsg::Switch> commands;
private:
    vsg::ref_ptr<vsg::vec4Array> source_;
    vsg::ref_ptr<vsg::Switch> work_;
    std::vector<vsg::vec4> previous_;
    VoxelWeatherFrame frame_;
    uint64_t epoch_=0;
    bool initialized_=false;
    std::shared_ptr<std::atomic<uint64_t>> recorded_=std::make_shared<std::atomic<uint64_t>>(0);
    uint64_t lastRecord_=0,pendingEpoch_=0,bakedEpoch_=0;
};
}
