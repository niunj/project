#pragma once

#include <vsg/state/ShaderStage.h>
#include <string_view>

namespace vsgraysim::earth {
// SPIR-V generated from the repository's GLSL at build time.
vsg::ref_ptr<vsg::ShaderStage> earthShader(std::string_view name);
}
