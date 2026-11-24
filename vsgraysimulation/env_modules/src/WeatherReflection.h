#pragma once
#include <atomic>
#include <rocky_env/VoxelWeather.h>
#include <vsg/nodes/Switch.h>
#include <vsg/state/ColorBlendState.h>
namespace rockyenv {
// Source-over alpha preserves opaque backgrounds, for straight or premultiplied RGB.
vsg::ref_ptr<vsg::ColorBlendState> weatherLayerBlend(bool premultiplied = false);
// Linear premultiplied reflection radiance; alpha remains cloud opacity.
vsg::ref_ptr<vsg::ColorBlendState> weatherReflectionBlend();
// One bounded hemisphere probe per viewport, shared by every water draw.
// Cached radiance only: never sampled by dynamics or measurement kernels.
class WeatherReflection {
  public:
    WeatherReflection(vsg::Device *, std::shared_ptr<CloudGpuResources>);
    void setDraw(vsg::ref_ptr<vsg::Node>);
    void update(const VoxelWeatherFrame &);
    vsg::ref_ptr<vsg::Switch> commands;

  private:
    std::shared_ptr<CloudGpuResources> resources_;
    VoxelWeatherFrame previous_, pending_;
    vsg::ref_ptr<vsg::Device> device_;
    std::shared_ptr<std::atomic<uint64_t>> recorded_ = std::make_shared<std::atomic<uint64_t>>(0);
    uint64_t lastRecord_ = 0;
    bool valid_ = false;
};
} // namespace rockyenv
