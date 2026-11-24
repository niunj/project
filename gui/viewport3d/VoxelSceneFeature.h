#pragma once
#include "IViewportFeature.h"
#include <vsgraysim/earth/VoxelScene.h>
#include <vsg/nodes/Switch.h>
#include <optional>
namespace vsgraysim::viewport3d {
/// Versioned voxel scene in its own geodetic chart. Runtime edits remain on
/// the GUI owner thread; sensors may pin immutable runtime snapshots.
class VoxelSceneFeature final : public IViewportFeature {
public:
    static constexpr const char* kId="voxel_scene";
    const char* id() const override {return kId;}
    void configure(const nlohmann::json&) override;
    nlohmann::json saveState() const override;
    void onAttach(ViewportContext&) override;
    void onDetach(ViewportContext&) override;
    void onFrame(ViewportContext&,const FrameTick&) override;
    void setEnabled(bool) override;
    earth::VoxelScene* runtime() const {return runtime_.get();}
    // Read-only simulation publication. No local dynamics are constructed or
    // advanced in this mode; callers bind snapshots on the viewport thread.
    void bindSnapshot(earth::VoxelSceneSnapshotPtr);
    earth::VoxelSceneSnapshotPtr snapshot() const {return runtime_?runtime_->snapshot():external_;}
    const earth::VoxelSceneDisplay& display()const{return display_;}
    const std::string& lastError() const {return lastError_;}
private:
    void load(const nlohmann::json&,const nlohmann::json* prepared=nullptr);
    std::optional<earth::VoxelScene::Preparation> preparing_;
    nlohmann::json preparingConfig_;
    void prepare(earth::VoxelSceneSnapshotPtr,bool force=false,const nlohmann::json* renderConfig=nullptr);
    void synchronize();
    void synchronizeTerrainQuery();
    void decorateContext(earth::VoxelSceneDisplay&,earth::VoxelSceneSnapshotPtr);
    void prepareClusters(earth::VoxelSceneDisplay&,const nlohmann::json&);
    void mountClusterCommands();
    nlohmann::json config_=nlohmann::json::object();
    ViewportContext* context_=nullptr;
    std::unique_ptr<earth::VoxelScene> runtime_;
    earth::VoxelSceneSnapshotPtr external_;
    nlohmann::json environmentUtc_;
    earth::VoxelSceneDisplay display_;
    std::optional<earth::VoxelSceneDisplay> pending_;
    vsg::ref_ptr<vsg::Switch> holder_,oceanCommands_,clusterCommands_;
    vsg::ref_ptr<vsg::MatrixTransform> chart_;
    double targetSeconds_=0.,timeScale_=1.;
    std::string lastError_;
    std::shared_ptr<IGeoTerrainQuery> terrainQuery_;
    std::string contextKey_;
    std::shared_ptr<const earth::VoxelOcean> contextOcean_;
    vsg::ref_ptr<vsg::Node> contextNode_;
    bool legacyOceanSuppressed_=false,legacyOceanWasEnabled_=false;
};
}
