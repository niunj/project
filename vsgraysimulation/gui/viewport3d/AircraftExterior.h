#pragma once
#include "platform_animation/F16CAnimation.h"
#include <vsg/core/ref_ptr.h>
#include <string>
#include <vsg/maths/vec3.h>
namespace vsg {class Node;class Options;}
namespace vsgraysim::viewport3d {
// The nozzle ring from the bundled OBJ after the 15 m canonical normalization.
inline constexpr vsg::dvec3 f16ExhaustNozzle{platform_animation::f16NozzleAt15m[0],
    platform_animation::f16NozzleAt15m[1],platform_animation::f16NozzleAt15m[2]};

vsg::ref_ptr<vsg::Node> loadAircraftExterior(const std::string& path,const std::string& upAxis,
    const std::string& kind,double maximumDimension,vsg::ref_ptr<vsg::Options> options,bool& representative,double headingDegrees=0);
}
