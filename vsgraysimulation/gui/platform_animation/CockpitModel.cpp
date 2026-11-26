#include "CockpitModel.h"
#include "F16CAnimation.h"

namespace vsgraysim::platform_animation {

const CockpitModel* findCockpitModel(std::string_view modelId) {
    if (modelId == "f16c") return &f16cCockpitModel();
    return nullptr;
}

} // namespace vsgraysim::platform_animation
