#pragma once
/// @file SimulationEvent.h
/// @brief Cross-subsystem event definitions.

#ifndef VSGRAYSIM_KERNEL_SIMULATIONEVENT_H
#define VSGRAYSIM_KERNEL_SIMULATIONEVENT_H

#include <string>
#include <unordered_map>
#include <any>
#include <cstdint>
#include <functional>

namespace vsgraysim {

/// Simulation event types
enum class EventType {
    // Entity lifecycle
    EntitySpawned,
    EntityDespawned,
    EntityTransformChanged,

    // Physics
    CollisionStarted,
    CollisionEnded,
    CollisionPersistent,

    // Sensors
    SensorDetection,
    SensorTrackInitiated,
    SensorTrackUpdated,
    SensorTrackLost,

    // Weapons / Effects
    WeaponLaunched,
    WeaponDetonated,
    EffectSpawned,

    // Communications
    CommsLinkEstablished,
    CommsLinkLost,
    MessageReceived,

    // Damage / Health
    DamageApplied,
    EntityDestroyed,
    HealthChanged,

    // System
    SubsystemStarted,
    SubsystemStopped,
    SubsystemError,
    ConfigurationChanged,

    // Custom (user-defined)
    Custom
};

/// Event payload (type-erased)
struct SimulationEvent {
    EventType type = EventType::Custom;
    std::string source_subsystem;      // e.g., "PlatformEquipmentSystem"
    std::string source_entity_id;      // Optional
    std::string target_entity_id;      // Optional
    uint64_t frame_id = 0;
    double timestamp = 0.0;            // Simulation time
    std::unordered_map<std::string, std::any> payload;
    std::string description;           // Human-readable for logging

    // Type-safe payload access
    template<typename T>
    std::optional<T> get(const std::string& key) const {
        auto it = payload.find(key);
        if (it != payload.end()) {
            try {
                return std::any_cast<T>(it->second);
            } catch (const std::bad_any_cast&) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    template<typename T>
    void set(const std::string& key, T value) {
        payload[key] = std::move(value);
    }
};

/// Event subscription callback
using EventCallback = std::function<void(const SimulationEvent&)>;

/// Event bus interface (implemented by TimingBus)
class IEventBus {
public:
    virtual ~IEventBus() = default;

    /// Subscribe to event type
    virtual void subscribe(EventType type, EventCallback callback) = 0;

    /// Unsubscribe
    virtual void unsubscribe(EventType type, EventCallback callback) = 0;

    /// Publish event (immediate or queued)
    virtual void publish(const SimulationEvent& event) = 0;

    /// Publish event (by type)
    virtual void publish(EventType type, const SimulationEvent& event) = 0;
};

} // namespace vsgraysim

#endif // VSGRAYSIM_KERNEL_SIMULATIONEVENT_H