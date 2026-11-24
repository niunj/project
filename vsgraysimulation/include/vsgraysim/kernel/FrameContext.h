#pragma once
/// @file FrameContext.h
/// @brief Per-frame shared context (products + transient data).

#ifndef VSGRAYSIM_KERNEL_FRAMECONTEXT_H
#define VSGRAYSIM_KERNEL_FRAMECONTEXT_H

#include "vsgraysim/products/Products.h"
#include "vsgraysim/kernel/TimeTick.h"
#include "vsgraysim/kernel/SimulationEvent.h"

#include <memory>
#include <vector>
#include <unordered_map>
#include <any>
#include <mutex>

namespace vsgraysim {

/// Frame context — holds all products and transient data for one simulation frame.
/// Managed by TimingBus, passed to subsystems via pointers.
struct FrameContext {
    // Immutable products (set at project load, read-only during simulation)
    TerrainProductConstPtr terrain;
    SceneProductConstPtr scene;

    // Mutable products (written each frame by respective subsystems)
    EquipmentStateProductPtr equipment_state;
    DisplayFrameProductPtr display_frame;

    // Current time tick
    TimeTick tick;

    // Event queue for this frame (published during onTick, dispatched in onPostTick)
    std::vector<SimulationEvent> events;

    // Scratchpad for inter-subsystem transient data (key -> any)
    std::unordered_map<std::string, std::any> scratchpad;

    // Mutex for thread-safe access to scratchpad/events
    mutable std::mutex mutex;

    // ---- Helpers ----

    /// Clear per-frame transient data
    void clearTransient() {
        if (equipment_state) equipment_state->clearTransient();
        if (display_frame) display_frame->clearTransient();
        events.clear();
        scratchpad.clear();
    }

    /// Publish event (thread-safe)
    void publishEvent(const SimulationEvent& evt) {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back(evt);
    }

    /// Get scratchpad value (type-safe)
    template<typename T>
    std::shared_ptr<T> getScratchpad(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = scratchpad.find(key);
        if (it != scratchpad.end()) {
            try {
                return std::any_cast<std::shared_ptr<T>>(it->second);
            } catch (const std::bad_any_cast&) {
                return nullptr;
            }
        }
        return nullptr;
    }

    /// Set scratchpad value
    template<typename T>
    void setScratchpad(const std::string& key, std::shared_ptr<T> value) {
        std::lock_guard<std::mutex> lock(mutex);
        scratchpad[key] = std::move(value);
    }
};

using FrameContextPtr = std::shared_ptr<FrameContext>;

} // namespace vsgraysim

#endif // VSGRAYSIM_KERNEL_FRAMECONTEXT_H