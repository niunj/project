#pragma once
/// @file UndoRedoSystem.h
/// @brief Undo/Redo management with checkpoints and macro support.

#ifndef VSGRAYSIM_PROJECT_UNDOREDO_H
#define VSGRAYSIM_PROJECT_UNDOREDO_H

#include "vsgraysim/project/ICommand.h"
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <mutex>
#include <deque>

namespace vsgraysim {

/// Undo/Redo system
class UndoRedoSystem {
public:
    UndoRedoSystem(size_t max_history = 1000);
    ~UndoRedoSystem() = default;

    // Non-copyable
    UndoRedoSystem(const UndoRedoSystem&) = delete;
    UndoRedoSystem& operator=(const UndoRedoSystem&) = delete;

    /// Execute a command (adds to history, clears redo stack)
    bool execute(std::shared_ptr<ICommand> command);

    /// Undo last command
    bool undo();

    /// Redo last undone command
    bool redo();

    /// Check if undo is available
    bool canUndo() const { return !undo_stack_.empty(); }

    /// Check if redo is available
    bool canRedo() const { return !redo_stack_.empty(); }

    /// Get undo stack descriptions (for UI)
    std::vector<std::string> getUndoDescriptions(size_t count = 10) const;

    /// Get redo stack descriptions
    std::vector<std::string> getRedoDescriptions(size_t count = 10) const;

    /// Clear all history
    void clear();

    /// Create a named checkpoint (for save points)
    void setCheckpoint(const std::string& name);

    /// Rollback to checkpoint (discards history after checkpoint)
    bool rollbackToCheckpoint(const std::string& name);

    /// List checkpoints
    std::vector<std::string> listCheckpoints() const;

    /// Set maximum history size
    void setMaxHistory(size_t max) { max_history_ = max; trimHistory(); }

    /// Register callback for history changes
    using HistoryChangedCallback = std::function<void(bool can_undo, bool can_redo)>;
    void setHistoryChangedCallback(HistoryChangedCallback cb) { history_changed_cb_ = std::move(cb); }

    /// Begin a macro command (groups subsequent commands)
    void beginMacro(const std::string& description);

    /// End macro command (commits or discards)
    void endMacro(bool commit = true);

    /// Check if currently recording a macro
    bool isRecordingMacro() const { return macro_command_ != nullptr; }

private:
    void trimHistory();
    void notifyHistoryChanged();

    size_t max_history_ = 1000;
    std::deque<std::shared_ptr<ICommand>> undo_stack_;
    std::deque<std::shared_ptr<ICommand>> redo_stack_;

    struct Checkpoint {
        std::string name;
        size_t undo_stack_size = 0;
    };
    std::vector<Checkpoint> checkpoints_;

    std::shared_ptr<MacroCommand> macro_command_;
    HistoryChangedCallback history_changed_cb_;
    mutable std::mutex mutex_;
};

} // namespace vsgraysim

#endif // VSGRAYSIM_PROJECT_UNDOREDO_H