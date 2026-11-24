#include <vsgraysim/earth/SurfaceMesh.h>
/**
 * Simulation.cpp
 *
 * Implementation of the per-frame loop (section 4.2.2) and the new
 * analytical single-shot helpers (listSensors / validateSensors / traceSensor).
 */

#include "vsgraysim/Simulation.h"
#include "vsgraysim/layer2_rt/SimulationGpuAdapter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "vsgraysim/core/ScenarioClockConfig.h"
#include <vsg/nodes/VertexIndexDraw.h>

namespace vsgraysim {

namespace {
template<class Visitor>
void groundFootprint(const earth::EarthSurfaceSnapshot& surface,const Simulation::SensorPose& pose,
                     double fov_x,double fov_y,double range,Visitor visit) {
    if(!std::isfinite(fov_x)||!std::isfinite(fov_y)||fov_x<0||fov_y<0||fov_x>=180||fov_y>=180||!std::isfinite(range)||range<=0)
        throw std::invalid_argument("invalid Earth measurement footprint");
    visit(pose.position);
    const double minimum=surface.min_height,maximum=surface.max_height;
    const auto forward=vsg::normalize(pose.direction),right=vsg::normalize(vsg::cross(forward,pose.up)),up=vsg::normalize(vsg::cross(right,forward));
    const double tx=std::tan(fov_x*3.14159265358979323846/360),ty=std::tan(fov_y*3.14159265358979323846/360);
    for(int x:{-1,0,1})for(int y:{-1,0,1}) {
        const auto ray=vsg::normalize(forward+right*(x*tx)+up*(y*ty));
        if(ray.y>=0 && pose.position.y>maximum)continue;
        const double distance=ray.y<0 && pose.position.y>minimum ? std::min(range,(pose.position.y-minimum)/-ray.y):range;
        const auto end=pose.position+ray*distance;
        visit(end);
    }
}
}

struct Simulation::PreparedRegion {
    earth::EarthSurfaceSnapshotPtr surface;
    std::shared_ptr<PhysicsEngine::PreparedMesh> collision;
    void prepareCollision() {
        RigidBodyDef ground;
        ground.object_id="__earth_surface"; ground.mass=0; ground.use_trimesh_shape=true;
        ground.earth_surface_revision=surface->revision;
        collision=PhysicsEngine::prepareMesh(earth::makeSurfaceDraw(*surface,false),ground);
        if(!collision) throw std::runtime_error("terrain collision preparation failed");
    }
    SimulationGpuAdapter::PreparedTerrain gpu;
    std::function<void()> publishView;
    double preparation_ms=0;
};
struct Simulation::TerrainStream {
    using Key=std::array<int,3>;
    std::optional<Key> requested;
    Key pending{};
    std::future<PreparedRegion> job;
    std::optional<PreparedRegion> ready;
    std::exception_ptr failure;
    // The next worker releases old GPU resources before preparing another scene.
    PreparedRegion retired;
};

Simulation::Simulation() {}
Simulation::~Simulation() { stop(); }

void Simulation::setEarthSurfaceOverride(earth::EarthSurfaceSnapshotPtr snapshot) {
    if(initialised_)throw std::logic_error("set Earth binding while the simulation is stopped");
    earth_surface_override_=std::move(snapshot);
}

bool Simulation::init(const SimulationConfig& cfg) try {
    auto earth_override=std::move(earth_surface_override_);earth_surface_override_.reset();
    cfg_ = cfg;

    resetLayer0();
    earth_surface_=std::move(earth_override);

    phys_->loadObjectConfig(cfg_.object_config_path);
    if(earth_surface_){auto material=earth_surface_->material_config;material["object_id"]="__earth_surface";material["face_count"]=0;phys_->loadObjectConfigFromJson(material);}
    phys_->validateAll();
    phys_->buildGeoUnitArray();
    phys_->buildBandParamMap();

    atm_->loadConfig(cfg_.atmosphere_config_path);
    if (!cfg_.weather_scenario.empty()) {
        atm_->applyWeatherScenario(cfg_.weather_scenario, cfg_.weather_severity);
    }

    rules_->loadConfig(cfg_.sensor_config_path);
    rules_->buildRoutes();

    std::ifstream sensors(cfg_.sensor_config_path);
    nlohmann::json config; sensors >> config;
    scenario_config_["sensors"]=std::move(config);
    return initRemainingLayers() && initGpuSensors();
} catch (const std::exception& e) {
    initialised_=false; spdlog::error("Simulation initialization failed: {}",e.what()); return false;
}

void Simulation::resetLayer0() {
    terrain_stream_.reset(); // Join the worker before destroying its device and simulation.
    gpu_sensor_.reset();
    earth_surface_.reset();
    earth_valid_from_=0.;
    scenario_config_=nlohmann::json::object();
    initialised_ = false;
    sim_time_ = 0.0; accumulated_time_=0.0; time_compensation_=0.0;
    frame_count_ = 0;
    last_frames_.clear();
    completed_frames_.clear();
    gpu_frames_.clear();
    fresh_frame_sids_.clear();
    signal_processors_.clear();
    phys_  = std::make_shared<PhysParamManager>();
    atm_   = std::make_shared<AtmosphereModel>();
    rules_ = std::make_shared<CrossInductionRules>();
    // 重新初始化时清空上一场景的挂载/信号参数/帧序状态, 避免残留串场。
    sensor_mounts_.clear();
    sensor_signal_params_.clear();
}

bool Simulation::initFromScenario(const std::string& scenario_path) try {
    auto earth_override=std::move(earth_surface_override_);earth_surface_override_.reset();
    std::ifstream in(scenario_path);
    if (!in) {
        spdlog::error("Simulation: cannot open scenario '{}'", scenario_path);
        return false;
    }
    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        spdlog::error("Simulation: scenario '{}' parse failed: {}", scenario_path, e.what());
        return false;
    }
    if (!j.contains("objects")) {
        spdlog::error("Simulation: '{}' is not a scenario (no objects)", scenario_path);
        return false;
    }

    SimulationConfig cfg;
    cfg.headless = true;
    cfg.window_width  = 1280;
    cfg.window_height = 720;
    cfg.shader_dir    = "shaders";
    if (j.contains("simulation")) {
        const auto& s = j["simulation"];
        const auto clock=ScenarioClockConfig::fromJson(s);
        cfg.frame_dt=clock.frame_dt; cfg.physics_step=clock.physics_step;
        cfg.history_seconds=clock.history_seconds; cfg.epoch_utc=clock.epoch_utc;
        cfg.use_gpu_rt=s.value("use_gpu_rt",true);
        cfg.sensor_backend=s.value("sensor_backend",std::string("auto"));
        cfg.gpu_validation=s.value("gpu_validation",false);
        cfg.max_frames     = s.value("max_frames", 0u);
        cfg.total_duration = s.value("total_duration", 0.0f);
    }
    if (j.contains("atmosphere")) {
        const auto& a = j["atmosphere"];
        cfg.atmosphere_config_path = a.value("config_path", std::string("config/atmosphere_layers.json"));
        cfg.weather_scenario       = a.value("weather_scenario", std::string("clear"));
        cfg.weather_severity       = a.value("weather_severity", 1.0f);
    }
    cfg_ = cfg;

    resetLayer0();

    scenario_config_=j;
    earth_surface_=earth::EarthDataService::loadScenario(j,scenario_path);
    if (!earth_surface_ || (earth_override && earth_surface_->source == earth_override->source &&
        earth_surface_->revision == earth_override->revision))
        earth_surface_ = std::move(earth_override);
    if(earth_surface_ && j.contains("geo_anchor")) {
        const auto& requested=j["geo_anchor"];const auto& actual=earth_surface_->anchor;
        if(std::abs(requested.value("lat",actual.lat_deg)-actual.lat_deg)>1e-9 ||
           std::abs(std::remainder(requested.value("lon",actual.lon_deg)-actual.lon_deg,360.))>1e-9 ||
           std::abs(requested.value("alt",requested.value("alt_m",0.))-actual.alt_m)>1e-6 ||
           std::abs(std::remainder(requested.value("heading_deg",0.)-actual.heading_deg,360.))>1e-9)
            throw std::invalid_argument("host Earth snapshot anchor differs from scenario.geo_anchor");
    }
    if(earth_surface_) {
        auto ground=earth_surface_->material_config;ground["object_id"]="__earth_surface";ground["face_count"]=0;
        phys_->loadObjectConfigFromJson(ground);
    }

    // Objects (scenario schema; face_groups optional, defaulted internally).
    for (const auto& obj : j.at("objects")) {
        if(obj.value("object_id",std::string())=="__earth_surface")throw std::invalid_argument("reserved Earth surface object id");
        phys_->loadObjectConfigFromJson(obj);
    }
    phys_->validateAll();
    phys_->buildGeoUnitArray();
    phys_->buildBandParamMap();

    // Sensors (same schema as sensor_rich_set.json).
    const auto& sensors = j.value("sensors", nlohmann::json::object());
    nlohmann::json rules_json;
    rules_json["sensor_list"] = sensors.value("sensor_list", nlohmann::json::array());
    if (sensors.contains("cross_priority"))
        rules_json["cross_priority"] = sensors["cross_priority"];
    rules_->loadConfigFromJson(rules_json);
    rules_->buildRoutes();

    // 逐装备信号参数 (场景设计器落盘的 signal_params): 覆盖全局默认值,
    // 使距离门/多普勒门/积分时间/帧序等可按装备定制。
    for (const auto& js : sensors.value("sensor_list", nlohmann::json::array())) {
        const uint32_t sid = js.value("sensor_id", 0u);
        if (sid == 0 || !js.contains("signal_params") || !js["signal_params"].is_object())
            continue;
        const auto sp = js["signal_params"];
        SensorSignalParams p;
        if (sp.contains("range_gates"))      p.range_gates      = sp["range_gates"].get<uint32_t>();
        if (sp.contains("doppler_bins"))     p.doppler_bins     = sp["doppler_bins"].get<uint32_t>();
        if (sp.contains("chirp_bandwidth"))  p.chirp_bandwidth  = sp["chirp_bandwidth"].get<float>();
        if (sp.contains("chirp_duration"))   p.chirp_duration   = sp["chirp_duration"].get<float>();
        if (sp.contains("fs"))               p.fs               = sp["fs"].get<float>();
        if (sp.contains("image_width"))      p.image_width      = sp["image_width"].get<uint32_t>();
        if (sp.contains("image_height"))     p.image_height     = sp["image_height"].get<uint32_t>();
        if (sp.contains("integration_time")) p.integration_time = sp["integration_time"].get<float>();
        if (sp.contains("netd"))             p.netd             = sp["netd"].get<float>();
        p.frame_interval_s = sp.value("frame_interval_s", 0.0);
        sensor_signal_params_[sid] = p;
    }
    if (!sensor_signal_params_.empty())
        spdlog::info("Simulation: {} sensors carry per-sensor signal_params", sensor_signal_params_.size());

    // Atmosphere (external config path + weather overrides).
    {
        std::ifstream af(cfg_.atmosphere_config_path);
        if (af) {
            atm_->loadConfig(cfg_.atmosphere_config_path);
        } else {
            spdlog::warn("Simulation: atmosphere config '{}' missing - using defaults",
                         cfg_.atmosphere_config_path);
        }
        if (!cfg_.weather_scenario.empty())
            atm_->applyWeatherScenario(cfg_.weather_scenario, cfg_.weather_severity);
    }

    spdlog::info("Simulation: scenario '{}' parsed ({} objects, {} sensors)",
                 scenario_path, j["objects"].size(),
                 sensors.value("sensor_list", nlohmann::json::array()).size());

    if (!initRemainingLayers()) return false;
    if (j.contains("geo_anchor")) {
        const auto& a=j["geo_anchor"];
        simcore::geo::GeoAnchor anchor;
        anchor.lat_deg=a.value("lat",anchor.lat_deg); anchor.lon_deg=a.value("lon",anchor.lon_deg);
        anchor.alt_m=a.value("alt",0.0); anchor.heading_deg=a.value("heading_deg",0.0);
        physics_->setGeoAnchor(anchor);
    }
    // Mounting is an engine contract, also used by batch/headless runs.
    // The GUI does not supply a second, different set of sensor poses.
    for (const auto& js:sensors.value("sensor_list",nlohmann::json::array())) {
        const uint32_t id=js.at("sensor_id").get<uint32_t>();
        const auto ap=js.value("aperture",nlohmann::json::object());
        const auto xyz=ap.value("position_offset",nlohmann::json::array({0,2,0}));
        const auto dir=ap.value("direction",nlohmann::json::array({0,0,-1}));
        if (xyz.size()!=3 || dir.size()!=3) throw std::invalid_argument("invalid sensor mounting vector");
        const vsg::dvec3 offset(xyz[0].get<double>(),xyz[1].get<double>(),xyz[2].get<double>());
        const vsg::dvec3 direction(dir[0].get<double>(),dir[1].get<double>(),dir[2].get<double>());
        if (!simcore::finite(offset) || !simcore::finite(direction) || vsg::length(direction)<1e-12)
            throw std::invalid_argument("invalid sensor mounting pose");
        const auto platform=js.value("platform_object",js.value("platform",std::string()));
        if (!platform.empty()) {
            if (!physics_->bodyExists(platform)) throw std::invalid_argument("sensor platform has no scene body: "+platform);
            mountSensorOnObject(id,platform,offset,direction);
        } else for (auto& aperture:scene_->apertures()) if (aperture.sensor_id==id) {
            aperture.position=offset; aperture.direction=direction/vsg::length(direction);
        }
    }
    return initGpuSensors();
} catch (const std::exception& e) {
    initialised_=false; spdlog::error("Scenario '{}' is invalid: {}",scenario_path,e.what()); return false;
}

bool Simulation::initGpuSensors() {
    if(cfg_.sensor_backend!="auto" && cfg_.sensor_backend!="cpu" && cfg_.sensor_backend!="gpu")
        throw std::invalid_argument("sensor_backend must be auto, cpu or gpu");
    if(cfg_.sensor_backend=="cpu" || (cfg_.sensor_backend=="auto" && !cfg_.use_gpu_rt)) return true;
    if(!sensor_device_ && vis_) sensor_device_=vis_->device();
    try { gpu_sensor_=std::make_unique<SimulationGpuAdapter>(*this,phys_,scenario_config_); }
    catch(const std::exception& error) {
        if(cfg_.sensor_backend=="gpu") throw;
        spdlog::warn("GPU sensor initialization unavailable; using CPU reference: {}",error.what());
    }
    return true;
}

void Simulation::setSensorDevice(vsg::Device* device) {
    if(gpu_sensor_) throw std::logic_error("stop GPU sensors before replacing their device");
    sensor_device_=device;
}

bool Simulation::initRemainingLayers() {
    ScenarioClockConfig clock;
    clock.frame_dt=cfg_.frame_dt; clock.physics_step=cfg_.physics_step;
    clock.history_seconds=cfg_.history_seconds; clock.epoch_utc=cfg_.epoch_utc; clock.validate();
    atm_int_ = std::make_shared<AtmosphereIntegrator>(atm_);

    // ----- Physics engine (Layer 0: Bullet3 + vsgBullet) -----
    physics_ = std::make_shared<PhysicsEngine>();
    physics_->configureTimeline(cfg_.physics_step,cfg_.history_seconds,cfg_.epoch_utc);
    physics_->setGravity(vsg::dvec3(0.0, -9.81, 0.0));
    if(earth_surface_) {
        physics_->setGeoAnchor(earth_surface_->anchor);
        const auto& vertices=earth_surface_->vertices();const auto& indices=earth_surface_->indices();
        auto mesh=earth::makeSurfaceDraw(*earth_surface_, false);
        RigidBodyDef ground;ground.earth_surface_revision=earth_surface_->revision;ground.object_id="__earth_surface";ground.mass=0;ground.use_trimesh_shape=true;
        if(!physics_->createRigidBodyFromMesh(mesh,ground))throw std::runtime_error("failed to install authoritative Earth collision surface");
        spdlog::info("Earth surface pinned revision={} vertices={} triangles={} resolution={}m provenance_assumed={}",earth_surface_->revision,vertices.size(),indices.size()/3,earth_surface_->resolution_m,earth_surface_->provenance_assumed);
    } else physics_->addGroundPlane(0.0); // Legacy local fixtures explicitly have no Earth dataset.

    // ----- Layer 1 -----
    scene_ = std::make_shared<SceneBuilder>(phys_, physics_);
    scene_->build();

    // Attach sensor receiver apertures from the rules.
    // Positions are matched to the inspector/panel layout in the GUI.
    for (const auto& s : rules_->sensors()) {
        SensorAperture a{};
        a.sensor_id    = s.sensor_id;
        a.direction    = vsg::dvec3(0.0, 0.0, -1.0);
        a.half_angle   = 0.1;
        a.aperture_area = 0.05;
        // Sensor positions cover the 10-sensor rich config as well as the
        // original 5-sensor config.  Positions are arranged around the scene
        // origin so each sensor has a clear line of sight to the target.
        switch (s.sensor_id) {
            // Radar cluster (long range, low elevation)
            case 1:  a.position = vsg::dvec3(  0,  3,  55); break; // Radar X
            case 2:  a.position = vsg::dvec3( 12,  3,  60); break; // Radar C
            case 3:  a.position = vsg::dvec3(-12,  3,  60); break; // Radar Ku
            // IR cluster (mid range, high elevation)
            case 4:  a.position = vsg::dvec3(-30,  8,  30); break; // IR SWIR
            case 5:  a.position = vsg::dvec3(  0,  8,  25); break; // IR MWIR
            case 6:  a.position = vsg::dvec3( 30,  8,  30); break; // IR LWIR
            // Laser cluster (short range, low elevation)
            case 7:  a.position = vsg::dvec3(  0,  3,  35); break; // Laser 1064
            case 8:  a.position = vsg::dvec3(  6,  3,  35); break; // Laser 1550
            // Passive optical
            case 9:  a.position = vsg::dvec3( 25,  8,  30); break; // VIS
            case 10: a.position = vsg::dvec3(-15, 12,  45); break; // SAR
            default: a.position = vsg::dvec3(  0,  5,  30); break;
        }
        scene_->addSensorAperture(a);
    }

    xdetector_ = std::make_shared<CrossInductionDetector>(rules_, scene_);

    // ----- Layer 5: Visualizer (must be initialised before RT so VkDevice is available) -----
    if (!cfg_.headless) {
        vis_ = std::make_shared<Visualizer>();
        vis_->init(cfg_.window_width, cfg_.window_height);
        vis_->setScene(scene_->build());
        vis_->updateAtmosphereOverlay(*atm_);
    } else {
        spdlog::info("Layer5: headless mode - VSG viewer skipped");
    }

    // ----- Layer 2: Vulkan RT pipeline (GPU) -----
    rt_ = std::make_shared<RayTracingPipelineWrap>();
    {
        RTPipelineDescriptor rt_desc{};
        rt_desc.width       = static_cast<uint32_t>(cfg_.window_width);
        rt_desc.height      = static_cast<uint32_t>(cfg_.window_height);
        rt_desc.shader_dir  = cfg_.shader_dir;
        rt_desc.scene_node  = nullptr;
        // VkDevice handles are wired through the VSG viewer if available
        if (vis_ && rt_->supportsMeasurementSnapshots()) {
            auto vsg_dev = vis_->device();
            if (vsg_dev) {
                rt_desc.vsg_device      = vsg_dev;
                rt_desc.device          = vsg_dev->vk();
                rt_desc.physical_device = vsg_dev->getPhysicalDevice()->vk();
                rt_desc.queue_family    = static_cast<uint32_t>(
                    vsg_dev->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT));
                vkGetDeviceQueue(rt_desc.device, rt_desc.queue_family, 0, &rt_desc.queue);
                // Create a dedicated command pool for RT dispatch
                VkCommandPoolCreateInfo cpci{};
                cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                cpci.queueFamilyIndex = rt_desc.queue_family;
                vkCreateCommandPool(rt_desc.device, &cpci, nullptr, &rt_desc.command_pool);
            }
        }
        rt_->initialise(rt_desc, scene_->build());
        rt_->uploadGeoPhys(phys_->geoUnitArray());
        rt_->uploadAtmosphere(atm_->layers());
        rt_->uploadBandMap(phys_->bandParamMap());
    }

    std::vector<RayTracingPipelineWrap::ApertureDescGPU> aps;
    for (const auto& a : scene_->apertures()) {
        RayTracingPipelineWrap::ApertureDescGPU g{};
        g.sensor_id     = a.sensor_id;
        g.pos[0] = float(a.position.x); g.pos[1] = float(a.position.y); g.pos[2] = float(a.position.z);
        g.dir[0] = float(a.direction.x); g.dir[1] = float(a.direction.y); g.dir[2] = float(a.direction.z);
        g.half_angle    = float(a.half_angle);
        g.aperture_area = float(a.aperture_area);
        aps.push_back(g);
    }
    rt_->uploadApertures(aps);

    std::vector<RayTracingPipelineWrap::RouteDescGPU> routes_gpu;
    for (const auto& r : rules_->routes()) {
        RayTracingPipelineWrap::RouteDescGPU g{};
        g.tx_sensor_id    = r.tx_sensor_id;
        g.rx_sensor_id    = r.rx_sensor_id;
        g.tx_type         = (int32_t)r.tx_type;
        g.rx_type         = (int32_t)r.rx_type;
        g.cross_type      = (int32_t)r.cross_type;
        g.priority        = r.priority;
        g.coupling_factor = r.coupling_factor;
        g.band_match      = r.band_match;
        g.is_process      = r.is_process ? 1 : 0;
        routes_gpu.push_back(g);
    }
    rt_->uploadRoutes(routes_gpu);

    // ----- Layer 2: Ray Tracer (CPU reference implementation) -----
    ray_tracer_ = std::make_shared<RayTracer>(atm_, scene_, rules_);
    ray_tracer_->setPhysicalParameters(phys_);
    ray_tracer_->setRaysPerSensor(100);  // 100 rays per sensor
    ray_tracer_->setAtmosphereEnabled(true);
    ray_tracer_->setSceneIntersectionEnabled(true);
    // 接入物理世界 (Bullet 静态三角网格内置 BVH 加速结构): 求交命中真实几何,
    // 未命中回落占位面 (回波链路非零)。
    if (physics_) ray_tracer_->setPhysicsEngine(physics_.get());
    spdlog::info("Layer2: RayTracer initialised (CPU reference path)");

    // ----- Layer 3 (CUDA signal processing) -----
    cuda_ = std::make_shared<SignalProcessor>();
    if (!cuda_->init()) {
        spdlog::warn("Layer3: CUDA SignalProcessor init failed - using CPU fallback");
        cuda_.reset();  // Disable CUDA, will use CPU fallback path
    } else {
        spdlog::info("Layer3: CUDA SignalProcessor enabled");
        // Upload crosstalk routes + receiver apertures once so cross-
        // induction detection can run on the GPU (no CPU round-trip of
        // the scatter buffer).
        std::vector<CrossRouteGPU> cross_routes;
        cross_routes.reserve(rules_->routes().size());
        for (const auto& r : rules_->routes()) {
            CrossRouteGPU g{};
            g.tx_sensor_id    = r.tx_sensor_id;
            g.rx_sensor_id    = r.rx_sensor_id;
            g.priority        = r.priority;
            g.coupling_factor = r.coupling_factor;
            g.band_match      = r.band_match;
            cross_routes.push_back(g);
        }
        std::vector<ApertureGPU> cross_aps;
        cross_aps.reserve(scene_->apertures().size());
        for (const auto& a : scene_->apertures()) {
            ApertureGPU g{};
            g.sensor_id = a.sensor_id;
            g.dir[0] = float(a.direction.x);
            g.dir[1] = float(a.direction.y);
            g.dir[2] = float(a.direction.z);
            g.half_angle    = float(a.half_angle);
            g.aperture_area = float(a.aperture_area);
            cross_aps.push_back(g);
        }
        cuda_->uploadCrossRoutes(cross_routes, cross_aps);
    }

    // ----- Layer 4 -----
    sched_ = std::make_shared<SensorScheduler>(rules_);
    for(const auto& sensor:rules_->sensors()) {
        const auto parameters=sensor_signal_params_.find(sensor.sensor_id);
        sched_->setImageCadence(sensor.sensor_id,
            parameters==sensor_signal_params_.end() || parameters->second.frame_interval_s==0 ? cfg_.frame_dt : parameters->second.frame_interval_s,
            parameters==sensor_signal_params_.end() ? signal_cfg_.integration_time :
                parameters->second.integration_time.value_or(signal_cfg_.integration_time));
    }
    disp_  = std::make_shared<DataDispatcher>(rules_, cuda_);

    // ----- Layer 3b: Signal formation config -----
    signal_cfg_.image_width  = static_cast<uint32_t>(cfg_.window_width);
    signal_cfg_.image_height = static_cast<uint32_t>(cfg_.window_height);
    signal_cfg_base_ = signal_cfg_;   // 逐装备覆盖的基线 (避免装备间参数串漏)

    initialised_ = true;
    spdlog::info("Simulation: all layers initialised (headless={})",
                 cfg_.headless ? "yes" : "no");
    return true;
}

std::size_t Simulation::run() {
    if (!initialised_) return 0;
    running_ = true;
    const auto start_count=frame_count_;
    simcore::SimulationClock clock(simcore::PeriodicTimeline::fromHz(1/cfg_.frame_dt));
    clock.reset(simcore::fromSeconds(sim_time_));
    while (running_) {
        const auto before=clock.now(), after=clock.advance();
        if (!step(simcore::toSeconds(after-before),simcore::toSeconds(after))) break;
        if (cfg_.total_duration > 0.0f && sim_time_ >= cfg_.total_duration) break;
        if (cfg_.max_frames > 0 && frame_count_ >= cfg_.max_frames) break;
    }
    return frame_count_-start_count;
}

std::size_t Simulation::runHeadless(std::size_t n_frames) {
    if (!initialised_) return 0;
    running_ = true;
    const auto start_count=frame_count_;
    simcore::SimulationClock clock(simcore::PeriodicTimeline::fromHz(1/cfg_.frame_dt));
    clock.reset(simcore::fromSeconds(sim_time_));
    while (running_ && frame_count_-start_count < n_frames) {
        const auto before=clock.now(), after=clock.advance();
        if (!step(simcore::toSeconds(after-before),simcore::toSeconds(after))) break;
    }
    running_ = false;

    // Save signal frames to PPM for offline verification
    std::vector<float> out_imgs[4];
    generateBandImages(640, 480, out_imgs);

    return frame_count_-start_count;
}

void Simulation::stop() {
    running_ = false;
    initialised_ = false;
    terrain_stream_.reset(); // Join the worker before destroying its device and simulation.
    gpu_sensor_.reset();
    // Destroy RT pipeline before Visualizer since it references vis_'s VkDevice
    rt_.reset();
    if (vis_) vis_->close();
}

void Simulation::setSensorGate(bool enabled, std::set<uint32_t> sids) {
    std::lock_guard<std::mutex> lock(gate_mtx_);
    sensor_gate_enabled_ = enabled;
    sensor_gate_ = std::move(sids);
}

bool Simulation::generateBandImages(uint32_t width, uint32_t height,
                                    std::vector<float> out_images[4]) {
    if (!initialised_) return false;

    // ---- Primary path: CUDA ----
    if (cuda_ && cuda_usable_) {
        static const BandChannel channels[4] = { CH_VIS, CH_IR, CH_RADAR, CH_SAR };
        bool all_ok = true;
        for (int i = 0; i < 4; ++i) {
            if (!cuda_->generateSensorImage(width, height,
                                             static_cast<uint32_t>(channels[i]),
                                             /*gain*/ 1.0f, out_images[i])) {
                all_ok = false;
                break;
            }
        }
        if (all_ok) return true;
        // CUDA failed mid-way -> fall through to CPU fallback
        cuda_usable_ = false;
        spdlog::warn("generateBandImages: CUDA path failed - using CPU fallback");
    }

    // ---- SignalFrame path: use radar RD maps / SAR / VIS / IR frames ----
    {
        auto frameTypeToChannel = [](SensorType t) -> uint32_t {
            switch (t) {
                case SensorType::VIS:   return CH_VIS;
                case SensorType::LASER: return CH_VIS;
                case SensorType::IR:    return CH_IR;
                case SensorType::RADAR: return CH_RADAR;
                case SensorType::SAR:   return CH_SAR;
            }
            return CH_VIS;
        };

        // Find the first available frame for each band channel
        for (const auto& [sid, frame] : last_frames_) {
            if (frame.empty()) continue;
            uint32_t ch = frameTypeToChannel(frame.sensor_type);
            if (!out_images[ch].empty()) continue; // first frame per channel wins


            std::vector<float>& img = out_images[ch];
            img.assign(size_t(width) * size_t(height) * 4, 0.0f);

            float max_mag = 1e-12f;
            std::vector<float> mag(frame.width * frame.height);
            for (size_t i = 0; i < frame.width * frame.height; ++i) {
                mag[i] = std::abs(frame.samples[i]);
                if (mag[i] > max_mag) max_mag = mag[i];
            }
            float scale = (max_mag > 1e-12f) ? 1.0f / max_mag : 1.0f;

            // Nearest-neighbor resize to output dimensions
            for (uint32_t py = 0; py < height; ++py) {
                for (uint32_t px = 0; px < width; ++px) {
                    size_t si = size_t(float(py) / float(height) * frame.height) * frame.width
                              + size_t(float(px) / float(width) * frame.width);
                    si = std::min(si, size_t(frame.width) * frame.height - 1);

                    float v = mag[si] * scale;
                    v = 20.0f * std::log10(v + 1e-12f);
                    v = (v + 60.0f) / 60.0f;
                    v = std::clamp(v, 0.0f, 1.0f);

                    float r = v, g = v, b = v;
                    if (ch == CH_RADAR || ch == CH_SAR) {
                        r = std::min(v * 2.0f, 1.0f);
                        g = std::min(v * 2.0f - 0.5f, 1.0f);
                        b = v * 0.5f + 0.2f;
                        r = std::clamp(r, 0.0f, 1.0f);
                        g = std::clamp(g, 0.0f, 1.0f);
                        b = std::clamp(b, 0.0f, 1.0f);
                    }

                    size_t oi = (size_t(py) * width + px) * 4;
                    img[oi+0] = r; img[oi+1] = g; img[oi+2] = b; img[oi+3] = 1.0f;
                }
            }

            const char* names[4] = { "vis", "ir", "radar", "sar" };
            std::ostringstream path;
            path << "/tmp/vsgraysim_signal_" << names[ch] << "_sid" << sid << ".ppm";
            std::ofstream ofs(path.str(), std::ios::binary);
            if (ofs) {
                ofs << "P6\n" << width << " " << height << "\n255\n";
                for (uint32_t i = 0; i < width * height; ++i) {
                    const float* p = &img[i * 4];
                    char r = char(std::min(255.0f, p[0] * 255.0f));
                    char g = char(std::min(255.0f, p[1] * 255.0f));
                    char b = char(std::min(255.0f, p[2] * 255.0f));
                    ofs.write(&r, 1); ofs.write(&g, 1); ofs.write(&b, 1);
                }
                spdlog::info("Saved signal frame to {}", path.str());
            }
        }
    }

    // ---- Fallback path: CPU synthesis from cached scatter points ----
    // This produces a meaningful top-down (x,z) projection of the latest
    // scatter-point cloud, per band.  Each pixel accumulates the amplitude
    // of scatter points that fall into its world-space cell, with a small
    // Gaussian splat to avoid single-pixel dots.  A faint noise floor is
    // added so empty bands are not pure black.

    // Local band-mask lookup (the file-scope helper is defined further down).
    auto typeToBand = [](SensorType t) -> BandMask {
        switch (t) {
            case SensorType::VIS:   return BAND_VIS;
            case SensorType::LASER:  return BAND_VIS;
            case SensorType::IR:     return BAND_IR;
            case SensorType::RADAR:  return BAND_RADAR;
            case SensorType::SAR:    return BAND_SAR;
        }
        return BAND_VIS;
    };

    struct BandInfo {
        BandChannel ch;
        BandMask    mask;
        // Background tint (RGBA) - band-specific so panels are distinguishable
        // even when no scatter points are present.
        float bg_r, bg_g, bg_b;
    };
    static const BandInfo bands[4] = {
        { CH_VIS,   BAND_VIS,   0.02f, 0.03f, 0.02f }, // dark green
        { CH_IR,    BAND_IR,    0.05f, 0.03f, 0.01f }, // dark amber
        { CH_RADAR, BAND_RADAR, 0.01f, 0.03f, 0.05f }, // dark cyan
        { CH_SAR,   BAND_SAR,   0.05f, 0.01f, 0.05f }, // dark magenta
    };

    // World-space window covered by the image: 200m x 200m centred on origin.
    // This matches the typical sensor-to-target distance in the demo scene.
    const float world_half_x = 100.0f;   // +-100m in x
    const float world_half_z = 100.0f;   // +-100m in z
    const float px_size_x = (2.0f * world_half_x) / float(width);
    const float px_size_z = (2.0f * world_half_z) / float(height);

    // Simple deterministic noise (no rand() to keep builds reproducible).
    auto noise = [](uint32_t i) -> float {
        uint32_t x = (i * 1103515245u + 12345u) & 0x7fffffffu;
        return float(x) / float(0x7fffffffu);
    };

    for (int bi = 0; bi < 4; ++bi) {
        const auto& b = bands[bi];
        std::vector<float>& img = out_images[bi];
        img.assign(size_t(width) * size_t(height) * 4, 0.0f);

        // 1. Background: band tint + faint noise (simulates receiver noise floor)
        for (uint32_t py = 0; py < height; ++py) {
            for (uint32_t px = 0; px < width; ++px) {
                const size_t idx = (size_t(py) * width + px) * 4;
                const float n = noise((py * 1973u + px * 9177u + uint32_t(bi) * 31u) & 0xffffu);
                img[idx + 0] = b.bg_r + n * 0.01f;
                img[idx + 1] = b.bg_g + n * 0.01f;
                img[idx + 2] = b.bg_b + n * 0.01f;
                img[idx + 3] = 1.0f;
            }
        }

        // 2. Splat each scatter point matching this band
        // Accumulate into a separate intensity buffer, then composite.
        std::vector<float> intensity(size_t(width) * size_t(height), 0.0f);
        std::vector<float> max_intensity_per_pixel(size_t(width) * size_t(height), 0.0f);
        std::size_t band_point_count = 0;

        for (const auto& sp : last_scatter_points_) {
            // Filter by band: a scatter point belongs to a band if its
            // band_type mask intersects the band's mask.
            if ((sp.band_type & b.mask) == 0) continue;
            ++band_point_count;

            // Project world (x,z) -> pixel (px,py).  Y axis = world z.
            const float fx = (sp.position.x + world_half_x) / px_size_x;
            const float fz = (sp.position.z + world_half_z) / px_size_z;
            const int px = int(fx);
            const int py = int(fz);
            if (px < 0 || px >= int(width) || py < 0 || py >= int(height)) continue;

            // Amplitude can vary by many orders of magnitude across sensors.
            // Use log-compression so weak and strong returns are both visible.
            const float a = std::max(1e-12f, sp.amplitude);
            const float log_a = std::log10(a) + 12.0f;  // -120dBFS .. +
            const float norm = std::max(0.0f, log_a / 12.0f);  // 0..1+

            // 3x3 Gaussian splat (sigma ~= 1 px)
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int qx = px + dx;
                    const int qy = py + dy;
                    if (qx < 0 || qx >= int(width) || qy < 0 || qy >= int(height)) continue;
                    const float w = (dx == 0 && dy == 0) ? 1.0f
                                  : ((dx == 0 || dy == 0) ? 0.5f : 0.25f);
                    const size_t idx = size_t(qy) * width + qx;
                    intensity[idx] += norm * w;
                    if (norm > max_intensity_per_pixel[idx]) {
                        max_intensity_per_pixel[idx] = norm;
                    }
                }
            }
        }

        // 3. Composite intensity into the RGBA image with band tint.
        // Use a tanh compression so very bright returns saturate gracefully.
        for (uint32_t py = 0; py < height; ++py) {
            for (uint32_t px = 0; px < width; ++px) {
                const size_t idx = (size_t(py) * width + px) * 4;
                const float v = std::tanh(intensity[idx / 4] * 0.5f);
                // Blend background with signal: signal replaces background
                // proportional to v.
                const float r = b.bg_r + v * (1.0f - b.bg_r);
                const float g = b.bg_g + v * (1.0f - b.bg_g);
                const float bl = b.bg_b + v * (1.0f - b.bg_b);
                img[idx + 0] = r;
                img[idx + 1] = g;
                img[idx + 2] = bl;
                img[idx + 3] = 1.0f;
            }
        }

        // 4. Mark sensor positions as bright reference dots so the user can
        // see where each sensor is in the top-down view.
        for (const auto& a : scene_->apertures()) {
            // Only draw sensors that belong to this band.
            const auto* se = rules_->findSensor(a.sensor_id);
            if (!se) continue;
            const BandMask sm = typeToBand(se->type);
            if ((sm & b.mask) == 0) continue;

            const float fx = (float(a.position.x) + world_half_x) / px_size_x;
            const float fz = (float(a.position.z) + world_half_z) / px_size_z;
            const int px = int(fx);
            const int py = int(fz);
            // 5x5 bright cross marker
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    const int qx = px + dx;
                    const int qy = py + dy;
                    if (qx < 0 || qx >= int(width) || qy < 0 || qy >= int(height)) continue;
                    if (dx != 0 && dy != 0 && std::abs(dx) + std::abs(dy) > 2) continue;
                    const size_t idx = (size_t(qy) * width + qx) * 4;
                    img[idx + 0] = 1.0f;
                    img[idx + 1] = 1.0f;
                    img[idx + 2] = 1.0f;
                    img[idx + 3] = 1.0f;
                }
            }
        }

        // One-time log: report how many scatter points fed this band image.
        static std::size_t s_log_count = 0;
        if (s_log_count < 4) {
            const char* bn = (bi == 0) ? "VIS" : (bi == 1) ? "IR"
                           : (bi == 2) ? "RADAR" : "SAR";
            spdlog::info("generateBandImages[CPU]: band {} used {} scatter points (of {} cached)",
                         bn, band_point_count, last_scatter_points_.size());
            ++s_log_count;
        }
    }

    // Save the first generated image set to /tmp for offline verification.
    static bool s_saved = false;
    if (!s_saved) {
        s_saved = true;
        const char* names[4] = { "vis", "ir", "radar", "sar" };
        for (int bi = 0; bi < 4; ++bi) {
            std::ostringstream path;
            path << "/tmp/vsgraysim_band_" << names[bi] << ".ppm";
            std::ofstream ofs(path.str(), std::ios::binary);
            if (ofs) {
                ofs << "P6\n" << width << " " << height << "\n255\n";
                for (uint32_t i = 0; i < width * height; ++i) {
                    const float* p = &out_images[bi][i * 4];
                    char r = char(std::min(255.0f, p[0] * 255.0f));
                    char g = char(std::min(255.0f, p[1] * 255.0f));
                    char b = char(std::min(255.0f, p[2] * 255.0f));
                    ofs.write(&r, 1); ofs.write(&g, 1); ofs.write(&b, 1);
                }
                spdlog::info("Saved band image to {}", path.str());
            }
        }
    }

    return true;
}

std::array<double,3> Simulation::sensorTerrainBounds(uint32_t id,double beamWidth) const {
    if(gpu_sensor_) return gpu_sensor_->footprint(id);
    const double angle=std::clamp(beamWidth*180./3.14159265358979323846,.01,170.);
    std::array<double,3> bounds{angle,angle,10000.};
    if(!scenario_config_.contains("sensors")) return bounds;
    const auto& sensors=scenario_config_.at("sensors");
    if(!sensors.contains("sensor_list")) return bounds;
    for(const auto& sensor:sensors.at("sensor_list")) if(sensor.value("sensor_id",0u)==id) {
        const auto ap=sensor.value("aperture",nlohmann::json::object());
        bounds[0]=std::max(angle,ap.value("fov_horizontal",angle));
        bounds[1]=std::max(angle,ap.value("fov_vertical",bounds[0]));
        bounds[2]=std::max(bounds[2],ap.value("range_m",bounds[2]));
        break;
    }
    return bounds;
}

void Simulation::selectEarthRegion(double latitude,double longitude,int lod) {
    if (!initialised_ || !physics_) throw std::logic_error("initialize before selecting a terrain region");
    terrain_stream_.reset();
    auto next=earth::EarthDataService::selectTiles(earth_surface_,latitude,longitude,lod);
    if (next==earth_surface_) return;
    PreparedRegion region; region.surface=next;
    region.prepareCollision();
    if(gpu_sensor_) region.gpu=gpu_sensor_->captureTerrainBuilder()(*next);
    if(prepare_earth_view_) region.publishView=prepare_earth_view_(next);
    commitEarthRegion(region);
}

void Simulation::requestEarthRegion(double latitude,double longitude,int lod) {
    if(!initialised_ || !earth_surface_ || !earth_surface_->resident_tiles)
        throw std::logic_error("tile streaming requires an initialized tiled Earth");
    if(!terrain_stream_) terrain_stream_=std::make_unique<TerrainStream>();
    terrain_stream_->requested=earth::EarthDataService::tileKey(latitude,longitude,lod);
    terrain_stream_->failure=nullptr; // An explicit retry may follow a repaired source.
}

void Simulation::updateEarthStream(const std::array<int,3>& demand,const std::array<int,3>& prefetch) {
    if(!terrain_stream_) terrain_stream_=std::make_unique<TerrainStream>();
    auto& stream=*terrain_stream_;
    const auto active=earth_surface_->declaration.at("tile").get<TerrainStream::Key>();
    const auto wanted=active==demand ? prefetch : demand;
    if(stream.job.valid()) {
        if(stream.job.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
        try { stream.ready=stream.job.get(); }
        catch(...) { stream.failure=std::current_exception(); }
    }
    if(stream.failure) {
        if(stream.pending==demand) std::rethrow_exception(stream.failure);
        if(stream.pending==wanted) return;
        stream.failure=nullptr;
    }
    if(stream.ready) {
        if(stream.pending==wanted && wanted!=active) {
            if(stream.pending!=demand) return;
            if(!stream.ready->gpu.stillCurrent || stream.ready->gpu.stillCurrent()) {
                commitEarthRegion(*stream.ready);
                stream.retired=std::move(*stream.ready);
                stream.ready.reset();
                return;
            }
        }
        stream.retired=std::move(*stream.ready);
        stream.ready.reset();
    }
    if(active==wanted) return;
    stream.pending=wanted;
    auto build=gpu_sensor_ ? gpu_sensor_->captureTerrainBuilder() : SimulationGpuAdapter::TerrainBuilder{};
    stream.job=std::async(std::launch::async,[base=earth_surface_,wanted,build=std::move(build),
                                              view=prepare_earth_view_,retired=std::move(stream.retired)]() mutable {
        retired={};
        const auto start=std::chrono::steady_clock::now();
        const double span=std::ldexp(180.,-wanted[0]);
        PreparedRegion prepared;
        prepared.surface=earth::EarthDataService::selectTiles(base,90.-(wanted[2]+.5)*span,
                                                              -180.+(wanted[1]+.5)*span,wanted[0]);
        prepared.prepareCollision();
        if(view) prepared.publishView=view(prepared.surface);
        if(build) prepared.gpu=build(*prepared.surface);
        prepared.preparation_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        return prepared;
    });
}

void Simulation::commitEarthRegion(PreparedRegion& region) {
    auto& next=region.surface;
    if (next->material_config!=earth_surface_->material_config)
        throw std::runtime_error("terrain material set changed during streaming; reload scenario");
    if (next->strict) {
        for (const auto& [id,state]:physics_->snapshot()->bodies)
            if (id!="__earth_surface" && !next->containsLocal(state.position.x,state.position.z))
                throw std::runtime_error("selected tile window excludes active body: "+id);
        for(const auto& ap:scene_->apertures()) {
            SensorPose pose;
            if(!sampleSensorPose(ap.sensor_id,sim_time_,pose)) throw std::runtime_error("sensor pose unavailable for terrain switch");
            const auto bounds=sensorTerrainBounds(ap.sensor_id,rules_->findSensor(ap.sensor_id)->tx_params.beam_width);
            groundFootprint(*next,pose,bounds[0],bounds[1],bounds[2],[&](const auto& point) {
                if(!next->containsLocal(point.x,point.z))
                    throw std::runtime_error("selected tile window excludes active sensor footprint");
            });
        }
    }
    if(!physics_->installMesh(*region.collision,true))
        throw std::runtime_error("terrain collision replacement failed");
    if(region.gpu.commit) region.gpu.commit();
    earth_surface_.swap(next);
    earth_valid_from_=sim_time_;
    ray_tracer_->setPhysicsEngine(physics_.get());
    signal_processors_.clear(); // discard partial CPU integrations from the previous surface
    if(region.publishView) region.publishView();
    if(earth_commit_handler_) earth_commit_handler_(earth_surface_);
    spdlog::info("Earth tile window {} committed at {} s, revision {}, preparation {:.2f} ms",earth_surface_->declaration.at("tile").dump(),sim_time_,earth_surface_->revision,region.preparation_ms);
}

void Simulation::followEarthRegion(double dt) {
    if(!earth_surface_ || !earth_surface_->resident_tiles) return;
    if(terrain_stream_ && terrain_stream_->requested) {
        const auto key=*terrain_stream_->requested;
        updateEarthStream(key,key);
        if(earth_surface_->declaration.at("tile")==key) terrain_stream_->requested.reset();
        return;
    }
    const auto& config=earth_surface_->declaration;
    auto world=physics_->snapshot();
    std::string follow=config.value("follow_body",std::string());
    if(!config.contains("follow_body")) {
        for(size_t i=0;i<physics_->bodyCount();++i) if(physics_->bodyActive(i) && !physics_->bodyStatic(i)) {
            follow=physics_->bodyId(i); break;
        }
    }
    if(follow.empty()) return;
    auto it=world->bodies.find(follow);
    if(it==world->bodies.end()) throw std::runtime_error("unknown Earth follow_body: "+follow);
    const auto p=simcore::geo::localToECEF(earth_surface_->anchor,it->second.position);
    const auto geo=simcore::geo::ecefToGeodetic(p.x,p.y,p.z);
    double width=1000.;
    for(const auto& [id,state]:world->bodies) if(id!="__earth_surface")
        width=std::max(width,2.*(vsg::length(state.position-it->second.position)+vsg::length(state.velocity)*dt));
    for(const auto& ap:scene_->apertures()) {
        SensorPose pose;
        if(!sampleSensorPose(ap.sensor_id,sim_time_,pose)) throw std::runtime_error("sensor terrain focus unavailable");
        const auto bounds=sensorTerrainBounds(ap.sensor_id,rules_->findSensor(ap.sensor_id)->tx_params.beam_width);
        groundFootprint(*earth_surface_,pose,bounds[0],bounds[1],bounds[2],[&](const auto& point){
            width=std::max(width,2.*std::hypot(point.x-it->second.position.x,point.z-it->second.position.z));
        });
    }
    const auto levelAt=[&](const auto& focus) {
        const double scale=std::max(.001,std::cos(simcore::geo::deg2rad(focus.x)));
        const double extent=std::max(width,2.*std::abs(focus.z));
        return std::clamp(int(std::floor(std::log2(20037508.*scale/extent))),
                          config.at("min_lod").get<int>(),config.at("max_lod").get<int>());
    };
    const auto key=earth::EarthDataService::tileKey(geo.x,geo.y,levelAt(geo));
    const auto lead=it->second.position+it->second.velocity*std::max(dt,config.value("prefetch_seconds",0.25));
    const auto ecef=simcore::geo::localToECEF(earth_surface_->anchor,lead);
    const auto predicted=simcore::geo::ecefToGeodetic(ecef.x,ecef.y,ecef.z);
    updateEarthStream(key,earth::EarthDataService::tileKey(predicted.x,predicted.y,levelAt(predicted)));
}

bool Simulation::step(double dt, std::optional<double> authoritative_time) try {
    completed_frames_.clear();
    if (!initialised_ || !std::isfinite(dt) || dt <= 0.0f ||
        (authoritative_time && (!std::isfinite(*authoritative_time) || *authoritative_time < sim_time_)))
        return false;
    // In headless mode we still drive a viewer-less frame: just sleep
    // briefly to avoid 100% CPU, since there are no display events.
    if (!cfg_.headless) {
        if (!vis_ || !vis_->frame()) return false;
    }
    // Preserve fractional steps without cumulative binary floating-point drift.
    // Publish the result on the same nanosecond clock used by physics/scheduling.
    const double increment=dt-time_compensation_;
    const double accumulated=authoritative_time.value_or(accumulated_time_+increment);
    const double compensation=authoritative_time ? 0. : (accumulated-accumulated_time_)-increment;
    const double nextTime=simcore::toSeconds(simcore::fromSeconds(accumulated));
    followEarthRegion(dt);
    accumulated_time_=accumulated;time_compensation_=compensation;
    sim_time_ = nextTime;
    ++frame_count_;

    // ----- 1. Scene dynamic update (drain dynamic nodes) -----
    for (auto& dyn : scene_->dynNodes()) {
        auto pending = dyn->drainPending();
        for (const auto& u : pending) {
            phys_->updateDynamicParams(dyn->objectId(),
                                       u.rcs_modify_factor, u.temp_offset_K);
        }
        dyn->runPerFrameHook(dt);
    }

    // ----- 1b. Physics step + platform sync -----
    // Advance Bullet, then push the resulting poses out to sensor
    // apertures mounted on moving platforms.  MotionState already wrote
    // the body transforms into the scene-graph MatrixTransform nodes.
    if (physics_) {
        physics_->step(dt, sim_time_);
        if(earth_surface_ && earth_surface_->strict) {
            const auto world=physics_->snapshot();
            for(const auto& [id,state]:world->bodies) if(id!="__earth_surface" &&
                !earth_surface_->containsLocal(state.position.x,state.position.z))
                throw std::runtime_error("physics body left the pinned Earth coverage: "+id);
        }
        syncMountedSensors();
    }

    // Cross-process link gate (BrokerNet): snapshot the active sensor set.
    // When the gate is enabled only linked sensors are simulated/transmitted.
    std::set<uint32_t> gate_set;
    bool gate_on = false;
    {
        std::lock_guard<std::mutex> lock(gate_mtx_);
        gate_on = sensor_gate_enabled_;
        gate_set = sensor_gate_;
    }
    auto gate_allows = [&gate_on, &gate_set](uint32_t sid) {
        return !gate_on || gate_set.count(sid) > 0;
    };

    if(gpu_sensor_) {
        fresh_frame_sids_.clear(); gpu_frames_.clear();
        gpu_sensor_->advanceTo(sim_time_,[&](SignalFrame frame){completeFrame(std::move(frame));},
            [&](uint32_t id){return signal_enabled_ && gate_allows(id) && (!cfg_.only_sensor_id || *cfg_.only_sensor_id==id);});
        std::stable_sort(completed_frames_.begin(),completed_frames_.end(),[](const auto& a,const auto& b) {
            return a.sim_time!=b.sim_time ? a.sim_time<b.sim_time : a.sensor_id<b.sensor_id;
        });
        if(vis_) for(const auto& frame:completed_frames_) {
            std::vector<float> rgba(frame.samples.size()*4,1.f);
            float maximum=1e-12f; for(auto value:frame.samples) maximum=std::max(maximum,std::abs(value));
            for(std::size_t i=0;i<frame.samples.size();++i) for(int c=0;c<3;++c) rgba[4*i+c]=std::abs(frame.samples[i])/maximum;
            const auto channel=frame.sensor_type==SensorType::IR ? CH_IR : frame.sensor_type==SensorType::RADAR ? CH_RADAR : frame.sensor_type==SensorType::SAR ? CH_SAR : CH_VIS;
            vis_->updateSensorImage(channel,frame.width,frame.height,rgba);
        }
        return true;
    }

    sched_->buildFrameSchedule(dt,sim_time_);

    // Optional single-sensor filter: drop scheduled tx entries that do not
    // match `only_sensor_id`, so that the rest of the pipeline only sees
    // the requested sensor's emissions.
    const std::vector<ScheduledTx>* schedule_ptr = &sched_->schedule();
    std::vector<ScheduledTx> filtered;

    if (cfg_.only_sensor_id.has_value() || gate_on || earth_valid_from_>0) {
        for (const auto& tx : *schedule_ptr) {
            if(tx.image_measurement && tx.measurement_start+1e-12<earth_valid_from_) continue;
            if (cfg_.only_sensor_id.has_value() &&
                tx.sensor_id != *cfg_.only_sensor_id)
                continue;
            if (gate_on && !gate_allows(tx.sensor_id))
                continue;   // 未链接装备: 不调度、不 RT、不产帧
            filtered.push_back(tx);
        }
        schedule_ptr = &filtered;
    }
    const auto& schedule = *schedule_ptr;

    // ----- 2b. Sample complete physics truth at each pulse timestamp -----
    std::vector<SensorPose> measurement_poses;
    measurement_poses.reserve(schedule.size());
    for (const auto& tx:schedule) {
        SensorPose pose;
        if(tx.image_measurement && !sampleSensorPose(tx.sensor_id,tx.measurement_start,pose)) {
            spdlog::error("Sensor {} has no truth for exposure start {:.9f}s",tx.sensor_id,tx.measurement_start);
            return false;
        }
        if (!sampleSensorPose(tx.sensor_id,tx.absolute_time,pose)) {
            spdlog::error("Sensor {} has no physics truth at {:.9f}s",tx.sensor_id,tx.absolute_time);
            return false;
        }
        auto bounds=sensorTerrainBounds(tx.sensor_id,tx.beam_width);
        validateEarthMeasurement(pose,bounds[0],bounds[1],bounds[2]);
        measurement_poses.push_back(pose);
    }

    // ----- 3. Ray tracing -----
    // Use GPU RT pipeline when available, fall back to CPU reference
    std::vector<ScatterPoint> sps;
    bool cross_done_on_gpu = false;   // true = 串扰检测已在 GPU 完成
    bool frames_on_gpu     = false;   // true = 信号成帧已在 GPU 完成 (散射不回传)
    gpu_frames_.clear();
    fresh_frame_sids_.clear();   // 每步重置: 仅记录本步新产帧的传感器 (GPU/CPU 成帧随后写入)
    gpu_scatter_valid_ = gpu_scatter_cross_ = 0;

    if (cfg_.use_gpu_rt && rt_ && rt_->supportsMeasurementSnapshots() && rt_->scatterCapacity() > 0) {
        // GPU path: batch all scheduled sensor dispatches into a single
        // command buffer/submit to remove per-sensor vkWaitForFences and
        // launch overhead.
        std::vector<RayTracingPipelineWrap::TracePush> pushes;
        pushes.reserve(schedule.size());
        for (std::size_t si = 0; si < schedule.size(); ++si) {
            const auto& tx = schedule[si];

            RayTracingPipelineWrap::TracePush push{};
            push.sensor_idx   = static_cast<uint32_t>(si);
            push.band_channel = static_cast<uint32_t>(tx.band_mask);
            push.tx_power     = tx.tx_power;
            push.beam_width   = tx.beam_width;
            push.frequency    = (tx.wavelength > 0.0f)
                                ? 2.99792458e8f / tx.wavelength : 0.0f;
            push.wavelength   = tx.wavelength;

            // 脉冲级时序: 发射时刻插值孔径位 + 脉冲序号/慢时间 (写入散射点)
            const auto& pose = measurement_poses[si];
            const vsg::dvec3 o = pose.position;
            push.origin[0] = float(o.x);
            push.origin[1] = float(o.y);
            push.origin[2] = float(o.z);
            push.pulse_seq = tx.pulse_seq;
            push.pulse_slow_time = tx.fire_time;
            push.flags=3;
            push.direction[0]=float(pose.direction.x);
            push.direction[1]=float(pose.direction.y);
            push.direction[2]=float(pose.direction.z);
            push.pad = 0;

            pushes.push_back(push);
        }

        rt_->traceBatch(pushes);

        // GPU 驻留零拷贝映射: 散射 SSBO 经外部内存句柄导入 CUDA (只建立一次,
        // 同一持久缓冲跨帧复用)。串扰检测与信号成帧均直接在设备上消费。
        if (cuda_) {
            rt_->waitForScatterPoints();
            if (!cuda_->hasExternalScatterBuffer()) {
                const int64_t fd = rt_->exportScatterSharedHandle();
                if (fd >= 0)
                    cuda_->importExternalScatterBuffer(static_cast<int>(fd),
                                                       rt_->scatterCapacity());
            }
            if (cuda_->hasExternalScatterBuffer() && cuda_->crossDetectionReady()) {
                cross_done_on_gpu = cuda_->detectCrossInduction();
                if (cross_done_on_gpu)
                    spdlog::debug("Layer3: cross-induction detected on GPU (zero-copy)");
            }
        }

        // 散射统计替代整缓冲回传: 仅两个 32 位计数器过 PCIe (供日志/监控)。
        const float noise_floor_w = 1e-12f;
        if (cuda_ && cuda_->hasExternalScatterBuffer()) {
            uint32_t valid = 0, cross_n = 0;
            if (cuda_->scatterStats(valid, cross_n, noise_floor_w)) {
                gpu_scatter_valid_ = valid;
                gpu_scatter_cross_ = cross_n;
            }
        }

        // Layer 3b GPU 成帧: 直接在设备散射缓冲上生成各传感器信号帧,
        // 散射数据全程驻留 GPU —— 原整缓冲 CPU 回传已移除。
        if (signal_enabled_ && cuda_ && cuda_->hasExternalScatterBuffer())
            frames_on_gpu = formFramesOnGpu(schedule, dt, gate_on, gate_allows);

        if (!frames_on_gpu) {
            // 回落: GPU 成帧不可用 (无 CUDA/旧库) 时才整缓冲回传交 CPU 成帧。
            auto raw_sps = rt_->readbackScatterPoints();
            // Filter out empty scatter points (amplitude below noise floor)
            sps.clear();
            for (auto& sp : raw_sps) {
                if (sp.amplitude > noise_floor_w) {
                    sps.push_back(sp);
                }
            }
            spdlog::debug("GPU RT: {} raw -> {} filtered scatter points",
                          raw_sps.size(), sps.size());
        }
    }

    if (!cfg_.use_gpu_rt || (!frames_on_gpu && sps.empty())) {
        // CPU reference path: fallback
        if (ray_tracer_) {
            std::vector<RayTraceConfig> trace_configs;
            trace_configs.reserve(schedule.size());

            for (std::size_t si=0;si<schedule.size();++si) {
                const auto& tx=schedule[si];
                const auto& pose=measurement_poses[si];
                RayTraceConfig config;
                config.sensor_id = tx.sensor_id;
                config.sensor_type = tx.type;
                // 脉冲发射位置: fire_time 插值孔径位; 未注册孔径保留旧默认发射位
                const auto o=pose.position;
                config.origin={float(o.x),float(o.y),float(o.z)};
                config.direction={float(pose.direction.x),float(pose.direction.y),float(pose.direction.z)};
                config.tx_power = tx.tx_power;
                config.beam_width = tx.beam_width;
                config.wavelength = tx.wavelength;
                config.band_mask = static_cast<BandMask>(tx.band_mask);
                config.num_rays = ray_tracer_->raysPerSensor();
                config.max_distance = 10000.0f;
                // 脉冲级时序: 写入散射点供信号成帧按序消费
                config.pulse_seq = tx.pulse_seq;
                config.pulse_slow_time = tx.fire_time;
                config.measurement_time=tx.absolute_time;
                config.sample_index=tx.pulse_index;
                trace_configs.push_back(config);
            }
            sps = ray_tracer_->traceFrame(trace_configs);
        }
    }

    // Cross-induction detection for each scatter point.
    // GPU RT 路径下检测已在 CUDA kernel 完成并随回传带回结果, 此处仅作为
    // CPU RT / GPU 检测不可用时的回退路径。
    if (!cross_done_on_gpu) {
        for (auto& sp : sps) {
            // Build ray payload for cross-induction detection
            Vec3 origin = sp.position;
            Vec3 dir = {0.0f, 0.0f, -1.0f};  // Back toward sensor
            float distance = std::sqrt(sp.position.x * sp.position.x +
                                       sp.position.y * sp.position.y +
                                       sp.position.z * sp.position.z);

            RayPayload pl{};
            pl.origin = origin;
            pl.dir = dir;
            pl.distance = distance;
            pl.tx_power = sp.amplitude;
            pl.tx_sensor_id = sp.tx_sensor_id;
            pl.band_type = sp.band_type;
            pl.atm_total_atten = sp.atm_atten;
            pl.atm_scatter_power = sp.atm_scatter;
            pl.atm_refract_offset = 0.0f;
            pl.is_cross_induction = 0;

            // Detect cross-induction events
            auto ev = xdetector_->detect(pl, distance);
            if (ev) {
                sp.has_cross = 1;
                sp.cross_power = ev->cross_power;
                sp.cross_rx_sensor_id = ev->rx_sensor_id;
            }
        }
    }

    // ----- 4. Data directed dispatch -----
    auto batches = disp_->dispatch(sps);

    // ----- 4.5. Scatter points on device (Layer 3) -----
    // GPU RT 路径: 设备缓冲已是零拷贝导入的 Vulkan SSBO (检测结果已由
    // kernel 写入), 无需再上传; CPU RT 路径回落 H2D 上传。
    if (cuda_ && !cross_done_on_gpu && !sps.empty()) {
        cuda_->uploadScatterPoints(sps);
    }

    // ----- 5. Signal formation (Layer 3b) -----
    last_scatter_points_ = sps;

    if (signal_enabled_ && !frames_on_gpu) {
        // Group scatter points by transmitting sensor
        std::map<uint32_t, std::vector<ScatterPoint>> per_sensor;
        for (const auto& tx:schedule) per_sensor.try_emplace(tx.sensor_id);
        for (const auto& sp : sps)
            per_sensor[sp.tx_sensor_id].push_back(sp);

        // Process each sensor's scatter points through its signal processor
        for (auto& [sid, sensor_sps] : per_sensor) {
            if (gate_on && !gate_allows(sid))
                continue;   // 未链接装备: 不做信号成帧
            const auto* se = rules_ ? rules_->findSensor(sid) : nullptr;
            if (!se) continue;

            // Set sensor-specific signal config
            signal_cfg_.prf = se->tx_params.pulse_rate;
            signal_cfg_.carrier_hz = (se->tx_params.wavelength > 0.0f)
                ? 2.99792458e8 / se->tx_params.wavelength : 1e9;
            applySensorSignalParams(sid);   // 场景契约逐装备信号参数覆盖

            // Look up sensor aperture position
            signal_cfg_.sensor_position = {};
            for (const auto& a : scene_->apertures()) {
                if (a.sensor_id == sid) {
                    signal_cfg_.sensor_position = {
                        float(a.position.x),
                        float(a.position.y),
                        float(a.position.z)
                    };
                    break;
                }
            }

            // Platform velocity -> real Doppler (zero for fixed sensors)
            signal_cfg_.sensor_velocity = {};
            auto mit = sensor_mounts_.find(sid);
            if (physics_ && mit != sensor_mounts_.end()) {
                SensorPose pose;
                if (!sampleSensorPose(sid,sim_time_,pose)) return false;
                signal_cfg_.sensor_velocity = {float(pose.velocity.x),float(pose.velocity.y),float(pose.velocity.z)};
            }

            auto* proc = getOrCreateSignalProcessor(sid, se->type);
            if (!proc) continue;

            // ---- 脉冲级时序消费 ----
            // 时序字段: 帧起始时刻 + 该装备本帧脉冲列表 (schedule 已按 fire_time 排序)
            signal_cfg_.sim_time = double(sim_time_) - double(dt);
            signal_cfg_.frame_id = frame_count_;

            std::vector<const ScheduledTx*> pulses;
            for (const auto& tx : schedule)
                if (tx.sensor_id == sid) pulses.push_back(&tx);

            if (!pulses.empty() && se->type==SensorType::RADAR) {
                // 多脉冲装备 (按 PRF 发射的雷达): 散射点按 pulse_seq 分桶,
                // 沿 fire_time 顺序逐脉冲喂给真脉冲 CPI 路径,
                // 最后一拍产出 RD 图 (中间拍返回空帧)。
                signal_cfg_.pulses_in_frame = uint32_t(pulses.size());
                std::map<uint32_t, std::vector<ScatterPoint>> per_pulse;
                for (const auto& sp : sensor_sps)
                    per_pulse[sp.pulse_seq].push_back(sp);

                static const std::vector<ScatterPoint> kEmpty;
                for (const auto* tx : pulses) {
                    signal_cfg_.pulse_seq = tx->pulse_seq;
                    signal_cfg_.pulse_slow_time = tx->fire_time;
                    SensorPose pose;
                    if (!sampleSensorPose(sid,tx->absolute_time,pose)) return false;
                    signal_cfg_.sensor_position={float(pose.position.x),float(pose.position.y),float(pose.position.z)};
                    signal_cfg_.sensor_velocity={float(pose.velocity.x),float(pose.velocity.y),float(pose.velocity.z)};
                    auto pit = per_pulse.find(tx->pulse_seq);
                    const std::vector<ScatterPoint>* feed =
                        (pit != per_pulse.end()) ? &pit->second : nullptr;
                    // An empty pulse is a valid zero return; never replay an
                    // earlier target's scatter points to fill a CPI window.
                    auto frame = proc->process(feed ? *feed : kEmpty, signal_cfg_, sid);
                    if (!frame.empty()) {
                        completeFrame(std::move(frame));
                    }
                }
            } else if(se->type==SensorType::IR || se->type==SensorType::VIS) {
                std::map<uint32_t,std::vector<ScatterPoint>> exposures;
                for(const auto& point:sensor_sps) exposures[point.pulse_seq].push_back(point);
                for(const auto* event:pulses) {
                    SensorPose pose;
                    if(!sampleSensorPose(sid,event->absolute_time,pose)) return false;
                    signal_cfg_.sensor_position={float(pose.position.x),float(pose.position.y),float(pose.position.z)};
                    signal_cfg_.sensor_direction={float(pose.direction.x),float(pose.direction.y),float(pose.direction.z)};
                    signal_cfg_.sensor_up={float(pose.up.x),float(pose.up.y),float(pose.up.z)};
                    signal_cfg_.sim_time=event->absolute_time;
                    signal_cfg_.frame_id=event->pulse_index;
                    signal_cfg_.pulses_in_frame=0;
                    auto frame=proc->process(exposures[event->pulse_seq],signal_cfg_,sid);
                    if(!frame.empty()) completeFrame(std::move(frame));
                }
            } else {
                // 单脉冲/被动传感器: 保持整帧路径 (行为不变)
                signal_cfg_.pulses_in_frame = 0;
                signal_cfg_.pulse_seq = 0;
                signal_cfg_.pulse_slow_time = 0.0f;
                auto frame = proc->process(sensor_sps, signal_cfg_, sid);
                if (!frame.empty()) completeFrame(std::move(frame));
            }
        }
    }

    std::stable_sort(completed_frames_.begin(),completed_frames_.end(),[](const auto& a,const auto& b) {
        if(a.sim_time!=b.sim_time) return a.sim_time<b.sim_time;
        return a.sensor_id<b.sensor_id;
    });
    // ----- 6. Visualisation update: crosstalk paths + signal frames -----
    if (vis_) {
        std::vector<CrossInductionPath> paths;
        for (const auto& b : batches) {
            if (!b.is_crosstalk) continue;
            CrossInductionPath p{};
            p.tx_sensor_id = 0;
            p.rx_sensor_id = b.rx_sensor_id;
            for (const auto& a : scene_->apertures()) {
                if (a.sensor_id == b.rx_sensor_id) p.rx_pos = a.position;
            }
            p.tx_pos = vsg::dvec3(0.0, 1.5, 1000.0);
            p.power = b.points.empty() ? 0.0f : b.points.front().cross_power;
            paths.push_back(p);
        }
        vis_->setCrossInductionPaths(paths);
        vis_->updateAtmosphereOverlay(*atm_);

        // Display signal frames (radar RD maps, SAR images, VIS/IR camera images)
        auto renderFrameToVis = [&](const SignalFrame& frame) {
            auto sensorTypeToChannel = [](SensorType t) -> uint32_t {
                switch (t) {
                    case SensorType::VIS:   return CH_VIS;
                    case SensorType::LASER: return CH_VIS;
                    case SensorType::IR:    return CH_IR;
                    case SensorType::RADAR: return CH_RADAR;
                    case SensorType::SAR:   return CH_SAR;
                }
                return CH_VIS;
            };

            uint32_t ch = sensorTypeToChannel(frame.sensor_type);
            uint32_t W = frame.width;
            uint32_t H = frame.height;
            std::vector<float> rgba(W * H * 4, 0.0f);

            // Compute log-magnitude from complex samples
            float max_mag = 1e-12f;
            std::vector<float> mag(W * H);
            for (size_t i = 0; i < W * H; ++i) {
                mag[i] = std::abs(frame.samples[i]);
                if (mag[i] > max_mag) max_mag = mag[i];
            }

            float scale = (max_mag > 1e-12f) ? 1.0f / max_mag : 1.0f;
            for (size_t i = 0; i < W * H; ++i) {
                float v = mag[i] * scale;
                v = 20.0f * std::log10(v + 1e-12f);
                v = (v + 60.0f) / 60.0f;
                v = std::clamp(v, 0.0f, 1.0f);

                if (ch == CH_RADAR || ch == CH_SAR) {
                    float r = std::min(v * 2.0f, 1.0f);
                    float g = std::min(v * 2.0f - 0.5f, 1.0f);
                    float b = v * 0.5f + 0.2f;
                    rgba[i * 4 + 0] = std::clamp(r, 0.0f, 1.0f);
                    rgba[i * 4 + 1] = std::clamp(g, 0.0f, 1.0f);
                    rgba[i * 4 + 2] = std::clamp(b, 0.0f, 1.0f);
                } else {
                    rgba[i * 4 + 0] = v;
                    rgba[i * 4 + 1] = v;
                    rgba[i * 4 + 2] = v;
                }
                rgba[i * 4 + 3] = 1.0f;
            }

            vis_->updateSensorImage(ch, W, H, rgba);
        };

        for (auto& [sid, frame] : last_frames_) {
            if (frame.empty()) continue;
            const std::size_t npix = std::size_t(frame.width) * frame.height;
            if (frame.samples.size() >= npix) {
                // CPU 成帧路径: 样本在主机内存, 直接渲染。
                renderFrameToVis(frame);
            } else if (gpu_frames_.count(sid)) {
                // GPU 成帧路径: 样本驻留显存, 仅按需回传帧本体 (小图/小谱)。
                SignalFrame dl = frame;
                if (downloadFrameSamples(sid, dl.samples) &&
                    dl.samples.size() >= npix)
                    renderFrameToVis(dl);
            }
        }
    }

    // ----- 7. Time-step sync / logging -----
    const std::size_t log_interval = cfg_.per_frame_detail ? 1 : 60;
    if (frame_count_ % log_interval == 0) {
        // GPU 成帧路径不回传散射缓冲: 用设备侧统计计数代替。
        const std::size_t scatter_n = frames_on_gpu ? gpu_scatter_valid_ : sps.size();
        spdlog::info("Frame {} t={:.3f}s scatter={} batches={} crosstalk={}{}",
                     frame_count_, sim_time_, scatter_n, batches.size(),
                     frames_on_gpu ? gpu_scatter_cross_
                                   : disp_->lastCrosstalkCount(),
                     frames_on_gpu ? " (gpu-resident)" : "");

        if (cfg_.per_frame_detail) {
            for (std::size_t i = 0; i < schedule.size(); ++i) {
                const auto& tx = schedule[i];
                const auto* sp  = i < sps.size() ? &sps[i] : nullptr;
                std::ostringstream oss;
                // For wavelengths use scientific notation when very small,
                // fixed notation otherwise.  This avoids "0.0000m" for the
                // laser's 1.064e-6 m wavelength.
                auto fmtNum = [](float v) {
                    std::ostringstream s;
                    if (v != 0.0f && std::fabs(v) < 1.0e-3f) {
                        s << std::setprecision(3) << std::scientific << v;
                    } else {
                        s << std::setprecision(4) << std::fixed << v;
                    }
                    return s.str();
                };
                oss << std::fixed << std::setprecision(4);
                oss << "  TX sid=" << tx.sensor_id
                    << " type=" << sensorTypeName(tx.type)
                    << " P=" << tx.tx_power << "W"
                    << " beam=" << tx.beam_width << "rad"
                    << " lambda=" << fmtNum(tx.wavelength) << "m"
                    << " fire=" << tx.fire_time << "s";
                if (sp) {
                    oss << " | atm_atten=" << sp->atm_atten
                        << " atm_scatter=" << sp->atm_scatter << "W"
                        << " cross=";
                    if (sp->has_cross) {
                        oss << "yes(rx=" << sp->cross_rx_sensor_id
                            << " P=" << sp->cross_power << "W)";
                    } else {
                        oss << "no";
                    }
                }
                spdlog::info(oss.str());
            }
        }
    }
    return true;
} catch (const std::exception& e) {
    spdlog::error("Simulation step failed at {:.9f}s: {}",sim_time_,e.what()); return false;
}

// ---------------------------------------------------------------------------
//                          analytical helpers
// ---------------------------------------------------------------------------

namespace {

const char* bandChannelName(BandChannel ch) {
    switch (ch) {
        case CH_VIS:   return "VIS";
        case CH_IR:    return "IR";
        case CH_RADAR: return "RADAR";
        case CH_SAR:   return "SAR";
        default:       return "?";
    }
}

/// Map a sensor type to the band mask it should produce / receive.
BandMask sensorTypeToBand(SensorType t) {
    switch (t) {
        case SensorType::VIS:   return BAND_VIS;
        case SensorType::LASER:  return BAND_VIS;   // laser is in the VIS/NIR window
        case SensorType::IR:     return BAND_IR;
        case SensorType::RADAR:  return BAND_RADAR;
        case SensorType::SAR:    return BAND_SAR;
    }
    return BAND_VIS;
}

/// Expected centre wavelength (m) for each sensor type.
float expectedWavelength(SensorType t) {
    switch (t) {
        case SensorType::VIS:   return 5.5e-7f;   // 550 nm
        case SensorType::LASER:  return 1.064e-6f; // 1064 nm Nd:YAG
        case SensorType::IR:     return 10.0e-6f;  // 10 um LWIR
        case SensorType::RADAR:  return 3.0e-2f;   // 3 cm X-band
        case SensorType::SAR:    return 5.0e-2f;   // 5 cm C-band
    }
    return 0.0f;
}

/// True if the sensor type is intrinsically active (must transmit).
bool isActiveSensor(SensorType t) {
    return t == SensorType::LASER || t == SensorType::RADAR || t == SensorType::SAR;
}

} // anonymous namespace

// ===========================================================================
// Signal processor factory (Layer 3b)
// ===========================================================================
SensorSignalProcessor* Simulation::getOrCreateSignalProcessor(uint32_t sensor_id,
                                                               SensorType type) {
    auto it = signal_processors_.find(sensor_id);
    if (it != signal_processors_.end())
        return it->second.get();

    auto proc = SensorSignalProcessor::create(type);
    if (!proc) {
        spdlog::warn("No signal processor for sensor {} type {}", sensor_id,
                     sensorTypeName(type));
        return nullptr;
    }
    auto* ptr = proc.get();
    signal_processors_[sensor_id] = std::move(proc);
    spdlog::info("Created signal processor for sensor {} ({})",
                 sensor_id, sensorTypeName(type));
    return ptr;
}
void Simulation::completeFrame(SignalFrame frame) {
    frame.surface_revision=earth_surface_ ? earth_surface_->revision : 0;
    std::size_t bytes=frame.samples.size()*sizeof(std::complex<float>);
    for(const auto& pending:completed_frames_) bytes+=pending.samples.size()*sizeof(std::complex<float>);
    if(bytes>512ull*1024*1024) throw std::runtime_error("completed measurement queue exceeds 512 MiB; reduce the host step");
    fresh_frame_sids_.insert(frame.sensor_id);
    last_frames_[frame.sensor_id]=frame;
    completed_frames_.push_back(std::move(frame));
}

void Simulation::listSensors() {
    if (!initialised_) {
        spdlog::error("listSensors: simulation not initialised");
        return;
    }

    std::cout << "\n=== Sensor list (" << rules_->sensors().size() << " sensors) ===\n";
    std::cout << "+------+--------+---------+-----------+-----------+-----------+-----------+-------------+\n";
    std::cout << "|  id  | type   | band    | power(W)  | beam(rad) | lambda(m) | pulse(Hz) | duty        |\n";
    std::cout << "+------+--------+---------+-----------+-----------+-----------+-----------+-------------+\n";
    for (const auto& s : rules_->sensors()) {
        std::printf("| %4u | %-6s | %-7s | %9.3f | %9.4f | %9.3g | %9.1f | %11.3f |\n",
                    s.sensor_id,
                    sensorTypeName(s.type),
                    s.band_label.c_str(),
                    s.tx_params.power,
                    s.tx_params.beam_width,
                    s.tx_params.wavelength,
                    s.tx_params.pulse_rate,
                    s.tx_params.duty_cycle);
    }
    std::cout << "+------+--------+---------+-----------+-----------+-----------+-----------+-------------+\n";

    std::cout << "\n=== Cross-induction routes (" << rules_->routes().size() << " routes) ===\n";
    std::cout << "+------+------+----------+----------+-----------+-----------+-----------+----------+\n";
    std::cout << "|  tx  |  rx  | tx_type  | rx_type  | priority  | coupling  | band_match| process  |\n";
    std::cout << "+------+------+----------+----------+-----------+-----------+-----------+----------+\n";
    for (const auto& r : rules_->routes()) {
        std::printf("| %4u | %4u | %-8s | %-8s | %9d | %9.4f | %9.4f | %-8s |\n",
                    r.tx_sensor_id, r.rx_sensor_id,
                    sensorTypeName(r.tx_type),
                    sensorTypeName(r.rx_type),
                    r.priority,
                    r.coupling_factor,
                    r.band_match,
                    r.is_process ? "yes" : "no");
    }
    std::cout << "+------+------+----------+----------+-----------+-----------+-----------+----------+\n";

    std::cout << "\n=== Receiver apertures (" << scene_->apertures().size() << ") ===\n";
    std::cout << "+------+---------------------+---------------------+-----------+-----------+\n";
    std::cout << "|  id  | position (xyz)      | direction (xyz)    | half_ang  | area(m^2) |\n";
    std::cout << "+------+---------------------+---------------------+-----------+-----------+\n";
    for (const auto& a : scene_->apertures()) {
        std::printf("| %4u | %8.2f %8.2f %8.2f | %6.3f %6.3f %6.3f | %9.4f | %9.4f |\n",
                    a.sensor_id,
                    a.position.x, a.position.y, a.position.z,
                    a.direction.x, a.direction.y, a.direction.z,
                    a.half_angle, a.aperture_area);
    }
    std::cout << "+------+---------------------+---------------------+-----------+-----------+\n";

    std::cout << "\n=== Atmosphere layers (" << atm_->layerCount() << " slabs) ===\n";
    std::cout << "+----+-------------+-------------+-------------+-------------+-------------+-------------+\n";
    std::cout << "| id |  h_min (m)  |  h_max (m)  | ext_vis(1/m)| sca_vis(1/m)| aerosol    | turb        |\n";
    std::cout << "+----+-------------+-------------+-------------+-------------+-------------+-------------+\n";
    {
        std::size_t idx = 0;
        for (const auto& L : atm_->layers()) {
            std::printf("| %2zu | %11.1f | %11.1f | %11.4g | %11.4g | %11.4g | %11.4g |\n",
                        idx, L.height_min, L.height_max,
                        L.ext_coeff[CH_VIS], L.scatter_coeff[CH_VIS],
                        L.aerosol, L.turbulence);
            ++idx;
        }
    }
    std::cout << "+----+-------------+-------------+-------------+-------------+-------------+-------------+\n";
    std::cout.flush();
}

std::size_t Simulation::validateSensors() {
    if (!initialised_) {
        spdlog::error("validateSensors: simulation not initialised");
        return 0;
    }
    std::size_t issues = 0;
    auto warn = [&](const std::string& msg) {
        spdlog::warn("[validate] {}", msg);
        ++issues;
    };

    spdlog::info("[validate] checking {} sensors...", rules_->sensors().size());

    for (const auto& s : rules_->sensors()) {
        const char* tn = sensorTypeName(s.type);

        // 1. power check
        if (isActiveSensor(s.type) && s.tx_params.power <= 0.0f) {
            std::ostringstream m;
            m << "sensor " << s.sensor_id << " (" << tn
              << ") is active type but has power=" << s.tx_params.power << "W (must be >0)";
            warn(m.str());
        }
        if (!isActiveSensor(s.type) && s.tx_params.power < 0.0f) {
            std::ostringstream m;
            m << "sensor " << s.sensor_id << " (" << tn
              << ") has negative power=" << s.tx_params.power << "W";
            warn(m.str());
        }

        // 2. wavelength check
        const float exp_lambda = expectedWavelength(s.type);
        if (exp_lambda > 0.0f && s.tx_params.wavelength > 0.0f) {
            const float ratio = s.tx_params.wavelength / exp_lambda;
            // ratio outside [0.5, 2.0] is suspicious
            if (ratio < 0.5f || ratio > 2.0f) {
                std::ostringstream m;
                m << "sensor " << s.sensor_id << " (" << tn
                  << ") wavelength " << s.tx_params.wavelength
                  << "m deviates from expected " << exp_lambda
                  << "m (ratio " << ratio << ")";
                warn(m.str());
            }
        }

        // 3. beam_width / duty_cycle ranges
        if (s.tx_params.beam_width < 0.0f || s.tx_params.beam_width > 1.0f) {
            warn("sensor " + std::to_string(s.sensor_id) + " beam_width out of (0,1) rad");
        }
        if (s.tx_params.duty_cycle < 0.0f || s.tx_params.duty_cycle > 1.0f) {
            warn("sensor " + std::to_string(s.sensor_id) + " duty_cycle out of [0,1]");
        }

        // 4. pulse_rate vs duty_cycle: power*duty should be plausible avg
        if (s.tx_params.pulse_rate > 0.0f && s.tx_params.duty_cycle == 0.0f) {
            warn("sensor " + std::to_string(s.sensor_id) +
                 " has pulse_rate>0 but duty_cycle=0 (no energy emitted)");
        }

        // 5. receiver list for active sensors should be non-empty
        if (isActiveSensor(s.type) && s.rx_cross_sensors.empty()) {
            warn("sensor " + std::to_string(s.sensor_id) +
                 " (" + tn + ") is active but defines no rx_cross_sensors");
        }
    }

    spdlog::info("[validate] checking {} routes...", rules_->routes().size());
    for (const auto& r : rules_->routes()) {
        if (r.coupling_factor < 0.0f || r.coupling_factor > 1.0f) {
            warn("route tx=" + std::to_string(r.tx_sensor_id) +
                 " rx=" + std::to_string(r.rx_sensor_id) +
                 " coupling_factor out of [0,1]: " + std::to_string(r.coupling_factor));
        }
        if (r.band_match < 0.0f || r.band_match > 1.0f) {
            warn("route tx=" + std::to_string(r.tx_sensor_id) +
                 " rx=" + std::to_string(r.rx_sensor_id) +
                 " band_match out of [0,1]: " + std::to_string(r.band_match));
        }

        // band_match plausibility: compare expected centre wavelengths
        const float lam_tx = expectedWavelength(r.tx_type);
        const float lam_rx = expectedWavelength(r.rx_type);
        if (lam_tx > 0.0f && lam_rx > 0.0f) {
            const float bigger = std::max(lam_tx, lam_rx);
            const float smaller = std::min(lam_tx, lam_rx);
            const float ratio = bigger / smaller;
            // wavelength ratio > 5x but band_match > 0.3 is implausible
            if (ratio > 5.0f && r.band_match > 0.3f) {
                std::ostringstream m;
                m << "route tx=" << r.tx_sensor_id
                  << " (" << sensorTypeName(r.tx_type) << " lam=" << lam_tx << "m)"
                  << " -> rx=" << r.rx_sensor_id
                  << " (" << sensorTypeName(r.rx_type) << " lam=" << lam_rx << "m)"
                  << " has band_match=" << r.band_match
                  << " but wavelengths differ by " << ratio << "x";
                warn(m.str());
            }
        }

        if (r.priority < 0) {
            warn("route tx=" + std::to_string(r.tx_sensor_id) +
                 " has negative priority");
        }
    }

    // Aperture sanity
    spdlog::info("[validate] checking {} apertures...", scene_->apertures().size());
    for (const auto& a : scene_->apertures()) {
        const double dnorm = vsg::length(a.direction);
        if (std::abs(dnorm - 1.0) > 1e-3) {
            warn("aperture sensor_id=" + std::to_string(a.sensor_id) +
                 " direction not unit (|" + std::to_string(dnorm) + "|)");
        }
        if (a.aperture_area <= 0.0f) {
            warn("aperture sensor_id=" + std::to_string(a.sensor_id) +
                 " non-positive area " + std::to_string(a.aperture_area));
        }
        if (a.half_angle <= 0.0f || a.half_angle > 1.5707963f) {
            warn("aperture sensor_id=" + std::to_string(a.sensor_id) +
                 " half_angle out of (0, pi/2): " + std::to_string(a.half_angle));
        }
    }

    // Atmosphere sanity
    if (atm_->layerCount() == 0) {
        warn("atmosphere has 0 layers");
    } else {
        std::size_t idx = 0;
        for (const auto& L : atm_->layers()) {
            const std::string id = std::to_string(idx);
            const float thickness = L.height_max - L.height_min;
            if (thickness <= 0.0f) {
                warn("atm layer " + id + " non-positive thickness (" +
                     std::to_string(thickness) + "m)");
            }
            for (uint32_t b = 0; b < BAND_CHANNEL_COUNT; ++b) {
                const float ext = L.ext_coeff[b];
                const float sca = L.scatter_coeff[b];
                const float abs = L.absorb_coeff[b];
                if (ext < 0.0f) {
                    warn("atm layer " + id + " band " + std::to_string(b) +
                         " negative extinction " + std::to_string(ext));
                }
                if (sca < 0.0f || sca > ext) {
                    warn("atm layer " + id + " band " + std::to_string(b) +
                         " scatter_coef > extinction_coef (unphysical)");
                }
                if (abs < 0.0f || abs > ext) {
                    warn("atm layer " + id + " band " + std::to_string(b) +
                         " absorb_coef > extinction_coef (unphysical)");
                }
            }
            if (L.refract_index < 1.0f) {
                warn("atm layer " + id + " refract_index < 1.0");
            }
            ++idx;
        }
    }

    // Geometry sanity
    if (phys_->geoUnitArray().empty()) {
        warn("no geo-units loaded");
    }

    spdlog::info("[validate] DONE - {} issue(s) found", issues);
    return issues;
}

bool Simulation::traceSensor(uint32_t sensor_id, std::size_t n_frames) {
    if (!initialised_) {
        spdlog::error("traceSensor: simulation not initialised");
        return false;
    }
    const SensorCrossEntry* se = rules_->findSensor(sensor_id);
    if (!se) {
        spdlog::error("traceSensor: sensor_id {} not found in config", sensor_id);
        return false;
    }

    spdlog::info("=== traceSensor: id={} type={} band={} ===",
                sensor_id, sensorTypeName(se->type), se->band_label);
    spdlog::info("  tx_params: P={}W  beam={}rad  lambda={}m  pulse={}Hz  duty={}",
                se->tx_params.power, se->tx_params.beam_width,
                se->tx_params.wavelength, se->tx_params.pulse_rate,
                se->tx_params.duty_cycle);

    const auto routes = rules_->routesFrom(sensor_id);
    spdlog::info("  cross-routes from this sensor: {}", routes.size());
    for (const auto* r : routes) {
        spdlog::info("    -> rx={} ({}) priority={} coupling={} band_match={} process={}",
                    r->rx_sensor_id, sensorTypeName(r->rx_type),
                    r->priority, r->coupling_factor, r->band_match,
                    r->is_process ? "yes" : "no");
    }

    // Drive the per-frame loop n_frames times, but force the schedule filter.
    struct RestoreFilter {
        SimulationConfig& config;
        std::optional<uint32_t> sensor;
        bool detail;
        ~RestoreFilter() { config.only_sensor_id=sensor; config.per_frame_detail=detail; }
    } restore{cfg_,cfg_.only_sensor_id,cfg_.per_frame_detail};
    cfg_.only_sensor_id = sensor_id;
    cfg_.per_frame_detail = true;
    const auto start_frame=frame_count_;
    simcore::SimulationClock clock(simcore::PeriodicTimeline::fromHz(1/cfg_.frame_dt));
    clock.reset(simcore::fromSeconds(sim_time_));

    BandChannel ch = CH_VIS;
    const BandMask bm = sensorTypeToBand(se->type);
    if (bm == BAND_IR)         ch = CH_IR;
    else if (bm == BAND_RADAR) ch = CH_RADAR;
    else if (bm == BAND_SAR)   ch = CH_SAR;

    spdlog::info("  band channel: {} (mask 0x{:x})", bandChannelName(ch), bm);
    spdlog::info("  running {} frames @ dt={:.4f}s ...", n_frames, cfg_.frame_dt);

    bool completed=true;
    for (std::size_t i = 0; i < n_frames; ++i) {
        const auto end=clock.advance();
        if (!step(simcore::toSeconds(end)-sim_time_,simcore::toSeconds(end))) {
            spdlog::warn("  viewer closed at frame {}", frame_count_);
            completed=false;
            break;
        }
    }

    // Final summary
    spdlog::info("=== traceSensor summary: {} frames executed, sim_time={:.3f}s ===",
                frame_count_-start_frame, sim_time_);

    // Run an explicit atmospheric integration ray so we can report the
    // exact attenuation along the demo 1 km path for this sensor's band.
    Vec3 origin{0.0f, 50.0f, 1000.0f};
    Vec3 dir{0.0f, 0.0f, -1.0f};
    auto r = atm_int_->integrate(origin, dir, 1000.0f,
                                  se->tx_params.power, ch);
    spdlog::info("  1km atm integration @ band {}:", bandChannelName(ch));
    spdlog::info("    total_attenuation = {:.6f}  ({:.2f}%)", r.total_attenuation,
                r.total_attenuation * 100.0f);
    spdlog::info("    scatter_power     = {:.6e} W", r.scatter_power);
    spdlog::info("    refraction_offset = {:.6f} rad", r.refraction_offset);
    spdlog::info("    last_layer_id     = {}", r.last_layer_id);

    // Expected crosstalk power per route (analytical check)
    spdlog::info("  expected crosstalk per route (P_tx * L_atm * eta * S * band_match):");
    for (const auto* r2 : routes) {
        const SensorCrossEntry* rx = rules_->findSensor(r2->rx_sensor_id);
        const float aperture_area = rx ? 0.05f : 0.05f; // demo aperture
        const float P_cross = se->tx_params.power
                             * r.total_attenuation
                             * r2->coupling_factor
                             * aperture_area
                             * r2->band_match;
        spdlog::info("    -> rx {}: P_cross = {:.6e} W  (route priority={}, process={})",
                    r2->rx_sensor_id, P_cross, r2->priority,
                    r2->is_process ? "yes" : "no");
    }

    return completed;
}

// ====================================================================
// Rigid-body physics helpers (sensor platform mounting)
// ====================================================================

const SignalFrame* Simulation::lastFrame(uint32_t sensor_id) const {
    const auto it = last_frames_.find(sensor_id);
    return (it != last_frames_.end() && !it->second.empty()) ? &it->second : nullptr;
}

void Simulation::mountSensorOnObject(uint32_t sensor_id,                                     const std::string& object_id,
                                     const vsg::dvec3& local_offset,
                                     const vsg::dvec3& local_direction) {
    if (!physics_ || !physics_->bodyExists(object_id)) {
        spdlog::warn("Physics: cannot mount sensor {} on unknown body '{}'",
                     sensor_id, object_id);
        return;
    }
    sensor_mounts_[sensor_id] = SensorMount{object_id, local_offset, local_direction};

    // Snap the aperture to the platform pose immediately.
    auto& apertures = scene_->apertures();
    for (auto& a : apertures) {
        if (a.sensor_id == sensor_id) {
            const auto m = vsg::dmat4(physics_->transform(object_id));
            const vsg::dvec4 off = m * vsg::dvec4(local_offset, 1.0);
            a.position = vsg::dvec3(off.x, off.y, off.z);
            const vsg::dvec4 dir = m * vsg::dvec4(local_direction, 0.0);
            const vsg::dvec3 d(dir.x, dir.y, dir.z);
            const double len = std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
            if (len > 1e-12) a.direction = d / len;
            spdlog::info("Physics: sensor {} mounted on '{}' offset=({:.2f},{:.2f},{:.2f})",
                         sensor_id, object_id,
                         local_offset.x, local_offset.y, local_offset.z);
        }
    }
}

void Simulation::applySensorSignalParams(uint32_t sid) {
    auto pit = sensor_signal_params_.find(sid);
    // 先恢复基线再叠加覆盖, 避免上一装备的覆盖值泄漏给本装备。
    signal_cfg_.range_gates      = signal_cfg_base_.range_gates;
    signal_cfg_.doppler_bins     = signal_cfg_base_.doppler_bins;
    signal_cfg_.chirp_bandwidth  = signal_cfg_base_.chirp_bandwidth;
    signal_cfg_.chirp_duration   = signal_cfg_base_.chirp_duration;
    signal_cfg_.fs               = signal_cfg_base_.fs;
    signal_cfg_.image_width      = signal_cfg_base_.image_width;
    signal_cfg_.image_height     = signal_cfg_base_.image_height;
    signal_cfg_.integration_time = signal_cfg_base_.integration_time;
    signal_cfg_.netd             = signal_cfg_base_.netd;
    if (pit == sensor_signal_params_.end()) return;
    const SensorSignalParams& p = pit->second;
    if (p.range_gates)      signal_cfg_.range_gates      = *p.range_gates;
    if (p.doppler_bins)     signal_cfg_.doppler_bins     = *p.doppler_bins;
    if (p.chirp_bandwidth)  signal_cfg_.chirp_bandwidth  = *p.chirp_bandwidth;
    if (p.chirp_duration)   signal_cfg_.chirp_duration   = *p.chirp_duration;
    if (p.fs)               signal_cfg_.fs               = *p.fs;
    if (p.image_width)      signal_cfg_.image_width      = *p.image_width;
    if (p.image_height)     signal_cfg_.image_height     = *p.image_height;
    if (p.integration_time) signal_cfg_.integration_time = *p.integration_time;
    if (p.netd)             signal_cfg_.netd             = *p.netd;
}

bool Simulation::formFramesOnGpu(const std::vector<ScheduledTx>& schedule,
                                 double dt, bool gate_on,
                                 const std::function<bool(uint32_t)>& gate_allows) {
    if (!cuda_ || !cuda_->hasExternalScatterBuffer()) return false;
    const float noise_floor = 1e-12f;

    // 聚合每个传感器本步脉冲数 (多脉冲雷达一次成帧, 脉冲数记入元数据)
    std::map<uint32_t, uint32_t> pulse_count;
    for (const auto& tx : schedule)
        pulse_count[tx.sensor_id]++;

    bool any = false;
    for (const auto& [sid, pulses] : pulse_count) {
        if (gate_on && !gate_allows(sid))
            continue;   // 未链接装备: 不做信号成帧
        const auto* se = rules_ ? rules_->findSensor(sid) : nullptr;
        if (!se) continue;

        // 逐装备信号参数叠加 (与 CPU 路径同一来源)
        signal_cfg_.prf = se->tx_params.pulse_rate;
        signal_cfg_.carrier_hz = (se->tx_params.wavelength > 0.0f)
            ? 2.99792458e8 / se->tx_params.wavelength : 1e9;
        applySensorSignalParams(sid);

        FrameFormGPU fcfg{};
        fcfg.sensor_id = sid;
        fcfg.noise_floor = noise_floor;
        fcfg.carrier_hz = float(signal_cfg_.carrier_hz);
        fcfg.prf = signal_cfg_.prf;
        fcfg.fs = signal_cfg_.fs;
        fcfg.coherent_pulses = pulses;
        fcfg.seed = uint32_t(frame_count_ * 131u + sid);

        // VIS/IR 光学参数 (RADAR/SAR kernel 忽略)
        fcfg.focal_length     = signal_cfg_.focal_length;
        fcfg.pixel_pitch      = signal_cfg_.pixel_pitch;
        fcfg.f_number         = signal_cfg_.f_number;
        fcfg.integration_time = signal_cfg_.integration_time;
        fcfg.solar_irradiance = signal_cfg_.solar_irradiance;
        fcfg.netd             = signal_cfg_.netd;
        fcfg.responsivity     = signal_cfg_.responsivity;

        switch (se->type) {
            case SensorType::VIS:
            case SensorType::LASER: fcfg.mode = 0; break;
            case SensorType::IR:    fcfg.mode = 1; break;
            case SensorType::RADAR: fcfg.mode = 2; break;
            case SensorType::SAR:   fcfg.mode = 3; break;
        }
        if (fcfg.mode <= 1) {
            fcfg.width  = signal_cfg_.image_width  ? signal_cfg_.image_width  : 640;
            fcfg.height = signal_cfg_.image_height ? signal_cfg_.image_height : 480;
        } else {
            fcfg.width  = signal_cfg_.range_gates;
            fcfg.height = signal_cfg_.doppler_bins;
        }

        // 孔径位置 + 平台速度 (多普勒)
        for (int k = 0; k < 3; ++k) { fcfg.sensor_pos[k] = 0.0f; fcfg.sensor_vel[k] = 0.0f; }
        for (const auto& a : scene_->apertures()) {
            if (a.sensor_id != sid) continue;
            fcfg.sensor_pos[0] = float(a.position.x);
            fcfg.sensor_pos[1] = float(a.position.y);
            fcfg.sensor_pos[2] = float(a.position.z);
            break;
        }
        auto mit = sensor_mounts_.find(sid);
        if (physics_ && mit != sensor_mounts_.end()) {
            SensorPose pose;
            if (!sampleSensorPose(sid,sim_time_,pose)) continue;
            fcfg.sensor_vel[0] = float(pose.velocity.x);
            fcfg.sensor_vel[1] = float(pose.velocity.y);
            fcfg.sensor_vel[2] = float(pose.velocity.z);
        }

        std::size_t bytes = 0;
        void* dev = cuda_->formFrame(fcfg, &bytes);
        if (!dev || bytes == 0) continue;   // 该传感器成帧失败: 不阻断其余
        any = true;

        // GPU 驻留帧描述 (分发侧据此 D2D 拷贝, 不落主机)
        GpuFormedFrame gf{};
        gf.type = se->type;
        gf.width = fcfg.width;
        gf.height = fcfg.height;
        gf.dev_ptr = dev;
        gf.bytes = bytes;
        gf.complex = (fcfg.mode >= 2);
        gf.sim_time = double(sim_time_) - double(dt);
        gf.frame_id = frame_count_;
        gf.coherent_pulses = pulses;
        gf.prf = signal_cfg_.prf;
        gf.carrier_hz = float(signal_cfg_.carrier_hz);
        if (gf.complex) {
            gf.range_min = 0.0f;
            gf.range_max = float(double(fcfg.width) * 2.99792458e8 /
                                 (2.0 * double(signal_cfg_.fs)));
        }
        gpu_frames_[sid] = gf;

        // 主机侧仅留元数据帧 (供装备注册/监控/可视化尺寸), 样本驻留显存。
        // last_frames_[sid] 每步整体替换, 上一步的旧元数据不会残留。
        SignalFrame meta{};
        meta.width = fcfg.width;
        meta.height = fcfg.height;
        meta.fs = signal_cfg_.fs;
        meta.prf = signal_cfg_.prf;
        meta.carrier_hz = signal_cfg_.carrier_hz;
        meta.sensor_type = se->type;
        meta.sensor_id = sid;
        meta.sim_time = gf.sim_time;
        meta.frame_id = gf.frame_id;
        meta.coherent_pulses = pulses;
        meta.range_min = gf.range_min;
        meta.range_max = gf.range_max;
        if (gf.complex && fcfg.height > 0) {
            meta.range_axis.resize(fcfg.width);
            const float dr = (gf.range_max - gf.range_min) / float(fcfg.width);
            for (uint32_t i = 0; i < fcfg.width; ++i)
                meta.range_axis[i] = gf.range_min + (float(i) + 0.5f) * dr;
            meta.doppler_axis.resize(fcfg.height);
            const float dv = signal_cfg_.prf / float(fcfg.height);
            for (uint32_t j = 0; j < fcfg.height; ++j) {
                float v = float(j) * dv;
                if (v > signal_cfg_.prf * 0.5f) v -= signal_cfg_.prf;  // fftshift
                meta.doppler_axis[j] = v;
            }
        }
        meta.surface_revision=earth_surface_ ? earth_surface_->revision : 0;
        last_frames_[sid] = std::move(meta);
        fresh_frame_sids_.insert(sid);
    }

    if (any)
        spdlog::debug("Layer3b: formed {} frame(s) on GPU (scatter stays on device)",
                      gpu_frames_.size());
    return any;
}

bool Simulation::downloadFrameSamples(uint32_t sensor_id,
                                      std::vector<std::complex<float>>& out) const {
    if (!cuda_) return false;
    auto git = gpu_frames_.find(sensor_id);
    if (git == gpu_frames_.end()) return false;

    std::vector<float> raw;
    if (!cuda_->downloadFrame(sensor_id, raw)) return false;

    const std::size_t npix = std::size_t(git->second.width) * git->second.height;
    if (git->second.complex) {
        // RADAR/SAR: 交织 I/Q 打包为复数样本
        if (raw.size() < npix * 2) return false;
        out.resize(npix);
        for (std::size_t i = 0; i < npix; ++i)
            out[i] = std::complex<float>(raw[2 * i], raw[2 * i + 1]);
    } else {
        // VIS/IR: 实数亮度 -> 复数样本 (虚部为 0, 与 CPU 帧布局一致)
        if (raw.size() < npix) return false;
        out.resize(npix);
        for (std::size_t i = 0; i < npix; ++i)
            out[i] = std::complex<float>(raw[i], 0.0f);
    }
    return true;
}

void Simulation::syncMountedSensors() {
    if (sensor_mounts_.empty()) return;
    auto& apertures = scene_->apertures();
    for (auto& [sid, m] : sensor_mounts_) {
        if (!physics_->bodyExists(m.object_id)) continue;
        const vsg::dmat4 body(physics_->transform(m.object_id));
        for (auto& a : apertures) {
            if (a.sensor_id != sid) continue;
            const vsg::dvec4 off = body * vsg::dvec4(m.local_offset, 1.0);
            a.position = vsg::dvec3(off.x, off.y, off.z);
            const vsg::dvec4 dir = body * vsg::dvec4(m.local_direction, 0.0);
            const vsg::dvec3 d(dir.x, dir.y, dir.z);
            const double len = std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
            if (len > 1e-12) a.direction = d / len;
        }
    }
}

bool Simulation::sampleSensorPose(uint32_t id,double time,SensorPose& out) const {
    if (!scene_ || !std::isfinite(time) || time<0 || time>sim_time_+1e-9) return false;
    SensorPose pose; pose.time=time;
    const auto mount=sensor_mounts_.find(id);
    if (mount!=sensor_mounts_.end()) {
        simcore::RigidState state;
        if (!physics_ || !physics_->sampleBody(mount->second.object_id,time,state)) return false;
        pose.position=state.point(mount->second.local_offset);
        pose.direction=state.orientation*mount->second.local_direction;
        const auto localDirection=vsg::normalize(mount->second.local_direction);
        const vsg::dvec3 reference=std::abs(localDirection.y)<.99 ? vsg::dvec3(0,1,0) : vsg::dvec3(0,0,1);
        const auto localRight=vsg::normalize(vsg::cross(localDirection,reference));
        pose.up=state.orientation*vsg::cross(localRight,localDirection);
        pose.velocity=state.pointVelocity(mount->second.local_offset);
        pose.angular_velocity=state.angular_velocity;
    } else {
        const auto& apertures=scene_->apertures();
        auto it=std::find_if(apertures.begin(),apertures.end(),[&](const auto& a){return a.sensor_id==id;});
        if (it==apertures.end()) return false;
        pose.position=it->position; pose.direction=it->direction;
    }
    const double length=vsg::length(pose.direction);
    if (!simcore::finite(pose.position) || !std::isfinite(length) || length<1e-12) return false;
    pose.direction=pose.direction/length;
    if(vsg::length(vsg::cross(pose.direction,pose.up))<1e-9) pose.up={0,0,1};
    if(earth_surface_ && earth_surface_->strict && !earth_surface_->containsLocal(pose.position.x,pose.position.z))
        throw std::runtime_error("sensor aperture outside pinned Earth coverage: "+std::to_string(id));
    out=pose;
    return true;
}

void Simulation::validateEarthMeasurement(const SensorPose& pose,double fov_x,double fov_y,double range)const {
    if(!earth_surface_ || !earth_surface_->strict)return;
    groundFootprint(*earth_surface_,pose,fov_x,fov_y,range,[&](const auto& point) {
        if(!earth_surface_->containsLocal(point.x,point.z))
            throw std::runtime_error("measurement footprint exceeds pinned Earth coverage");
    });
}

} // namespace vsgraysim
