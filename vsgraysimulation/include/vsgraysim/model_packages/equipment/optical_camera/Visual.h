#pragma once

#include <vsgraysim/model_sdk/EquipmentVisual.h>

namespace vsgraysim::model_packages {

class OpticalCameraVisual final : public model_sdk::EquipmentVisual {
public:
    explicit OpticalCameraVisual(std::array<int,3> color): color_(color) {}

    model_sdk::EquipmentVisualStyle style(const model_sdk::ApertureDisplay& aperture) const override {
        return {color_, aperture.horizontalDegrees, aperture.verticalDegrees, aperture.rangeMetres, false};
    }

    double azimuthDegrees(const model_sdk::ApertureDisplay&, double) const override { return 0.; }

private:
    std::array<int,3> color_;
};

} // namespace vsgraysim::model_packages
