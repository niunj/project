/**
 * Simulation.h
 *
 * Top-level coordinator that wires together all six layers of the
 * multi-spectral simulation.  Implements the per-frame loop described in
 * section 4.2.2 of the design document:
 *
 *   1. Scene dynamic update
 *   2. Multi-sensor transmit scheduling
 *   3. Vulkan RT ray tracing (atmospheric integration + crosstalk detection)
 *   4. Data directed dispatch
 *   5. CUDA signal processing (atmospheric coupling + crosstalk processing)
 *   6. Visualisation & data storage
 *   7. Time-step sync
 *
 * In addition to the original `run()` loop, the Simulation class now exposes
 * a set of single-shot methods driven by the enhanced CLI in `main.cpp`:
 *
 *   - listSensors()       : print a tabular summary of every sensor & route
 *   - validateSensors()   : sanity-check tx params, coupling, band_match, ...
 *   - traceSensor(id, n)   : run RT for ONE sensor only and dump its stats
 *   - runHeadless(n)       : full pipeline without VSG viewer (pure sim)
 *   - run()                : full pipeline with VSG viewer (default)
 */

#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <map>
#include "vsgraysim/layer0_phys/PhysParamManager.h"
#include "vsgraysim/layer0_phys/AtmosphereModel.h"
#include "vsgraysim/layer0_phys/CrossInductionRules.h"
#include "vsgraysim/layer0_phys/PhysicsEngine.h"
#include "vsgraysim/layer1_scene/SceneBuilder.h"
#include "vsgraysim/layer2_rt/AtmosphereIntegrator.h"
#include "vsgraysim/layer2_rt/CrossInductionDetector.h"
#include "vsgraysim/layer2_rt/RayTracingPipelineWrap.h"
#include "vsgraysim/layer2_rt/RayTracer.h"
#include "vsgraysim/layer3_cuda/SignalProcessor.h"
#include "vsgraysim/layer3b_signal/SignalFrame.h"
#include "vsgraysim/layer3b_signal/SensorSignalProcessor.h"
#include "vsgraysim/layer4_sched/SensorScheduler.h"
#include "vsgraysim/layer4_sched/DataDispatcher.h"
#include "vsgraysim/layer5_vis/Visualizer.h"
#include "vsgraysim/earth/EarthDataService.h"

namespace vsgraysim {
class SimulationGpuAdapter;

struct SimulationConfig {
    std::string object_config_path     = "config/tank_multispectral.json";
    std::string atmosphere_config_path = "config/atmosphere_layers.json";
    std::string sensor_config_path     = "config/sensor_rich_set.json";
    std::string shader_dir             = "shaders";
    int         window_width           = 1280;
    int         window_height          = 720;
    double      frame_dt               = 1.0 / 60.0;
    double      physics_step           = 1.0 / 120.0;
    double      history_seconds        = 10.0;
    std::string epoch_utc              = "2026-09-10T04:00:00Z";
    float       total_duration         = 0.0f;          // 0 = infinite
    std::string weather_scenario       = "clear";
    float       weather_severity       = 1.0f;

    // ---- CLI-driven extras ----
    bool                headless          = false; ///< skip VSG viewer
    std::optional<uint32_t> only_sensor_id;       ///< filter schedule to one sensor
    std::string         log_level         = "info"; ///< trace|debug|info|warn|error
    std::string         log_file;                  ///< optional log file path
    std::size_t         max_frames        = 0;    ///< 0 = unlimited
    bool                per_frame_detail   = false;///< verbose per-sensor frame log
    bool                use_gpu_rt        = true; ///< use GPU RT pipeline vs CPU reference
    std::string         sensor_backend = "auto"; ///< auto | cpu | gpu (strict)
    bool                gpu_validation = false;
};

class Simulation {
public:
    Simulation();
    ~Simulation();

    /// Initialise all layers from the given config.
    /// When `cfg.headless` is true the VSG viewer (Layer 5) is skipped.
    bool init(const SimulationConfig& cfg);

    /// Initialise the whole pipeline from a scenario file (the disk
    /// contract produced by the scene designer, see
    /// workspace/scenes/*.scenario.json).  The scenario JSON carries
    /// objects (with optional face_groups), sensors, atmosphere and
    /// simulation settings; `terrain_ref` may point at a terrain project
    /// whose height-field is loaded as the ground.
    /// Always headless: the GUI owns rendering.
    bool initFromScenario(const std::string& scenario_path);

    /// Run the simulation loop until `total_duration` elapses or the
    /// viewer closes.  Returns the number of frames simulated.
    std::size_t run();

    /// Run `n_frames` frames without the VSG viewer.  Returns frame count.
    std::size_t runHeadless(std::size_t n_frames);

    /// Stop the simulation loop.
    void stop();

    /// Execute one frame manually (useful for unit tests / headless mode).
    bool step(double dt, std::optional<double> authoritative_time = {});

    /// Generate the four-band (VIS / IR / RADAR / SAR) sensor preview
    /// images via the Layer-3 CUDA processor.  Each entry in `out_images`
    /// is resized to `width * height * 4` floats (RGBA per pixel) and
    /// indexed by `BandChannel` (CH_VIS=0, CH_IR=1, CH_RADAR=2, CH_SAR=3).
    /// Returns false if the simulation is not initialised.
    bool generateBandImages(uint32_t width, uint32_t height,
                            std::vector<float> out_images[4]);

    // -------- single-shot analytical helpers --------
    /// Print a table of every sensor and every cross-route.
    void listSensors();

    /// Run parameter sanity checks across all sensors & routes.
    /// Returns the number of issues found.
    std::size_t validateSensors();

    /// Run the RT pipeline for ONE sensor only, for `n_frames` frames,
    /// and dump per-frame statistics (schedule, scatter, atm, crosstalk).
    /// Returns false if the sensor_id is unknown.
    bool traceSensor(uint32_t sensor_id, std::size_t n_frames);

    // -------- Rigid-body physics (Bullet3 + vsgBullet) --------
    /// Mount a sensor on a physics object: every frame the aperture
    /// position/direction are derived from the body pose + local_offset.
    void mountSensorOnObject(uint32_t sensor_id, const std::string& object_id,
                             const vsg::dvec3& local_offset = {0.0, 0.0, 0.0},
                             const vsg::dvec3& local_direction = {0.0, 0.0, -1.0});
    /// Access the physics engine (create/step/query rigid bodies).
    PhysicsEnginePtr physics() const { return physics_; }
    // Called between completed steps under the same owner lock as step().
    /// Owner-thread streaming request. Only step() installs a prepared window.
    void requestEarthRegion(double latitude,double longitude,int lod);
    void selectEarthRegion(double latitude,double longitude,int lod);
    void setEarthViewPreparation(earth::PrepareSurfaceView prepare) { prepare_earth_view_=std::move(prepare); }
    void setEarthCommitHandler(std::function<void(earth::EarthSurfaceSnapshotPtr)> handler) {
        earth_commit_handler_=std::move(handler);
    }
    double earthValidFrom() const { return earth_valid_from_; }
    earth::EarthSurfaceSnapshotPtr earthSurface() const { return earth_surface_; }
    /// One-shot, host-validated binding for scenes without an explicit dataset.
    /// Explicit scene earth/terrain_ref takes priority. Set only while stopped.
    void setEarthSurfaceOverride(earth::EarthSurfaceSnapshotPtr snapshot);
    const SimulationConfig& config() const { return cfg_; }
    bool gpuSensorBackendActive() const { return bool(gpu_sensor_); }
    /// Host-owned device, configured with GpuContext::configureDevice before
    /// creation. Set only while the simulation is stopped, before initialization.
    void setSensorDevice(vsg::Device* device);
    vsg::Device* sensorDevice() const { return sensor_device_; }
    struct SensorPose {
        double time = 0;
        vsg::dvec3 position{}, direction{0,0,-1}, up{0,1,0}, velocity{}, angular_velocity{};
    };
    bool sampleSensorPose(uint32_t id, double time, SensorPose& out) const;
    /// Strict Earth runs reject aperture/frustum footprints beyond the pinned
    /// ROI rather than interpreting unavailable ground as valid empty space.
    void validateEarthMeasurement(const SensorPose& pose,double fov_x_degrees,
                                  double fov_y_degrees,double range_m) const;

    // -------- Layer 3b: signal-level data access --------
    /// Latest signal frame (RD map) for the given sensor, or nullptr if
    /// the sensor has not produced a frame yet / is not signal-enabled.
    const SignalFrame* lastFrame(uint32_t sensor_id) const;

    /// Whether the layer-3b signal processing pipeline is active.
    bool signalEnabled() const { return signal_enabled_; }

    /// All latest signal frames, keyed by sensor id (read-only view
    /// for GUI overlays; frame contents are replaced each step).
    const std::map<uint32_t, SignalFrame>& lastFrames() const { return last_frames_; }
    /// All completed CPU measurements in this step, in timestamp order. The
    /// latest-frame map is only a display cache, never a delivery queue.
    const std::vector<SignalFrame>& completedFrames() const { return completed_frames_; }

    /// 本帧新产生非空帧的传感器集合 (分发侧据此决定是否外发/录制:
    /// CPI 窗口预热、帧率节流空窗期未产新帧时不重发旧帧)。
    const std::set<uint32_t>& freshFrameSensors() const { return fresh_frame_sids_; }

    // -------- Layer 3b GPU 成帧 (零拷贝数据流) --------
    /// GPU 驻留帧描述: 帧数据本体留在 SignalProcessor 的每传感器暂存显存,
    /// 分发侧经 device_ptr 做 D2D 拷贝进通道 GpuBuffer, 全程不落主机。
    struct GpuFormedFrame {
        SensorType type = SensorType::VIS;
        uint32_t width = 0, height = 0;
        const void* dev_ptr = nullptr;  ///< 下一帧同名传感器成帧前有效
        std::size_t bytes = 0;
        bool complex = false;           ///< RADAR/SAR 复数, VIS/IR 实数
        double  sim_time = 0.0;
        uint64_t frame_id = 0;
        uint32_t coherent_pulses = 0;
        float   prf = 0.0f;
        float   carrier_hz = 0.0f;
        float   range_min = 0.0f, range_max = 0.0f;
    };
    /// 本步在 GPU 上成帧的传感器 (数据驻留显存)。
    const std::map<uint32_t, GpuFormedFrame>& gpuFrames() const { return gpu_frames_; }

    /// 下载某传感器最近一次 GPU 成帧结果为复数样本 (仅供可视化/录制等
    /// 必须落在主机的消费方; 帧本体小, 与散射回传无关)。
    bool downloadFrameSamples(uint32_t sensor_id,
                              std::vector<std::complex<float>>& out) const;

    // -------- Cross-process link gate (BrokerNet 发现面驱动) --------
    /// 传感器产帧门控: enabled 时仅对 sids 集合内的传感器执行 RT 散射与
    /// 信号成帧 (其余跳过, 既不算数据也不传输)。由显控建链/断链驱动:
    /// 发现面没有链接时仿真侧不产帧, 节省 RT/CUDA 算力。线程安全。
    void setSensorGate(bool enabled, std::set<uint32_t> sids);

private:
    SimulationConfig cfg_;
    PhysParamManagerPtr            phys_;
    AtmosphereModelPtr             atm_;
    CrossInductionRulesPtr         rules_;
    SceneBuilderPtr                scene_;
    PhysicsEnginePtr               physics_;
    std::function<void(earth::EarthSurfaceSnapshotPtr)> earth_commit_handler_;
    double earth_valid_from_=0.;
    earth::PrepareSurfaceView prepare_earth_view_;
    struct TerrainStream;
    struct PreparedRegion;
    std::unique_ptr<TerrainStream> terrain_stream_;
    void updateEarthStream(const std::array<int,3>& demand,const std::array<int,3>& prefetch);
    void commitEarthRegion(PreparedRegion&);
    void followEarthRegion(double dt);
    std::array<double,3> sensorTerrainBounds(uint32_t id,double beamWidth) const;
    earth::EarthSurfaceSnapshotPtr earth_surface_;
    earth::EarthSurfaceSnapshotPtr earth_surface_override_;
    std::unique_ptr<SimulationGpuAdapter> gpu_sensor_;
    vsg::ref_ptr<vsg::Device> sensor_device_;
    nlohmann::json scenario_config_;
    AtmosphereIntegratorPtr        atm_int_;
    CrossInductionDetectorPtr      xdetector_;
    RayTracingPipelineWrapPtr      rt_;
    RayTracerPtr                   ray_tracer_;
    SignalProcessorPtr            cuda_;
    SensorSchedulerPtr            sched_;
    DataDispatcherPtr             disp_;
    VisualizerPtr                vis_;
    bool                          running_       = false;
    bool                          initialised_   = false;
    double                         sim_time_      = 0.0;
    double                         accumulated_time_ = 0.0;
    double                         time_compensation_ = 0.0;
    std::size_t                    frame_count_   = 0;

    // ---- Physics: sensor platform mounts ----
    struct SensorMount {
        std::string object_id;
        vsg::dvec3  local_offset;
        vsg::dvec3  local_direction;
    };
    std::map<uint32_t, SensorMount> sensor_mounts_;

    // ---- 场景契约的逐装备信号参数 (sensors[].signal_params) ----
    /// 按装备定制的信号形成参数; 未设置的字段沿用全局 signal_cfg_ 默认值。
    struct SensorSignalParams {
        std::optional<uint32_t> range_gates;      ///< 雷达距离门数
        std::optional<uint32_t> doppler_bins;     ///< 雷达多普勒门数
        std::optional<float>    chirp_bandwidth;  ///< chirp 带宽 (Hz)
        std::optional<float>    chirp_duration;   ///< chirp 时宽 (s)
        std::optional<float>    fs;               ///< 采样率 (Hz)
        std::optional<uint32_t> image_width;      ///< IR/VIS 图像宽 (px)
        std::optional<uint32_t> image_height;     ///< IR/VIS 图像高 (px)
        std::optional<float>    integration_time; ///< 红外积分时间 (s)
        std::optional<float>    netd;             ///< 红外噪声等效温差 (K)
        double frame_interval_s = 0.0;            ///< >0: 独立帧序 (如红外), 0 = 跟随主时钟
    };
    std::map<uint32_t, SensorSignalParams> sensor_signal_params_;

    // ---- CPU fallback cache for generateBandImages() ----
    std::vector<ScatterPoint>     last_scatter_points_;
    bool                          cuda_usable_   = true;

    // ---- Cross-process link gate (线程安全, BrokerNet 驱动) ----
    mutable std::mutex            gate_mtx_;
    bool                          sensor_gate_enabled_ = false;
    std::set<uint32_t>            sensor_gate_;

    // ---- Layer 3b: Signal formation (signal-level processing) ----
    SignalFormationConfig                              signal_cfg_;
    SignalFormationConfig                              signal_cfg_base_;  ///< 逐装备覆盖前的基线快照
    std::map<uint32_t, std::unique_ptr<SensorSignalProcessor>> signal_processors_;
    std::map<uint32_t, SignalFrame>                    last_frames_;
    std::vector<SignalFrame> completed_frames_;
    void completeFrame(SignalFrame frame);
    std::set<uint32_t>                                 fresh_frame_sids_;  ///< 本帧新产非空帧的传感器 (每步重置)
    bool                                               signal_enabled_ = true;

    // ---- Layer 3b GPU 成帧状态 ----
    std::map<uint32_t, GpuFormedFrame>                 gpu_frames_;        ///< 本步 GPU 驻留帧 (每步重置)
    std::size_t                                        gpu_scatter_valid_ = 0; ///< GPU 统计: 有效散射点数
    std::size_t                                        gpu_scatter_cross_ = 0; ///< GPU 统计: 串扰点数

    SensorSignalProcessor* getOrCreateSignalProcessor(uint32_t sensor_id, SensorType type);

    /// GPU 成帧: 对 schedule 内每个门控内传感器在设备散射缓冲上直接成帧,
    /// 成功时填充 gpu_frames_/last_frames_(仅元数据)/fresh_frame_sids_。
    bool formFramesOnGpu(const std::vector<ScheduledTx>& schedule,
                         double dt, bool gate_on,
                         const std::function<bool(uint32_t)>& gate_allows);

    /// 把场景契约的逐装备信号参数叠加到全局 signal_cfg_ (每帧成帧前调用)。
    void applySensorSignalParams(uint32_t sensor_id);

    /// Update apertures of sensors mounted on physics platforms from the
    /// current rigid-body pose (called after each physics step).
    void syncMountedSensors();

    // ---- internal helpers shared by init() / initFromScenario() ----
    void resetLayer0();
    bool initRemainingLayers();
    bool initGpuSensors();
};

} // namespace vsgraysim
