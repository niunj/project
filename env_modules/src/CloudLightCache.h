#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <rocky_env/VoxelWeather.h>
#include <vsg/all.h>
namespace rockyenv {
class CloudLightCache {
  public:
    CloudLightCache(vsg::Device*,vsg::ref_ptr<vsg::vec4Array>);
    void update(const VoxelWeatherFrame&);
    vsg::ref_ptr<vsg::ImageInfo> image;
    vsg::ref_ptr<vsg::vec4Array> state;
    // Device-written baked keys; never dirty/upload this after compilation.
    vsg::ref_ptr<vsg::vec4Array> bakedKeys;
    vsg::ref_ptr<vsg::BufferInfo> bakedKeyBuffer;
    vsg::ref_ptr<vsg::Switch> commands;
  private:
    vsg::ref_ptr<vsg::vec4Array> source_;
    vsg::ref_ptr<vsg::Dispatch> dispatch_;
    vsg::ref_ptr<vsg::Switch> work_;
    std::array<vsg::vec4,144> keys_{};
    std::array<bool,144> valid_{};
    std::vector<std::pair<int,vsg::vec4>> pending_;
    std::shared_ptr<std::atomic<uint64_t>> recorded_=std::make_shared<std::atomic<uint64_t>>(0);
    uint64_t lastRecord_=0;
};
}
