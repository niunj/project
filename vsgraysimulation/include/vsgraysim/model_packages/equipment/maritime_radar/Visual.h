#pragma once

#include <cmath>
#include <vsgraysim/model_sdk/EquipmentVisual.h>

namespace vsgraysim::model_packages {

class MaritimeRadarVisual final : public model_sdk::EquipmentVisual {
public:
    MaritimeRadarVisual(std::array<int, 3> color, double previewPeriod)
        : color_(color), previewPeriod_(previewPeriod) {}

    model_sdk::EquipmentVisualStyle style(const model_sdk::ApertureDisplay& aperture) const override {
        return {color_, aperture.horizontalDegrees, aperture.verticalDegrees, aperture.rangeMetres, true};
    }

    double azimuthDegrees(const model_sdk::ApertureDisplay& aperture, double time) const override {
        // Visual phase only. The measurement runtime owns the sensor aperture.
        const double period=aperture.previewPeriodSeconds>0. ? aperture.previewPeriodSeconds : previewPeriod_;
        return std::remainder(aperture.phaseDegrees+std::remainder(time,period)*360./period,360.);
    }

private:
    std::array<int,3> color_;
    double previewPeriod_;
};

} // namespace vsgraysim::model_packages
