#pragma once

#include <vsg/core/Data.h>
#include <vsg/nodes/Node.h>

#include <string>

namespace vsgraysim::earth {
vsg::ref_ptr<vsg::Node> loadAssimpVoxelMesh(const std::string& path);
vsg::ref_ptr<vsg::Data> loadVoxelTexture(const std::string& path);
}
