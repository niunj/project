#pragma once
/// @file ICommand.h
/// @brief Command interface for Undo/Redo system.

#ifndef VSGRAYSIM_PROJECT_ICOMMAND_H
#define VSGRAYSIM_PROJECT_ICOMMAND_H

#include <string>
#include <memory>
#include <any>
#include <nlohmann/json.hpp>

namespace vsgraysim {

/// Base command interface
class ICommand {
public:
    virtual ~ICommand() = default;

    /// Unique command identifier (for debugging/history)
    virtual std::string id() const = 0;

    /// Human-readable description (for UI)
    virtual std::string description() const = 0;

    /// Execute the command (do)
    virtual bool execute() = 0;

    /// Undo the command (rollback)
    virtual bool undo() = 0;

    /// Check if command can be merged with another (for macro commands)
    virtual bool canMerge(const ICommand* other) const { return false; }

    /// Merge with another command of same type
    virtual bool merge(std::shared_ptr<ICommand> other) { return false; }

    /// Serialize to JSON (for persistence)
    virtual nlohmann::json serialize() const { return {}; }

    /// Deserialize from JSON
    virtual bool deserialize(const nlohmann::json& json) { return false; }

    /// Get affected subsystem name
    virtual std::string subsystem() const = 0;

    /// Get affected product type
    virtual std::string productType() const = 0;
};

/// Macro command (groups multiple commands)
class MacroCommand : public ICommand {
public:
    MacroCommand(const std::string& description = "Macro Command");

    std::string id() const override { return macro_id_; }
    std::string description() const override { return description_; }
    std::string subsystem() const override { return "Macro"; }
    std::string productType() const override { return "Multiple"; }

    void addCommand(std::shared_ptr<ICommand> cmd);
    bool execute() override;
    bool undo() override;

    nlohmann::json serialize() const override;
    bool deserialize(const nlohmann::json& json) override;

    bool hasCommands() const { return !commands_.empty(); }
    const std::vector<std::shared_ptr<ICommand>>& getCommands() const { return commands_; }

protected:
    std::string macro_id_;
    std::string description_;
    std::vector<std::shared_ptr<ICommand>> commands_;
    bool executed_ = false;
};

/// Command that wraps a lambda (for simple operations)
template<typename DoFn, typename UndoFn>
class LambdaCommand : public ICommand {
public:
    LambdaCommand(const std::string& id, const std::string& desc, const std::string& subsystem, const std::string& product_type,
                  DoFn do_fn, UndoFn undo_fn)
        : id_(id), desc_(desc), subsystem_(subsystem), product_type_(product_type),
          do_fn_(std::move(do_fn)), undo_fn_(std::move(undo_fn)) {}

    std::string id() const override { return id_; }
    std::string description() const override { return desc_; }
    std::string subsystem() const override { return subsystem_; }
    std::string productType() const override { return product_type_; }

    bool execute() override { return do_fn_(); }
    bool undo() override { return undo_fn_(); }

private:
    std::string id_;
    std::string desc_;
    std::string subsystem_;
    std::string product_type_;
    DoFn do_fn_;
    UndoFn undo_fn_;
};

/// Helper to create lambda commands
template<typename DoFn, typename UndoFn>
std::shared_ptr<ICommand> makeCommand(const std::string& id, const std::string& desc,
                                      const std::string& subsystem, const std::string& product_type,
                                      DoFn do_fn, UndoFn undo_fn) {
    return std::make_shared<LambdaCommand<DoFn, UndoFn>>(id, desc, subsystem, product_type,
                                                         std::move(do_fn), std::move(undo_fn));
}

} // namespace vsgraysim

#endif // VSGRAYSIM_PROJECT_ICOMMAND_H