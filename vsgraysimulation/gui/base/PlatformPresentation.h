#pragma once

#include <nlohmann/json.hpp>
#include <string_view>

namespace vsgraysim::gui {

enum class PlatformDomain { Air, Sea, Ground };

inline PlatformDomain platformDomain(const nlohmann::json& sample) {
    for (const char* field : {"category", "kind"}) {
        const auto value = sample.find(field);
        if (value == sample.end() || !value->is_string()) continue;
        const std::string_view kind = value->get_ref<const std::string&>();
        if (kind == "NavalVessel" || kind == "ship" || kind == "boat" || kind == "vessel" || kind == "naval_vessel")
            return PlatformDomain::Sea;
        if (kind == "GroundVehicle" || kind == "tank" || kind == "ground_vehicle" || kind == "vehicle")
            return PlatformDomain::Ground;
    }
    return PlatformDomain::Air;
}

}
