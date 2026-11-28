#pragma once
#include "IViewportFeature.h"
#include <QMetaObject>
#include <vsgraysim/earth/VoxelScene.h>
#include <vsg/nodes/Switch.h>
#include <optional>
#include <set>
#include <map>
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
    void bindSnapshot(earth::VoxelSceneSnapshotPtr,nlohmann::json presentation=nlohmann::json::array(),
        nlohmann::json equipmentVisuals=nlohmann::json::object());
    const nlohmann::json& presentation() const {return presentation_;}
    const nlohmann::json& equipmentVisuals() const {return equipmentVisuals_;}
    uint64_t equipmentVisualGeneration() const {return equipmentVisualGeneration_;}
    void setEquipmentVisuals(nlohmann::json);
    void setPresentationHiddenObjects(std::set<std::string>);
    earth::VoxelSceneSnapshotPtr snapshot() const {return runtime_?runtime_->snapshot():external_;}
    const earth::VoxelSceneDisplay& display()const{return display_;}
    // Exclude only the active cockpit's exterior in this viewport. The
    // authoritative snapshot, sensors and all other views are unaffected.
    void setCockpitOwnship(const std::string& objectId);
    bool cockpitOwnshipHidden() const {return hiddenSwitches_.count(ownshipId_)!=0;}
    nlohmann::json terrainDisplayPageStats() const;
    nlohmann::json terrainAtlasReuseStats() const;
    const std::string& lastError() const {return lastError_;}
private:
    void load(const nlohmann::json&,const nlohmann::json* prepared=nullptr);
    std::optional<earth::VoxelScene::Preparation> preparing_;
    nlohmann::json preparingConfig_;
    void prepare(earth::VoxelSceneSnapshotPtr,bool force=false,const nlohmann::json* renderConfig=nullptr);
    void synchronize();
    void mountCockpitOwnship();
    void synchronizeTerrainQuery();
    void decorateContext(earth::VoxelSceneDisplay&,earth::VoxelSceneSnapshotPtr,bool pageTerrain);
    void updateTerrainDisplayPages();
    void prepareClusters(earth::VoxelSceneDisplay&,const nlohmann::json&);
    void mountClusterCommands();
    nlohmann::json config_=nlohmann::json::object();
    ViewportContext* context_=nullptr;
    QMetaObject::Connection terrainPageRecord_;
    std::unique_ptr<earth::VoxelScene> runtime_;
    earth::VoxelSceneSnapshotPtr external_;
    nlohmann::json environmentUtc_;
    earth::VoxelSceneDisplay display_;
    std::string ownshipId_;
    std::set<std::string> hiddenPresentation_;
    std::map<std::string,vsg::ref_ptr<vsg::Switch>> hiddenSwitches_;
    nlohmann::json presentation_=nlohmann::json::array();
    nlohmann::json equipmentVisuals_=nlohmann::json::object();
    uint64_t equipmentVisualGeneration_=0;
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
