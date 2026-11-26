#pragma once

#include "CockpitModel.h"
#include "PlatformAnimation.h"
#include <vsgraysim/model_packages/platform/fixed_wing/Acoustics.h>

namespace vsgraysim::platform_animation {

// Nozzle position in the installed 15 m visual mesh's bind pose.
using vsgraysim::model_packages::f16NozzleAt15m;

std::unique_ptr<PlatformAnimation> makeF16CAnimation();
const CockpitModel& f16cCockpitModel();

} // namespace vsgraysim::platform_animation
