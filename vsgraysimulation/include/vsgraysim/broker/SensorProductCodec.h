#pragma once
#include <vsgraysim/broker/SensorFrames.h>
#include <variant>
namespace vsgraysim::wire {
using SensorProduct=std::variant<RadarFrame,SarFrame,IrFrame,VisFrame>;
std::vector<uint8_t> encodeSensorProduct(const SensorProduct&);
bool decodeSensorProduct(const void*,size_t,SensorProduct&);
}
