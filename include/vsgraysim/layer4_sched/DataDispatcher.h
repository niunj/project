#pragma once
/// @file DataDispatcher.h
/// @brief Layer 4: Routes the post-RT data into per-sensor CUDA streams.
///
/// Reads the scatter-point buffer produced by Layer 2 (via the Vulkan-CUDA
/// shared memory handle) and partitions it into:
///   - normal scatter points (sensor receives its own emission's echo);
///   - cross-induction events (crosstalk into another sensor's stream).
///
/// Cross-induction events are dispatched in priority order (0 = highest)
/// to the matching receiver's CUDA stream.  This implements section 3.2.2
/// stage (4) "Data directed dispatch".

#include <cstdint>
#include <memory>
#include <vector>
#include "vsgraysim/core/RayPayload.h"
#include "vsgraysim/layer0_phys/CrossInductionRules.h"

namespace vsgraysim {

class SignalProcessor;

/// One routed batch of scatter points intended for a single receiver.
struct RoutedBatch {
    uint32_t                  rx_sensor_id;
    bool                      is_crosstalk;
    std::vector<ScatterPoint> points;
};

class DataDispatcher {
public:
    DataDispatcher(CrossInductionRulesPtr rules,
                   std::shared_ptr<SignalProcessor> signal_proc);

    /// Partition a flat scatter-point list into per-receiver routed batches.
    /// Crosstalk batches are appended in priority order (highest priority
    /// first).  Normal batches come before any crosstalk batches for the
    /// same receiver.
    std::vector<RoutedBatch> dispatch(std::vector<ScatterPoint> sps);

    /// Helper: total number of crosstalk events in the last dispatch().
    std::size_t lastCrosstalkCount() const { return last_cross_count_; }

private:
    CrossInductionRulesPtr              rules_;
    std::shared_ptr<SignalProcessor>    signal_proc_;
    std::size_t                          last_cross_count_ = 0;
};

using DataDispatcherPtr = std::shared_ptr<DataDispatcher>;

} // namespace vsgraysim
