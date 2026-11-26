#pragma once

#include <array>
#include <nlohmann/json.hpp>

namespace vsgraysim::earth::terrain_pages {

using Rect = std::array<int, 4>; // min x, min z, end x, end z
struct Layout {
    std::array<int, 3> dimensions;
    std::array<double, 3> origin;
    double cell;
    int subdivisions;
    std::array<int, 2> imagery;
    bool sea;

    explicit Layout(const nlohmann::json& index);
    Rect cells(int page) const;
    Rect heights(int page) const;
    Rect pixels(int page) const;
    std::array<double, 4> bounds(int page) const;
};

nlohmann::json index(const nlohmann::json& patch, const nlohmann::json& volume);
std::array<nlohmann::json, 4> split(const Layout&, nlohmann::json& patch, nlohmann::json& volume);
void validatePage(const Layout&, int page, const nlohmann::json&);
void restore(const Layout&, const std::array<nlohmann::json, 4>&, nlohmann::json& patch, nlohmann::json& volume);

}
