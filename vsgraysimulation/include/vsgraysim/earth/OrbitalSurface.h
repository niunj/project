#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace vsgraysim::earth {
/// Low-resolution native volume top surfaces and imagery, north-first, with two hemispheres.
struct OrbitalSurface {
    unsigned side=0, texture_side=0;
    std::vector<double> heights; // NaN denotes an empty column.
    std::vector<uint8_t> rgba;
    uint64_t dataset=0;
    std::optional<double> top(double latitude,double longitude) const;
};
}
