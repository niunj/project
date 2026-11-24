#include "vsgraysim/project/UndoRedoSystem.h"

#include <spdlog/spdlog.h>

namespace vsgraysim {

UndoRedoSystem::UndoRedoSystem(size_t max_history)
    : max_history_(max_history) {}

bool UndoRedoSystem::execute(std::shared_ptr<ICommand> command) {
    if (!command) return false;

    std::lock_guard<std::mutex> lock(mutex_);

    // If recording macro, add to macro instead of executing directly
    if (macro_command_) {
        macro_command_->addCommand(command);
        return true;
    }

    // Execute command
    if (!command->execute()) {
        spdlog::warn("Command execution failed: {}", command->description());
        return false;
    }

    // Add to undo stack
    undo_stack_.push_back(command);
    // Clear redo stack on new command
    redo_stack_.clear();

    // Trim history
    trimHistory();

    notifyHistoryChanged();
    return true;
}

bool UndoRedoSystem::undo() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (undo_stack_.empty()) return false;

    auto command = undo_stack_.back();
    undo_stack_.pop_back();

    if (!command->undo()) {
        spdlog::error("Command undo failed: {}", command->description());
        // Put it back
        undo_stack_.push_back(command);
        return false;
    }

    redo_stack_.push_back(command);
    notifyHistoryChanged();
    return true;
}

bool UndoRedoSystem::redo() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (redo_stack_.empty()) return false;

    auto command = redo_stack_.back();
    redo_stack_.pop_back();

    if (!command->execute()) {
        spdlog::error("Command redo failed: {}", command->description());
        redo_stack_.push_back(command);
        return false;
    }

    undo_stack_.push_back(command);
    notifyHistoryChanged();
    return true;
}

std::vector<std::string> UndoRedoSystem::getUndoDescriptions(size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> descs;
    size_t start = undo_stack_.size() > count ? undo_stack_.size() - count : 0;
    for (size_t i = start; i < undo_stack_.size(); ++i) {
        descs.push_back(undo_stack_[i]->description());
    }
    return descs;
}

std::vector<std::string> UndoRedoSystem::getRedoDescriptions(size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> descs;
    size_t start = redo_stack_.size() > count ? redo_stack_.size() - count : 0;
    for (size_t i = start; i < redo_stack_.size(); ++i) {
        descs.push_back(redo_stack_[i]->description());
    }
    return descs;
}

void UndoRedoSystem::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    undo_stack_.clear();
    redo_stack_.clear();
    checkpoints_.clear();
    macro_command_.reset();
    notifyHistoryChanged();
}

void UndoRedoSystem::setCheckpoint(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    checkpoints_.push_back({name, undo_stack_.size()});
    // Limit checkpoints
    if (checkpoints_.size() > 100) {
        checkpoints_.erase(checkpoints_.begin(), checkpoints_.end() - 100);
    }
}

bool UndoRedoSystem::rollbackToCheckpoint(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find_if(checkpoints_.begin(), checkpoints_.end(),
        [&name](const Checkpoint& cp) { return cp.name == name; });

    if (it == checkpoints_.end()) return false;

    // Undo until we reach checkpoint size
    while (undo_stack_.size() > it->undo_stack_size) {
        auto command = undo_stack_.back();
        undo_stack_.pop_back();
        if (!command->undo()) {
            spdlog::error("Rollback undo failed: {}", command->description());
            // Can't easily recover, stop rollback
            break;
        }
        redo_stack_.push_back(command);
    }

    // Remove this and later checkpoints
    checkpoints_.erase(it, checkpoints_.end());

    notifyHistoryChanged();
    return true;
}

std::vector<std::string> UndoRedoSystem::listCheckpoints() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& cp : checkpoints_) {
        names.push_back(cp.name);
    }
    return names;
}

void UndoRedoSystem::beginMacro(const std::string& description) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (macro_command_) {
        spdlog::warn("Macro already recording, nesting not supported");
        return;
    }
    macro_command_ = std::make_shared<MacroCommand>(description);
}

void UndoRedoSystem::endMacro(bool commit) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!macro_command_) return;

    if (commit && macro_command_->hasCommands()) {
        // Execute the macro command
        if (macro_command_->execute()) {
            undo_stack_.push_back(macro_command_);
            trimHistory();
        }
    }
    macro_command_.reset();
    notifyHistoryChanged();
}

void UndoRedoSystem::trimHistory() {
    while (undo_stack_.size() > max_history_) {
        undo_stack_.pop_front();
    }
    while (redo_stack_.size() > max_history_) {
        redo_stack_.pop_front();
    }
}

void UndoRedoSystem::notifyHistoryChanged() {
    if (history_changed_cb_) {
        history_changed_cb_(canUndo(), canRedo());
    }
}

} // namespace vsgraysim