#include "vsgraysim/project/ProjectManager.h"

#include <algorithm>
#include <fstream>
#include <filesystem>

#include <spdlog/spdlog.h>

namespace vsgraysim {

ProjectManager::ProjectManager(std::shared_ptr<UndoRedoSystem> undo_redo)
    : undo_redo_(std::move(undo_redo)), serializer_(std::make_unique<ProjectSerializer>()) {
    if (!undo_redo_) {
        undo_redo_ = std::make_shared<UndoRedoSystem>();
    }
}

TerrainProductPtr ProjectManager::newTerrainProject(const std::string& name,
                                                    const std::string& crs,
                                                    const vsg::dbox& bounds,
                                                    double resolution) {
    closeProject();

    auto terrain = TerrainProduct::create(
        [&crs]() {
            CRSInfo info;
            info.epsg_code = crs;
            return info;
        }(),
        bounds, resolution);

    terrain->name = name;
    current_terrain_ = terrain;
    current_type_ = "terrain";
    is_modified_ = true;

    notifyModified(true);
    return terrain;
}

SceneProductPtr ProjectManager::newSceneProject(const std::string& name,
                                                const TerrainProductPtr& terrain) {
    closeProject();

    auto scene = SceneProduct::create(name);
    if (terrain) {
        scene->terrain_ref = AssetRef<TerrainProduct>(terrain->product_id);
    }
    current_scene_ = scene;
    current_type_ = "scene";
    is_modified_ = true;

    notifyModified(true);
    return scene;
}

std::shared_ptr<void> ProjectManager::loadProject(const std::string& path) {
    // Try terrain first
    auto manifest = serializer_->inspectTerrain(path);
    if (manifest && manifest->container_type == "vsgterrain") {
        return loadTerrainProject(path);
    }

    // Try scene
    auto manifest2 = serializer_->inspectScene(path);
    if (manifest2 && manifest2->container_type == "vsgscene") {
        return loadSceneProject(path);
    }

    spdlog::error("Unknown project type: {}", path);
    return nullptr;
}

TerrainProductPtr ProjectManager::loadTerrainProject(const std::string& path) {
    closeProject();

    auto terrain = serializer_->loadTerrain(path);
    if (!terrain) {
        spdlog::error("Failed to load terrain project: {}", path);
        return nullptr;
    }

    current_terrain_ = terrain;
    current_path_ = path;
    current_type_ = "terrain";
    is_modified_ = false;
    is_read_only_ = (std::filesystem::status(path).permissions() & std::filesystem::perms::owner_write) == std::filesystem::perms::none;

    addRecentFile(path);
    notifyLoaded(path, "terrain");
    notifyModified(false);

    return terrain;
}

SceneProductPtr ProjectManager::loadSceneProject(const std::string& path) {
    closeProject();

    auto scene = serializer_->loadScene(path);
    if (!scene) {
        spdlog::error("Failed to load scene project: {}", path);
        return nullptr;
    }

    current_scene_ = scene;
    current_path_ = path;
    current_type_ = "scene";
    is_modified_ = false;
    is_read_only_ = (std::filesystem::status(path).permissions() & std::filesystem::perms::owner_write) == std::filesystem::perms::none;

    addRecentFile(path);
    notifyLoaded(path, "scene");
    notifyModified(false);

    return scene;
}

bool ProjectManager::saveProject() {
    if (current_path_.empty()) return saveProjectAs(current_path_);

    if (current_type_ == "terrain" && current_terrain_) {
        SerializeOptions opts;
        if (serializer_->saveTerrain(current_terrain_, current_path_, opts)) {
            is_modified_ = false;
            notifySaved(current_path_);
            notifyModified(false);
            return true;
        }
    } else if (current_type_ == "scene" && current_scene_) {
        SerializeOptions opts;
        if (serializer_->saveScene(current_scene_, current_path_, opts)) {
            is_modified_ = false;
            notifySaved(current_path_);
            notifyModified(false);
            return true;
        }
    }
    return false;
}

bool ProjectManager::saveProjectAs(const std::string& path) {
    if (current_type_ == "terrain" && current_terrain_) {
        SerializeOptions opts;
        if (serializer_->saveTerrain(current_terrain_, path, opts)) {
            current_path_ = path;
            is_modified_ = false;
            addRecentFile(path);
            notifySaved(path);
            notifyModified(false);
            return true;
        }
    } else if (current_type_ == "scene" && current_scene_) {
        SerializeOptions opts;
        if (serializer_->saveScene(current_scene_, path, opts)) {
            current_path_ = path;
            is_modified_ = false;
            addRecentFile(path);
            notifySaved(path);
            notifyModified(false);
            return true;
        }
    }
    return false;
}

void ProjectManager::closeProject() {
    if (hasProject()) {
        current_terrain_.reset();
        current_scene_.reset();
        current_path_.clear();
        current_type_.clear();
        is_modified_ = false;
        notifyClosed();
    }
}

void ProjectManager::markModified(bool modified) {
    if (is_modified_ != modified) {
        is_modified_ = modified;
        notifyModified(modified);
    }
}

bool ProjectManager::promptSaveIfModified() {
    // In headless mode, just return true
    // In GUI mode, this would show a dialog
    return !is_modified_;
}

void ProjectManager::addRecentFile(const std::string& path) {
    auto it = std::find(recent_files_.begin(), recent_files_.end(), path);
    if (it != recent_files_.end()) {
        recent_files_.erase(it);
    }
    recent_files_.insert(recent_files_.begin(), path);
    if (recent_files_.size() > MAX_RECENT) {
        recent_files_.resize(MAX_RECENT);
    }
}

std::vector<ProjectInfo> ProjectManager::recentFiles(size_t max) const {
    std::vector<ProjectInfo> infos;
    for (const auto& path : recent_files_) {
        if (infos.size() >= max) break;
        std::error_code ec;
        auto status = std::filesystem::status(path, ec);
        if (ec) continue;

        ProjectInfo info;
        info.path = path;
        info.file_size = std::filesystem::file_size(path, ec);

        // Try to load manifest for metadata
        auto manifest = serializer_->inspectTerrain(path);
        if (!manifest) manifest = serializer_->inspectScene(path);
        if (manifest) {
            info.name = manifest->product_id; // Would be better to store name in manifest
            info.product_id = manifest->product_id;
            info.type = manifest->container_type == "vsgterrain" ? "terrain" : "scene";
            info.created = manifest->created_timestamp;
            info.modified = manifest->modified_timestamp;
        }
        info.is_read_only = (status.permissions() & std::filesystem::perms::owner_write) == std::filesystem::perms::none;
        infos.push_back(info);
    }
    return infos;
}

void ProjectManager::clearRecentFiles() {
    recent_files_.clear();
}

void ProjectManager::notifyLoaded(const std::string& path, const std::string& type) {
    if (loaded_cb_) loaded_cb_(path, type);
}

void ProjectManager::notifySaved(const std::string& path) {
    if (saved_cb_) saved_cb_(path);
}

void ProjectManager::notifyClosed() {
    if (closed_cb_) closed_cb_();
}

void ProjectManager::notifyModified(bool modified) {
    if (modified_cb_) modified_cb_(modified);
}

} // namespace vsgraysim