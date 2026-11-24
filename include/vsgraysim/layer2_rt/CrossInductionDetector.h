#pragma once
/// @file CrossInductionDetector.h
/// @brief Layer 2: Cross-induction judgement & crosstalk signal computation.
///
/// Implements stages (2) and (3) of section 3.2.2:
///   - Given a ray emitted by sensor `tx` and an intersected receiver
///     aperture, decide whether the intersection qualifies as a
///     cross-induction event (rule-matched, FoV-in-range, distance-in-range,
///     above-sensitivity-threshold).
///   - Compute crosstalk physical quantities (power, delay, band-match)
///     using the formulas in section 3.2.2 (3).
///
/// This is the CPU reference path; the GPU shader mirrors the same
/// computation in `closesthit.rchit`.

#include <memory>
#include <optional>
#include "vsgraysim/core/RayPayload.h"
#include "vsgraysim/layer0_phys/CrossInductionRules.h"
#include "vsgraysim/layer1_scene/SceneBuilder.h"

namespace vsgraysim {

struct CrossInductionEvent {
    uint32_t    tx_sensor_id;
    uint32_t    rx_sensor_id;
    CrossType   cross_type;
    float       cross_power;     ///< W
    float       cross_delay;     ///< s
    float       band_match;
    float       coupling_factor;
    Vec3        hit_position;
};

class CrossInductionDetector {
public:
    CrossInductionDetector(CrossInductionRulesPtr rules,
                           std::shared_ptr<SceneBuilder> scene);

    /// Test whether ray `payload` (post-atmosphere) triggers a crosstalk
    /// event on any registered aperture.  Returns the highest-priority
    /// event if so, otherwise std::nullopt.
    std::optional<CrossInductionEvent> detect(const RayPayload& payload,
                                             float distance_travelled) const;

    /// Compute crosstalk power: P_cross = P_tx * L_atm * eta * S * band_match
    static float computeCrossPower(float tx_power,
                                   float atm_atten,
                                   float coupling,
                                   float aperture_area,
                                   float band_match);

    const std::vector<SensorAperture>& apertures() const;

private:
    CrossInductionRulesPtr              rules_;
    std::shared_ptr<SceneBuilder>       scene_;
};

using CrossInductionDetectorPtr = std::shared_ptr<CrossInductionDetector>;

} // namespace vsgraysim
