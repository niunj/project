#pragma once

#include <memory>
#include <vector>
#include <random>
#include "vsgraysim/core/RayPayload.h"
#include "vsgraysim/core/SensorTypes.h"
#include "vsgraysim/core/GeoUnitPhysParams.h"
#include "vsgraysim/layer0_phys/AtmosphereModel.h"
#include "vsgraysim/layer0_phys/CrossInductionRules.h"
#include "vsgraysim/layer1_scene/SceneBuilder.h"
#include "vsgraysim/layer2_rt/SensorProcessor.h"
#include "vsgraysim/layer0_phys/PhysParamManager.h"
#include <simcore/WorldState.h>

namespace vsgraysim {

class PhysicsEngine;   // Layer 0: Bullet 物理世界 (BVH 三角网格射线求交加速结构)

class RayTracer; namespace detail { class WorkerPool; class SceneBVH; }

struct RayTraceResult {
    bool hit = false;
    Vec3 hit_position{};
    Vec3 hit_normal{};
    float distance = 0.0f;
    float atm_attenuation = 1.0f;
    float atm_scatter_power = 0.0f;
    float atm_refraction = 0.0f;
    uint32_t hit_geo_unit_id = 0;
    uint32_t hit_object_id = 0;
    std::string object_name;
    uint32_t primitive_id = 0;
};

struct RayTraceConfig {
    uint32_t sensor_id = 0;
    SensorType sensor_type = SensorType::VIS;
    Vec3 origin{};
    Vec3 direction{};
    float tx_power = 0.0f;
    float beam_width = 0.0f;
    float wavelength = 0.0f;
    BandMask band_mask = BAND_VIS;
    uint32_t num_rays = 100;
    float max_distance = 10000.0f;
    float scan_angle_az = 0.0f;
    float scan_angle_el = 0.0f;
    // 脉冲级时序 (由发射调度传入, 写入散射点供信号成帧按序消费)
    uint32_t pulse_seq = 0;         ///< 帧内脉冲序号 (0-based)
    float    pulse_slow_time = 0.0f; ///< 帧内慢时间 (s, fire_time)
    std::optional<double> measurement_time; ///< historical world truth; absent = latest snapshot
    uint64_t sample_index = 0; ///< stable per-sensor event index for deterministic ray sampling
};

class RayTracer {
public:
    RayTracer(AtmosphereModelPtr atm,
              std::shared_ptr<SceneBuilder> scene,
              CrossInductionRulesPtr rules);
    ~RayTracer();

    std::vector<ScatterPoint> traceSensor(const RayTraceConfig& config);
    std::vector<ScatterPoint> traceFrame(const std::vector<RayTraceConfig>& configs);

    void setRaysPerSensor(uint32_t n) { rays_per_sensor_ = n; }
    uint32_t raysPerSensor() const { return rays_per_sensor_; }

    void setAtmosphereEnabled(bool e) { atm_enabled_ = e; }
    bool atmosphereEnabled() const { return atm_enabled_; }

    void setSceneIntersectionEnabled(bool e) { scene_intersect_enabled_ = e; }
    bool sceneIntersectionEnabled() const { return scene_intersect_enabled_; }

    /// 接入物理引擎: 首次追踪时从物理世界提取真实几何三角形, 在仿真侧自建
    /// Per-body local BVHs with immutable per-measurement poses; worker queries are read-only.
    /// No physics/geometry means empty space; no Bullet latest-pose fallback.
    void setPhysicsEngine(PhysicsEngine* phys);
    void setPhysicalParameters(PhysParamManagerPtr parameters) { parameters_=std::move(parameters); }

private:
    /// 刷新场景求交加速结构 (仿真线程, 并行分发前调用):
    /// Retain one local BVH per stable body slot; poses are per measurement.
    void refreshSceneBVH();
    struct TraceScene;
    std::shared_ptr<const TraceScene> captureScene(const RayTraceConfig& config) const;
    std::vector<ScatterPoint> traceSensorInternal(const RayTraceConfig& config,
                                                  std::mt19937& rng,const TraceScene& scene);

    RayTraceResult traceRay(const Ray& ray, const RayTraceConfig& config,const TraceScene& scene);

    void integrateAtmosphere(const Vec3& origin, const Vec3& dir,
                            float distance, BandChannel ch,
                            float& attenuation, float& scatter_power,
                            float& refraction);

    bool intersectScene(const Vec3& origin, const Vec3& dir,
                       float max_distance, RayTraceResult& result,const TraceScene& scene);

    ScatterPoint makeScatterPoint(const RayTraceResult& result,
                                  const RayTraceConfig& config,
                                  const Ray& ray,
                                  float amplitude) const;

    AtmosphereModelPtr atm_;
    std::shared_ptr<SceneBuilder> scene_;
    CrossInductionRulesPtr rules_;
    PhysicsEngine* physics_ = nullptr;
    PhysParamManagerPtr parameters_;

    uint32_t rays_per_sensor_ = 100;
    bool atm_enabled_ = true;
    bool scene_intersect_enabled_ = true;

    std::unique_ptr<detail::WorkerPool> pool_;  ///< 传感器级并行线程池 (懒创建)

    // Local geometry survives static/kinematic authority changes and late arrivals.
    struct DynBody {
        std::unique_ptr<detail::SceneBVH> bvh;   ///< 刚体局部系三角形树 (建一次)
        Vec3 local_center{};                     ///< 局部包围球心
        float local_radius = 0.0f;               ///< 局部包围球半径
    };
    std::vector<DynBody> dyn_bodies_;            ///< 与物理刚体索引对齐 (可含空项)
    struct DynPose {
        std::size_t body = 0;
        Vec3 center{};                           ///< bounding sphere at this measurement time
        float radius = 0.0f;
        vsg::dmat4 inverse;
        vsg::dquat rotation;
    };
};

using RayTracerPtr = std::shared_ptr<RayTracer>;

} // namespace vsgraysim
