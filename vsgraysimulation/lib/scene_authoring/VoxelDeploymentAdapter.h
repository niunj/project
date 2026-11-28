#pragma once
#include "Deployment.h"
#include <filesystem>
#include <optional>
#include <vsgraysim/earth/VoxelScene.h>

namespace vsgraysim::earth::deployment {
using scene_deployment::Json;

struct Edit {
    VoxelSceneSnapshotPtr before;
    Json equipmentBefore;
    Json equipmentAfter;
    Json imports;
    Json commands;
};

struct Undo {
    VoxelSceneSnapshotPtr before;
    VoxelSceneSnapshotPtr after;
    Json equipmentBefore;
    Json equipmentAfter;
};

// These calls are UI-independent. Only commit and undo mutate the engine/document.
class Adapter {
public:
    explicit Adapter(const std::filesystem::path& catalog);
    Json context(const VoxelSceneSnapshot&,const std::string& selected,
                 std::optional<vsg::dvec3> mapPoint) const;
    Edit stage(const Json& plan,VoxelSceneSnapshotPtr,const Json& topic,
               const std::string& topicPath,std::optional<vsg::dvec3> mapPoint={}) const;
    void applyInWorker(VoxelScene&, Json& topic, const Json& plan, const std::string& topicPath) const;
    static Undo commit(VoxelScene&,Json& topic,const Edit&,VoxelSceneSnapshotPtr prepared={});
    static void undo(VoxelScene&,Json& topic,const Undo&);
private:
    Json catalog_;
    std::filesystem::path root_;
};
}
