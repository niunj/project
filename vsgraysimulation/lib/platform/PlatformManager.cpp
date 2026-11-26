/// @file PlatformManager.cpp
/// @brief 平台管理器实现。

#include "PlatformManager.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>
#include <simcore/Orientation.h>

// 包含各运动模型头文件
#include "GroundVehicleMotion.h"
#include "AircraftMotion.h"
#include "NavalVesselMotion.h"
#include "MissileMotion.h"

namespace vsgraysim::platform {

// =============================================================================
PlatformManager::~PlatformManager() {
    shutdown();
}

// =============================================================================
bool PlatformManager::initialize(const nlohmann::json& config) {
    std::lock_guard<std::mutex> lock(mtx_);

    if (config.contains("environment")) {
        auto& env = config["environment"];
        if (env.contains("sea_level")) env_context_.sea_level = env["sea_level"];
        if (env.contains("wind_speed")) env_context_.wind_speed = env["wind_speed"];
        if (env.contains("air_density")) env_context_.air_density = env["air_density"];
        const double az=env.value("wind_az",80.0)*3.14159265358979323846/180;
        env_context_.wind_direction={std::sin(az),0,-std::cos(az)};
    }

    initialized_ = true;
    return true;
}

// =============================================================================
void PlatformManager::shutdown() {
    std::lock_guard<std::mutex> lock(mtx_);
    physics_bridge_.reset();
    physics_ = nullptr;
    platforms_.clear();
    motion_models_.clear();
    equipment_links_.clear();
    pending_commands_.clear();
    flight_controls_.clear();
    prev_velocities_.clear();
    route_plans_.clear(); route_segments_.clear(); engagements_.clear();
    initialized_ = false;
    running_ = false;
}

// =============================================================================
void PlatformManager::onPreTick(const TimeTick& tick) {
    if (!initialized_ || !running_) return;
    double dt = tick.dt;
    if (dt <= 0.0) dt = 1.0 / 60.0;

    updateFlightControlLeases(tick.sim_time);

    // 1. 航迹调度: 根据仿真时间生成为有航迹的平台生成运动控制指令
    if (!manual_override_) scheduleRoutes(tick.sim_time, dt);

    // 1.5 交战闭环: 为在交战中的红方拦截弹喂给目标实时位置 (制导信息)
    injectEngagementGuidance();

    // 2. 更新所有平台运动模型 (计算期望速度)
    updateAllPlatformsMotion(dt, env_context_, tick.sim_time);
}

// =============================================================================
void PlatformManager::onTick(const TimeTick& tick) {
    // 物理引擎在 EquipmentDataSource::onTick 中步进 (sim_->step())
    // 这里不做额外处理
}

// =============================================================================
void PlatformManager::onPostTick(const TimeTick& tick) {
    if (!initialized_ || !running_) return;
    double dt = tick.dt;
    if (dt <= 0.0) dt = 1.0 / 60.0;

    // 1. 从物理引擎读取结果, 更新平台运动状态 (post-step)
    {
    std::lock_guard<std::mutex> lock(mtx_);
    if (physics_bridge_) {
        for (auto& [id, platform] : platforms_) {
            if (!platform->alive) continue;

            // 读取物理引擎结果
            physics_bridge_->readMotionState(id, platform->state);

            // 估算加速度
            auto prev_it = prev_velocities_.find(id);
            if (prev_it != prev_velocities_.end()) {
                platform->state.accel = (platform->state.velocity - prev_it->second) / std::max(dt, 1e-6);
            }
            prev_velocities_[id] = platform->state.velocity;
        }
    }

    // 2. 更新装备联动状态
    updateAllPlatformsEquipment(dt);
    }

    // 2.5 交战结果判定: 拦截弹命中 -> 摧毁目标
    resolveEngagements(dt, tick.sim_time);

    // Commands are consumed in pre-tick; commands arriving during physics
    // remain queued for the next tick rather than being erased here.
}

// =============================================================================
void PlatformManager::onStart() {
    running_ = true;
}

// =============================================================================
void PlatformManager::onStop() {
    running_ = false;
}

// =============================================================================
void* PlatformManager::getMutableProduct(const std::string& product_type) {
    return nullptr;
}

const void* PlatformManager::getConstProduct(const std::string& product_type) const {
    return nullptr;
}

// =============================================================================
void PlatformManager::setPhysicsEngine(PhysicsEngine* physics) {
    physics_bridge_.reset();
    physics_ = physics;
    if (physics_) {
        physics_bridge_ = std::make_unique<PlatformPhysicsBridge>(physics_);
        spdlog::info("PlatformManager: physics engine attached");
    } else {
        physics_bridge_.reset();
    }
}

// =============================================================================
uint32_t PlatformManager::createPlatform(
    const std::string& name,
    PlatformCategory category,
    const nlohmann::json& requested_motion_config,
    const MotionState& initial_state)
{
    const auto motion_config=requested_motion_config.is_null() ? nlohmann::json::object() : requested_motion_config;
    if(!motion_config.is_object())throw std::invalid_argument("platform motion configuration must be an object");
    if(motion_config.contains("max_integration_step") && (!motion_config["max_integration_step"].is_number() || !std::isfinite(motion_config["max_integration_step"].get<double>()) || motion_config["max_integration_step"].get<double>()<0 || motion_config["max_integration_step"].get<double>()>60))
        throw std::invalid_argument("invalid platform integration step");
    std::lock_guard<std::mutex> lock(mtx_);

    uint32_t id = next_platform_id_++;

    auto platform = std::make_unique<PlatformInstance>();
    platform->platform_id = id;
    platform->name = name;
    platform->category = category;
    platform->state = initial_state;
    const std::string authority=motion_config.value("motion_authority",std::string("dynamic"));
    if (authority!="dynamic" && authority!="kinematic")
        throw std::invalid_argument("motion_authority must be dynamic or kinematic");
    platform->kinematic=authority=="kinematic";
    platform->flight_control_enabled=motion_config.value("flight_control_enabled",true);

    // 创建运动模型
    auto motion = createMotionModel(category);
    if (motion) {
        if(!motion->initialize(motion_config, initial_state)) throw std::invalid_argument("platform motion initialization failed");
        platform->motion_model = motion.get();
        motion_models_[id] = std::move(motion);
    }

    // 决策分步步长: 优先显式配置 (motion_params), 否则按类别默认 ——
    // 导弹 1ms (800m/s 时决策粒度 ~0.8m, 保证引信/导引不漏检),
    // 飞机 5ms, 地面/水面不分步 (帧步长已足够)。
    if (motion_config.contains("max_integration_step")) {
        platform->max_integration_step = motion_config.value("max_integration_step", 0.0);
    } else {
        switch (category) {
            case PlatformCategory::MISSILE:  platform->max_integration_step = 0.001; break;
            case PlatformCategory::AIRCRAFT: platform->max_integration_step = 0.005; break;
            default:                         platform->max_integration_step = 0.0;   break;
        }
    }

    // 创建装备联动
    auto equipment = std::make_unique<PlatformEquipment>(id);
    equipment_links_[id] = std::move(equipment);

    platforms_[id] = std::move(platform);

    // 创建物理引擎刚体
    if (physics_bridge_) {
        const bool existing=physics_->bodyExists(name);
        physics_bridge_->createBody(
            id, name, category,
            initial_state.position,
            vsg::dvec3{initial_state.yaw, initial_state.pitch, initial_state.roll},
            PlatformPhysicsBridge::defaultExtents(category));
        physics_bridge_->setAuthority(id,platforms_[id]->kinematic);
        if(!existing) physics_bridge_->applyVelocity(id,initial_state.velocity);
        physics_bridge_->readMotionState(id,platforms_[id]->state);
        if (platforms_[id]->motion_model)
            static_cast<IPlatformMotion*>(platforms_[id]->motion_model)->initialize(motion_config,platforms_[id]->state);
    }

    return id;
}

// =============================================================================
void PlatformManager::removePlatform(uint32_t platform_id) {
    std::lock_guard<std::mutex> lock(mtx_);

    // 移除物理引擎刚体
    if (physics_bridge_) {
        physics_bridge_->removeBody(platform_id);
    }

    platforms_.erase(platform_id);
    motion_models_.erase(platform_id);
    equipment_links_.erase(platform_id);
    pending_commands_.erase(platform_id);
    flight_controls_.erase(platform_id);
    prev_velocities_.erase(platform_id);
}

// =============================================================================
PlatformQueryResult PlatformManager::queryPlatform(uint32_t platform_id) {
    std::lock_guard<std::mutex> lock(mtx_);

    auto it = platforms_.find(platform_id);
    if (it == platforms_.end()) {
        return {false, nullptr, nullptr, nullptr};
    }

    PlatformQueryResult result;
    result.found = true;
    result.platform = it->second.get();

    auto mit = motion_models_.find(platform_id);
    if (mit != motion_models_.end()) {
        result.motion = mit->second.get();
    }

    auto eit = equipment_links_.find(platform_id);
    if (eit != equipment_links_.end()) {
        result.equipment = eit->second.get();
    }

    return result;
}

// =============================================================================
std::vector<PlatformInstance*> PlatformManager::allPlatforms() {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<PlatformInstance*> result;
    result.reserve(platforms_.size());
    for (auto& [id, p] : platforms_) {
        result.push_back(p.get());
    }
    return result;
}

// =============================================================================
std::vector<PlatformInstance*> PlatformManager::platformsByCategory(PlatformCategory cat) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<PlatformInstance*> result;
    for (auto& [id, p] : platforms_) {
        if (p->category == cat) {
            result.push_back(p.get());
        }
    }
    return result;
}

// =============================================================================
void PlatformManager::sendCommand(uint32_t platform_id, const ControlCommand& cmd) {
    std::lock_guard<std::mutex> lock(mtx_);
    pending_commands_[platform_id] = cmd;
}


// Fly-by-command ownership is per platform. Installation is acknowledged here;
// actual rate-limited input and motion only advance on simulation ticks.
vsgraysim::FlightControlResult PlatformManager::applyFlightControl(const vsgraysim::FlightControlCommand& command,double sim_time) {
    auto result=vsgraysim::flightResult(command,"rejected","invalid_input"); result.sim_time=sim_time;
    const auto invalid=vsgraysim::validateFlightControl(command);
    if(!invalid.empty()){result.code=invalid;return result;}
    if(command.presentation_pose){result.code="presentation_unavailable";return result;}
    std::lock_guard<std::mutex> lock(mtx_);
    auto platform_it=platforms_.find(command.platform_id);
    if(platform_it==platforms_.end()){result.code="unknown_platform";return result;}
    auto& platform=*platform_it->second;
    if(platform.category!=PlatformCategory::AIRCRAFT){result.code="unsupported_platform";return result;}
    if(!platform.alive){result.code="platform_dead";return result;}
    if(!platform.flight_control_enabled){result.code="disabled";return result;}
    if(platform.kinematic){result.code="kinematic_authority";return result;}
    const auto model_it=motion_models_.find(command.platform_id);
    auto* model=model_it==motion_models_.end() ? nullptr : dynamic_cast<AircraftMotion*>(model_it->second.get());
    if(!model){result.code="unsupported_model";return result;}
    const auto now=std::chrono::steady_clock::now();
    const auto existing=flight_controls_.find(command.platform_id);
    if(existing!=flight_controls_.end()) {
        auto& state=existing->second;
        if(!state.owner.empty() && now>=state.deadline){stabilizeFlight(command.platform_id,state);state.revision++;}
        result.revision=state.revision;
        if(!state.owner.empty() && state.owner!=command.client_id){result.code="control_owned";return result;}
    }
    if(command.aircraft_system) {
        if(existing==flight_controls_.end()||existing->second.owner!=command.client_id||
            (existing->second.mode!="manual"&&existing->second.mode!="altitude_hold")) {
            result.code="aircraft_system_requires_lease";return result;
        }
        auto& state=existing->second;
        const auto& demand=*command.aircraft_system;
        if(demand.channel=="gear_down")state.gearDown=demand.enabled;
        else if(demand.channel=="nav_lights")state.navLights=demand.enabled;
        else if(demand.channel=="landing_light")state.landingLight=demand.enabled;
        ++state.systemsRevision;
        result.stage="applied";result.code="aircraft_system_installed";
        result.revision=state.systemsRevision;
        result.message="Aircraft system selected; output is reported in platform systems telemetry.";
        return result;
    }
    if(command.mode=="route" && !command.release) {
        auto route=route_plans_.find(command.platform_id);
        if(route==route_plans_.end() || route->second.waypoints.size()<2){result.code="route_unavailable";return result;}
        if(!route->second.loop && sim_time>=route->second.start_delay_s+route->second.totalDuration()){result.code="route_expired";return result;}
    }
    if(command.mode=="altitude_hold" && !command.release) {
        const auto& p=model->params();
        if(command.target_speed<p.min_speed || command.target_speed>p.max_speed || command.target_altitude<p.min_altitude || command.target_altitude>p.max_altitude){result.code="outside_flight_envelope";return result;}
    }
    auto& state=flight_controls_[command.platform_id];
    state.command=command; state.revision++;
    if(command.release)stabilizeFlight(command.platform_id,state);
    else {
        state.mode=command.mode;state.failsafe=false;
        state.owner=command.mode=="route" ? std::string() : command.client_id;
        state.deadline=now+std::chrono::seconds(2);
    }
    pending_commands_.erase(command.platform_id);
    result.stage="applied";result.code=command.release ? "released" : "installed";result.revision=state.revision;
    result.message="Control installed; actual actuator inputs evolve on simulation ticks.";
    return result;
}
void PlatformManager::stabilizeFlight(uint32_t id,FlightState& control) {
    const auto it=platforms_.find(id); if(it==platforms_.end())return;
    const auto& state=it->second->state;
    control.mode="stabilized";control.owner.clear();control.failsafe=true;
    control.command.elevator=control.command.aileron=control.command.rudder=0;
    control.command.target_speed=vsg::length(state.velocity);
    const auto model=motion_models_.find(id);
    if(model!=motion_models_.end()) if(const auto* aircraft=dynamic_cast<AircraftMotion*>(model->second.get())) {
        const auto& limits=aircraft->params();
        control.command.throttle=std::clamp((control.command.target_speed-limits.min_speed)/std::max(.001,limits.max_speed-limits.min_speed),0.,1.);
    }
    control.command.target_altitude=state.position.y;
    control.command.target_heading=std::fmod(std::fmod(state.yaw,360.)+360.,360.);
    pending_commands_.erase(id);
}
void PlatformManager::updateFlightControlLeases(double) {
    std::lock_guard<std::mutex> lock(mtx_);
    const auto now=std::chrono::steady_clock::now();
    for(auto& [id,control]:flight_controls_) if(!control.owner.empty() && now>=control.deadline) {
        stabilizeFlight(id,control);control.revision++;
    }
}
void PlatformManager::resetFlightControls() {
    std::lock_guard<std::mutex> lock(mtx_);
    for(auto& [id,control]:flight_controls_) if(control.mode!="stabilized" || !control.owner.empty()) {
        stabilizeFlight(id,control);control.revision++;
    }
}
nlohmann::json PlatformManager::flightSnapshot(const PlatformInstance& platform) const {
    const auto id=platform.platform_id;
    const auto route=route_plans_.find(id);
    const bool has_route=route!=route_plans_.end() && route->second.waypoints.size()>=2;
    const auto model_it=motion_models_.find(id);
    auto* model=model_it==motion_models_.end()?nullptr:dynamic_cast<AircraftMotion*>(model_it->second.get());
    std::string reason;
    if(platform.category!=PlatformCategory::AIRCRAFT || !model)reason="unsupported_platform";
    else if(!platform.flight_control_enabled)reason="disabled";
    else if(platform.kinematic)reason="kinematic_authority";
    else if(!platform.alive)reason="platform_dead";
    nlohmann::json snapshot={{"available",reason.empty()},{"enabled",platform.flight_control_enabled},{"model","point_mass_rate"},{"unavailable_reason",reason},{"has_route",has_route},
        {"mode",has_route ? "route" : "autonomous"},{"owner",""},{"owner_client_id",""},{"revision",uint64_t(0)},{"last_request_id",""},{"failsafe",false},{"lease_remaining_ms",0}};
    snapshot["actual"]=model ? model->controlInputs() : nlohmann::json::object();
    vsgraysim::FlightControlCommand command;
    command.target_speed=vsg::length(platform.state.velocity);command.target_altitude=platform.state.position.y;command.target_heading=platform.state.yaw;
    if(model) {
        const auto& limits=model->params();
        command.throttle=std::clamp((command.target_speed-limits.min_speed)/std::max(.001,limits.max_speed-limits.min_speed),0.,1.);
        snapshot["limits"]={{"min_speed",limits.min_speed},{"max_speed",limits.max_speed},{"min_altitude",limits.min_altitude},{"max_altitude",limits.max_altitude},
            {"max_roll_angle",limits.max_roll_angle},{"max_pitch_rate",limits.max_pitch_rate},{"max_roll_rate",limits.max_roll_rate},{"max_yaw_rate",limits.max_yaw_rate},
            {"max_accel",limits.max_accel},{"max_decel",limits.max_decel},{"climb_rate_max",limits.climb_rate_max},{"sink_rate_max",limits.sink_rate_max}};
    }
    snapshot["systems"]={{"available",reason.empty()},{"gear_down",false},{"gear_extension",0.},
                         {"nav_lights",false},{"landing_light",false},{"landing_light_active",false},{"revision",uint64_t(0)}};
    const auto control=flight_controls_.find(id);
    if(control!=flight_controls_.end()) {
        const auto& value=control->second;command=value.command;
        snapshot["systems"].update({{"gear_down",value.gearDown},{"gear_extension",value.gearExtension},
            {"nav_lights",value.navLights},{"landing_light",value.landingLight},
            {"landing_light_active",value.landingLight&&value.gearDown&&value.gearExtension>=1.},
            {"revision",value.systemsRevision}});
        snapshot["mode"]=value.mode;snapshot["owner"]=value.owner;snapshot["owner_client_id"]=value.owner;
        snapshot["revision"]=value.revision;snapshot["last_request_id"]=value.command.request_id;snapshot["failsafe"]=value.failsafe;
        if(!value.owner.empty())snapshot["lease_remaining_ms"]=std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(value.deadline-std::chrono::steady_clock::now()).count());
    }
    snapshot["command"]={{"throttle",command.throttle},{"elevator",command.elevator},{"aileron",command.aileron},{"rudder",command.rudder},{"target_speed",command.target_speed},{"target_altitude",command.target_altitude},{"target_heading",command.target_heading}};
    return snapshot;
}

// =============================================================================
void PlatformManager::setEnvironmentContext(const EnvironmentContext& env) {
    env_context_ = env;
}

// =============================================================================
size_t PlatformManager::activePlatformCount() const {
    return std::count_if(platforms_.begin(), platforms_.end(),
                         [](const auto& p) { return p.second->alive; });
}

// =============================================================================
bool PlatformManager::addEquipment(uint32_t platform_id, uint32_t equipment_id,
                                    EquipmentType type, const std::string& name) {
    std::lock_guard<std::mutex> lock(mtx_);

    auto it = platforms_.find(platform_id);
    if (it == platforms_.end()) return false;

    EquipmentInstance eq;
    eq.equipment_id = equipment_id;
    eq.type = type;
    eq.name = name;
    eq.powered = true;
    eq.active = false;
    it->second->equipment.push_back(eq);

    // 通知装备联动
    auto eit = equipment_links_.find(platform_id);
    if (eit != equipment_links_.end()) {
        eit->second->registerEquipment(equipment_id, type, name);
    }

    // 记录装备挂载绑定 (EQUIPPED_ON)
    graph_.addEdge(EntityKind::PLATFORM, platform_id,
                   EntityKind::EQUIPMENT, equipment_id,
                   RelationType::EQUIPPED_ON, {}, name);

    return true;
}

// =============================================================================
bool PlatformManager::removeEquipment(uint32_t platform_id, uint32_t equipment_id) {
    std::lock_guard<std::mutex> lock(mtx_);

    auto it = platforms_.find(platform_id);
    if (it == platforms_.end()) return false;

    auto& eqs = it->second->equipment;
    eqs.erase(std::remove_if(eqs.begin(), eqs.end(),
                              [equipment_id](const EquipmentInstance& e) {
                                  return e.equipment_id == equipment_id;
                              }),
               eqs.end());

    return true;
}

// =============================================================================
bool PlatformManager::setEquipmentPower(uint32_t platform_id, uint32_t equipment_id,
                                         bool powered) {
    std::lock_guard<std::mutex> lock(mtx_);

    auto it = platforms_.find(platform_id);
    if (it == platforms_.end()) return false;

    for (auto& eq : it->second->equipment) {
        if (eq.equipment_id == equipment_id) {
            eq.powered = powered;
            return true;
        }
    }
    return false;
}

// =============================================================================
// 阵营
// =============================================================================
void PlatformManager::setPlatformFaction(uint32_t platform_id, int faction) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = platforms_.find(platform_id);
    if (it == platforms_.end()) return;
    it->second->faction = faction;
}

int PlatformManager::platformFaction(uint32_t platform_id) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = platforms_.find(platform_id);
    return it != platforms_.end() ? it->second->faction : 0;
}

// =============================================================================
// 交战闭环: 制导雷达请求发射 -> 指派红方地空导弹拦截蓝方飞行目标
// =============================================================================
uint32_t PlatformManager::requestEngagement(const vsg::dvec3& target_pos,
                                            double gate_m, double sim_time)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_) return 0;

    // 1) 关联蓝方飞行目标 (导弹/无人机) 中距请求位置最近者 (门限内)
    uint32_t target_id = 0;
    double best_d = gate_m;
    for (const auto& [id, p] : platforms_) {
        if (!p->alive || p->faction != 1) continue;                 // 蓝方 & 存活
        if (p->category != PlatformCategory::MISSILE &&
            p->category != PlatformCategory::AIRCRAFT) continue;    // 仅拦截飞行目标
        const double d = vsg::length(p->state.position - target_pos);
        if (d <= best_d) { best_d = d; target_id = id; }
    }
    if (target_id == 0) {
        spdlog::warn("PlatformManager: engage request -> no blue flying target within {:.0f}m",
                     gate_m);
        return 0;
    }

    // 2) 指派一枚红方地空导弹 (已就位, 未参与交战, 类别 MISSILE)
    uint32_t interceptor_id = 0;
    double ibest = 1e18;
    for (const auto& [id, p] : platforms_) {
        if (!p->alive || p->faction != 2) continue;               // 红方 & 存活
        if (p->category != PlatformCategory::MISSILE) continue;
        if (engagements_.count(id)) continue;                     // 已在交战
        auto mit = motion_models_.find(id);
        if (mit == motion_models_.end()) continue;
        auto* mm = dynamic_cast<MissileMotion*>(mit->second.get());
        if (!mm || mm->hasExpired()) continue;                    // 须为可发射导弹
        const double d = vsg::length(p->state.position - target_pos);
        if (d < ibest) { ibest = d; interceptor_id = id; }
    }
    if (interceptor_id == 0) {
        spdlog::warn("PlatformManager: engage request -> no ready red interceptor available");
        return 0;
    }

    // 3) 建立交战记录 (解锁发射, 转制导拦截)
    EngagementRec rec;
    rec.interceptor_id = interceptor_id;
    rec.target_id = target_id;
    rec.launched = true;
    rec.in_flight = true;
    rec.launch_time = sim_time;
    engagements_[interceptor_id] = rec;

    spdlog::info("PlatformManager: RED SAM [{}] ENGAGE blue target [{}] @ T={:.1f}s",
                 platforms_[interceptor_id]->name,
                 platforms_[target_id]->name, sim_time);
    return target_id;
}

// =============================================================================
// 每帧: 为在交战中的拦截弹喂给目标实时位置 (制导雷达信息的仿真侧载体)
// =============================================================================
void PlatformManager::injectEngagementGuidance() {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& [interceptor_id, rec] : engagements_) {
        if (rec.impacted || !rec.in_flight) continue;

        auto tit = platforms_.find(rec.target_id);
        if (tit == platforms_.end() || !tit->second->alive) {
            rec.impacted = true; rec.in_flight = false; rec.target_destroyed = true;
            continue;
        }

        // 制导雷达持续下传目标实时位置给拦截弹 (仿真侧即时闭环)
        ControlCommand cmd;
        cmd.auto_control = true;
        cmd.has_target = true;
        cmd.target_platform_id = rec.target_id;
        cmd.target_position = tit->second->state.position;
        cmd.desired_speed = 300.0; // 制导目标速度 (由 MissileMotion 按需)
        pending_commands_[interceptor_id] = cmd;
    }
}

// =============================================================================
// 每帧: 拦截弹命中断定 -> 摧毁蓝方目标
// =============================================================================
void PlatformManager::resolveEngagements(double dt, double sim_time) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& [interceptor_id, rec] : engagements_) {
        if (rec.impacted) continue;

        auto iit = platforms_.find(interceptor_id);
        if (iit == platforms_.end() || !iit->second->alive) {
            rec.impacted = true; rec.in_flight = false; rec.impact_time = sim_time;
            continue;
        }

        auto mit = motion_models_.find(interceptor_id);
        if (mit != motion_models_.end()) {
            auto* mm = dynamic_cast<MissileMotion*>(mit->second.get());
            if (mm) {
                if (mm->hasHitTarget()) {
                    // 命中: 摧毁蓝方目标
                    auto tit = platforms_.find(rec.target_id);
                    if (tit != platforms_.end()) {
                        tit->second->alive = false;
                        tit->second->health = 0.0;
                    }
                    rec.target_destroyed = true;
                    rec.impacted = true; rec.in_flight = false; rec.impact_time = sim_time;
                    spdlog::info("PlatformManager: interceptor hit target, target [{}] destroyed @ T={:.1f}s",
                                 rec.target_id, sim_time);
                    continue;
                }
                if (mm->hasExpired()) {
                    rec.impacted = true; rec.in_flight = false; rec.impact_time = sim_time;
                }
            }
        }
    }
}

// =============================================================================
// 交战状态快照 (供仿真控制软件回传显控)
// =============================================================================
nlohmann::json PlatformManager::engagementsSnapshot() const {
    std::lock_guard<std::mutex> lock(mtx_);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [interceptor_id, rec] : engagements_) {
        auto iit = platforms_.find(interceptor_id);
        auto tit = platforms_.find(rec.target_id);
        nlohmann::json j;
        j["interceptor_id"] = interceptor_id;
        j["interceptor_name"] = iit != platforms_.end() ? iit->second->name : "";
        j["target_id"] = rec.target_id;
        j["target_name"] = tit != platforms_.end() ? tit->second->name : "";
        j["launched"] = rec.launched;
        j["in_flight"] = rec.in_flight && !rec.impacted;
        j["impacted"] = rec.impacted;
        j["target_destroyed"] = rec.target_destroyed;
        if (iit != platforms_.end() && tit != platforms_.end() && tit->second->alive) {
            j["range_km"] = vsg::length(iit->second->state.position -
                                        tit->second->state.position) / 1000.0;
        } else {
            j["range_km"] = 0.0;
        }
        j["launch_time"] = rec.launch_time;
        j["sim_time"] = rec.launch_time; // 近似, 由调用方覆盖
        arr.push_back(j);
    }
    return arr;
}

// =============================================================================
void PlatformManager::updateAllPlatformsMotion(double dt, const EnvironmentContext& env, double end_time) {
    std::lock_guard<std::mutex> lock(mtx_);
    decltype(pending_commands_) commands;
    commands.swap(pending_commands_);

    for (auto& [id, platform] : platforms_) {
        if (!platform->alive) continue;

        // 更新运动模型 (计算期望速度)
        auto it = motion_models_.find(platform->platform_id);
        if (it == motion_models_.end()) continue;

        ControlCommand cmd;
        auto cmd_it = commands.find(platform->platform_id);
        if (cmd_it != commands.end()) {
            cmd = cmd_it->second;
        }

        auto flight=flight_controls_.find(id);
        // Sim-time actuator travel; a paused command can change handle demand
        // but cannot move the gear. Existing collision shapes stay untouched.
        if(flight!=flight_controls_.end()) {
            auto& gear=flight->second;
            constexpr double travelSeconds=3.;
            gear.gearExtension=gear.gearDown?std::min(1.,gear.gearExtension+dt/travelSeconds)
                                            :std::max(0.,gear.gearExtension-dt/travelSeconds);
            if(gear.gearExtension>1.-1e-9)gear.gearExtension=1.;
            if(gear.gearExtension<1e-9)gear.gearExtension=0.;
        }
        if(flight!=flight_controls_.end() && flight->second.mode=="route" && cmd.flight_mode=="route") {
            flight->second.command.target_speed=cmd.desired_speed;
            flight->second.command.target_altitude=cmd.desired_altitude;
            flight->second.command.target_heading=cmd.target_heading;
        }
        if(flight!=flight_controls_.end() && flight->second.mode!="route") {
            const auto& control=flight->second;
            cmd.flight_mode=control.mode;
            cmd.throttle=control.command.throttle; cmd.elevator=control.command.elevator;
            cmd.aileron=control.command.aileron; cmd.rudder=control.command.rudder;
            cmd.desired_speed=control.command.target_speed;
            cmd.desired_altitude=control.command.target_altitude;
            cmd.target_heading=control.command.target_heading; cmd.auto_control=false;
        }

        // 决策分步: 快平台把帧步长 dt 细分为 max_integration_step 级子步,
        // 导引律/引信/姿态决策不再受帧粒度限制 (物理积分仍由 Bullet 120Hz 子步完成,
        // 此处只细分决策, 最后仍单次 applyVelocity, 与三明治结构兼容)。
        const double max_step = platform->max_integration_step;
        int n_sub = 1;
        if (max_step > 0.0 && dt > max_step) {
            n_sub = static_cast<int>(std::ceil(dt / max_step));
        }
        const double sub_dt = dt / n_sub;
        MotionState desired=platform->state;
        for (int s = 0; s < n_sub; ++s) {
            auto local_env=env;
            if (physics_) physics_->groundAt(desired.position.x,desired.position.z,
                                             local_env.terrain_height,local_env.terrain_normal);
            it->second->update(sub_dt, local_env, cmd, desired);
        }
        if (physics_bridge_ && physics_bridge_->bodyExists(id))
            physics_bridge_->applyMotionCommand(id,desired,end_time,dt,platform->kinematic);
        else platform->state=desired;
    }
}

// =============================================================================
void PlatformManager::updateAllPlatformsEquipment(double dt) {
    for (auto& [id, platform] : platforms_) {
        if (!platform->alive) continue;

        auto eit = equipment_links_.find(platform->platform_id);
        if (eit == equipment_links_.end()) continue;

        auto& eq_link = eit->second;
        eq_link->updateFromMotion(platform->state, dt);

        // 同步回 PlatformInstance 的 equipment 列表
        for (auto& eq_instance : platform->equipment) {
            EquipmentState state = eq_link->getEquipmentState(eq_instance.equipment_id);
            eq_instance.active = state.active;
            eq_instance.current_power = state.current_power;
            eq_instance.current_range = state.current_range;
            eq_instance.motion_effect.perf_degradation = state.perf_degradation;
        }
    }
}

// =============================================================================
std::unique_ptr<IPlatformMotion> PlatformManager::createMotionModel(PlatformCategory cat) {
    switch (cat) {
        case PlatformCategory::GROUND_VEHICLE:
            return std::make_unique<GroundVehicleMotion>();
        case PlatformCategory::AIRCRAFT:
            return std::make_unique<AircraftMotion>();
        case PlatformCategory::NAVAL_VESSEL:
            return std::make_unique<NavalVesselMotion>();
        case PlatformCategory::MISSILE:
            return std::make_unique<MissileMotion>();
        default:
            return nullptr;
    }
}

// =============================================================================
nlohmann::json PlatformManager::getPlatformSnapshot(uint32_t platform_id) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = platforms_.find(platform_id);
    if (it == platforms_.end()) return {};

    auto& p = *it->second;
    nlohmann::json j;
    j["platform_id"] = p.platform_id;
    j["name"] = p.name;
    j["category"] = platformCategoryName(p.category);
    j["alive"] = p.alive;
    j["health"] = p.health;
    j["position"] = {p.state.position.x, p.state.position.y, p.state.position.z};
    j["velocity"] = {p.state.velocity.x, p.state.velocity.y, p.state.velocity.z};
    j["speed"] = vsg::length(p.state.velocity);
    j["yaw"] = p.state.yaw;
    j["pitch"] = p.state.pitch;
    j["roll"] = p.state.roll;
    j["rotation"]={p.state.rotation.x,p.state.rotation.y,p.state.rotation.z,p.state.rotation.w};
    j["motion_authority"]=p.kinematic ? "kinematic" : "dynamic";
    j["equipment_count"] = p.equipment.size();

    nlohmann::json eqs = nlohmann::json::array();
    for (auto& eq : p.equipment) {
        eqs.push_back({
            {"equipment_id", eq.equipment_id},
            {"name", eq.name},
            {"type", static_cast<int>(eq.type)},
            {"powered", eq.powered},
            {"active", eq.active},
            {"degradation", eq.motion_effect.perf_degradation}
        });
    }
    j["equipment"] = eqs;
    j["flight_control"] = flightSnapshot(p);

    return j;
}

// =============================================================================
nlohmann::json PlatformManager::getAllPlatformsSnapshot() const {
    std::lock_guard<std::mutex> lock(mtx_);
    nlohmann::json arr = nlohmann::json::array();
    for (auto& [id, p] : platforms_) {
        nlohmann::json j;
        PlatformInstance& platform = *p;
        j["platform_id"] = platform.platform_id;
        j["name"] = platform.name;
        j["category"] = platformCategoryName(platform.category);
        j["alive"] = platform.alive;
        j["health"] = platform.health;
        j["position"] = {platform.state.position.x, platform.state.position.y, platform.state.position.z};
        j["velocity"] = {platform.state.velocity.x, platform.state.velocity.y, platform.state.velocity.z};
        j["speed"] = vsg::length(platform.state.velocity);
        j["yaw"] = platform.state.yaw;   // 航向 (度, 3D 位姿下发用)
        j["pitch"]=platform.state.pitch; j["roll"]=platform.state.roll;
        j["rotation"]={platform.state.rotation.x,platform.state.rotation.y,platform.state.rotation.z,platform.state.rotation.w};
        j["motion_authority"]=platform.kinematic ? "kinematic" : "dynamic";
        j["flight_control"]=flightSnapshot(platform);
        arr.push_back(j);
    }
    return arr;
}

// =============================================================================
// 航迹规划调度
// =============================================================================
bool PlatformManager::assignRoute(uint32_t platform_id, const RoutePlan& route) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!platforms_.count(platform_id)) {
        spdlog::warn("PlatformManager: assignRoute failed, platform {} not found", platform_id);
        return false;
    }
    route_plans_[platform_id] = route;
    route_segments_[platform_id] = 0;
    spdlog::info("PlatformManager: assigned route '{}' to platform {} ({} waypoints, type={}, loop={})",
                 route.platform_name, platform_id, route.waypoints.size(),
                 routeTypeName(route.type), route.loop ? "yes" : "no");
    return true;
}

// =============================================================================
void PlatformManager::clearRoute(uint32_t platform_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    route_plans_.erase(platform_id);
    route_segments_.erase(platform_id);
    pending_commands_.erase(platform_id);
}

// =============================================================================
bool PlatformManager::hasRoute(uint32_t platform_id) const {
    std::lock_guard<std::mutex> lock(mtx_);
    return route_plans_.count(platform_id) > 0;
}

// =============================================================================
RoutePlan PlatformManager::getRoute(uint32_t platform_id) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = route_plans_.find(platform_id);
    return it != route_plans_.end() ? it->second : RoutePlan{};
}

// =============================================================================
double PlatformManager::wrapAngle(double deg) {
    deg = std::fmod(deg, 360.0);
    if (deg > 180.0) deg -= 360.0;
    if (deg < -180.0) deg += 360.0;
    return deg;
}

// =============================================================================
// 将航点目标转换为控制指令
// 目标方位: 计算平台到目标的偏航角, 生成 desired_yaw_rate 使其转向目标
// =============================================================================
ControlCommand PlatformManager::makeRouteCommand(uint32_t platform_id,
                                                 const RoutePlan& route,
                                                 const MotionState& state,
                                                 double sim_time) const {
    ControlCommand cmd;
    cmd.auto_control = true;

    // 相对启动时刻
    double elapsed = sim_time - route.start_delay_s;

    const auto platform=platforms_.find(platform_id);
    const bool aircraft=platform!=platforms_.end() && platform->second->category==PlatformCategory::AIRCRAFT;
    // Fixed-wing aircraft cannot stop in mid-air before/after a timed route.
    if(aircraft && (elapsed<0 || (!route.loop && elapsed>=route.totalDuration()))) {
        cmd.flight_mode="route";cmd.auto_control=false;
        cmd.desired_speed=vsg::length(state.velocity);cmd.desired_altitude=state.position.y;cmd.target_heading=state.yaw;
        return cmd;
    }
    // 尚未到达启动时刻: 保持静止/悬停
    if (elapsed < 0) {
        cmd.desired_speed = 0.0;
        cmd.waypoint_index = 0;
        return cmd;
    }

    // 采样目标位置/速度
    vsg::dvec3 target;
    double speed = 0.0;
    bool on_route = route.sample(elapsed, target, speed, !route.loop);

    if (!on_route) {
        // 航迹结束且不循环: 保持最后位置, 停住
        cmd.desired_speed = 0.0;
        cmd.waypoint_index = route.waypoints.size();
        return cmd;
    }

    cmd.waypoints.clear();
    for (const auto& w : route.waypoints) cmd.waypoints.push_back(w.position);

    // 期望速度 (若采样速度为 0, 使用平台当前速度方向; 直接取采样段速度)
    double target_speed = speed;
    if (target_speed <= 0.0) {
        // 未指定速度: 计算由当前到目标的行进速度 (简单恒速段间估计)
        target_speed = 10.0;
    }
    cmd.desired_speed = target_speed;

    // 目标方位 (世界坐标, 偏航: atan2(dx, -dz), 与平台 yaw 定义一致)
    vsg::dvec3 delta = target - state.position;
    double dx = delta.x, dz = delta.z;
    double target_yaw = std::atan2(dx, -dz) * 180.0 / M_PI;
    double yaw_err = wrapAngle(target_yaw - state.yaw);
    // 转向速率: 比例增益转向目标 (运动模型会再钳制到 max_turn_rate)
    const double steer_gain = 3.0; // (度/秒) 每度航向偏差
    cmd.desired_yaw_rate = std::max(-180.0, std::min(180.0, yaw_err * steer_gain));

    // 目标位置供运动模型/显控引用
    cmd.target_position = target;
    cmd.has_target = true;
    if(aircraft) {
        cmd.flight_mode="route";cmd.auto_control=false;
        cmd.desired_altitude=target.y;
        cmd.target_heading=std::fmod(target_yaw+360.,360.);
        if(vsg::length(delta)<1e-6)cmd.target_heading=state.yaw;
        if(speed<=0)cmd.desired_speed=vsg::length(state.velocity);
    }

    return cmd;
}

// =============================================================================
// 航迹调度: 每帧为有航迹的平台生成控制指令
// =============================================================================
void PlatformManager::scheduleRoutes(double sim_time, double dt) {
    std::lock_guard<std::mutex> lock(mtx_);

    for (auto& [id, platform] : platforms_) {
        if (!platform->alive) continue;
        const auto flight=flight_controls_.find(id);
        if(flight!=flight_controls_.end() && flight->second.mode!="route") continue;

        auto r_it = route_plans_.find(id);
        if (r_it == route_plans_.end()) continue;

        const RoutePlan& route = r_it->second;
        if (platform->motion_model == nullptr) continue;

        ControlCommand cmd = makeRouteCommand(id, route, platform->state, sim_time);
        pending_commands_[id] = cmd;
    }
}

} // namespace vsgraysim::platform
