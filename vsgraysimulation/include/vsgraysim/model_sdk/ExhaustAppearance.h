#pragma once

#include <optional>

namespace vsgraysim::visual_effects {

struct ExhaustAppearance {
    // An absent value uses the renderer's default for the effect style.
    std::optional<float> smokeOpacity;
    float lifetimeScale = 1.f;
    float spreadScale = 1.f;

    bool operator==(const ExhaustAppearance& other) const {
        return smokeOpacity==other.smokeOpacity && lifetimeScale==other.lifetimeScale && spreadScale==other.spreadScale;
    }
};

} // namespace vsgraysim::visual_effects
