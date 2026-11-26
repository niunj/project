#pragma once
#include <string_view>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>

namespace vsg { class Node; }
namespace vsgraysim::viewport3d {
// Procedural cabin in metres relative to the pilot's eyes (X right, Y up, -Z forward).
struct CockpitInterior {
    vsg::dvec3 eye{0, 1.2, 0}; // relative to the platform origin
    float halfWidth = .91f;
    float brow = -.30f;
    float windscreenDepth = 1.30f;
    float glassHalfWidth = .285f;
    float glassCentreY = .027f;
    float glassHalfHeight = .181f;
    float glassDepth = 1.02f;
    bool interior = true;
    bool widePanel = false;
    bool splitWindscreen = false;
};
CockpitInterior cockpitInterior(std::string_view category);
vsg::ref_ptr<vsg::Node> makeCockpitInterior(const CockpitInterior&);
}
