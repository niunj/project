#pragma once
#include <rocky_env/VoxelWeather.h>
#include <vsg/commands/Draw.h>
namespace rockyenv {
class Precipitation {
public:
    Precipitation();
    void update(const VoxelWeatherFrame&);
    vsg::ref_ptr<vsg::Switch> node;
private:
    vsg::ref_ptr<vsg::vec4Array> parameters_;
    vsg::ref_ptr<vsg::Draw> draw_;
};
}
