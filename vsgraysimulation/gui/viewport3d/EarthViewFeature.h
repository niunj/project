#pragma once
#include "EarthView.h"
#include <vsgraysim/earth/PlanetWeather.h>
#include <vsgraysim/earth/TerrainCloudShadow.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/Switch.h>

namespace vsgraysim::earth { class OrbitalGlobe;class VoxelOcean; }
namespace vsgraysim::viewport3d {
class RockyGlobeBackend;
class EarthViewFeature final : public EarthView {
public:
    EarthViewFeature();
    ~EarthViewFeature() override;
    void configure(const nlohmann::json&) override;
    void onAttach(ViewportContext&) override;
    void onFrame(ViewportContext&,const FrameTick&) override;
    void onDetach(ViewportContext&) override;
    earth::PrepareSurfaceView captureViewPreparation() override;
    bool streamSurface(const std::string&,std::function<earth::EarthSurfaceSnapshotPtr()>) override;
    void bindSurface(earth::EarthSurfaceSnapshotPtr, bool frame) override;
    bool loadEarthScenario(const nlohmann::json&, const std::string&, std::string&) override;
    earth::EarthSurfaceSnapshotPtr surfaceSnapshot() const override { return surface_; }
    nlohmann::json earthConfiguration() const override { return earth_scenario_.value("earth",nlohmann::json::object()); }
    earth::EarthSurfaceSnapshotPtr observerSnapshot() const;
    const GeoAnchor& anchor() const override { return anchor_; }
    void setGeoAnchor(const GeoAnchor&) override;
    enum class EnvironmentLayer { Sky, GlobalSea, LocalSea, GlobalCloud, LocalCloud };
    vsg::Group* environmentRoot(EnvironmentLayer layer) const { return layers_[size_t(layer)].get(); }
    const earth::PlanetWeatherFrame* terrainWeather()const{return planet_?&weather_:nullptr;}
    const earth::PlanetWeatherFrame* weather() const { return planet_&&(!scene_query_||showsSceneContext()) ? &weather_ : nullptr; }
    const vsg::dmat4& weatherModel() const { return weatherModel_; }
    uint64_t weatherTerrainRevision() const { return weatherRevision_; }
    float weatherBed(double x,double z,float level) const;
    void* rockyContext() const; // Native extension consumed only by the Rocky sky adapter.
    vsg::MatrixTransform* enuRoot() const { return enu_root_.get(); }
    /// A unified physics scene owns the local ground and its holes. Hide the
    /// preview globe rather than drawing undeformed terrain through excavations.
    void setSceneTerrainQuery(std::shared_ptr<IGeoTerrainQuery>);
    void setSceneOcean(std::shared_ptr<const earth::VoxelOcean> ocean){sceneOcean_=std::move(ocean);}
    bool usesSceneTerrain()const{return bool(scene_query_);}
    bool showsSceneContext()const{return surface_&&params_.value("scene_context",bool(orbital_));}
    std::shared_ptr<rockyenv::CloudSceneBinding> sceneCloud(const vsg::dmat4& model)const {
        return rockyenv::createCloudSceneBinding(cloudResources_,model);
    }
    std::shared_ptr<rockyenv::CloudGpuResources> cloudResources()const{return cloudResources_;}
    size_t sceneCloudColliders()const{return planet_?planet_->colliders().size():0;}
    /// Presentation only. The field, wind, colliders and sensor binding remain
    /// active; acceptance runs can skip uncaptured colour draws between probes.
    void setCloudPresentationEnabled(bool);

private:
    void updateWeather(ViewportContext&,const FrameTick&);
    vsg::ref_ptr<vsg::Switch> environmentFilter_;
    std::array<vsg::ref_ptr<vsg::Group>,5> layers_;
    std::shared_ptr<rockyenv::CloudGpuResources> cloudResources_=rockyenv::createCloudGpuResources();
    std::unique_ptr<earth::PlanetWeather> planet_;
    vsg::ref_ptr<vsg::Switch> gpuCloudInitialize_,gpuCloudStep_,gpuCloudTemporal_;
    int gpuCloudInitializeFrames_=0;
    std::shared_ptr<earth::TerrainCloudShadow> terrainCloudShadow_;
    earth::PlanetWeatherFrame weather_;
    earth::EarthSurfaceSnapshotPtr weatherSurface_;
    GeoAnchor weatherAnchor_;
    vsg::dmat4 weatherModel_, toTerrain_, fromTerrain_;
    uint64_t weatherRevision_=0;
    void ensureRockyMap();
    void updateOrbitalObserver(const vsg::dvec3& focus,double distance);
    void publishObserver();
    std::unique_ptr<earth::OrbitalGlobe> orbital_;
    vsg::ref_ptr<vsg::Group> observer_root_;
    class GeoQuery;
    struct Stream;
    std::unique_ptr<Stream> stream_;
    void installCameraOverride(ViewportContext&);
    void publishView(bool frameTerrain = true,bool compiled=false);
    ViewportContext* ctx_ = nullptr;
    nlohmann::json params_ = nlohmann::json::object();
    GeoAnchor anchor_;
    double view_height_ = 600.;
    std::unique_ptr<RockyGlobeBackend> rocky_;
    vsg::ref_ptr<vsg::MatrixTransform> enu_root_;
    vsg::ref_ptr<vsg::Group> terrain_root_;
    vsg::ref_ptr<vsg::Switch> terrain_visibility_;
    vsg::ref_ptr<vsg::Switch> active_visibility_;
    std::shared_ptr<IGeoTerrainQuery> scene_query_;
    std::shared_ptr<const earth::VoxelOcean> sceneOcean_;
    bool cloudPresentation_=true;
    vsg::ref_ptr<vsg::Node> voxel_node_;
    std::shared_ptr<GeoQuery> geo_query_;
    earth::EarthSurfaceSnapshotPtr surface_;
    nlohmann::json earth_scenario_ = nlohmann::json::object();
    std::string earth_scenario_path_;
};
}
