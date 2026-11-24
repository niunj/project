#pragma once
#include "vsgraysim/broker/SensorFrames.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vsgraysim::wire {
/// Version-1 payload with a tagged trailing extension for measurement axes.
/// Old readers ignore the extension; new readers also accept old payloads.
std::vector<uint8_t> encodeRadarFrame(const RadarFrame& frame);
bool decodeRadarFrame(const void* data, size_t size, RadarFrame& frame);
}
