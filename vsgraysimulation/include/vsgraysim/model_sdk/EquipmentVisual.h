#pragma once

#include <array>
#include <string_view>

namespace vsgraysim::model_sdk {

struct ApertureDisplay {
    double horizontalDegrees;
    double verticalDegrees;
    double rangeMetres;
    double previewPeriodSeconds;
    double phaseDegrees;
};

struct EquipmentVisualStyle {
    std::array<int, 3> rgb;
    double horizontalDegrees;
    double verticalDegrees;
    double rangeMetres;
    bool azimuthRing;
};

class IEquipmentVisual {
public:
    virtual ~IEquipmentVisual() = default;
    virtual EquipmentVisualStyle style(const ApertureDisplay&) const = 0;
    virtual double azimuthDegrees(const ApertureDisplay&, double simulationSeconds) const = 0;
};

using EquipmentVisual = IEquipmentVisual;

} // namespace vsgraysim::model_sdk
