#pragma once
#include <vsg/core/ref_ptr.h>
#include <string>
namespace vsg {class Node;class Options;}
namespace vsgraysim::viewport3d {
vsg::ref_ptr<vsg::Node> loadAircraftExterior(const std::string& path,const std::string& upAxis,
    const std::string& kind,double maximumDimension,vsg::ref_ptr<vsg::Options> options,bool& representative,double headingDegrees=0);
}
