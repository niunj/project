#pragma once
#include <cstdint>
#include <string_view>
namespace simcore {
/// Stable across processes/toolchains. Importers must reject collisions.
inline uint32_t objectId(std::string_view name) {
    uint32_t id=2166136261u;
    for (unsigned char byte:name) { id^=byte; id*=16777619u; }
    return id;
}
}
