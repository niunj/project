#pragma once
/// @file ProjectManager.h
/// @brief High-level project management — load/save, dependency tracking, recent files.

#ifndef VSGRAYSIM_PROJECT_PROJECTMANAGER_H
#define VSGRAYSIM_PROJECT_PROJECTMANAGER_H

#include "vsgraysim/project/ProjectSerializer.h"
#include "vsgraysim/project/UndoRedoSystem.h"
#include "vsgraysim/products/Products.h"

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>
#include <filesystem>

namespace vsgraysim {

/// Project metadata
struct ProjectInfo {
    std::string path;                 // Full path to .vsgterrain or .vsgscene
    std::string name;                 // Project name
    std::string type;                 // "terrain" or "scene"
    std::string product_id;           // UUID
    std::string created;              // ISO 8601
    std::string modified;             // ISO 8601
    bool is_modified = false;         // Unsaved changes
    bool is_read_only = false;        // File permissions
    uint64_t file_size = 0;           // Bytes
};

/// ProjectManager — central project lifecycle manager
class ProjectManager {
public:
    explicit ProjectManager(std::shared_ptr<UndoRedoSystem> undo_redo = nullptr);
    ~ProjectManager() = default;

    // Non-copyable
    ProjectManager(const ProjectManager&) = delete;
    ProjectManager& operator=(const ProjectManager&) = delete;

    // ---- Project lifecycle ----

    /// Create new terrain project
    TerrainProductPtr newTerrainProject(const std::string& name,
                                        const std::string& crs = "EPSG:4978",
                                        const vsg::dbox& bounds = {{-5000, -100, -5000}, {5000, 5000, 5000}},
                                        double resolution = 1.0);

    /// Create new scene project
    SceneProductPtr newSceneProject(const std::string& name,
                                    const TerrainProductPtr& terrain = nullptr);

    /// Load project from file (auto-detect type)
    std::shared_ptr<void> loadProject(const std::string& path);

    /// Load terrain project
    TerrainProductPtr loadTerrainProject(const std::string& path);

    /// Load scene project
    SceneProductPtr loadSceneProject(const std::string& path);

    /// Save current project
    bool saveProject();

    /// Save project as (new path)
    bool saveProjectAs(const std::string& path);

    /// Close current project
    void closeProject();

    // ---- Current project access ----

    TerrainProductPtr currentTerrain() const { return current_terrain_; }
    SceneProductPtr currentScene() const { return current_scene_; }
    std::shared_ptr<void> currentProduct() const {
        return current_terrain_ ? std::static_pointer_cast<void>(current_terrain_)
                                : std::static_pointer_cast<void>(current_scene_);
    }

    std::string currentPath() const { return current_path_; }
    std::string currentType() const { return current_type_; }
    bool hasProject() const { return current_terrain_ || current_scene_; }
    bool isModified() const { return is_modified_; }

    // ---- Modification tracking ----

    void markModified(bool modified = true);
    bool promptSaveIfModified();  // Returns true if safe to continue (saved or discarded)

    // ---- Recent files ----

    void addRecentFile(const std::string& path);
    std::vector<ProjectInfo> recentFiles(size_t max = 10) const;
    void clearRecentFiles();

    // ---- Callbacks ----

    using ProjectLoadedCallback = std::function<void(const std::string& path, const std::string& type)>;
    using ProjectSavedCallback = std::function<void(const std::string& path)>;
    using ProjectClosedCallback = std::function<void()>;
    using ModifiedChangedCallback = std::function<void(bool modified)>;

    void setProjectLoadedCallback(ProjectLoadedCallback cb) { loaded_cb_ = std::move(cb); }
    void setProjectSavedCallback(ProjectSavedCallback cb) { saved_cb_ = std::move(cb); }
    void setProjectClosedCallback(ProjectClosedCallback cb) { closed_cb_ = std::move(cb); }
    void setModifiedChangedCallback(ModifiedChangedCallback cb) { modified_cb_ = std::move(cb); }

    // ---- Undo/Redo integration ----

    UndoRedoSystem* undoRedo() const { return undo_redo_.get(); }

private:
    std::shared_ptr<UndoRedoSystem> undo_redo_;
    std::unique_ptr<ProjectSerializer> serializer_;

    TerrainProductPtr current_terrain_;
    SceneProductPtr current_scene_;
    std::string current_path_;
    std::string current_type_;  // "terrain" or "scene"
    bool is_modified_ = false;
    bool is_read_only_ = false;

    std::vector<std::string> recent_files_;
    static constexpr size_t MAX_RECENT = 20;

    ProjectLoadedCallback loaded_cb_;
    ProjectSavedCallback saved_cb_;
    ProjectClosedCallback closed_cb_;
    ModifiedChangedCallback modified_cb_;

    void notifyLoaded(const std::string& path, const std::string& type);
    void notifySaved(const std::string& path);
    void notifyClosed();
    void notifyModified(bool modified);
};

} // namespace vsgraysim

#endif // VSGRAYSIM_PROJECT_PROJECTMANAGER_H