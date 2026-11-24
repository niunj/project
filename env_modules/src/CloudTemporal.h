#pragma once
#include <rocky_env/VoxelWeather.h>
#include <vsg/app/Camera.h>
#include <vsg/nodes/Switch.h>
namespace rockyenv {
// Display-only history: never changes cloud density, wakes, shadows or sensors.
class CloudTemporal {
  public:
    explicit CloudTemporal(vsg::Device *);
    ~CloudTemporal();
    vsg::ref_ptr<vsg::vec4Array> parameters;
    vsg::ref_ptr<vsg::vec4Array> motions;
    vsg::ref_ptr<vsg::Switch> display, commands;
    void setDraws(vsg::ref_ptr<vsg::Node> direct, vsg::ref_ptr<vsg::Node> raw);
    bool update(vsg::Camera *, uint32_t width, uint32_t height, const VoxelWeatherFrame &);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rockyenv
