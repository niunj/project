#pragma once
/// @file CrossInductionRules.h
/// @brief Layer 0: Multi-sensor cross-induction rule manager (section 3.2.1).
///
/// Loads the `sensor_cross_induction.json` configuration and provides:
///   - sensor metadata (id, type, tx params, rx cross-sensor list);
///   - crosstalk priority ordering;
///   - lookup helpers used by the RT cross-induction detector and by the
///     Layer-4 dispatcher.

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include "vsgraysim/core/SensorTypes.h"
#include "vsgraysim/core/BandTypes.h"
#include <nlohmann/json.hpp>

namespace vsgraysim {

/// A crosstalk routing entry: tx -> rx with priority & coupling info.
struct CrossRoute {
    uint32_t    tx_sensor_id;
    uint32_t    rx_sensor_id;
    SensorType  tx_type;
    SensorType  rx_type;
    CrossType   cross_type;
    int         priority;          ///< 0 = highest
    float       coupling_factor;
    float       band_match;
    bool        is_process;
};

class CrossInductionRules {
public:
    /// Load `sensor_cross_induction.json`.
    void loadConfig(const std::string& json_path);

    /// Load from an in-memory JSON object (same schema as loadConfig:
    /// {"sensor_list": [...], "cross_priority": {...}}).
    void loadConfigFromJson(const nlohmann::json& j);

    /// Build the crosstalk routing table (sorted by priority).
    void buildRoutes();

    /// Lookup a sensor entry by id (nullptr if missing).
    const SensorCrossEntry* findSensor(uint32_t id) const;

    /// Enumerate cross-routes whose tx sensor matches `tx_id`.
    std::vector<const CrossRoute*> routesFrom(uint32_t tx_id) const;

    /// All routes, sorted by ascending priority (highest priority first).
    const std::vector<CrossRoute>& routes() const { return routes_; }

    const std::vector<SensorCrossEntry>& sensors() const { return sensors_; }

private:
    std::vector<SensorCrossEntry>                sensors_;
    std::unordered_map<uint32_t, std::size_t>    sensor_index_;
    std::vector<CrossRoute>                      routes_;
    std::map<std::string, int>                   priority_overrides_;
};

using CrossInductionRulesPtr = std::shared_ptr<CrossInductionRules>;

/// Convert a (tx_type, rx_type) pair into a CrossType code.
CrossType classifyCross(SensorType tx, SensorType rx);

} // namespace vsgraysim
