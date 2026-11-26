#include "VoxelSceneFeature.h"
#include "ViewportContext.h"
#include "TerrainDisplayPages.h"
#include "FeatureRegistry.h"
#include "EnvFeatures.h"
#include "EarthViewFeature.h"
#include "ViewportKit.h"
#include "VsgQtWidget.h"
#include <vsgraysim/earth/SurfaceClusters.h>
#include <vsgraysim/earth/TerrainDisplay.h>
#include <vsgraysim/earth/TerrainCloudShadow.h>
#include <vsgraysim/earth/VoxelEarthPatch.h>
#include <vsgraysim/earth/VoxelOcean.h>
#include <vsgraysim/earth/VoxelOceanDisplay.h>
#include <simcore/GeoFrame.h>
#include <vsg/all.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
namespace vsgraysim::viewport3d {
namespace {
vsg::ref_ptr<vsg::Node> compilationRoot(vsg::ref_ptr<vsg::Node> draw,const earth::VoxelSceneDisplay& display) {
    if(!display.clusters)return draw;
    auto group=vsg::Group::create();
    group->addChild(display.clusters->commands());group->addChild(draw);
    return group;
}
struct TerrainLightingBindings : vsg::Inherit<vsg::Object,TerrainLightingBindings> {
    std::vector<std::shared_ptr<earth::TerrainCloudShadow>> passes;
    struct Entry {
        const void* source = nullptr;
        const void* clouds = nullptr;
        std::shared_ptr<earth::TerrainCloudShadow> pass;
        std::shared_ptr<TerrainDisplayPages> pages;
    };
    std::map<std::string,Entry> cached;
};
struct OceanLightingBindings : vsg::Inherit<vsg::Object,OceanLightingBindings> {std::shared_ptr<earth::VoxelOceanDisplay> pass;};
class SceneTerrainQuery final : public IGeoTerrainQuery {
    std::function<earth::VoxelSceneSnapshotPtr()> snapshot_;
public:
    explicit SceneTerrainQuery(std::function<earth::VoxelSceneSnapshotPtr()> snapshot):snapshot_(std::move(snapshot)){}
    bool geoActive()const override{return bool(snapshot_());}
    bool hasAuthoritativeSurface()const override{return geoActive();}
    uint64_t revision()const override{auto s=snapshot_();return s?s->contentRevision:0;}
    bool heightLocal(double x,double z,double& height)const override {
        auto s=snapshot_();if(!s)return false;
        auto hit=earth::raycastVoxelScene(*s,{x,1e6,z},{0,-1,0},2e6,true);
        if(!hit)return false;height=hit->position.y;return true;
    }
    bool intersectRayGeo(const vsg::dvec3& origin,const vsg::dvec3& direction,double& lat,double& lon,double& height)const override {
        auto s=snapshot_();if(!s||vsg::length(direction)<1e-12)return false;
        const auto model=simcore::geo::makeEnuToWorldMatrix(s->anchor),inverse=vsg::inverse(model);
        const auto d=inverse*vsg::dvec4(direction,0.);
        auto hit=earth::raycastVoxelScene(*s,inverse*origin,{d.x,d.y,d.z},2e6,true);if(!hit)return false;
        const auto world=model*hit->position;const auto llh=simcore::geo::ecefToGeodetic(world.x,world.y,world.z);
        lat=llh.x;lon=llh.y;height=llh.z;return true;
    }
    bool groundElevation(double lat,double lon,double& height)const override {
        auto s=snapshot_();if(!s)return false;
        const auto top=simcore::geo::geodeticToECEF(lat,lon,s->anchor.alt_m+1e6),bottom=simcore::geo::geodeticToECEF(lat,lon,s->anchor.alt_m-100000);
        double a,b;return intersectRayGeo(top,bottom-top,a,b,height);
    }
};
}
void VoxelSceneFeature::load(const nlohmann::json& config,const nlohmann::json* prepared) try {
    std::optional<std::pair<vsg::dvec3,vsg::dvec3>> requestedCamera;vsg::dvec3 requestedUp(0,1,0);
    if(config.contains("camera_local")){
        const auto& camera=config.at("camera_local");auto vector=[](const nlohmann::json& j){if(!j.is_array()||j.size()!=3)throw std::invalid_argument("topic camera requires triples");vsg::dvec3 p(j[0].get<double>(),j[1].get<double>(),j[2].get<double>());if(!std::isfinite(vsg::length(p)))throw std::invalid_argument("invalid topic camera");return p;};
        const auto eye=vector(camera.at("eye_m")),target=vector(camera.at("target_m"));if(camera.contains("up"))requestedUp=vector(camera.at("up"));if(vsg::length(eye-target)<1e-5||vsg::length(vsg::cross(target-eye,requestedUp))<1e-6)throw std::invalid_argument("degenerate topic camera");
        requestedCamera=std::make_pair(eye,target);
    }
    if(!prepared&&!config.contains("checkpoint")&&config.value("background_loading",false)){
        if(preparing_)throw std::runtime_error("scene preparation is already running");preparingConfig_=config;preparing_.emplace(earth::VoxelScene::prepareFileAsync(config.at("scene_json").get<std::string>()));lastError_.clear();return;
    }
    auto candidate=prepared?earth::VoxelScene::load(*prepared):config.contains("checkpoint")?earth::VoxelScene::load(config.at("checkpoint")):
        earth::VoxelScene::open(config.at("scene_json").get<std::string>());
    candidate->setPaused(config.value("paused",true));
    auto display=earth::makeVoxelSceneDisplay(*candidate->snapshot(),nullptr,true);
    decorateContext(display,candidate->snapshot(),config.value("terrain_display_paging",false));
    prepareClusters(display,config);
    auto chart=vsg::MatrixTransform::create(simcore::geo::makeEnuToWorldMatrix(candidate->snapshot()->anchor));chart->addChild(display.node);
    if(context_->viewer()&&context_->viewer()->compileManager) {
        context_->viewer()->deviceWaitIdle();
        auto compiled=context_->viewer()->compileManager->compile(compilationRoot(chart,display));
        if(!compiled)throw std::runtime_error("voxel scene compile failed: "+compiled.message);
        vsg::updateViewer(*context_->viewer(),compiled);
    }
    if(auto* sky=context_->featureAs<SkyFeature>(SkyFeature::kId))sky->configure(candidate->snapshot()->utcOrigin);
    runtime_=std::move(candidate);display_=std::move(display);chart_=chart;pending_.reset();
    mountClusterCommands();
    mountCockpitOwnship();
    if(auto* ocean=context_->featureAs<OceanFeature>(OceanFeature::kId)) {
        if(runtime_->snapshot()->ocean){if(!legacyOceanSuppressed_)legacyOceanWasEnabled_=ocean->enabled();ocean->setEnabled(false);legacyOceanSuppressed_=true;}
        else if(legacyOceanSuppressed_){ocean->setEnabled(legacyOceanWasEnabled_);legacyOceanSuppressed_=false;}
    }
    runtime_->setPreparePublication([this](auto snapshot){prepare(std::move(snapshot));});
    holder_->children.clear();holder_->addChild(enabled_,chart_);
    synchronizeTerrainQuery();
    targetSeconds_=runtime_->snapshot()->timeSeconds();lastError_.clear();context_->requestCompile();
    if(config.value("frame_scene",true)&&context_->camera()) {
        const auto snapshot=runtime_->snapshot();vsg::dbox bounds;
        for(const auto& [id,o]:snapshot->objects) {
            const auto box=o.localBounds();const auto placement=o.pose(snapshot->tick,snapshot->fixedStep);
            for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
                const auto p=placement*vsg::dvec3(x?box.max.x:box.min.x,y?box.max.y:box.min.y,z?box.max.z:box.min.z);
                bounds.add(p);
            }
        }
        if(bounds.valid())if(auto look=context_->camera()->viewMatrix.cast<vsg::LookAt>()) {
            const auto centre=(bounds.min+bounds.max)*.5;
            const double radius=std::max(5.,vsg::length(bounds.max-bounds.min));
            look->center=chart_->matrix*centre;look->eye=chart_->matrix*(centre+vsg::dvec3(.9,.65,1.)*radius);
            const auto up=chart_->matrix*vsg::dvec4(0,1,0,0);look->up=vsg::dvec3(up.x,up.y,up.z);
        }
    }
    if(requestedCamera&&context_->camera())if(auto look=context_->camera()->viewMatrix.cast<vsg::LookAt>()){look->eye=chart_->matrix*requestedCamera->first;look->center=chart_->matrix*requestedCamera->second;const auto up=chart_->matrix*vsg::dvec4(requestedUp,0.);look->up={up.x,up.y,up.z};}

}catch(const vsg::Exception& error){throw std::runtime_error("voxel display preparation: "+error.message);}
void VoxelSceneFeature::configure(const nlohmann::json& values) {
    if(!values.is_object())throw std::invalid_argument("voxel scene feature configuration must be an object");
    auto candidate=config_;candidate.update(values);
    if(values.contains("scene_json")&&!values.contains("checkpoint"))candidate.erase("checkpoint");
    const double scale=candidate.value("time_scale",1.);
    if(!std::isfinite(scale)||scale<0||scale>100)throw std::invalid_argument("voxel time_scale must be in [0,100]");
    const double error=candidate.value("cluster_error_pixels",.65);
    if(!std::isfinite(error)||error<0||error>2)throw std::invalid_argument("cluster_error_pixels must be in [0,2]");
    if(candidate.contains("clustered_rendering"))candidate.at("clustered_rendering").get<bool>();
    const bool pagingChanged = candidate.value("terrain_display_paging",false) != config_.value("terrain_display_paging",false);
    if(context_&&(values.contains("scene_json")||values.contains("checkpoint")))load(candidate);
    else {
        if(context_&&snapshot()&&(values.contains("clustered_rendering")||pagingChanged)){
            prepare(snapshot(),true,&candidate);synchronize();
        }
        if(runtime_&&values.contains("paused"))runtime_->setPaused(candidate.at("paused").get<bool>());
    }
    if(preparing_)for(const auto* key:{"paused","clustered_rendering","cluster_error_pixels","terrain_display_paging"})
        if(values.contains(key))preparingConfig_[key]=candidate.at(key);
    config_=std::move(candidate);timeScale_=scale;
}
nlohmann::json VoxelSceneFeature::saveState() const {
    auto state=config_;state["time_scale"]=timeScale_;
    if(runtime_){state["checkpoint"]=runtime_->checkpoint();state["paused"]=runtime_->paused();}return state;
}
void VoxelSceneFeature::onAttach(ViewportContext& context) {
    context_=&context;holder_=vsg::Switch::create();oceanCommands_=vsg::Switch::create();clusterCommands_=vsg::Switch::create();
    try{if(!config_.value("external_only",false))load(config_);}catch(...){context_=nullptr;holder_=nullptr;oceanCommands_=nullptr;clusterCommands_=nullptr;throw;}
    context.worldRoot()->addChild(holder_);
    terrainPageRecord_ = QObject::connect(context.widget(), &gui::VsgQtWidget::beforeSceneRecord,
        context.widget(), [this] { updateTerrainDisplayPages(); });
    if(auto* pre=context.preRenderRoot()){pre->addChild(oceanCommands_);pre->addChild(clusterCommands_);}
}
void VoxelSceneFeature::bindSnapshot(earth::VoxelSceneSnapshotPtr next,nlohmann::json presentation,nlohmann::json equipmentVisuals) {
    if(!context_||!config_.value("external_only",false)||!next)throw std::logic_error("voxel viewport is not a ready read-only consumer");
    const bool first=!external_;
    if(first) {
        chart_=vsg::MatrixTransform::create(simcore::geo::makeEnuToWorldMatrix(next->anchor));
        holder_->addChild(enabled_,chart_);
        if(auto* sky=context_->featureAs<SkyFeature>(SkyFeature::kId))sky->configure(next->utcOrigin);
    }
    bool changed=!display_.node||next->contentRevision!=display_.contentRevision;
    if(external_&&!changed){
        changed=next->objects.size()!=external_->objects.size();
        for(const auto& [id,o]:next->objects){auto old=external_->objects.find(id);if(old==external_->objects.end()||old->second.volume!=o.volume||old->second.geometry!=o.geometry){changed=true;break;}}
    }
    if(changed){chart_->matrix=simcore::geo::makeEnuToWorldMatrix(next->anchor);prepare(next,true);}
    presentation_=std::move(presentation);
    setEquipmentVisuals(std::move(equipmentVisuals));
    external_=std::move(next);synchronize();
    if(first&&config_.contains("camera_local")&&context_->camera())if(auto look=context_->camera()->viewMatrix.cast<vsg::LookAt>()) {
        const auto& camera=config_.at("camera_local");const auto& e=camera.at("eye_m");const auto& t=camera.at("target_m");
        look->eye=chart_->matrix*vsg::dvec3(e[0].get<double>(),e[1].get<double>(),e[2].get<double>());
        look->center=chart_->matrix*vsg::dvec3(t[0].get<double>(),t[1].get<double>(),t[2].get<double>());
        const auto u=camera.value("up",nlohmann::json::array({0,1,0}));const auto up=chart_->matrix*vsg::dvec4(u[0].get<double>(),u[1].get<double>(),u[2].get<double>(),0);look->up={up.x,up.y,up.z};
    }
    if(changed)context_->requestCompile();
}

void VoxelSceneFeature::setEquipmentVisuals(nlohmann::json data) {
    equipmentVisuals_=std::move(data);
    ++equipmentVisualGeneration_;
}
void VoxelSceneFeature::onDetach(ViewportContext& context) {
    QObject::disconnect(terrainPageRecord_);
    if(legacyOceanSuppressed_)if(auto* ocean=context.featureAs<OceanFeature>(OceanFeature::kId))ocean->setEnabled(legacyOceanWasEnabled_);
    legacyOceanSuppressed_=false;
    if(auto* map=context.featureAs<EarthViewFeature>(EarthView::kId)){map->setSceneOcean({});map->setSceneTerrainQuery(nullptr);}
    terrainQuery_.reset();
    if(holder_) {
        auto& children=context.worldRoot()->children;children.erase(std::remove(children.begin(),children.end(),holder_),children.end());
    }
    if(auto* pre=context.preRenderRoot()) {
        auto& children=pre->children;
        children.erase(std::remove_if(children.begin(),children.end(),[&](const auto& child){return child==oceanCommands_||child==clusterCommands_;}),children.end());
    }
    oceanCommands_=nullptr;clusterCommands_=nullptr;
    preparing_.reset();preparingConfig_=nlohmann::json::object();
    contextOcean_.reset();contextKey_.clear();contextNode_=nullptr;
    hiddenSwitches_.clear();hiddenPresentation_.clear();ownshipId_.clear();presentation_=nlohmann::json::array();equipmentVisuals_=nlohmann::json::object();++equipmentVisualGeneration_;
    runtime_.reset();external_.reset();pending_.reset();display_={};holder_=nullptr;chart_=nullptr;context_=nullptr;
}
void VoxelSceneFeature::prepare(earth::VoxelSceneSnapshotPtr snapshot,bool force,const nlohmann::json* renderConfig) try {
    if(!force&&snapshot->contentRevision==display_.contentRevision)return;
    const auto& options = renderConfig ? *renderConfig : config_;
    const bool pageTerrain = options.value("terrain_display_paging",false);
    const bool freshDraw = pageTerrain && (!config_.value("terrain_display_paging",false) ||
        !display_.node || !display_.node->getObject<TerrainLightingBindings>("voxel-earth-lighting"));
    auto candidate = earth::makeVoxelSceneDisplay(*snapshot,
        (freshDraw || std::getenv("VSGRAYSIM_SCENE_REBUILD_ALL")) ? nullptr : &display_, true);
    decorateContext(candidate,snapshot,pageTerrain);
    prepareClusters(candidate,options);
    if(context_->viewer()&&context_->viewer()->compileManager) {
        context_->viewer()->deviceWaitIdle();
        auto compiled=context_->viewer()->compileManager->compile(compilationRoot(candidate.node,candidate));
        if(!compiled)throw std::runtime_error("voxel scene update compile failed: "+compiled.message);
        vsg::updateViewer(*context_->viewer(),compiled);
    }
    pending_=std::move(candidate);
}catch(const vsg::Exception& error){throw std::runtime_error("voxel display publication: "+error.message);}
void VoxelSceneFeature::synchronize() {
    const auto snapshot=this->snapshot();
    if(environmentUtc_!=snapshot->utcOrigin) {
        if(auto* sky=context_->featureAs<SkyFeature>(SkyFeature::kId))sky->configure(snapshot->utcOrigin);
        environmentUtc_=snapshot->utcOrigin;
    }
    if(pending_&&pending_->contentRevision==snapshot->contentRevision) {
        display_=std::move(*pending_);pending_.reset();chart_->children.clear();chart_->addChild(display_.node);
        mountClusterCommands();
        mountCockpitOwnship();
    }
    display_.sync(*snapshot);
    if(auto* ocean=context_->featureAs<OceanFeature>(OceanFeature::kId)) {
        if(snapshot->ocean){if(!legacyOceanSuppressed_)legacyOceanWasEnabled_=ocean->enabled();ocean->setEnabled(false);legacyOceanSuppressed_=true;}
        else if(legacyOceanSuppressed_){ocean->setEnabled(legacyOceanWasEnabled_);legacyOceanSuppressed_=false;}
    }
    synchronizeTerrainQuery();
}
void VoxelSceneFeature::setCockpitOwnship(const std::string& objectId) {
    if(ownshipId_==objectId)return;
    ownshipId_=objectId;
    mountCockpitOwnship();
}
void VoxelSceneFeature::setPresentationHiddenObjects(std::set<std::string> ids) {
    if(ids==hiddenPresentation_)return;
    hiddenPresentation_=std::move(ids);
    mountCockpitOwnship();
}
void VoxelSceneFeature::mountCockpitOwnship() {
    if(!display_.node)return;
    auto ids=hiddenPresentation_;
    if(!ownshipId_.empty())ids.insert(ownshipId_);
    for(const auto& [id,hide]:hiddenSwitches_) {
        for(auto& child:display_.node->children)if(child==hide) {
            child=hide->children.front().node;
            break;
        }
    }
    hiddenSwitches_.clear();
    const auto state=snapshot();
    if(!state)return;
    for(const auto& id:ids) {
        const auto object=state->objects.find(id);
        const auto placement=display_.placements.find(id);
        if(object==state->objects.end()||object->second.role=="terrain"||placement==display_.placements.end())continue;
        for(auto& child:display_.node->children)if(child==placement->second) {
            auto hide=vsg::Switch::create();
            hide->addChild(false,child);
            child=hide;
            hiddenSwitches_.emplace(id,std::move(hide));
            break;
        }
    }
}
void VoxelSceneFeature::prepareClusters(earth::VoxelSceneDisplay& display,const nlohmann::json& config) {
    const auto mode=std::getenv("VSGRAYSIM_CLUSTERED_RENDERING");
    if(!config.value("clustered_rendering",mode&&std::string(mode)=="1"))return;
    if(!context_->widget()->supportsClusteredDraws()||!context_->preRenderRoot()) {
        spdlog::debug("Voxel cluster draw requires a graphics queue with compute support");
        return;
    }
    if(display.node->getObject<OceanLightingBindings>("voxel-ocean-lighting"))return;
    try {
        const auto previous=std::getenv("VSGRAYSIM_SCENE_REBUILD_ALL")?nullptr:display_.clusters.get();
        display.clusters=earth::SurfaceClusterDisplay::prepare(display,previous,*context_->window()->getOrCreateDevice(),context_->widget()->view()->viewID);
    }catch(const vsg::Exception& error){spdlog::warn("Voxel cluster draw unavailable: {}",error.message);}
    catch(const std::exception& error){spdlog::warn("Voxel cluster draw unavailable: {}",error.what());}
}
void VoxelSceneFeature::mountClusterCommands() {
    if(!clusterCommands_)return;
    clusterCommands_->children.clear();
    if(display_.clusters)clusterCommands_->addChild(enabled_,display_.clusters->commands());
}
void VoxelSceneFeature::decorateContext(earth::VoxelSceneDisplay& display,earth::VoxelSceneSnapshotPtr snapshot,bool pageTerrain) {
    if(oceanCommands_)oceanCommands_->children.clear();
    if(snapshot->ocean) {
        auto binding=OceanLightingBindings::create();binding->pass=std::make_shared<earth::VoxelOceanDisplay>(*snapshot,!std::getenv("VSGRAYSIM_SCENE_REBUILD_ALL")&&display_.node&&display_.node->getObject<OceanLightingBindings>("voxel-ocean-lighting")?display_.node->getObject<OceanLightingBindings>("voxel-ocean-lighting")->pass.get():nullptr,context_->window()->getOrCreateDevice().get(),context_->featureAs<EarthViewFeature>(EarthView::kId)?context_->featureAs<EarthViewFeature>(EarthView::kId)->cloudResources():nullptr);
        if(oceanCommands_)oceanCommands_->addChild(enabled_,binding->pass->commands);
        for(const auto& [id,o]:snapshot->objects){
            if(o.surface().hasMaterialBlend)continue;
            if(o.earth&&o.body.motion=="static"&&o.parentId.empty()&&!std::getenv("VSGRAYSIM_OCEAN_REFERENCE_HYDRO"))continue;
            auto placement=display.placements.at(id);
            if(placement->children.empty())continue;
            auto group=placement->children.front().cast<vsg::StateGroup>();
            if(!group||group->children.empty())continue;
            auto draw=group->children.front().cast<vsg::VertexIndexDraw>();
            if(draw)placement->children.front()=binding->pass->object(o,draw);
        }
        display.node->addChild(binding->pass->background);display.node->addChild(binding->pass->depthSurface);display.node->addChild(binding->pass->surface);display.node->addChild(binding->pass->spray);display.node->setObject("voxel-ocean-lighting",binding);
    }
    auto* map=context_->featureAs<EarthViewFeature>(EarthView::kId);
    if(!map||!map->surfaceSnapshot())return;
    const auto previousLights=!std::getenv("VSGRAYSIM_SCENE_REBUILD_ALL")&&display_.node?display_.node->getObject<TerrainLightingBindings>("voxel-earth-lighting"):nullptr;
    auto lights=TerrainLightingBindings::create();
    display.node->setObject("voxel-earth-lighting",lights);
    for(const auto& [id,o]:snapshot->objects)if((!snapshot->ocean||!std::getenv("VSGRAYSIM_OCEAN_REFERENCE_HYDRO"))&&o.earth&&o.body.motion=="static"&&o.parentId.empty()) {
        auto placement=display.placements.at(id);if(placement->children.empty())continue;
        auto group=placement->children.front().cast<vsg::StateGroup>();if(!group||group->children.empty())continue;
        auto draw=group->children.front().cast<vsg::VertexIndexDraw>();if(!draw)continue;
        TerrainLightingBindings::Entry entry;
        if (previousLights) {
            const auto found = previousLights->cached.find(id);
            if (found != previousLights->cached.end() && found->second.source == draw.get() &&
                found->second.clouds == map->cloudResources().get() && bool(found->second.pages) == pageTerrain)
                entry = found->second;
        }
        if (!entry.pass) {
            entry.source = draw.get();
            entry.clouds = map->cloudResources().get();
            entry.pass = std::make_shared<earth::TerrainCloudShadow>(o.surface(), draw, map->cloudResources());
            if (pageTerrain) {
                const auto [x, z] = o.earth->pageSplitXZ();
                entry.pages = std::make_shared<TerrainDisplayPages>(draw, x, z, o.earth->cellSize() * 4);
                entry.pass->node->children.front() = entry.pages->node();
            }
        }
        placement->children.front() = entry.pass->node;
        lights->passes.push_back(entry.pass);
        lights->cached.emplace(id, std::move(entry));
    }
    if(!map->showsSceneContext())return;
    std::set<const earth::VoxelEarthPatch*> seen;std::vector<vsg::dbox> windows;
    auto sameWater=[](const auto& a,const auto& b){return a&&b&&a->halfExtent==b->halfExtent&&a->seaLevel==b->seaLevel&&a->bottom==b->bottom&&a->referenceSize==b->referenceSize&&a->referenceX==b->referenceX&&a->referenceZ==b->referenceZ&&a->referenceStepX==b->referenceStepX&&a->referenceStepZ==b->referenceStepZ&&a->referenceHeights==b->referenceHeights;};
    if(bool(contextOcean_)!=bool(snapshot->ocean)||(contextOcean_&&snapshot->ocean&&!sameWater(contextOcean_,snapshot->ocean)))contextKey_.clear();contextOcean_=snapshot->ocean;
    auto key=nlohmann::json::array({map->surfaceSnapshot()->revision,snapshot->anchor.lat_deg,snapshot->anchor.lon_deg,snapshot->anchor.alt_m,snapshot->anchor.heading_deg});
    for(const auto& [id,o]:snapshot->objects)if(o.earth&&seen.insert(o.earth.get()).second) {
        const auto box=o.earth->bounds();windows.push_back(box);key.push_back({box.min.x,box.min.z,box.max.x,box.max.z});
    }
    if(snapshot->ocean&&seen.empty()){const double half=snapshot->ocean->halfExtent;windows.push_back({{-half,snapshot->ocean->bottom,-half},{half,snapshot->ocean->seaLevel,half}});key.push_back({-half,-half,half,half});}
    if(windows.empty())return;
    if(std::getenv("VSGRAYSIM_SCENE_REBUILD_ALL")||contextKey_!=key.dump()) {
        std::function<double(double,double)> sea;if(snapshot->ocean)sea=[water=snapshot->ocean](double x,double z){return water->meanSurface(x,z).x+.25;};auto node=earth::makeTerrainContextDisplay(*map->surfaceSnapshot(),snapshot->anchor,windows,sea);
        contextNode_=node;contextKey_=key.dump();
    }
    auto contextGroup=contextNode_.cast<vsg::StateGroup>();
    if(contextGroup&&!contextGroup->children.empty()) {
        auto draw=contextGroup->children.front().cast<vsg::VertexIndexDraw>();
        if(draw) {
            std::shared_ptr<earth::TerrainCloudShadow> lighting;
            if(previousLights){const auto found=previousLights->cached.find("@context");if(found!=previousLights->cached.end()&&found->second.source==contextNode_.get()&&found->second.clouds==map->cloudResources().get())lighting=found->second.pass;}
            if(!lighting) {
                auto metadata=*map->surfaceSnapshot();metadata.anchor=snapshot->anchor;
                lighting=std::make_shared<earth::TerrainCloudShadow>(metadata,draw,map->cloudResources());
            }
            display.contextDrawable=display.node->children.size();
            lights->cached["@context"]={contextNode_.get(),map->cloudResources().get(),lighting};display.node->addChild(lighting->node);lights->passes.push_back(std::move(lighting));return;
        }
    }
    display.contextDrawable=display.node->children.size();
    display.node->addChild(contextNode_);
}
nlohmann::json VoxelSceneFeature::terrainDisplayPageStats() const {
    auto result = nlohmann::json::object();
    const auto* lights = display_.node ? display_.node->getObject<TerrainLightingBindings>("voxel-earth-lighting") : nullptr;
    if (lights) for (const auto& [id, entry] : lights->cached) {
        if (!entry.pages) continue;
        const auto& stats = entry.pages->stats();
        result[id] = {{"visible",stats.visible}, {"resident",stats.resident}, {"loads",stats.loads},
            {"evictions",stats.evictions}, {"resident_geometry_bytes",stats.residentBytes},
            {"unpartitioned_geometry_bytes",stats.originalBytes}};
    }
    return result;
}

void VoxelSceneFeature::updateTerrainDisplayPages() {
    if (!enabled_ || !display_.node || !chart_) return;
    auto* viewer = context_->viewer();
    auto* camera = context_->camera();
    if (!viewer || !viewer->compileManager || !camera) return;
    auto* lights = display_.node->getObject<TerrainLightingBindings>("voxel-earth-lighting");
    if (!lights) return;
    for (auto& [id, entry] : lights->cached) {
        if (!entry.pages) continue;
        const auto fallback = [&](const std::string& error) {
            spdlog::warn("Terrain page rendering for {} switched to the complete draw: {}", id, error);
            viewer->deviceWaitIdle();
            const auto draw = entry.pages->source();
            auto compiled = viewer->compileManager->compile(draw);
            if (!compiled) throw std::runtime_error("terrain draw upload failed: " + compiled.message);
            vsg::updateViewer(*viewer, compiled);
            entry.pass->node->children.front() = draw;
            entry.pages.reset();
        };
        try {
            entry.pages->update(*viewer, *camera, chart_->matrix * display_.placements.at(id)->matrix);
        } catch (const vsg::Exception& error) {
            fallback(error.message);
        } catch (const std::exception& error) {
            fallback(error.what());
        }
    }
}

void VoxelSceneFeature::synchronizeTerrainQuery() {
    if(!context_||!snapshot())return;
    auto* map=context_->featureAs<EarthViewFeature>(EarthView::kId);if(!map)return;
    const auto s=snapshot();const bool unified=enabled_&&(s->ocean||std::any_of(s->objects.begin(),s->objects.end(),[](const auto& entry){return bool(entry.second.earth);}));
    if(unified&&!terrainQuery_)terrainQuery_=std::make_shared<SceneTerrainQuery>([this]{return snapshot();});
    if(!unified)terrainQuery_.reset();
    if(unified)map->setGeoAnchor(s->anchor);
    map->setSceneOcean(unified?s->ocean:std::shared_ptr<const earth::VoxelOcean>{});
    map->setSceneTerrainQuery(terrainQuery_);
}
void VoxelSceneFeature::onFrame(ViewportContext&,const FrameTick& tick) {
    if(preparing_&&preparing_->document.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
        auto definition=preparingConfig_;try{auto document=preparing_->document.get();preparing_.reset();load(definition,&document);}catch(const std::exception& error){preparing_.reset();lastError_=std::string("scene preparation failed: ")+error.what();spdlog::error("{}",lastError_);}
    }
    if(!snapshot()||!enabled_)return;
    try {
        // External simulation time is absolute; editor time advances only
        // while playing. Pausing never accrues wall-clock catch-up debt.
        if(runtime_&&!runtime_->paused()&&config_.value("advance_from_frames",true)) {
            if(tick.simulation_clock)targetSeconds_=tick.elapsed_sec;
            else {
                targetSeconds_=std::max(targetSeconds_,runtime_->snapshot()->timeSeconds());
                targetSeconds_+=std::max(0.,tick.dt_sec)*timeScale_;
            }
            runtime_->advanceTo(targetSeconds_);
        }
        synchronize();
        if (config_.value("terrain_display_paging",false) &&
            !display_.node->getObject<TerrainLightingBindings>("voxel-earth-lighting")) {
            auto* map = context_->featureAs<EarthViewFeature>(EarthViewFeature::kId);
            if (map && map->surfaceSnapshot()) {
                prepare(snapshot(),true);
                synchronize();
            }
        }
        if(display_.clusters&&context_->camera())
            clusterCommands_->setAllChildren(display_.clusters->update(display_,chart_->matrix,*context_->camera(),
                config_.value("cluster_error_pixels",.65f),context_->widget()->property("renderFrameSerial").toULongLong()));
        if(auto* binding=display_.node->getObject<OceanLightingBindings>("voxel-ocean-lighting")) {
            vsg::dmat4 vp;vsg::dvec3 eye;gui::ViewportKit::cameraMatrices(context_->camera(),vp,eye);rockyenv::AtmosphereState light;
            if(auto* sky=context_->featureAs<SkyFeature>(SkyFeature::kId)) {
                if(auto* provider=sky->lightingProvider())provider->getAtmosphereState(light);
                int y,m,d;double h;sky->currentDateTime(tick,y,m,d,h);
                if(auto* provider=sky->sunProvider()){provider->syncDateTime(y,m,d,h);rockyenv::EnvSunInfo sun,moon;if(provider->sunInfo(chart_->matrix,sun))light.sun_dir_enu=sun.dirENU;if(provider->moonInfo(chart_->matrix,moon,light.moon_phase))light.moon_dir_enu=moon.dirENU;if(!provider->solarEnabled())light.sun_light={0,0,0};if(!provider->lunarEnabled())light.moon_light={0,0,0};}
                light.exposure=sky->skyParams().exposure;
            }
            auto* sky=context_->featureAs<SkyFeature>(SkyFeature::kId);
            binding->pass->update(*snapshot(),vp*chart_->matrix,vsg::inverse(chart_->matrix)*eye,light,
                sky&&sky->enabled()&&sky->skyParams().show_atmosphere,tick.simulation_clock?tick.elapsed_sec:-1.);
        }
        if(auto* map=context_->featureAs<EarthViewFeature>(EarthView::kId))if(const auto* weather=map->terrainWeather())
            if(auto* binding=display_.node->getObject<TerrainLightingBindings>("voxel-earth-lighting")) {
                vsg::vec3 horizon(.60f,.72f,.83f),zenith(.20f,.42f,.72f);rockyenv::AtmosphereState atmosphere;
                if(auto* sky=context_->featureAs<SkyFeature>(SkyFeature::kId))if(auto* provider=sky->lightingProvider();provider&&provider->getAtmosphereState(atmosphere)){horizon=atmosphere.horizon_color;zenith=atmosphere.zenith_color;}
                auto localWeather=*weather;
                if(!map->showsSceneContext()) {
                    // Hidden background weather does not advance. The local
                    // Earth surface still needs this frame's eye and light;
                    // using the stale zero/Earth-centre eye veils it white.
                    localWeather.clouds=false;vsg::dmat4 vp;
                    gui::ViewportKit::cameraMatrices(context_->camera(),vp,localWeather.eye);
                    if(auto* sky=context_->featureAs<SkyFeature>(SkyFeature::kId)) {
                        int y,m,d;double hours;sky->currentDateTime(tick,y,m,d,hours);
                        if(auto provider=sky->sunProvider()) {
                            provider->syncDateTime(y,m,d,hours);rockyenv::EnvSunInfo sun,moon;
                            if(provider->sunInfo(chart_->matrix,sun))localWeather.sun=provider->solarEnabled()?sun.dirECEF:vsg::vec3{};
                            if(provider->moonInfo(chart_->matrix,moon,localWeather.moonPhase))localWeather.moon=moon.dirECEF;
                            if(!provider->lunarEnabled())localWeather.moonPhase=0;
                        }
                        localWeather.exposure=sky->skyParams().exposure;localWeather.turbidity=sky->skyParams().turbidity;
                        const auto* control=sky->skyControl();localWeather.displayAtmosphere=sky->enabled()&&(control?control->getShowAtmosphere():sky->skyParams().show_atmosphere);
                    }
                }
                for(const auto& pass:binding->passes)pass->update(localWeather,horizon,zenith,localWeather.displayAtmosphere&&config_.value("terrain_atmosphere",true),snapshot()->ocean.get());
            }
    } catch(const std::exception& error) {
        if(runtime_)runtime_->setPaused(true);synchronize();lastError_=error.what();
        spdlog::error("Voxel scene view failed at tick {}: {}",snapshot()->tick,lastError_);
    }
}
void VoxelSceneFeature::setEnabled(bool enabled){enabled_=enabled;if(holder_)holder_->setAllChildren(enabled);if(oceanCommands_)oceanCommands_->setAllChildren(enabled);if(clusterCommands_)clusterCommands_->setAllChildren(enabled);synchronizeTerrainQuery();}
VSGVP_REGISTER_FEATURE(VoxelSceneFeature);
}
