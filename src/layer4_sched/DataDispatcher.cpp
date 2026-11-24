#include "vsgraysim/layer4_sched/DataDispatcher.h"
#include "vsgraysim/layer3_cuda/SignalProcessor.h"

#include <algorithm>
#include <unordered_map>
#include <spdlog/spdlog.h>

namespace vsgraysim {

DataDispatcher::DataDispatcher(CrossInductionRulesPtr rules,
                               std::shared_ptr<SignalProcessor> signal_proc)
    : rules_(std::move(rules)), signal_proc_(std::move(signal_proc)) {}

std::vector<RoutedBatch> DataDispatcher::dispatch(std::vector<ScatterPoint> sps) {
    std::vector<RoutedBatch> out;
    last_cross_count_ = 0;

    // Bucket by (rx_sensor_id, is_crosstalk)
    std::unordered_map<uint32_t, std::size_t> normal_idx; // rx -> batch idx
    std::unordered_map<uint32_t, std::size_t> cross_idx;  // rx -> batch idx

    for (auto& sp : sps) {
        uint32_t rx = sp.has_cross ? sp.cross_rx_sensor_id : sp.rx_sensor_id;
        if (rx == 0) continue; // no receiver assigned
        if (sp.has_cross) {
            auto it = cross_idx.find(rx);
            if (it == cross_idx.end()) {
                RoutedBatch b{rx, true, {}};
                out.push_back(std::move(b));
                cross_idx[rx] = out.size() - 1;
                ++last_cross_count_;
            }
            out[cross_idx[rx]].points.push_back(sp);
        } else {
            auto it = normal_idx.find(rx);
            if (it == normal_idx.end()) {
                RoutedBatch b{rx, false, {}};
                out.push_back(std::move(b));
                normal_idx[rx] = out.size() - 1;
            }
            out[normal_idx[rx]].points.push_back(sp);
        }
    }

    // Reorder: crosstalk batches first, in priority order
    if (rules_) {
        const auto& routes = rules_->routes();
        std::sort(out.begin(), out.end(),
            [&](const RoutedBatch& a, const RoutedBatch& b) {
                if (a.is_crosstalk != b.is_crosstalk) return a.is_crosstalk; // cross first
                if (!a.is_crosstalk) return a.rx_sensor_id < b.rx_sensor_id;
                // Both are crosstalk: lookup priority
                auto find_prio = [&](uint32_t rx) -> int {
                    for (const auto& r : routes) {
                        if (r.rx_sensor_id == rx) return r.priority;
                    }
                    return std::numeric_limits<int>::max();
                };
                return find_prio(a.rx_sensor_id) < find_prio(b.rx_sensor_id);
            });
    }

    spdlog::info("Layer4: dispatched {} points ({} crosstalk) across {} batches",
                 sps.size(), last_cross_count_, out.size());
    return out;
}

} // namespace vsgraysim
