#pragma once

#include <vsg/nodes/Node.h>

#include <string>

namespace vsgraysim::earth {
vsg::ref_ptr<vsg::Node> loadUsdVoxelMesh(const std::string& path);
}
