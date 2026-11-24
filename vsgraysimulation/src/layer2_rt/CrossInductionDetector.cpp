#include "vsgraysim/layer2_rt/CrossInductionDetector.h"

#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>
#include <limits>
#include <optional>

namespace vsgraysim {

CrossInductionDetector::CrossInductionDetector(CrossInductionRulesPtr rules,
                                               std::shared_ptr<SceneBuilder> scene)
    : rules_(std::move(rules)), scene_(std::move(scene)) {}

float CrossInductionDetector::computeCrossPower(float tx_power,
                                                float atm_atten,
                                                float coupling,
                                                float aperture_area,
                                                float band_match) {
    // P_cross = P_tx * L_atm * eta * S * band_match
    return tx_power * atm_atten * coupling * aperture_area * band_match;
}

const std::vector<SensorAperture>& CrossInductionDetector::apertures() const {
    static const std::vector<SensorAperture> kEmpty;
    return scene_ ? scene_->apertures() : kEmpty;
}

std::optional<CrossInductionEvent>
CrossInductionDetector::detect(const RayPayload& payload,
                                float distance_travelled) const {
    if (!rules_ || !scene_) return std::nullopt;
    if (payload.is_cross_induction) return std::nullopt; // already a crosstalk ray

    // Iterate over routes emitted by the ray's source sensor; pick the
    // first whose receiver aperture satisfies all four judgement criteria.
    auto routes = rules_->routesFrom(payload.tx_sensor_id);
    if (routes.empty()) return std::nullopt;

    std::optional<CrossInductionEvent> best;
    int best_priority = std::numeric_limits<int>::max();

    for (const auto* rt : routes) {
        // Find receiver aperture
        const SensorAperture* ap = nullptr;
        for (const auto& a : scene_->apertures()) {
            if (a.sensor_id == rt->rx_sensor_id) { ap = &a; break; }
        }
        if (!ap) continue;

        // --- Criterion 1: rule-matched (already true via routesFrom) ---
        // --- Criterion 2: ray direction inside receiver FoV ---------------
        // The angle between -ray_dir and aperture boresight must be <= half_angle.
        Vec3 neg_dir{ -payload.dir.x, -payload.dir.y, -payload.dir.z };
        float dot = neg_dir.x * ap->direction.x +
                    neg_dir.y * ap->direction.y +
                    neg_dir.z * ap->direction.z;
        // (Ray dir & aperture dir are unit vectors in caller context.)
        float cos_angle = std::clamp(dot, -1.0f, 1.0f);
        float angle = std::acos(cos_angle);
        if (angle > static_cast<float>(ap->half_angle)) continue;

        // --- Criterion 3: distance within receiver range -------------------
        // (No explicit max range on aperture - use aperture area as proxy)
        if (distance_travelled <= 0.0f) continue;

        // --- Criterion 4: received power above sensitivity threshold -------
        // Sensitivity threshold (W) - simplified 1e-9 W (~-60 dBm)
        constexpr float SENSITIVITY_W = 1e-9f;
        float atm_atten = payload.atm_total_atten;
        float cross_power = computeCrossPower(payload.tx_power,
                                              atm_atten,
                                              rt->coupling_factor,
                                              static_cast<float>(ap->aperture_area),
                                              rt->band_match);
        if (cross_power < SENSITIVITY_W) continue;

        // Crosstalk delay: assume propagation at c
        constexpr float c = 2.99792458e8f;
        float delay = distance_travelled / c;

        CrossInductionEvent ev{};
        ev.tx_sensor_id    = rt->tx_sensor_id;
        ev.rx_sensor_id    = rt->rx_sensor_id;
        ev.cross_type      = rt->cross_type;
        ev.cross_power     = cross_power;
        ev.cross_delay     = delay;
        ev.band_match      = rt->band_match;
        ev.coupling_factor = rt->coupling_factor;
        // Hit position: ray origin + dir * distance
        ev.hit_position.x = payload.origin.x + payload.dir.x * distance_travelled;
        ev.hit_position.y = payload.origin.y + payload.dir.y * distance_travelled;
        ev.hit_position.z = payload.origin.z + payload.dir.z * distance_travelled;

        if (rt->priority < best_priority) {
            best_priority = rt->priority;
            best = ev;
        }
    }
    if (best) {
        spdlog::debug("Layer2: cross-induction detected tx={} -> rx={} type={} P={:.3e}W delay={:.3e}s",
                      best->tx_sensor_id, best->rx_sensor_id,
                      crossTypeName(best->cross_type),
                      best->cross_power, best->cross_delay);
    }
    return best;
}

} // namespace vsgraysim
