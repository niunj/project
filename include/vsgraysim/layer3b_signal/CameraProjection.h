#pragma once
#include "vsgraysim/layer3b_signal/SensorSignalProcessor.h"
#include <vsg/maths/vec3.h>
#include <stdexcept>
namespace vsgraysim {
/// Right-handed aperture frame; image rows increase downward.
class CameraProjection {
public:
    explicit CameraProjection(const SignalFormationConfig& config):origin_(config.sensor_position.x,
        config.sensor_position.y,config.sensor_position.z) {
        const vsg::dvec3 forward(config.sensor_direction.x,config.sensor_direction.y,config.sensor_direction.z);
        const vsg::dvec3 up(config.sensor_up.x,config.sensor_up.y,config.sensor_up.z);
        const double direction_length=vsg::length(forward),right_length=vsg::length(vsg::cross(forward,up));
        if(!std::isfinite(config.focal_length) || config.focal_length<=0 ||
           !std::isfinite(config.pixel_pitch) || config.pixel_pitch<=0 ||
           !std::isfinite(config.f_number) || config.f_number<=0 ||
           !std::isfinite(config.integration_time) || config.integration_time<0 ||
           !std::isfinite(config.netd) || config.netd<0 ||
           !config.image_width || !config.image_height ||
           uint64_t(config.image_width)*config.image_height>(1u<<24) ||
           !std::isfinite(direction_length) || !std::isfinite(right_length) ||
           direction_length<1e-12 || right_length<1e-12)
            throw std::invalid_argument("invalid camera aperture frame or optics");
        forward_=vsg::normalize(forward); right_=vsg::normalize(vsg::cross(forward_,up));
        up_=vsg::cross(right_,forward_);
    }
    bool point(const Vec3& world,double& x,double& y,double& depth) const {
        const auto relative=vsg::dvec3(world.x,world.y,world.z)-origin_;
        depth=vsg::dot(relative,forward_);
        x=vsg::dot(relative,right_); y=-vsg::dot(relative,up_);
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(depth) && depth>.0001;
    }
private:
    vsg::dvec3 origin_,forward_,right_,up_;
};
}
