#include "vsgraysim/layer2_rt/RayTracer.h"
#include "vsgraysim/layer2_rt/SensorProcessor.h"
#include "vsgraysim/layer0_phys/PhysicsEngine.h"
#include "SceneBVH.h"

#include <cmath>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <spdlog/spdlog.h>

#include <vsg/maths/transform.h>   // vsg::inverse(dmat4)

namespace vsgraysim {

namespace detail {

// ============================================================================
// 轻量屏障式线程池: traceFrame 每帧把各传感器追踪作为独立任务分发,
// 工作线程常驻避免每帧建销线程; 任务顺序无关, 结果按下标保序合并。
// ============================================================================
class WorkerPool {
public:
    explicit WorkerPool(unsigned count) {
        for(unsigned i=0;i<count;++i) workers_.emplace_back([this] { run(); });
    }
    ~WorkerPool() {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_=true; }
        work_.notify_all();
        for(auto& worker:workers_) worker.join();
    }
    void parallel(const std::vector<std::function<void()>>& tasks) {
        if(tasks.empty()) return;
        std::unique_lock<std::mutex> lock(mutex_);
        if(busy_) throw std::logic_error("overlapping ray-query batches");
        busy_=true; tasks_=&tasks; cursor_=0; remaining_=tasks.size(); failure_=nullptr;
        work_.notify_all();
        done_.wait(lock,[&] { return remaining_==0; });
        const auto failure=failure_;
        busy_=false;
        lock.unlock();
        if(failure) std::rethrow_exception(failure);
    }
private:
    void run() {
        std::unique_lock<std::mutex> lock(mutex_);
        for(;;) {
            // Late workers may wake AFTER an entire batch has finished. A
            // generation counter alone is not proof the caller's task vector lives.
            work_.wait(lock,[&] { return stopping_ || (tasks_ && cursor_<tasks_->size()); });
            if(stopping_) return;
            auto task=(*tasks_)[cursor_++]; // own the callable while the lock is held
            lock.unlock();
            std::exception_ptr failure;
            try { task(); } catch(...) { failure=std::current_exception(); }
            lock.lock();
            if(failure && !failure_) failure_=failure;
            if(--remaining_==0) {
                tasks_=nullptr; // clear BEFORE the caller may destroy its task vector
                done_.notify_one();
            }
        }
    }
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable work_,done_;
    const std::vector<std::function<void()>>* tasks_=nullptr;
    std::size_t cursor_=0,remaining_=0;
    std::exception_ptr failure_;
    bool stopping_=false,busy_=false;
};

} // namespace detail

namespace {

inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline float length(const Vec3& v) {
    return std::sqrt(dot(v, v));
}
inline Vec3 normalize(const Vec3& v) {
    float len = length(v);
    if (len < 1e-9f) return {0, 0, 0};
    return {v.x / len, v.y / len, v.z / len};
}
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 add(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 sub(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 scale(const Vec3& v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

BandChannel bandMaskToChannel(BandMask m) {
    if (m & BAND_VIS)   return CH_VIS;
    if (m & BAND_IR)    return CH_IR;
    if (m & BAND_RADAR) return CH_RADAR;
    if (m & BAND_SAR)   return CH_SAR;
    return CH_VIS;
}

} // anonymous namespace

RayTracer::RayTracer(AtmosphereModelPtr atm,
                     std::shared_ptr<SceneBuilder> scene,
                     CrossInductionRulesPtr rules)
    : atm_(std::move(atm))
    , scene_(std::move(scene))
    , rules_(std::move(rules)) {
    if (!atm_) {
        spdlog::warn("RayTracer: null atmosphere model; using empty model");
        atm_ = std::make_shared<AtmosphereModel>();
    }
}

RayTracer::~RayTracer() = default;

// ============================================================================
// Atmospheric propagation integration
// ============================================================================
void RayTracer::integrateAtmosphere(const Vec3& origin, const Vec3& dir,
                                   float distance, BandChannel ch,
                                   float& attenuation, float& scatter_power,
                                   float& refraction) {
    attenuation = 1.0f;
    scatter_power = 0.0f;
    refraction = 0.0f;

    if (!atm_enabled_ || !atm_) return;

    // 分层自适应步进: 每步推进到当前大气分层边界 (或步长上限), 层内系数视为恒定,
    // 与原 50m 定步长 Euler 积分物理等价但迭代次数从 O(距离/50m) 降到 O(层数)。
    const double max_step = 1000.0;
    const double min_step = 1e-4;
    const double altitude_origin=physics_ ? physics_->snapshot()->anchor.alt_m : 0;
    const auto& layers = atm_->layers();
    double travelled = 0;

    while (travelled < distance) {
        // This backend is anchored Y-up, not ECEF. Slabs use local vertical
        // height plus anchor altitude; spherical/geoid transport is a separate backend.
        const double alt = origin.y+double(dir.y)*travelled+altitude_origin;

        double dl = std::min(max_step, distance - travelled);

        // Exact intersections with this backend's horizontal atmosphere slabs.
        if (!layers.empty()) {
            const float radial = dir.y;
            double boundary = 0.0;
            bool have = false;
            if (radial > 1e-3f) {                   // 上行: 找包含当前高度的层上界 (取最小)
                for (const auto& l : layers)
                    if (l.height_max > alt && (!have || l.height_max < boundary)) {
                        boundary = l.height_max; have = true;
                    }
            } else if (radial < -1e-3f) {           // 下行: 找包含当前高度的层下界 (取最大)
                for (const auto& l : layers)
                    if (l.height_min < alt && (!have || l.height_min > boundary)) {
                        boundary = l.height_min; have = true;
                    }
            }
            if (have) {
                const double d_b = (boundary - alt) / radial;
                if (d_b > min_step) dl = std::min(dl, d_b);
            }
        }
        dl = std::min(distance-travelled,std::max(min_step,dl));
        const auto layer=atm_->sampleAtAltitude(alt+.5f*dir.y*dl);

        float sigma_e = layer.ext_coeff[ch];
        attenuation *= std::exp(-sigma_e * dl);

        float sigma_s = layer.scatter_coeff[ch];
        scatter_power += sigma_s * dl;

        float cos_zenith = std::abs(dir.y);
        float sin_zenith = std::sqrt(std::max(0.0f, 1.0f - cos_zenith * cos_zenith));
        float n = layer.refract_index;
        const float dndh=.5f*(atm_->sampleAtAltitude(float(alt+1)).refract_index-
            atm_->sampleAtAltitude(float(alt-1)).refract_index);
        refraction += -sin_zenith * (1.0f / std::max(1.0f, n)) * dndh * dl;

        travelled += dl;
    }
}

// ============================================================================
// Scene intersection acceleration (simulation-side two-level BVH, #7)
// ============================================================================
struct RayTracer::TraceScene {
    simcore::TimePoint time=0;
    std::vector<DynPose> poses;
    std::vector<double> ground_planes;
};

void RayTracer::setPhysicsEngine(PhysicsEngine* physics) {
    physics_=physics;
    dyn_bodies_.clear();
}
void RayTracer::refreshSceneBVH() {
    if (!physics_) return;
    dyn_bodies_.resize(physics_->bodyCount());
    for (std::size_t i=0;i<dyn_bodies_.size();++i) {
        if (!physics_->bodyActive(i)) { dyn_bodies_[i]=DynBody{}; continue; }
        auto& body=dyn_bodies_[i];
        if (body.bvh) continue;
        const auto& triangles=physics_->bodyLocalTriangles(i);
        if (triangles.empty()) throw std::runtime_error("ray-query geometry unavailable for body "+physics_->bodyId(i));
        body.bvh=std::make_unique<detail::SceneBVH>();
        body.bvh->build(triangles);
        vsg::dvec3 lo(1e30,1e30,1e30),hi(-1e30,-1e30,-1e30);
        for (const auto& vertex:triangles) for(int axis=0;axis<3;++axis) {
            lo[axis]=std::min(lo[axis],double((&vertex.x)[axis]));
            hi[axis]=std::max(hi[axis],double((&vertex.x)[axis]));
        }
        const auto center=(lo+hi)*.5;
        body.local_center={float(center.x),float(center.y),float(center.z)};
        for (const auto& vertex:triangles)
            body.local_radius=std::max(body.local_radius,float(vsg::length(vsg::dvec3(vertex.x,vertex.y,vertex.z)-center)));
    }
}
std::shared_ptr<const RayTracer::TraceScene> RayTracer::captureScene(const RayTraceConfig& config) const {
    auto scene=std::make_shared<TraceScene>();
    if (!physics_) return scene;
    const auto latest=physics_->snapshot();
    scene->time=config.measurement_time ? simcore::fromSeconds(*config.measurement_time) : latest->time;
    if (scene->time>latest->time) throw std::runtime_error("ray query exceeds available world truth");
    scene->ground_planes=physics_->groundPlanes();
    for(std::size_t i=0;i<dyn_bodies_.size();++i) {
        if(!physics_->bodyActive(i)) continue;
        simcore::RigidState state;
        if(!physics_->sampleBody(physics_->bodyId(i),simcore::toSeconds(scene->time),state))
            throw std::runtime_error("ray query has no historical truth for body "+physics_->bodyId(i));
        const auto& geometry=dyn_bodies_[i];
        DynPose pose; pose.body=i; pose.rotation=state.orientation;
        const auto center=state.point({geometry.local_center.x,geometry.local_center.y,geometry.local_center.z});
        pose.center={float(center.x),float(center.y),float(center.z)};
        pose.radius=geometry.local_radius;
        pose.inverse=vsg::inverse(state.matrix());
        scene->poses.push_back(pose);
    }
    return scene;
}
bool RayTracer::intersectScene(const Vec3& origin,const Vec3& direction,float max_distance,
                              RayTraceResult& result,const TraceScene& scene) {
    if (!scene_intersect_enabled_) return false;
    float closest=max_distance;
    const vsg::dvec3 o(origin.x,origin.y,origin.z),d(direction.x,direction.y,direction.z);
    for (const auto& pose:scene.poses) {
        const auto delta=o-vsg::dvec3(pose.center.x,pose.center.y,pose.center.z);
        const double b=vsg::dot(delta,d), c=vsg::dot(delta,delta)-double(pose.radius)*pose.radius;
        if ((c>0 && b>0) || b*b-c<0) continue;
        const auto localOrigin=pose.inverse*vsg::dvec4(o,1);
        const auto localDirection=pose.inverse*vsg::dvec4(d,0);
        detail::BvhHit hit;
        if(!dyn_bodies_[pose.body].bvh->intersect(
            {float(localOrigin.x),float(localOrigin.y),float(localOrigin.z)},
            {float(localDirection.x),float(localDirection.y),float(localDirection.z)},closest,hit)) continue;
        closest=hit.t; result.hit=true;
        const auto normal=pose.rotation*vsg::dvec3(hit.normal.x,hit.normal.y,hit.normal.z);
        result.hit_normal={float(normal.x),float(normal.y),float(normal.z)};
        result.object_name=physics_->bodyId(pose.body);
        result.primitive_id=hit.primitive_id;
    }
    if (std::abs(direction.y)>1e-12) for(double height:scene.ground_planes) {
        const double t=(height-origin.y)/direction.y;
        if(t>1e-4 && t<closest) {
            closest=float(t); result.hit=true; result.object_name.clear(); result.primitive_id=0;
            result.hit_normal={0,direction.y<0 ? 1.f : -1.f,0};
        }
    }
    if(!result.hit) return false;
    result.distance=closest;
    result.hit_position=add(origin,scale(direction,closest));
    if(parameters_ && !result.object_name.empty()) {
        const auto* material=parameters_->findFace(result.object_name,result.primitive_id);
        if(!material) throw std::runtime_error("missing face material for "+result.object_name);
        result.hit_object_id=material->object_id; result.hit_geo_unit_id=material->geo_unit_id;
    }
    return true;
}

// ============================================================================
// Single ray trace (common: intersection + atmosphere only)
// ============================================================================
RayTraceResult RayTracer::traceRay(const Ray& ray, const RayTraceConfig& config,const TraceScene& scene) {
    RayTraceResult result;
    bool hit = intersectScene(ray.origin, ray.direction, config.max_distance, result, scene);
    if (!hit) {
        result.hit = false;
        result.distance = config.max_distance;
        return result;
    }

    if (atm_enabled_) {
        float atx = 1.0f, sctx = 0.0f, rftx = 0.0f;
        float arx = 1.0f, scrx = 0.0f, rfrx = 0.0f;
        BandChannel ch = bandMaskToChannel(config.band_mask);

        integrateAtmosphere(ray.origin, ray.direction, result.distance, ch,
                           atx, sctx, rftx);

        if(config.sensor_type!=SensorType::IR && config.sensor_type!=SensorType::VIS) {
            Vec3 back_dir = scale(ray.direction, -1.0f);
            integrateAtmosphere(result.hit_position, back_dir, result.distance, ch,arx,scrx,rfrx);
        }

        result.atm_attenuation = atx * arx;
        result.atm_scatter_power = sctx + scrx;
        result.atm_refraction = rftx + rfrx;
    }

    return result;
}

// ============================================================================
// Build scatter point from ray trace result + amplitude
// ============================================================================
ScatterPoint RayTracer::makeScatterPoint(const RayTraceResult& result,
                                         const RayTraceConfig& config,
                                         const Ray& ray,
                                         float amplitude) const {
    ScatterPoint sp{};
    sp.position = result.hit_position;
    sp.normal = result.hit_normal;
    sp.band_type = config.band_mask;
    sp.geo_unit_id = result.hit_geo_unit_id;
    sp.object_id = result.hit_object_id;
    sp.tx_sensor_id = config.sensor_id;
    sp.rx_sensor_id = config.sensor_id;
    sp.amplitude = amplitude;
    // 脉冲级时序: 散射点携带所属脉冲的序号与慢时间 (供信号成帧按序消费)
    sp.pulse_seq = config.pulse_seq;
    sp.pulse_slow_time = config.pulse_slow_time;

    const double path=(config.sensor_type==SensorType::RADAR || config.sensor_type==SensorType::SAR) ? 2.0 : 1.0;
    sp.phase=float(std::remainder(-path*2.0*3.14159265358979323846*result.distance/std::max(1e-9,double(config.wavelength)),
                                 2.0*3.14159265358979323846));
    sp.time_delay = float(path*result.distance/299792458.0);

    sp.atm_atten = result.atm_attenuation;
    sp.atm_scatter = result.atm_scatter_power;

    float mag = sp.amplitude;
    float ph  = sp.phase;
    sp.i_amplitude = mag * std::cos(ph);
    sp.q_amplitude = mag * std::sin(ph);
    sp.frequency = (config.wavelength > 0.0f)
                   ? 2.99792458e8f / config.wavelength : 0.0f;
    sp.power = mag * mag;

    sp.has_cross = 0;
    sp.cross_power = 0.0f;
    sp.cross_rx_sensor_id = 0;

    return sp;
}

// ============================================================================
// Trace single sensor (polymorphic via SensorProcessor)
// ============================================================================
std::vector<ScatterPoint> RayTracer::traceSensor(const RayTraceConfig& config) {
    return traceFrame({config});
}

std::vector<ScatterPoint> RayTracer::traceSensorInternal(const RayTraceConfig& config,
                                                         std::mt19937& rng,const TraceScene& scene) {
    auto processor = SensorProcessor::create(config.sensor_type);

    auto rays = processor->emitRays(config.origin, config.direction,
                                     config.beam_width, config.num_rays,
                                     config.scan_angle_az, config.scan_angle_el,
                                     rng);

    std::vector<ScatterPoint> scatter_points;
    for (const auto& ray : rays) {
        RayTraceResult result = traceRay(ray, config, scene);
        if (!result.hit) continue;

        GeoUnitPhysParams params{};
        params.vis_albedo = 0.3f;
        params.radar_epsilon_real = 10.0f;
        params.radar_epsilon_imag = 2.0f;
        params.radar_surface_rough = 0.1f;
        params.rcs_modify_factor = 1.0f;
        params.ir_emissivity[0] = 0.9f;
        if(parameters_ && !result.object_name.empty()) params=*parameters_->findFace(result.object_name,result.primitive_id);

        float surface = processor->computeSurfaceScatter(
            ray.direction, result.hit_normal, params, config.wavelength);

        float amplitude = processor->computeReturnAmplitude(
            config.tx_power, surface, result.atm_attenuation, result.distance);
        // A passive visible camera receives reflected illumination, not its own
        // transmit power. Illumination is applied in the image formation stage.
        if(config.sensor_type==SensorType::VIS) amplitude=params.vis_albedo*result.atm_attenuation;

        scatter_points.push_back(makeScatterPoint(result, config, ray, amplitude));
    }

    spdlog::debug("RayTracer: sensor {} traced {} rays, {} hits",
                  config.sensor_id, rays.size(), scatter_points.size());

    return scatter_points;
}

// ============================================================================
// Trace frame (multiple sensors)
// ============================================================================
std::vector<ScatterPoint> RayTracer::traceFrame(const std::vector<RayTraceConfig>& configs) {
    if(configs.empty()) return {};
    refreshSceneBVH();
    std::map<simcore::TimePoint,std::shared_ptr<const TraceScene>> snapshots;
    std::vector<std::shared_ptr<const TraceScene>> scenes;
    for(const auto& config:configs) {
        if(!std::isfinite(config.max_distance) || config.max_distance<=0 || config.max_distance>2e7 ||
           config.num_rays>1000000 ||
           !std::isfinite(config.beam_width) || config.beam_width<0 ||
           !std::isfinite(config.wavelength) || config.wavelength<=0 ||
           !simcore::finite({config.origin.x,config.origin.y,config.origin.z}) ||
           !simcore::finite({config.direction.x,config.direction.y,config.direction.z}) ||
           std::abs(length(config.direction)-1)>1e-5)
            throw std::invalid_argument("invalid ray measurement geometry or wavelength");
        const auto time=config.measurement_time ? simcore::fromSeconds(*config.measurement_time) :
            physics_ ? physics_->snapshot()->time : 0;
        auto& snapshot=snapshots[time];
        if(!snapshot) snapshot=captureScene(config);
        scenes.push_back(snapshot);
    }
    std::vector<std::vector<ScatterPoint>> results(configs.size());
    std::vector<std::exception_ptr> failures(configs.size());
    auto evaluate=[&](std::size_t i) {
        try {
            const auto& config=configs[i];
            std::seed_seq seed{42u,config.sensor_id,uint32_t(config.sample_index),uint32_t(config.sample_index>>32),
                uint32_t(scenes[i]->time),uint32_t(uint64_t(scenes[i]->time)>>32)};
            std::mt19937 random(seed);
            results[i]=traceSensorInternal(config,random,*scenes[i]);
        } catch(...) { failures[i]=std::current_exception(); }
    };
    if(configs.size()==1) evaluate(0);
    else {
        if(!pool_) {
            const auto hw=std::thread::hardware_concurrency();
            pool_=std::make_unique<detail::WorkerPool>(std::max(1u,std::min(8u,hw>2 ? hw-2 : 1u)));
        }
        std::vector<std::function<void()>> tasks;
        for(std::size_t i=0;i<configs.size();++i) tasks.emplace_back([&,i] { evaluate(i); });
        pool_->parallel(tasks);
    }
    std::vector<ScatterPoint> all;
    for(std::size_t i=0;i<results.size();++i) {
        if(failures[i]) std::rethrow_exception(failures[i]);
        all.insert(all.end(),std::make_move_iterator(results[i].begin()),std::make_move_iterator(results[i].end()));
    }
    return all;
}

} // namespace vsgraysim
