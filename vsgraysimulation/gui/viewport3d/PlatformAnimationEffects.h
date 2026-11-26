#pragma once

#include "platform_animation/PlatformAnimation.h"
#include <functional>
#include <vsg/maths/mat4.h>

namespace vsgraysim::viewport3d {

class EngagementVisualFeature;

using AnimationAttachments = std::function<std::optional<vsg::dmat4>(std::string_view)>;
void publishAnimationEffects(EngagementVisualFeature&, std::string_view owner,
                             const platform_animation::AnimationFrame&, const AnimationAttachments&);

} // namespace vsgraysim::viewport3d
