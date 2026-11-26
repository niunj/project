#include "PlatformAnimation.h"
#include "F16CAnimation.h"
#include "DisplayVehicleAnimation.h"

#include <algorithm>

namespace vsgraysim::platform_animation {

std::unique_ptr<PlatformAnimation> makeM1A2Animation();
std::unique_ptr<PlatformAnimation> makeCG57Animation();

std::unique_ptr<PlatformAnimation> makePlatformAnimation(std::string_view modelId) {
    using Factory = std::unique_ptr<PlatformAnimation>(*)();
    static constexpr std::pair<std::string_view,Factory> factories[] = {
        {"m1a2", makeM1A2Animation}, {"f16c", makeF16CAnimation}, {"cg57", makeCG57Animation},
        {"display_vehicle", []()->std::unique_ptr<PlatformAnimation>{return std::make_unique<DisplayVehicleAnimation>();}}
    };
    for (const auto& [id, create] : factories)
        if (id == modelId) return create();
    return {};
}

double smoothStep(double fraction) {
    const double t = std::clamp(fraction, 0., 1.);
    return t*t*(3.-2.*t);
}

} // namespace vsgraysim::platform_animation
