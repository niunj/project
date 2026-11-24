#include "vsgraysim/layer0_phys/AtmosphereModel.h"

#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace vsgraysim {

using json = nlohmann::json;

namespace {

/// Linear interpolation.
float lerp(float a, float b, float t) { return a + (b - a) * t; }

void fillBandArray(float (&dst)[4], const json& j, const char* key,
                   float def) {
    for (int i = 0; i < 4; ++i) dst[i] = def;
    if (!j.contains(key)) return;
    const auto& a = j[key];
    if (a.is_array()) {
        for (int i = 0; i < std::min<int>(4, (int)a.size()); ++i) {
            dst[i] = a[i].get<float>();
        }
    } else if (a.is_number()) {
        for (int i = 0; i < 4; ++i) dst[i] = a.get<float>();
    }
}

} // anonymous namespace

void AtmosphereModel::loadConfig(const std::string& json_path) {
    std::ifstream ifs(json_path);
    if (!ifs) throw std::runtime_error("AtmosphereModel: cannot open " + json_path);
    json j;
    ifs >> j;

    layers_.clear();
    min_height_ = 1e9f;
    max_height_ = -1e9f;

    for (const auto& lj : j.at("layers")) {
        AtmLayerParams p{};
        p.height_min = lj.at("height_min").get<float>();
        p.height_max = lj.at("height_max").get<float>();
        p.temp       = lj.value("temp", 288.15f);
        p.pressure   = lj.value("pressure", 101325.0f);
        p.humidity   = lj.value("humidity", 0.5f);
        p.density    = lj.value("density", 1.225f);
        p.aerosol    = lj.value("aerosol", 1e-6f);

        fillBandArray(p.ext_coeff,     lj, "ext_coeff",     1e-4f);
        fillBandArray(p.scatter_coeff, lj, "scatter_coeff", 1e-5f);
        fillBandArray(p.absorb_coeff,  lj, "absorb_coeff",  1e-5f);

        p.refract_index = lj.value("refract_index", 1.0003f);
        p.turbulence    = lj.value("turbulence", 1e-15f);
        p.cloud_density = lj.value("cloud_density", 0.0f);
        p.reserved[0]   = 0.0f;
        p.reserved[1]   = 0.0f;

        min_height_ = std::min(min_height_, p.height_min);
        max_height_ = std::max(max_height_, p.height_max);
        layers_.push_back(p);
    }
    spdlog::info("Layer0: atmosphere loaded with {} slabs (alt {:.0f}m .. {:.0f}m)",
                 layers_.size(), min_height_, max_height_);
}

AtmLayerParams AtmosphereModel::sampleAtAltitude(float h) const {
    if (layers_.empty()) {
        AtmLayerParams p{};
        p.height_min = 0; p.height_max = 1e6f;
        p.temp = 288.15f; p.pressure = 101325.0f;
        p.humidity = 0.5f; p.density = 1.225f; p.aerosol = 0.0f;
        p.refract_index = 1.0003f; p.turbulence = 1e-15f;
        return p;
    }
    // Find slab i s.t. layers_[i].height_min <= h < layers_[i].height_max
    std::size_t i = 0;
    while (i + 1 < layers_.size() && h >= layers_[i].height_max) ++i;
    if (i + 1 >= layers_.size()) return layers_.back();

    const auto& a = layers_[i];
    const auto& b = layers_[i + 1];
    float t = (h - a.height_max) / std::max(1.0f, (b.height_min - a.height_max));
    t = std::clamp(t, 0.0f, 1.0f);

    AtmLayerParams r{};
    r.height_min = a.height_max;
    r.height_max = b.height_min;
    r.temp        = lerp(a.temp, b.temp, t);
    r.pressure   = lerp(a.pressure, b.pressure, t);
    r.humidity   = lerp(a.humidity, b.humidity, t);
    r.density    = lerp(a.density, b.density, t);
    r.aerosol    = lerp(a.aerosol, b.aerosol, t);
    r.refract_index = lerp(a.refract_index, b.refract_index, t);
    r.turbulence    = lerp(a.turbulence, b.turbulence, t);
    r.cloud_density = lerp(a.cloud_density, b.cloud_density, t);
    for (int k = 0; k < 4; ++k) {
        r.ext_coeff[k]     = lerp(a.ext_coeff[k],     b.ext_coeff[k],     t);
        r.scatter_coeff[k] = lerp(a.scatter_coeff[k], b.scatter_coeff[k], t);
        r.absorb_coeff[k]  = lerp(a.absorb_coeff[k],  b.absorb_coeff[k],  t);
    }
    return r;
}

void AtmosphereModel::updateDynamic(uint32_t layer_id,
                                    float turbulence,
                                    float aerosol,
                                    float cloud_density) {
    if (layer_id >= layers_.size()) {
        spdlog::warn("AtmosphereModel::updateDynamic: bad layer_id {}", layer_id);
        return;
    }
    auto& L = layers_[layer_id];
    L.turbulence    = turbulence;
    L.aerosol       = aerosol;
    L.cloud_density = cloud_density;
    spdlog::debug("Layer0: atmosphere layer {} updated (turb={:.2e}, aerosol={:.2e}, cloud={:.2f})",
                  layer_id, turbulence, aerosol, cloud_density);
}

void AtmosphereModel::applyWeatherScenario(const std::string& scenario,
                                           float severity) {
    severity = std::clamp(severity, 0.0f, 2.0f);
    float cloud_mul = 1.0f, turb_mul = 1.0f, ext_mul = 1.0f;
    if      (scenario == "clear")  { cloud_mul = 0.1f; turb_mul = 0.5f; ext_mul = 0.8f; }
    else if (scenario == "cloudy") { cloud_mul = 5.0f; turb_mul = 1.0f; ext_mul = 1.5f; }
    else if (scenario == "rain")   { cloud_mul = 8.0f; turb_mul = 2.0f; ext_mul = 2.0f; }
    else if (scenario == "fog")    { cloud_mul = 3.0f; turb_mul = 3.0f; ext_mul = 5.0f; }
    else { spdlog::warn("Unknown weather scenario '{}'", scenario); return; }

    for (auto& L : layers_) {
        L.cloud_density *= cloud_mul * severity;
        L.turbulence    *= turb_mul * severity;
        for (int k = 0; k < 4; ++k) {
            L.ext_coeff[k]     *= ext_mul;
            L.scatter_coeff[k] *= ext_mul;
        }
    }
    spdlog::info("Layer0: weather scenario '{}' applied (severity={:.2f})", scenario, severity);
}

} // namespace vsgraysim
