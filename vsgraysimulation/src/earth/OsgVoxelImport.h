#pragma once
#include <vsg/core/Data.h>
#include <vsg/nodes/Node.h>
#include <string>
namespace vsgraysim::earth {
vsg::ref_ptr<vsg::Node> loadOsgVoxelMesh(const std::string& path);
vsg::ref_ptr<vsg::Data> loadOsgVoxelTexture(const std::string& path);
}
