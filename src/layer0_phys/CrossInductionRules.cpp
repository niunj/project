#include "vsgraysim/layer0_phys/CrossInductionRules.h"

#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace vsgraysim {

using json = nlohmann::json;

namespace {

SensorType parseSensorType(const std::string& s) {
    if (s == "vis")   return SensorType::VIS;
    if (s == "laser") return SensorType::LASER;
    if (s == "ir")    return SensorType::IR;
    if (s == "radar") return SensorType::RADAR;
    if (s == "sar")   return SensorType::SAR;
    throw std::runtime_error("Unknown sensor type: " + s);
}

} // anonymous namespace

CrossType classifyCross(SensorType tx, SensorType rx) {
    if (tx == SensorType::LASER && rx == SensorType::IR)   return CrossType::LASER_TO_IR;
    if (tx == SensorType::RADAR && rx == SensorType::SAR)  return CrossType::RADAR_TO_SAR;
    if (tx == SensorType::LASER && rx == SensorType::VIS)  return CrossType::LASER_TO_VIS;
    if (tx == SensorType::IR   && rx == SensorType::RADAR) return CrossType::IR_TO_RADAR;
    return CrossType::CUSTOM;
}

void CrossInductionRules::loadConfig(const std::string& json_path) {
    spdlog::info("Layer0: loading cross-induction config from '{}'", json_path);
    std::ifstream ifs(json_path);
    if (!ifs) throw std::runtime_error("CrossInductionRules: cannot open " + json_path);
    json j;
    ifs >> j;
    loadConfigFromJson(j);
}

void CrossInductionRules::loadConfigFromJson(const json& j) {
    sensors_.clear();
    sensor_index_.clear();

    for (const auto& sj : j.at("sensor_list")) {
        SensorCrossEntry e{};
        e.sensor_id  = sj.at("sensor_id").get<uint32_t>();
        e.type       = parseSensorType(sj.at("type").get<std::string>());
        e.band_label = sj.value("band", std::string{});

        const auto& tx = sj.at("tx_params");
        e.tx_params.power       = tx.value("power", 0.0f);
        e.tx_params.beam_width  = tx.value("beam_width", 0.0f);
        e.tx_params.wavelength  = tx.value("wavelength", 0.0f);
        e.tx_params.pulse_rate  = tx.value("pulse_rate", 0.0f);
        e.tx_params.duty_cycle  = tx.value("duty_cycle", 0.0f);

        for (const auto& rx : sj.value("rx_cross_sensors", json::array())) {
            RxCrossSensor r{};
            r.rx_sensor_id     = rx.at("sensor_id").get<uint32_t>();
            r.rx_type          = parseSensorType(rx.at("type").get<std::string>());
            r.band_label       = rx.value("band", std::string{});
            r.receive_range    = rx.value("receive_range", 0.05f);
            r.coupling_factor  = rx.value("coupling_factor", 0.0f);
            r.band_match       = rx.value("band_match", 1.0f);
            r.is_process       = rx.value("is_process", true);
            e.rx_cross_sensors.push_back(r);
        }

        sensor_index_[e.sensor_id] = sensors_.size();
        sensors_.push_back(std::move(e));
    }

    // Priority overrides (optional, JSON object mapping "tx_to_rx" -> priority)
    priority_overrides_.clear();
    if (j.contains("cross_priority")) {
        for (auto it = j["cross_priority"].begin(); it != j["cross_priority"].end(); ++it) {
            priority_overrides_[it.key()] = it.value().get<int>();
        }
    }

    spdlog::info("Layer0: cross-induction config loaded ({} sensors)",
                 sensors_.size());
}

void CrossInductionRules::buildRoutes() {
    routes_.clear();
    for (const auto& s : sensors_) {
        for (const auto& r : s.rx_cross_sensors) {
            CrossRoute rt{};
            rt.tx_sensor_id    = s.sensor_id;
            rt.rx_sensor_id    = r.rx_sensor_id;
            rt.tx_type         = s.type;
            rt.rx_type         = r.rx_type;
            rt.cross_type      = classifyCross(s.type, r.rx_type);
            rt.coupling_factor = r.coupling_factor;
            rt.band_match      = r.band_match;
            rt.is_process      = r.is_process;

            // Default priority: derive from cross type
            switch (rt.cross_type) {
                case CrossType::LASER_TO_IR:  rt.priority = 0; break;
                case CrossType::RADAR_TO_SAR: rt.priority = 1; break;
                case CrossType::LASER_TO_VIS: rt.priority = 2; break;
                default:                      rt.priority = 3; break;
            }
            // Apply user override
            std::string key = std::string(sensorTypeName(s.type)) + "_to_" +
                              std::string(sensorTypeName(r.rx_type));
            auto pit = priority_overrides_.find(key);
            if (pit != priority_overrides_.end()) rt.priority = pit->second;

            routes_.push_back(rt);
        }
    }
    std::sort(routes_.begin(), routes_.end(),
              [](const CrossRoute& a, const CrossRoute& b) { return a.priority < b.priority; });
    spdlog::info("Layer0: built {} cross-induction routes (priority-sorted)",
                 routes_.size());
}

const SensorCrossEntry* CrossInductionRules::findSensor(uint32_t id) const {
    auto it = sensor_index_.find(id);
    return (it == sensor_index_.end()) ? nullptr : &sensors_[it->second];
}

std::vector<const CrossRoute*> CrossInductionRules::routesFrom(uint32_t tx_id) const {
    std::vector<const CrossRoute*> out;
    for (const auto& r : routes_) {
        if (r.tx_sensor_id == tx_id) out.push_back(&r);
    }
    return out;
}

} // namespace vsgraysim
