#include <vsgraysim/earth/VoxelOcean.h>
#include "EarthViewFeature.h"
#include <vsgraysim/earth/OrbitalGlobe.h>
#include <vsgraysim/earth/EnvironmentTerrain.h>
#include "EnvFeatures.h"
#include <rocky_env/AtmosphereRender.h>
#include <future>
#include <vsg/app/CompileManager.h>
#include "RockyGlobeBackend.h"
#include "FeatureRegistry.h"
#include "ViewportContext.h"
#include "VsgQtWidget.h"
#include "ViewportKit.h"
#include "VoxelSceneFeature.h"
#include <vsgraysim/earth/VoxelScene.h>
#include <vsgraysim/earth/EarthDataService.h>
#include <vsgraysim/earth/TerrainDisplay.h>
#include <vsgraysim/earth/SurfaceMesh.h>
#include <vsgraysim/earth/SurfaceQuality.h>
#include <simcore/GeoNavigation.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/app/Camera.h>
#include <vsg/app/Viewer.h>
#include <vsg/app/CompileManager.h>
#include <spdlog/spdlog.h>
#include <filesystem>

namespace vsgraysim::viewport3d {
#if VSGRAYSIM_WITH_ROCKY
constexpr const char* defaultEarthMap="config/earth/default.map.json";
#else
constexpr const char* defaultEarthMap="config/earth/native-volume.map.json";
#endif
namespace {
// Expand the same raster lattice around the pinned working surface. The
// continuation is drawable only and is never published to a world query.
earth::EarthSurfaceSnapshotPtr rasterPreview(const earth::EarthSurfaceSnapshotPtr& surface) {
    if (!surface->has_map_layers || surface->grid_size < 2 ||
        surface->valid_samples != surface->vertices().size() || surface->imagery_rgba.empty())
        return surface;

    const auto scale = surface->declaration.value("preview_context_scale", nlohmann::json(1));
    if (!scale.is_number_integer() || scale.get<double>() != 3.) return surface;
    const auto n = surface->grid_size;
    const uint32_t expanded = 3 * (n - 1) + 1;
    const double spanX = surface->max_x - surface->min_x;
    const double spanZ = surface->max_z - surface->min_z;
    if (expanded > 1025 || 3 * std::max(spanX, spanZ) > 200000.) return surface;

    try {
        auto outer = earth::EarthDataService::loadMap(surface->source, surface->anchor, {
            {"grid_size", expanded}, {"imagery_size", 1024}, {"strict", true},
            {"coverage", {{"min_x", surface->min_x - spanX}, {"max_x", surface->max_x + spanX},
                          {"min_z", surface->min_z - spanZ}, {"max_z", surface->max_z + spanZ}}}
        });
        if (outer->imagery_rgba.empty()) return surface;

        // Both grids use the same triangle diagonal. Reject a continuation if
        // any working vertex disagrees, rather than rendering a second ground.
        const uint32_t offset = n - 1;
        for (uint32_t z = 0; z < n; ++z) {
            for (uint32_t x = 0; x < n; ++x) {
                const auto delta = surface->vertices()[z * n + x] -
                                   outer->vertices()[(z + offset) * expanded + x + offset];
                if (vsg::length(delta) > 1e-5) {
                    spdlog::warn("Earth preview context does not match the pinned DEM");
                    return surface;
                }
            }
        }
        return outer;
    } catch (const std::exception& error) {
        spdlog::warn("Earth preview context unavailable: {}", error.what());
        return surface;
    }
}

void publishSurfaceQuality(ViewportContext* context,const earth::EarthSurfaceSnapshot& surface) {
    const auto q=earth::surfaceQuality(surface);
    if(context&&context->widget())context->widget()->setProperty("earthSurfaceQuality",QString::fromStdString(q.json().dump()));
    spdlog::info("Earth quality revision={} triangles={} top=[{},{}]m max_slope={}deg mesh_edge_mean={}m DEM={}m imagery={}m RGB_range=[{},{},{}]-[{},{},{}] contrast={} coarse={}",
        q.revision,q.validTriangles,q.heightMin,q.heightMax,q.slopeMaxDegrees,q.edgeMeanMetres,q.demSpacingMetres,q.imagerySpacingMetres,
        q.rgbMin[0],q.rgbMin[1],q.rgbMin[2],q.rgbMax[0],q.rgbMax[1],q.rgbMax[2],q.imageLumaStdDev,q.sourceCoarse);
}
}
class EarthViewFeature::GeoQuery : public IGeoTerrainQuery {
public:
    explicit GeoQuery(EarthViewFeature* owner) : owner_(owner) {}

    bool geoActive() const override
    {
        return owner_ && owner_->enu_root_;
    }

    bool intersectRayGeo(const vsg::dvec3& origin, const vsg::dvec3& dir,
                         double& lat_deg, double& lon_deg, double& elev_m) const override
    {
        if (!owner_ || !owner_->enu_root_ || vsg::length(dir)<1e-12) return false;
        const auto direction=vsg::normalize(dir);
        vsg::dvec3 hit;
        if (owner_->surface_) {
            double best=1e8;
            bool found=false;
            if(owner_->orbital_ && owner_->orbital_->intersect(origin,direction,hit)) {
                best=vsg::length(hit-origin);found=true;
            }
            for(const auto& surface:{owner_->surface_,owner_->observerSnapshot()}) {
                if(!surface) continue;
                const auto model=makeEnuToWorldMatrix(surface->anchor);
                const auto inverse=vsg::inverse(model);
                const auto o=inverse*vsg::dvec4(origin,1.0),d=inverse*vsg::dvec4(direction,0.0);
                vsg::dvec3 local,normal;
                if(!surface->intersectLocal({o.x,o.y,o.z},{d.x,d.y,d.z},best,local,normal)) continue;
                const auto world=model*vsg::dvec4(local,1.0);
                hit={world.x,world.y,world.z};best=vsg::length(hit-origin);found=true;
            }
            if(!found) return false;
        } else {
            // Preview-only queries intersect actual loaded terrain geometry,
            // never report an ellipsoid intersection as a terrain hit.
            if (!owner_->rocky_->intersect(origin, direction, hit)) return false;
        }
        const auto geo=ecefToGeodetic(hit.x,hit.y,hit.z);
        lat_deg=geo.x;lon_deg=geo.y;elev_m=geo.z;return true;
    }

    bool groundElevation(double lat_deg, double lon_deg, double& elev_m) const override
    {
        if (!owner_ || !owner_->enu_root_) return false;
        if(owner_->surface_ && owner_->surface_->orbital) {
            for(const auto& candidate:{owner_->surface_,owner_->observerSnapshot()}) {
                if(!candidate) continue;
                const auto solid=candidate->occupiedGeodetic(lat_deg,lon_deg,0.);
                if(solid.has_value()) {
                    auto top=candidate->columnTopGeodetic(lat_deg,lon_deg);
                    if(top) elev_m=*top;
                    return top.has_value();
                }
            }
            const auto top=owner_->surface_->orbital->top(lat_deg,lon_deg);
            if(top) elev_m=*top;
            return top.has_value();
        }
        if (owner_->surface_) {
            const auto& surface=*owner_->surface_;
            const auto inverse=vsg::inverse(makeEnuToWorldMatrix(surface.anchor));
            const auto radial=inverse*vsg::dvec4(geoUp(lat_deg,lon_deg),0.0);
            const vsg::dvec3 up{radial.x,radial.y,radial.z};
            double height=surface.anchor.alt_m;
            for(int i=0;i<8;++i) {
                const auto world=geodeticToECEF(lat_deg,lon_deg,height);
                const auto local=inverse*vsg::dvec4(world,1.0);
                const auto sample=surface.sampleLocal(local.x,local.z);
                if(sample.status==earth::SurfaceStatus::NoData)return false;
                const double denominator=vsg::dot(sample.normal,up);
                if(std::abs(denominator)<1e-6)return false;
                const double correction=(sample.height-local.y)*sample.normal.y/denominator;
                height+=correction;
                if(std::abs(correction)<1e-5){elev_m=height;return true;}
            }
            return false;
        }
        return owner_->rocky_->elevation(lat_deg, lon_deg, elev_m);
    }
    uint64_t revision() const override {
        return owner_ ? (owner_->surface_ ? owner_->surface_->revision : owner_->rocky_->revision()) : 0;
    }
    bool waterBedLocal(double x,double z,double level,double& height) const override {
        if(!owner_ || !owner_->surface_) return false;
        const auto sample=owner_->surface_->waterBedLocal(x,z,level);
        if(sample.status==earth::SurfaceStatus::NoData) return false;
        height=sample.height;return true;
    }
    bool heightLocal(double x, double z, double& height) const override {
        if(!owner_ || !owner_->surface_)return false;
        const auto sample=owner_->surface_->sampleLocal(x,z);
        if(sample.status==earth::SurfaceStatus::NoData)return false;
        height=sample.height;return true;
    }
    const rockyenv::ITerrainBridge* environmentTerrain() const override {
        return owner_ && owner_->surface_ ? &environment_ : nullptr;
    }
    void bindEnvironment(const earth::EarthSurfaceSnapshotPtr& surface) { environment_.bind(surface); }
    bool hasAuthoritativeSurface() const override { return owner_ && bool(owner_->surface_); }

private:
    earth::EnvironmentTerrain environment_;
    EarthViewFeature* owner_;
};

struct EarthViewFeature::Stream {
    struct Draw {
        earth::EarthSurfaceSnapshotPtr surface;
        vsg::ref_ptr<vsg::Node> node;
        vsg::CompileResult compilation;
        double contextSpan=0.;
        std::shared_ptr<earth::TerrainCloudShadow> cloudShadow;
    };
    static std::shared_ptr<Draw> prepare(earth::EarthSurfaceSnapshotPtr surface,vsg::ref_ptr<vsg::CompileManager> manager,
                                         std::shared_ptr<rockyenv::CloudGpuResources> cloud) try {
        auto prepared=std::make_shared<Draw>();
        prepared->surface=surface;
        if(cloud) {
            const auto preview = rasterPreview(surface);
            const bool extended = preview != surface;
            if (extended) prepared->contextSpan = preview->max_x - preview->min_x;
            prepared->cloudShadow=std::make_shared<earth::TerrainCloudShadow>(
                *preview,earth::makeSurfaceDraw(*preview,true),cloud,extended?surface.get():nullptr);
            auto displayOnly=vsg::Switch::create();
            displayOnly->addChild(~vsg::Mask(1),prepared->cloudShadow->node);
            prepared->node=displayOnly;
        } else {
            prepared->node=earth::makeTerrainDisplay(*surface);
        }
        if(manager && surface->resident_tiles) {
            prepared->compilation=manager->compile(prepared->node);
            if(!prepared->compilation) throw std::runtime_error("terrain viewport compilation failed: "+prepared->compilation.message);
        }
        return prepared;
    } catch(const vsg::Exception& error) { throw std::runtime_error(error.message); }
    struct Mailbox {
        std::mutex mutex;
        std::shared_ptr<Draw> published;
    };
    std::future<std::shared_ptr<Draw>> observerJob;
    std::string observerKey;
    std::shared_ptr<Draw> observer;
    std::shared_ptr<Mailbox> mailbox=std::make_shared<Mailbox>();
    std::string key;
    using Result=std::pair<earth::EarthSurfaceSnapshotPtr,std::function<void()>>;
    std::future<Result> job;
    std::future<Result> initialLoad;
    std::string initialKey, initialRetryKey;
    nlohmann::json initialScenario;
    std::string initialPath;
};
EarthViewFeature::EarthViewFeature() : stream_(std::make_unique<Stream>()),rocky_(std::make_unique<RockyGlobeBackend>()) {}
EarthViewFeature::~EarthViewFeature() = default;

void EarthViewFeature::configure(const nlohmann::json& params)
{
    if (!params.is_object()) return;
    params_.update(params);
    anchor_.lat_deg     = params.value("lat", anchor_.lat_deg);
    anchor_.lon_deg     = params.value("lon", anchor_.lon_deg);
    anchor_.alt_m       = params.value("alt", anchor_.alt_m);
    anchor_.heading_deg = params.value("heading", anchor_.heading_deg);
    view_height_        = params.value("view_height", view_height_);
}

void EarthViewFeature::installCameraOverride(ViewportContext& ctx)
{
    auto* widget = ctx.widget();
    if (!widget) return;
    auto pose = simcore::geo::initialCamera(anchor_, view_height_);
    if (voxel_node_ && surface_ && view_height_<1000000.) {
        const auto& terrain = *surface_;
        const vsg::dvec3 low{terrain.min_x, terrain.min_height, terrain.min_z};
        const vsg::dvec3 high{terrain.max_x, terrain.max_height, terrain.max_z};
        auto center = (low + high) * .5;
        double distance = vsg::length(high - low) * 1.6;
        if (terrain.resident_tiles) {
            // Frame the working surface. The bottom of the stored geology may be kilometres below it.
            auto ground = terrain.sampleLocal(0., 0.);
            if (ground.status != earth::SurfaceStatus::NoData) {
                center = {0., ground.height, 0.};
            } else {
                ground = terrain.sampleLocal(center.x, center.z);
                center.y = ground.status == earth::SurfaceStatus::NoData ? high.y : ground.height;
            }
            const double span = std::min(high.x-low.x, high.z-low.z);
            distance = std::min(span*.45, std::max(view_height_, span*.18));
        }
        pose.center = localToECEF(anchor_, center);
        const auto direction = vsg::normalize(vsg::dvec3(.7, terrain.resident_tiles ? .45 : .9, 1.));
        pose.eye = localToECEF(anchor_, center + direction * distance);
        pose.up = geoUp(anchor_.lat_deg,anchor_.lon_deg);
    }
    widget->setCameraOverride([pose](vsg::Camera* cam) -> bool {
        auto* lookAt = dynamic_cast<vsg::LookAt*>(cam->viewMatrix.get());
        auto* persp  = dynamic_cast<vsg::Perspective*>(cam->projectionMatrix.get());
        if (!lookAt || !persp) return false;
        lookAt->center = pose.center;
        lookAt->eye = pose.eye;
        lookAt->up = pose.up;
        const auto clip = simcore::geo::cameraClip(pose);
        persp->nearDistance = clip.first;
        persp->farDistance = clip.second;
        return true;
    });
}

void EarthViewFeature::ensureRockyMap() {
    if(!rocky_->skyContext()) {
        RockyGlobeBackend::configureEnvironment(params_);
        rocky_->attachContext(*ctx_);
    }
    rocky_->ensureMap(params_);
}

void* EarthViewFeature::rockyContext() const { return rocky_->skyContext(); }

void EarthViewFeature::publishView(bool frameTerrain,bool compiled) {
    if (!ctx_) return;
    enu_root_->matrix = makeEnuToWorldMatrix(anchor_);
    terrain_root_->children.clear();
    active_visibility_=vsg::Switch::create();active_visibility_->setAllChildren(!scene_query_);
    // Weather is independent of terrain storage. Raster DEM scenes must use
    // the same physical voxel cloud as native voxel terrain, not the legacy
    // local cloud sheet/sprite fallback.
    const bool weatherPresentation = surface_ && params_.value("weather_presentation", true);
    if (bool(planet_) != weatherPresentation) {
        if(auto* pre=ctx_->preRenderRoot()) {
            auto& children=pre->children;
            for(const auto& command:{gpuCloudInitialize_,gpuCloudStep_,gpuCloudTemporal_})
                children.erase(std::remove(children.begin(),children.end(),command),children.end());
        }
        gpuCloudInitialize_=nullptr;gpuCloudStep_=nullptr;gpuCloudTemporal_=nullptr;gpuCloudInitializeFrames_=0;
        for(auto layer : {EnvironmentLayer::GlobalSea,EnvironmentLayer::GlobalCloud})
            environmentRoot(layer)->children.clear();
        planet_.reset();
        weatherSurface_.reset();
        if (weatherPresentation) {
            auto device=ctx_->window() ? ctx_->window()->getOrCreateDevice() : vsg::ref_ptr<vsg::Device>{};
            planet_=std::make_unique<earth::PlanetWeather>(device.get(),cloudResources_);
            if(orbital_)environmentRoot(EnvironmentLayer::GlobalSea)->addChild(planet_->node->children.at(0));
            environmentRoot(EnvironmentLayer::GlobalCloud)->addChild(planet_->node->children.at(1));
            environmentRoot(EnvironmentLayer::GlobalCloud)->addChild(planet_->node->children.at(2)); // depth-tested rain/snow
            if(auto* pre=ctx_->preRenderRoot()) {
                gpuCloudTemporal_=vsg::ref_ptr<vsg::Switch>(static_cast<vsg::Switch*>(planet_->temporalCommands()));
                if(auto* command=planet_->gpuInitializationCommands()) {
                    gpuCloudInitialize_=vsg::Switch::create();
                    gpuCloudInitialize_->addChild(true,vsg::ref_ptr<vsg::Node>(command));
                    pre->addChild(gpuCloudInitialize_);gpuCloudInitializeFrames_=2;
                }
                if(auto* command=planet_->gpuStepCommands()) {
                    gpuCloudStep_=vsg::Switch::create();
                    gpuCloudStep_->addChild(false,vsg::ref_ptr<vsg::Node>(command));
                    pre->addChild(gpuCloudStep_);
                }
                // Both reflection and temporal integration consume the current
                // shared field, after initialization/advection has completed.
                pre->addChild(gpuCloudTemporal_);
            }
        }
    }
    // Ocean shells require a complete globe. A bounded raster map retains its
    // terrain-aware local ocean while sharing the full ECEF cloud volume.
    if(planet_){auto* sea=environmentRoot(EnvironmentLayer::GlobalSea);sea->children.clear();if(orbital_)sea->addChild(planet_->node->children.at(0));}
    if(orbital_) {
        auto globeOnly=vsg::Switch::create();
        globeOnly->addChild(~vsg::Mask(1),orbital_->node);
        terrain_root_->addChild(globeOnly);
        active_visibility_->addChild(!scene_query_,observer_root_);
        publishObserver();
    }
    if (voxel_node_) {
        auto frame = vsg::MatrixTransform::create();
        frame->matrix = enu_root_->matrix;
        frame->addChild(voxel_node_);
        active_visibility_->addChild(!scene_query_,frame);
    } else if (auto globe = rocky_->node()) {
        active_visibility_->addChild(!scene_query_,globe);
    }
    terrain_root_->addChild(active_visibility_);
    if(auto* widget=ctx_->widget()) widget->setOrbitalNavigation(bool(orbital_));
    if (auto* widget=ctx_->widget(); widget && params_.value("workspace_navigation",false))
        widget->setWorkspaceEarth(anchor_,params_.value("orbital_preview",false));
    if(!scene_query_)installCameraOverride(*ctx_);
    if (frameTerrain) ctx_->resetCamera();
    if(!compiled) ctx_->requestCompile();
}

void EarthViewFeature::onAttach(ViewportContext& ctx) {
    if (!ctx.viewer()) return;
    ctx_ = &ctx;
    weather_.cloudObjects=params_.value("cloud_objects",false);
    if(ctx.window() && !weather_.cloudObjects && !cloudResources_->field) {
        auto* device=ctx.window()->getOrCreateDevice().get();
        auto initialized=rockyenv::createCloudGpuResources(params_.contains("cloud_grid")?nullptr:device);
        if(params_.contains("cloud_grid")) {
            const auto& grid=params_.at("cloud_grid");rockyenv::GpuVoxelCloudConfig config;
            auto dimension=[&](const char* key,int fallback,int max){const auto v=grid.value(key,nlohmann::json(fallback));if(!v.is_number_integer()||v.get<double>()<8||v.get<double>()>max)throw std::invalid_argument("invalid Earth cloud grid dimension");return v.get<uint32_t>();};
            config.width=dimension("width",32,128);config.height=dimension("height",48,160);config.depth=dimension("depth",32,128);
            config.pressureIterations=dimension("pressure_iterations",8,32);
            config.voxelSizeMetres=grid.value("cell_size_m",24.f);
            if(!std::isfinite(config.voxelSizeMetres)||config.voxelSizeMetres<=0)throw std::invalid_argument("invalid Earth cloud grid spacing");
            initialized->field=std::make_shared<rockyenv::GpuVoxelCloud>();const auto built=initialized->field->build(*device,config);
            if(!built.ok)throw std::runtime_error(built.error);initialized->ownerDevice=device;
            initialized->image=vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>(initialized->field->sampler()),vsg::ref_ptr<vsg::ImageView>(initialized->field->thermodynamicMap()),VK_IMAGE_LAYOUT_GENERAL);
        }
        cloudResources_->field=initialized->field;cloudResources_->ownerDevice=initialized->ownerDevice;
        cloudResources_->image->sampler=initialized->image->sampler;
        cloudResources_->image->imageView=initialized->image->imageView;
        cloudResources_->image->imageLayout=initialized->image->imageLayout;
    }
    terrain_root_ = vsg::Group::create();
    observer_root_ = vsg::Group::create();
    enu_root_ = vsg::MatrixTransform::create();
    terrain_visibility_=vsg::Switch::create();terrain_visibility_->addChild(true,terrain_root_);
    ctx.worldRoot()->addChild(terrain_visibility_);
    ctx.worldRoot()->addChild(enu_root_);
    ctx.setContentRoot(enu_root_);
    environmentFilter_=vsg::Switch::create();
    for(auto& layer:layers_) {
        layer=vsg::Group::create();
        environmentFilter_->addChild(~vsg::Mask(1),layer);
    }
    ctx.worldRoot()->addChild(environmentFilter_);
    geo_query_ = std::make_shared<GeoQuery>(this);
    geo_query_->bindEnvironment(surface_);
    ctx.setGeoQuery(geo_query_);
    if (surface_) {
        publishSurfaceQuality(ctx_,*surface_);
        if(surface_->orbital) orbital_=std::make_unique<earth::OrbitalGlobe>(surface_->orbital,cloudResources_);
        // Every bound dataset is drawn from its pinned DEM/imagery snapshot.
        publishView();
        return;
    }
    nlohmann::json initial = nlohmann::json::object();
    if (auto source = params_.value("map_json",std::string()); !source.empty()) {
        initial["earth"] = {{"map",std::filesystem::absolute(source).string()}};
        if (params_.value("orbital_preview", false)) initial["earth"]["orbital_preview"] = true;
    }
    std::string error;
    if (!loadEarthScenario(initial, {}, error)) {
        spdlog::warn("Earth reference surface unavailable: {}", error);
        if(VSGRAYSIM_WITH_ROCKY && !params_.contains("map_json")) ensureRockyMap();
        publishView();
    }
}

void EarthViewFeature::updateWeather(ViewportContext& ctx,const FrameTick& tick) {
    auto* sky=ctx.featureAs<SkyFeature>(SkyFeature::kId);
    auto* cloud=ctx.featureAs<CloudFeature>(CloudFeature::kId);
    auto* ocean=ctx.featureAs<OceanFeature>(OceanFeature::kId);
    auto& w=weather_;
    w.denseRegionalClouds=params_.value("dense_regional_clouds",false);
    w.overcast=params_.value("overcast",false);
    vsg::dmat4 vp;
    gui::ViewportKit::cameraMatrices(ctx.camera(),vp,w.eye);
    if (vsg::length(w.eye)<1.) return;
    const auto llh=ecefToGeodetic(w.eye.x,w.eye.y,w.eye.z);
    const auto normal=geoUp(llh.x,llh.y);
    const auto sceneModel=makeEnuToWorldMatrix(anchor_);
    w.seconds=tick.elapsed_sec;
    // A paused authored world owns cloud age/wind as well as sun and moon UTC.
    // Explicit external clocks (including capture routes) take precedence.
    if(!tick.simulation_clock)if(auto* scene=ctx.featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);scene&&scene->snapshot())w.seconds=scene->snapshot()->timeSeconds();
    if (sky) {
        int y,m,d; double hours;
        sky->currentDateTime(tick,y,m,d,hours);
        w.seconds=sky->clockSeconds();
        if(auto provider=sky->sunProvider()) {
            provider->syncDateTime(y,m,d,hours);
            rockyenv::EnvSunInfo sun;
            if(provider->sunInfo(sceneModel,sun)) { w.sun=provider->solarEnabled()?sun.dirECEF:vsg::vec3{}; if(orbital_)orbital_->setSun(w.sun); }
            rockyenv::EnvSunInfo moon;
            if(provider->moonInfo(sceneModel,moon,w.moonPhase)) w.moon=moon.dirECEF;
            if(!provider->lunarEnabled()) w.moonPhase=0;
            w.exposure=sky->skyParams().exposure;w.turbidity=sky->skyParams().turbidity;
            if(orbital_)orbital_->setLighting(w.moon,w.moonPhase,w.exposure,w.turbidity);
        }
    }
    w.clouds=cloud && cloud->enabled();
    w.rain=0;w.opticalDepth=10.f;
    w.ocean=ocean && ocean->enabled();
    if (cloud) {
        w.windSpeed=cloud->windSpeed();
        const double az=vsg::radians(double(cloud->windAzimuth()));const auto dir=sceneModel*vsg::dvec4(std::sin(az),0.,-std::cos(az),0.);
        w.windAxis=vsg::normalize(vsg::cross(geoUp(anchor_.lat_deg,anchor_.lon_deg),vsg::dvec3(dir.x,dir.y,dir.z)));
        w.cover=cloud->cloudControl() ? cloud->cloudControl()->getCoverage() : std::clamp(cloud->sceneCover(),0.f,1.f);
        w.rain=cloud->rain();
        w.cloudBase=anchor_.alt_m+std::min(cloud->parameters().stratus_altitude,cloud->parameters().cumulus_altitude);
        w.cloudHeight=anchor_.alt_m+std::max(cloud->parameters().stratus_altitude+450.f,
                                             cloud->parameters().cumulus_altitude+600.f);
        if(auto control=cloud->cloudControl()) {
            const auto layers=control->getLayers();
            if(!layers.empty()) {
                w.cloudBase=std::numeric_limits<float>::max();
                w.cloudHeight=-std::numeric_limits<float>::max();
                for(const auto& layer:layers) {
                    w.cloudBase=std::min(w.cloudBase,float(anchor_.alt_m)+layer.altitude);
                    w.cloudHeight=std::max(w.cloudHeight,float(anchor_.alt_m)+layer.altitude+std::max(layer.thickness,1.f));
                }
            }
        }
        w.cloudHeight=std::max(w.cloudHeight,w.cloudBase+1.f);
        w.cloudRadius=std::max(1.f,.45f*cloud->parameters().stratus_size);
    }
    if(ocean) {
        w.seaLevel=anchor_.alt_m+ocean->parameters().sea_level;
        w.seaRadius=std::max(1.f,.48f*ocean->parameters().size);
        w.windSpeed=ocean->windSpeed(); w.waves=ocean->parameters().wave_height;
        if(auto sampler=ocean->oceanSampler()) {
            const auto state=sampler->seaState();
            w.waves=state.significant_wave_height;
            w.period=std::max(1.f,state.peak_period);
        }
        const double az=vsg::radians(double(ocean->windAzimuth()));
        const auto dir=sceneModel*vsg::dvec4(std::sin(az),0.,-std::cos(az),0.);
        w.windAxis=vsg::normalize(vsg::cross(geoUp(anchor_.lat_deg,anchor_.lon_deg),vsg::dvec3(dir.x,dir.y,dir.z)));
    }

    // Hold a tangent chart through local tile/LOD swaps. Only the terrain query
    // transform changes when a newer observer snapshot arrives.
    const bool distant=vsg::length(normal-geoUp(weatherAnchor_.lat_deg,weatherAnchor_.lon_deg))>.012;
    auto surface=surface_;
    if(auto remote=observerSnapshot(); remote &&
       vsg::length(normal-geoUp(remote->anchor.lat_deg,remote->anchor.lon_deg)) <
       vsg::length(normal-geoUp(anchor_.lat_deg,anchor_.lon_deg))) surface=remote;
    if (!weatherSurface_ || distant || surface!=weatherSurface_) {
        if(!weatherSurface_ || distant) {
            weatherAnchor_=anchor_;
            if(vsg::length(normal-geoUp(anchor_.lat_deg,anchor_.lon_deg))>.012) {
                weatherAnchor_.lat_deg=llh.x; weatherAnchor_.lon_deg=llh.y;
            }
            weatherModel_=makeEnuToWorldMatrix(weatherAnchor_);
        }
        toTerrain_=vsg::inverse(makeEnuToWorldMatrix(surface->anchor))*weatherModel_;
        fromTerrain_=vsg::inverse(toTerrain_);
        weatherSurface_=surface;
        ++weatherRevision_;
    }
    w.cloudAnchor=weatherModel_*vsg::dvec3(0.,0.,0.);
    if(sceneOcean_){const double az=vsg::radians(sceneOcean_->windAzimuth);const auto dir=sceneModel*vsg::dvec4(std::sin(az),0.,-std::cos(az),0.);w.windAxis=vsg::normalize(vsg::cross(geoUp(anchor_.lat_deg,anchor_.lon_deg),vsg::dvec3(dir.x,dir.y,dir.z)));w.ocean=true;w.seaLevel=anchor_.alt_m+sceneOcean_->meanSurface(0,0).x;w.windSpeed=sceneOcean_->windSpeed;w.waves=sceneOcean_->waveHeight;w.period=std::sqrt(sceneOcean_->waveLength*6.283185307179586/9.80665);const auto local=vsg::inverse(sceneModel)*w.eye;w.seaRadius=(std::max(std::abs(local.x),std::abs(local.z))<sceneOcean_->halfExtent*.25)?-float(sceneOcean_->halfExtent):0.f;}
    w.detail=earth::localWeatherDetail(llh.z-w.seaLevel);
    if(auto* feature=ctx.featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);feature&&feature->runtime()) {
        std::vector<rockyenv::GpuCloudCollider> bodies;const auto snapshot=feature->runtime()->snapshot();const auto scene=simcore::geo::makeEnuToWorldMatrix(snapshot->anchor);
        const auto& layout=*cloudResources_->layout;
        if((*cloudResources_->layout)[0].w>0&&cloudResources_->field) {
            const vsg::dvec3 anchor(layout[0].x,layout[0].y,layout[0].z),east(layout[1].x,layout[1].y,layout[1].z),up(layout[2].x,layout[2].y,layout[2].z),north(layout[3].x,layout[3].y,layout[3].z);
            const auto& cfg=cloudResources_->field->config();const auto high=cfg.originMetres+vsg::vec3(cfg.width,cfg.height,cfg.depth)*cfg.voxelSizeMetres;
            auto chart=[&](const vsg::dvec3& p){return vsg::vec3(vsg::dot(p,east),vsg::dot(p,up),vsg::dot(p,north));};
            for(const auto& [id,o]:snapshot->objects)if(o.role=="target") {
                const auto pose=o.pose(snapshot->tick,snapshot->fixedStep);const auto box=o.localBounds();
                const float radius=float(vsg::length(box.max-box.min)*.5)+cfg.voxelSizeMetres*.5f; // resolved wake envelope, not a solid contact shape
                const auto centre=chart(scene*(pose*o.localCentreOfMass())-anchor);
                if(centre.x+radius<cfg.originMetres.x||centre.x-radius>high.x||centre.y+radius<cfg.originMetres.y||centre.y-radius>high.y||centre.z+radius<cfg.originMetres.z||centre.z-radius>high.z)continue;
                if(bodies.size()==cfg.maxColliders)break;
                const auto c=pose*vsg::dvec4(o.localCentreOfMass(),0);const auto velocity=o.velocity+vsg::cross(o.angularVelocity,vsg::dvec3(c.x,c.y,c.z));const auto world=scene*vsg::dvec4(velocity,0);
                uint64_t key=1469598103934665603ULL;for(unsigned char c:id){key^=c;key*=1099511628211ULL;}
                bodies.push_back({key,centre,radius,chart({world.x,world.y,world.z}),1.f});
            }
        }
        planet_->setColliders(std::move(bodies));
    } else planet_->setColliders({});
    const auto* atmosphereControl=sky ? sky->skyControl() : nullptr;
    w.displayAtmosphere=sky && sky->enabled() &&
        (atmosphereControl ? atmosphereControl->getShowAtmosphere() : sky->skyParams().show_atmosphere);
    vsg::vec3 horizon{.60f,.72f,.83f};
    vsg::vec3 zenith{.20f,.42f,.72f};
    rockyenv::AtmosphereState atmosphere;
    if(sky) {
        auto* lighting=sky->lightingProvider();
        if(lighting && lighting->getAtmosphereState(atmosphere)) {
            horizon=atmosphere.horizon_color;
            zenith=atmosphere.zenith_color;
        }
    }
    w.fogVisibility=sceneOcean_?float(sceneOcean_->fogVisibility):params_.value("fog_visibility_m",0.f);
    w.fogHeight=sceneOcean_?float(sceneOcean_->fogHeight):params_.value("fog_height_m",180.f);w.fogLight=horizon;
    w.snow=w.wetness=w.snowCover=0;w.overcastAmount=-1;w.precipitationAltitude=float(llh.z);w.hasWindTravel=false;
    if(auto* scene=ctx.featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);scene&&scene->snapshot()&&scene->snapshot()->weather.enabled) {
        const auto weather=scene->snapshot()->weather.sample(w.seconds);
        w.cover=weather.cover;w.cloudBase=weather.cloudBase;w.cloudHeight=weather.cloudTop;
        w.opticalDepth=weather.opticalDepth;w.overcastAmount=weather.overcast;w.overcast=weather.overcast>.5;
        w.rain=std::clamp(float(weather.rain/40.),0.f,1.f);w.snow=std::clamp(float(weather.snow/8.),0.f,1.f);
        w.wetness=weather.wetness;w.snowCover=weather.snowCover;
        w.windSpeed=weather.windSpeed;
        const auto travel=scene->snapshot()->weather.windTravel(w.seconds);
        const auto distance=sceneModel*vsg::dvec4(travel[0],0.,-travel[1],0.);
        w.windTravel={distance.x,distance.y,distance.z};w.hasWindTravel=true;
        const double az=vsg::radians(weather.windAzimuth);const auto dir=sceneModel*vsg::dvec4(std::sin(az),0.,-std::cos(az),0.);
        w.windAxis=vsg::normalize(vsg::cross(geoUp(anchor_.lat_deg,anchor_.lon_deg),vsg::dvec3(dir.x,dir.y,dir.z)));
        w.fogVisibility=weather.visibility;w.fogHeight=weather.fogHeight;
        w.fogLight=rockyenv::precipitationFogLight(horizon,w.rain,w.snow,w.overcastAmount);
    }
    planet_->update(w);
    if(gpuCloudStep_)gpuCloudStep_->setAllChildren(planet_->gpuStepRequired());
    if(gpuCloudInitialize_ && gpuCloudInitializeFrames_>0 && --gpuCloudInitializeFrames_==0)
        gpuCloudInitialize_->setAllChildren(false);
    if(orbital_)orbital_->setWeather(w);
    if(auto* widget=ctx.widget()){const auto extent=ctx.window()->extent2D();if(planet_->updateTemporal(ctx.camera(),extent.width,extent.height,cloudPresentation_&&(!scene_query_||showsSceneContext())))ctx.requestCompile();const auto temporal=planet_->temporalState();widget->setProperty("cloudTemporalHistoryEligible",temporal.x>.5f);widget->setProperty("cloudTemporalRawWidth",int(temporal.z));widget->setProperty("cloudTemporalOutputWidth",int(temporal.w));}
    if(terrainCloudShadow_) terrainCloudShadow_->update(w,horizon,zenith,w.displayAtmosphere,sceneOcean_.get());
    if(stream_->observer && stream_->observer->cloudShadow)
        stream_->observer->cloudShadow->update(w,horizon,zenith,w.displayAtmosphere,sceneOcean_.get());
}

float EarthViewFeature::weatherBed(double x,double z,float level) const {
    if(!weatherSurface_) return std::numeric_limits<float>::quiet_NaN();
    auto point=toTerrain_*vsg::dvec4(x,level,z,1.);
    const auto bed=weatherSurface_->waterBedLocal(point.x,point.z,point.y);
    if (bed.status==earth::SurfaceStatus::NoData) return std::numeric_limits<float>::quiet_NaN();
    return float((fromTerrain_*vsg::dvec4(point.x,bed.height,point.z,1.)).y);
}

void EarthViewFeature::onFrame(ViewportContext& ctx,const FrameTick& tick) {
    if(stream_->initialLoad.valid() && stream_->initialLoad.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try {
            auto [surface,publish]=stream_->initialLoad.get();publish();bindSurface(surface,false);
            stream_->initialRetryKey.clear();
            earth_scenario_=stream_->initialScenario;earth_scenario_["earth"]=surface->declaration;earth_scenario_path_=stream_->initialPath;
            if(ctx.widget())ctx.widget()->setProperty("earthLoading",false);
        }catch(const std::exception& e){
            if(stream_->initialRetryKey!=stream_->initialKey) {
                stream_->initialRetryKey=stream_->initialKey;
                stream_->initialKey.clear();
                std::string retryError;
                if(loadEarthScenario(stream_->initialScenario,stream_->initialPath,retryError)) {
                    spdlog::warn("Earth background load retry: {}",e.what());
                    return;
                }
            }
            if(ctx.widget()){ctx.widget()->setProperty("earthLoading",false);ctx.widget()->setProperty("earthLoadError",QString::fromUtf8(e.what()));}
            spdlog::error("Earth background load failed: {}",e.what());
        }
    }
    if(scene_query_){if(showsSceneContext()&&planet_)updateWeather(ctx,tick);return;}
    if (planet_ && surface_) updateWeather(ctx,tick);
    if(!surface_ || !surface_->resident_tiles) return;
    if(!surface_->orbital && !params_.value("stream_camera",false)) return;
    if(!stream_->observerJob.valid() && !stream_->job.valid() && tick.frame_index%8) return;
    auto* cam=ctx.camera();
    auto* look=cam ? dynamic_cast<vsg::LookAt*>(cam->viewMatrix.get()) : nullptr;
    if(!look) return;
    const auto& point=look->center;
    if(vsg::length(point)<1.) return;
    const auto geo=ecefToGeodetic(point.x,point.y,point.z);
    const double distance=vsg::length(look->eye-point);
    if(surface_->orbital) {
        try {
            updateOrbitalObserver(geo,distance);
            if(distance<100000.) {
                const auto eye=ecefToGeodetic(look->eye.x,look->eye.y,look->eye.z);
                double elevation;
                if(geo_query_->groundElevation(eye.x,eye.y,elevation) && eye.z<elevation+1.) {
                    auto lift=geoUp(eye.x,eye.y)*(elevation+1.-eye.z);
                    look->eye+=lift;look->center+=lift;
                }
            }
        }
        catch(const std::exception& e) { spdlog::warn("Orbital observer terrain: {}",e.what()); }
        return;
    }
    const double width=std::max(1.,distance/6.);
    const int level=std::clamp(int(std::round(std::log2(20037508./width))),
        surface_->declaration.at("min_lod").get<int>(),surface_->declaration.at("max_lod").get<int>());
    try {
        const auto key=earth::EarthDataService::tileKey(geo.x,geo.y,level);
        if(surface_->declaration.at("tile")!=key)
            streamSurface(nlohmann::json(key).dump(),[base=surface_,geo,level]{
                return earth::EarthDataService::selectTiles(base,geo.x,geo.y,level);
            });
        else if(stream_->job.valid() && stream_->job.wait_for(std::chrono::seconds(0))==std::future_status::ready)
            stream_->job={};
    } catch(const std::exception& e) {spdlog::warn("Earth camera tile selection: {}",e.what());}
}

namespace {
// Both footprints span one tile to either side of the focus tile, clipped at the poles.
bool intersectWindows(const std::array<int,3>& a,const std::array<int,3>& b) {
    const double sa=std::ldexp(180.,-a[0]),sb=std::ldexp(180.,-b[0]);
    const double dx=std::remainder((a[1]+.5)*sa-(b[1]+.5)*sb,360.);
    const double dy=(a[2]+.5)*sa-(b[2]+.5)*sb;
    return std::abs(dx)<1.5*(sa+sb) && std::abs(dy)<1.5*(sa+sb);
}
void waitTerrainDraws(vsg::Viewer& viewer) {
    for(const auto& task:viewer.recordAndSubmitTasks)
        for(size_t i=0;auto fence=task->fence(i);++i)
            if(fence->hasDependencies() && fence->wait(UINT64_MAX)!=VK_SUCCESS)
                throw std::runtime_error("terrain viewport submission failed");
}

}
earth::EarthSurfaceSnapshotPtr EarthViewFeature::observerSnapshot() const {
    return stream_->observer ? stream_->observer->surface : nullptr;
}
void EarthViewFeature::publishObserver() {
    observer_root_->children.clear();
    if(auto& observer=stream_->observer) {
        auto frame=vsg::MatrixTransform::create();
        frame->matrix=makeEnuToWorldMatrix(observer->surface->anchor);
        frame->addChild(observer->node);
        observer_root_->addChild(frame);
    }
    using TileKey = earth::OrbitalGlobe::TileKey;
    std::optional<TileKey> observerKey;
    if (auto observer=observerSnapshot();observer&&!scene_query_) observerKey=observer->declaration.at("tile").get<TileKey>();
    orbital_->setWindows(surface_->declaration.at("tile").get<TileKey>(), observerKey);
}
void EarthViewFeature::updateOrbitalObserver(const vsg::dvec3& focus,double distance) {
    const auto& declaration=surface_->declaration;
    const int lod=std::clamp(int(std::floor(std::log2(20037508./std::max(1.,distance*.75)))),
                             declaration.at("min_lod").get<int>(),declaration.at("max_lod").get<int>());
    const auto wanted=earth::EarthDataService::tileKey(focus.x,focus.y,lod);
    const auto active=declaration.at("tile").get<std::array<int,3>>();
    const bool remote=distance<1000000. && !intersectWindows(wanted,active);
    const auto key=std::to_string(surface_->orbital->dataset)+nlohmann::json(wanted).dump();
    const auto releaseObserver=[&] {
        if(stream_->observer) waitTerrainDraws(*ctx_->viewer());
        stream_->observer.reset();
    };
    auto& pending=stream_->observerJob;
    if(pending.valid()) {
        if(pending.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
        auto prepared=pending.get();
        if(remote && stream_->observerKey==key && prepared->surface->orbital->dataset==surface_->orbital->dataset) {
            releaseObserver();
            if(prepared->compilation) vsg::updateViewer(*ctx_->viewer(),prepared->compilation);
            stream_->observer=std::move(prepared);
            publishObserver();
            if(!stream_->observer->compilation) ctx_->requestCompile();
        }
    }
    if(!remote) {
        if(stream_->observer) {releaseObserver();publishObserver();}
        // A design camera may refine the active local mesh while remaining in the scene frame.
        if(params_.value("stream_camera",false) && distance<1000000. && wanted!=active &&
           intersectWindows(wanted,active) && vsg::dot(geoUp(focus.x,focus.y),geoUp(anchor_.lat_deg,anchor_.lon_deg))>.99) {
            streamSurface(key,[base=surface_,focus,lod]{return earth::EarthDataService::selectTiles(base,focus.x,focus.y,lod);});
        }
        return;
    }
    if(stream_->observer && stream_->observer->surface->declaration.at("tile")==wanted) return;
    stream_->observerKey=key;
    pending=std::async(std::launch::async,[base=surface_,focus,lod,manager=ctx_->viewer()->compileManager,cloud=cloudResources_]{
        return Stream::prepare(earth::EarthDataService::observerTiles(base,focus.x,focus.y,lod),manager,cloud);
    });
}

void EarthViewFeature::onDetach(ViewportContext& ctx) {
    stream_=std::make_unique<Stream>(); // Join outstanding viewport preparation before detach.
    if(ctx.viewer())ctx.viewer()->deviceWaitIdle();
    if(auto* pre=ctx.preRenderRoot()) {
        auto& children=pre->children;
        for(const auto& command:{gpuCloudInitialize_,gpuCloudStep_,gpuCloudTemporal_})
            children.erase(std::remove(children.begin(),children.end(),command),children.end());
    }
    gpuCloudInitialize_=nullptr;gpuCloudStep_=nullptr;gpuCloudTemporal_=nullptr;gpuCloudInitializeFrames_=0;
    if (auto* widget = ctx.widget()) {
        widget->setCameraOverride(nullptr);widget->setOrbitalNavigation(false);widget->clearWorkspaceEarth();
    }
    ctx.setGeoQuery(nullptr);
    ctx.removeFromWorld(terrain_visibility_);terrain_visibility_=nullptr;scene_query_.reset();sceneOcean_.reset();
    ctx.removeFromWorld(enu_root_);
    ctx.removeFromWorld(environmentFilter_);environmentFilter_=nullptr;
    for(auto& layer:layers_) layer=nullptr;
    planet_.reset(); terrainCloudShadow_.reset(); weatherSurface_.reset();
    ctx.setContentRoot(nullptr);
    geo_query_.reset();
    orbital_.reset();
    observer_root_.reset();
    terrain_root_.reset();
    enu_root_.reset();
    rocky_ = std::make_unique<RockyGlobeBackend>();
    ctx_ = nullptr;
}
void EarthViewFeature::setSceneTerrainQuery(std::shared_ptr<IGeoTerrainQuery> query) {
    if(query==scene_query_)return;
    scene_query_=std::move(query);
    if(terrain_visibility_)terrain_visibility_->setAllChildren(!scene_query_||showsSceneContext());
    if(active_visibility_)active_visibility_->setAllChildren(!scene_query_);
    if(environmentFilter_)for(auto layer:{EnvironmentLayer::GlobalSea,EnvironmentLayer::GlobalCloud})
        environmentFilter_->children[size_t(layer)].mask=(scene_query_&&!showsSceneContext())||(layer==EnvironmentLayer::GlobalCloud&&!cloudPresentation_)?vsg::Mask(0):~vsg::Mask(1);
    if(scene_query_&&!showsSceneContext()) {
        if(gpuCloudStep_)gpuCloudStep_->setAllChildren(false);
        if(gpuCloudInitialize_)gpuCloudInitialize_->setAllChildren(false);
        if(gpuCloudTemporal_)gpuCloudTemporal_->setAllChildren(false);
    }
    if(ctx_)ctx_->setGeoQuery(scene_query_?scene_query_:geo_query_);
    if(orbital_)publishObserver();
}
void EarthViewFeature::setCloudPresentationEnabled(bool on) {
    cloudPresentation_=on;if(!on&&gpuCloudTemporal_)gpuCloudTemporal_->setAllChildren(false);
    if(environmentFilter_)environmentFilter_->children[size_t(EnvironmentLayer::GlobalCloud)].mask=
        on&&(!scene_query_||showsSceneContext())?~vsg::Mask(1):vsg::Mask(0);
}

void EarthViewFeature::setGeoAnchor(const GeoAnchor& anchor) {
    if (anchor.lat_deg == anchor_.lat_deg && anchor.lon_deg == anchor_.lon_deg &&
        anchor.alt_m == anchor_.alt_m && anchor.heading_deg == anchor_.heading_deg) return;
    makeEnuToWorldMatrix(anchor);
    if (surface_) {
        auto scenario = earth_scenario_;
        if(surface_->resident_tiles) scenario["earth"].erase("tile");
        scenario.erase("surface_revision");
        scenario["geo_anchor"] = {{"lat", anchor.lat_deg}, {"lon", anchor.lon_deg},
                                  {"alt", anchor.alt_m}, {"heading_deg", anchor.heading_deg}};
        std::string error;
        if (!loadEarthScenario(scenario, earth_scenario_path_, error)) throw std::runtime_error(error);
    } else {
        anchor_ = anchor;
        publishView();
    }
}

bool EarthViewFeature::loadEarthScenario(const nlohmann::json& scenario, const std::string& path,
                                         std::string& error) {
    error.clear();
    try {
        // Configure the data lookup before reading a raster map, including
        // when the display uses the pinned mesh without a Rocky MapNode.
        RockyGlobeBackend::configureEnvironment(params_);
        auto declared = scenario;
        if (!declared.contains("geo_anchor"))
            declared["geo_anchor"] = {{"lat",anchor_.lat_deg},{"lon",anchor_.lon_deg},
                                      {"alt",anchor_.alt_m},{"heading_deg",anchor_.heading_deg}};
        if (!declared.contains("earth") && !declared.contains("terrain_ref"))
            declared["earth"] = {{"map",std::filesystem::absolute(
                gui::ViewportKit::resolveAssetPath(defaultEarthMap)).string()}};
        if(params_.value("background_loading",false)) {
            nlohmann::json identity;for(const auto* name:{"geo_anchor","earth","terrain_ref","surface_revision"})if(declared.contains(name))identity[name]=declared.at(name);
            const auto key=identity.dump();
            if(stream_->initialKey==key)return true;
            // Do not destroy a running std::async future on the GUI thread.
            // Caller retries replacement after the current bounded load completes.
            if(stream_->initialLoad.valid()){error="Earth load pending";return false;}
            stream_->initialKey=key;stream_->initialScenario=declared;stream_->initialPath=path;
            if(ctx_&&ctx_->widget())ctx_->widget()->setProperty("earthLoading",true);
            stream_->initialLoad=std::async(std::launch::async,[declared,path,prepare=captureViewPreparation()] {
                auto surface=earth::EarthDataService::loadScenario(declared,path);
                if(!surface)throw std::runtime_error("Earth scenario has no usable terrain source");
                if(declared.contains("surface_revision")&&declared.at("surface_revision")!=surface->revision)throw std::runtime_error("Earth source revision mismatch");
                return Stream::Result(surface,prepare(surface));
            });
            return true;
        }
        auto surface = earth::EarthDataService::loadScenario(declared, path);
        if (!surface) throw std::invalid_argument("Earth scenario has no usable terrain source");
        if (declared.contains("surface_revision") &&
            declared.at("surface_revision") != surface->revision)
            throw std::runtime_error("地表版本与仿真主控不一致，请检查部署数据");

        const bool frameTerrain = !surface_ || (surface_->revision != surface->revision &&
            !(surface_->resident_tiles && surface->resident_tiles && surface_->source==surface->source &&
              surface_->anchor.lat_deg==surface->anchor.lat_deg && surface_->anchor.lon_deg==surface->anchor.lon_deg));
        stream_->job={}; // Explicit scenario replacement invalidates old camera requests.
        bindSurface(surface,frameTerrain);
        declared["earth"] = surface->declaration;
        earth_scenario_ = std::move(declared);
        earth_scenario_path_ = path;
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}
earth::PrepareSurfaceView EarthViewFeature::captureViewPreparation() {
    auto manager=ctx_ ? ctx_->viewer()->compileManager : vsg::ref_ptr<vsg::CompileManager>{};
    return [manager,mailbox=stream_->mailbox,cloud=cloudResources_](earth::EarthSurfaceSnapshotPtr surface) {
        auto prepared=Stream::prepare(surface,manager,cloud);
        return [mailbox,prepared]() mutable {
            std::lock_guard lock(mailbox->mutex);
            mailbox->published.swap(prepared);
        };
    };
}

bool EarthViewFeature::streamSurface(const std::string& key,std::function<earth::EarthSurfaceSnapshotPtr()> load) {
    auto& stream=*stream_;
    if(stream.job.valid()) {
        if(stream.job.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return false;
        if(stream.key==key) {
            auto [surface,publish]=stream.job.get();
            publish();
            bindSurface(std::move(surface),false);
            return true;
        }
        stream.job={}; // obsolete result, including any obsolete load error
    }
    stream.key=key;
    stream.job=std::async(std::launch::async,[load=std::move(load),prepare=captureViewPreparation()] {
        auto surface=load();
        return Stream::Result(surface,prepare(surface));
    });
    return false;
}

void EarthViewFeature::bindSurface(earth::EarthSurfaceSnapshotPtr surface,bool frameTerrain) {
    if (!surface) throw std::invalid_argument("cannot bind an empty Earth surface");
    if (surface_ && surface_->source==surface->source && surface_->revision==surface->revision) {
        surface_=std::move(surface);
        if (geo_query_) geo_query_->bindEnvironment(surface_);
        earth_scenario_["earth"]=surface_->declaration;
        return;
    }
    std::shared_ptr<Stream::Draw> prepared;
    // Raster and voxel maps share the same immutable display/query triangles.
    // Do not start a second, independently paged Rocky terrain behind them.
    {
        {
            std::lock_guard lock(stream_->mailbox->mutex);
            auto& slot=stream_->mailbox->published;
            if(slot && slot->surface==surface) prepared=std::move(slot);
        }
        if(!prepared) prepared=Stream::prepare(surface,ctx_ ? ctx_->viewer()->compileManager : nullptr,cloudResources_);
    }
    const bool globeChanged=(!orbital_ && surface->orbital) ||
        (orbital_ && (!surface->orbital || orbital_->source->dataset!=surface->orbital->dataset));
    std::unique_ptr<earth::OrbitalGlobe> replacement;
    if(globeChanged && surface->orbital && ctx_) replacement=std::make_unique<earth::OrbitalGlobe>(surface->orbital,cloudResources_);
    const bool compiled=prepared && prepared->compilation;
    if(voxel_node_ && ctx_) {
        // Only submissions referencing the outgoing drawable constrain its lifetime.
        // The transfer/compile queues can keep preparing subsequent regions.
        waitTerrainDraws(*ctx_->viewer());
    }
    if(compiled) vsg::updateViewer(*ctx_->viewer(),prepared->compilation);
    if(globeChanged) orbital_=std::move(replacement);
    if(stream_->observer && (globeChanged || !surface->orbital || intersectWindows(
            surface->declaration.at("tile").get<std::array<int,3>>(),
            stream_->observer->surface->declaration.at("tile").get<std::array<int,3>>()))) stream_->observer.reset();
    anchor_=surface->anchor;
    surface_=std::move(surface);
    if (geo_query_) geo_query_->bindEnvironment(surface_);
    earth_scenario_["earth"]=surface_->declaration;
    voxel_node_=prepared ? prepared->node : nullptr;
    terrainCloudShadow_=prepared ? prepared->cloudShadow : nullptr;
    if (ctx_ && ctx_->widget())
        ctx_->widget()->setProperty("earthRasterContextSpanMetres", prepared ? prepared->contextSpan : 0.);
    publishSurfaceQuality(ctx_,*surface_);
    publishView(frameTerrain,compiled && !globeChanged);
}
VSGVP_REGISTER_FEATURE(EarthViewFeature);
}
