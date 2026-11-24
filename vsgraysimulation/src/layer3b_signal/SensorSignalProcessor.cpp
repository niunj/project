#include "vsgraysim/layer3b_signal/SensorSignalProcessor.h"

namespace vsgraysim {

std::unique_ptr<SensorSignalProcessor> SensorSignalProcessor::create(SensorType type) {
    switch (type) {
        case SensorType::RADAR: return std::make_unique<RadarPulseProcessor>();
        case SensorType::SAR:   return std::make_unique<SarBackprojectProcessor>();
        case SensorType::VIS:
        case SensorType::LASER: return std::make_unique<VisCameraProcessor>();
        case SensorType::IR:    return std::make_unique<IrThermalProcessor>();
    }
    return std::make_unique<VisCameraProcessor>();
}

} // namespace vsgraysim
