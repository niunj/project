#pragma once

#include <string_view>

namespace vsgraysim::model_packages {

struct Alvin {
    static constexpr std::string_view id="alvin-hov";
    static std::string_view state(double depth, double verticalSpeed, double horizontalSpeed) {
        if(depth<=1.5)return "surfaced";
        if(verticalSpeed<-.12)return "diving";
        if(verticalSpeed>.12)return "ascending";
        return horizontalSpeed>.10?"submerged_cruise":"hovering";
    }
};

}
