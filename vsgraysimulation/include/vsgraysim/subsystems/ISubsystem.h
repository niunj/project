#pragma once
/// @file ISubsystem.h
/// @brief Base interface for all subsystems (editor + simulation).

#ifndef VSGRAYSIM_SUBSYSTEMS_ISUBSYSTEM_H
#define VSGRAYSIM_SUBSYSTEMS_ISUBSYSTEM_H

#include <string>
#include <memory>
#include <any>
#include <functional>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace vsgraysim {

/// Base subsystem interface
class ISubsystem {
public:
    virtual ~ISubsystem() = default;

    /// Unique subsystem name (e.g., "TerrainSystem", "SceneSystem")
    virtual std::string name() const = 0;

    /// Subsystem version
    virtual std::string version() const { return "1.0.0"; }

    /// Initialize from JSON configuration
    /// Called once at application startup.
    virtual bool initialize(const nlohmann::json& config) = 0;

    /// Shutdown and release resources
    virtual void shutdown() = 0;

    /// Get subsystem health status
    virtual bool isHealthy() const { return true; }

    /// Get diagnostic information (for debugging/UI)
    virtual nlohmann::json diagnostics() const { return {}; }

    /// Optional: handle application-level events
    virtual void onEvent(const std::string& event_type, const nlohmann::json& payload) {}
};

/// Editor-mode subsystem interface (extends ISubsystem)
class IEditorSubsystem : public ISubsystem {
public:
    ~IEditorSubsystem() override = default;

    /// Produce the data product (called when saving project)
    /// Returns a shared_ptr to the product (type-erased).
    virtual std::shared_ptr<void> produceProduct() const = 0;

    /// Consume a data product (called when loading project)
    /// The product type must match what this subsystem produces.
    virtual void consumeProduct(std::shared_ptr<const void> product) = 0;

    /// Execute an editor command (for Undo/Redo)
    /// Returns true if command was accepted and executed.
    virtual bool executeCommand(std::shared_ptr<void> command) = 0;

    /// Get the product type ID this subsystem produces
    virtual std::string productTypeId() const = 0;

    /// Check if subsystem has unsaved changes
    virtual bool isDirty() const { return false; }

    /// Mark dirty/clean
    virtual void setDirty(bool dirty) {}
};

/// Simulation-mode subsystem interface (extends ISubsystem)
class ISimulationSubsystem : public ISubsystem {
public:
    ~ISimulationSubsystem() override = default;

    /// Called before tick - read inputs, prepare
    virtual void onPreTick(const class TimeTick& tick) = 0;

    /// Core simulation tick - compute, write outputs
    virtual void onTick(const class TimeTick& tick) = 0;

    /// Called after tick - publish events, cleanup
    virtual void onPostTick(const class TimeTick& tick) = 0;

    /// Called when simulation starts
    virtual void onStart() {}

    /// Called when simulation stops
    virtual void onStop() {}

    /// Called for each sub-step (fixed timestep mode)
    virtual void onSubStep(const class TimeTick& tick, int substep_idx) {}
    /// Opt in explicitly; frame-rate systems still receive onTick once when
    /// the physics scheduler uses substeps.
    virtual bool usesSubSteps() const { return false; }

    /// Get mutable product pointers for writing (simulation mode only)
    virtual void* getMutableProduct(const std::string& product_type) = 0;
    virtual const void* getConstProduct(const std::string& product_type) const = 0;

    /// Priority for topological sorting (lower = earlier)
    virtual int tickPriority() const = 0;
};

/// Dual-mode subsystem (both editor and simulation)
class IDualModeSubsystem : public IEditorSubsystem, public ISimulationSubsystem {
public:
    ~IDualModeSubsystem() override = default;
};

/// Factory function type
using SubsystemFactoryFn = std::function<std::shared_ptr<ISubsystem>()>;

/// Subsystem registry (singleton)
class SubsystemRegistry {
public:
    static SubsystemRegistry& instance();

    /// Register a subsystem type
    void registerType(const std::string& type_name, SubsystemFactoryFn factory);

    /// Create subsystem instance by type name
    std::shared_ptr<ISubsystem> create(const std::string& type_name) const;

    /// List all registered types
    std::vector<std::string> listTypes() const;

    /// Check if type exists
    bool hasType(const std::string& type_name) const;

private:
    SubsystemRegistry() = default;
    std::unordered_map<std::string, SubsystemFactoryFn> factories_;
};

} // namespace vsgraysim

#endif // VSGRAYSIM_SUBSYSTEMS_ISUBSYSTEM_H
