#pragma once
#include <rocky_env/DynamicScene.h>
#include <rocky_env/SharedSceneResources.h>
#include <rocky_env/SensorApi.h>
#include <rocky_env/RadarChain.h>
#include <rocky_env/VoxelWeather.h>
#include <simcore/SensorRuntime.h>
#include <functional>
#include <optional>
#include <memory>
namespace rockyenv::sensor {
struct GpuSensorSpec {
    uint32_t id=0;
    SensorProfile profile;
    double period_seconds=1.0/60.0;
    std::optional<RadarChainParams> radar;
    bool echo_only=false; // SAR acquisition: raw echo, no fabricated focused image
    ProductStage output=ProductStage::Image;
};
struct GpuAperture { vsg::dvec3 position{},forward{0,0,-1},up{0,1,0}; };
struct GpuMeasurementProduct {
    uint32_t sensor_id=0,width=0,height=0;
    uint64_t frame_id=0;
    double start_time=0,end_time=0;
    ProductStage stage=ProductStage::Image;
    bool coherent=false,echo_only=false;
    double sample_rate_hz=0,prf=0,carrier_hz=0,range_min_m=0;
    std::vector<float> rgba;
    std::vector<std::complex<float>> iq;
    std::vector<vsg::vec4> terrain_footprint; // scene-local delta from center pulse, relative power
};
/// Device-owned offscreen backend for all registered sensor instances. Each
/// measurement/pulse updates scene truth and executes its own shader resources.
/// Host products own their data; completion fences precede every reuse/readback.
class GpuSensorRuntime {
public:
    using Truth=std::function<void(uint32_t,double,GpuAperture&,std::vector<vsg::dmat4>&,std::vector<bool>&)>;
    using Environment=std::function<EnvAnswer(uint32_t,double,const BandSpec&)>;
    using Sink=std::function<void(GpuMeasurementProduct&&)>;
    GpuSensorRuntime();
    ~GpuSensorRuntime();
    /// Bind a version controlled cloud scene before initialize. The owner
    /// must fence cloud evolution before advancing measurements and provide
    /// the field at their acquisition time; live preview state is not truth.
    void setVoxelCloud(std::shared_ptr<CloudSceneBinding>);
    void initialize(const std::vector<SceneInstance>&,const std::vector<RadarMaterial>&,
                    const std::vector<GpuSensorSpec>&,bool validation=false,vsg::Device* shared_device=nullptr,
                    const SharedSceneResources::TexArraySpec* textures=nullptr);
    using SceneBuilder = std::function<void(GpuSensorRuntime&, const std::vector<SceneInstance>&,
                                             const std::vector<RadarMaterial>&)>;
    SceneBuilder captureSceneBuilder() const;
    using TexturedSceneBuilder=std::function<void(GpuSensorRuntime&,const std::vector<SceneInstance>&,
        const std::vector<RadarMaterial>&,const SharedSceneResources::TexArraySpec*)>;
    TexturedSceneBuilder captureTexturedSceneBuilder() const;
    uint64_t rejectedSceneWindows()const{return rejected_scene_windows_;}
    void swap(GpuSensorRuntime& other) noexcept { impl_.swap(other.impl_); }
    void replaceScene(const std::vector<SceneInstance>&,const std::vector<RadarMaterial>&,
                      const SharedSceneResources::TexArraySpec* textures=nullptr);
    void advanceTo(double time,const Truth&,const Environment&,const Sink&,
                   const std::function<bool(uint32_t)>& enabled={}, double sceneValidFrom=0);
    const std::string& deviceName() const;
    bool hardwareRayTracing() const;
    struct Statistics {
        uint64_t graphSubmissions=0,readbackBatches=0,measurements=0;
        uint64_t stationaryCpis=0,dynamicCpis=0,multiEquipmentBatches=0;
        uint32_t maxBatchSize=0,lastProducts=0;
        bool batching=true;
    };
    const Statistics& statistics()const{return statistics_;}
private:
    Statistics statistics_;
    uint64_t rejected_scene_windows_=0;
    simcore::SensorRuntime clock_; // Owner-thread cadence survives scene replacement.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
