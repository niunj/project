#pragma once
#include <vsg/core/Array.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Switch.h>
#include <vsg/state/ImageInfo.h>
#include <vsg/state/DescriptorSet.h>
#include <rocky_env/SparseCloudVolume.h>
#include <rocky_env/GpuVoxelCloud.h>
#include <cstdint>
#include <memory>
#include <vector>
#include <array>

namespace vsg { class Device; class Command; class Camera; }

namespace rockyenv {
class CloudTemporal;
class WeatherReflection;
class CloudLightCache;
class CloudLightField;
class Precipitation;
// Share one device-local liquid-water image and its ECEF chart with every
// consumer. Create before compiling the scene; never bind across devices.
struct CloudGpuResources {
    std::shared_ptr<GpuVoxelCloud> field;
    vsg::ref_ptr<vsg::ImageInfo> image;
    vsg::ref_ptr<vsg::vec4Array> layout;
    vsg::ref_ptr<vsg::vec4Array> weather;
    vsg::Device* ownerDevice=nullptr;
    vsg::ref_ptr<vsg::ImageInfo> reflection;
    vsg::ref_ptr<vsg::vec4Array> reflectionState;
};
std::shared_ptr<CloudGpuResources> createCloudGpuResources(vsg::Device* device=nullptr);
struct CloudSceneBinding {
    vsg::ref_ptr<vsg::DescriptorSetLayout> layout;
    vsg::ref_ptr<vsg::DescriptorSet> descriptors;
    std::shared_ptr<CloudGpuResources> resources;
};
/// Build once for a sensor scene, before compilation, on the same device as
/// the cloud. localToECEF explicitly maps that scene's metre coordinates.
std::shared_ptr<CloudSceneBinding> createCloudSceneBinding(
    std::shared_ptr<CloudGpuResources>,const vsg::dmat4& localToECEF);
// Native Earth environment owned by rocky_env. Heights are ellipsoidal metres,
// time is simulation seconds, and both weather textures are three-dimensional.
struct VoxelWeatherFrame {
    vsg::dvec3 eye{}, windAxis{0,0,1};
    vsg::vec3 sun{0,0,1},moon{0,0,-1};
    float moonPhase=0,exposure=1,turbidity=1;
    double seconds=0;
    float windSpeed=0,cover=0,rain=0,cloudBase=500,cloudHeight=1600,seaLevel=0;
    float snow=0,wetness=0,snowCover=0,precipitationAltitude=0;
    float overcastAmount=-1; // negative keeps the legacy boolean control
    vsg::dvec3 windTravel{}; // optional ECEF integrated airflow displacement
    bool hasWindTravel=false;
    float cloudRadius=3000,seaRadius=4000,detail=0,waves=1,period=6;
    float opticalDepth=10.f,asymmetry=.76f;
    bool clouds=true,ocean=true;
    // ECEF centre of a regional weather chart. Zero selects observer-managed
    // residency for callers without a stable scene region.
    vsg::dvec3 cloudAnchor{};
    bool denseRegionalClouds=false;
    // Optional finite regional field of procedural cloud volumes.
    bool cloudObjects=false;
    bool displayAtmosphere=true;
    bool overcast=false;
    float fogVisibility=0,fogHeight=180;
    vsg::vec3 fogLight{.60f,.72f,.83f};
};

struct CloudVoxelLayout {
    uint32_t macroWidth=1024,macroHeight=512,macroDepth=40;
    uint32_t detailSide=160;
    float broadPeriodMetres=76800.f;
    float nearPeriodMetres=5120.f;
    float nearVoxelMetres=32.f;
};

CloudVoxelLayout planetCloudVoxelLayout();
vsg::ref_ptr<vsg::Data> planetCloudTexture();
vsg::ref_ptr<vsg::Data> planetCloudDetailTexture();
vsg::ref_ptr<vsg::Data> planetSparseCloudPageTable();
vsg::ref_ptr<vsg::Data> planetSparseCloudAtlas();
vsg::ref_ptr<vsg::Data> planetSparseCloudParameters();
vsg::vec4 planetAdvection(const VoxelWeatherFrame&);
float localWeatherDetail(double altitude);
float cloudFraction(const VoxelWeatherFrame&,const vsg::dvec3& geodeticNormal);
float cloudDensity(const VoxelWeatherFrame&,const vsg::dvec3& ecef);
float cloudTransmittance(const VoxelWeatherFrame&,const vsg::dvec3& origin,
                         const vsg::dvec3& direction,double distance,unsigned samples=16);

// One ECEF environment pass. The sea and cloud children retain separate UI
// visibility controls while sharing time, light and weather.
class VoxelWeather {
public:
    explicit VoxelWeather(vsg::Device* device=nullptr,
                          std::shared_ptr<CloudGpuResources> cloud={});
    ~VoxelWeather();
    void update(const VoxelWeatherFrame&);
    bool updateTemporal(vsg::Camera*,uint32_t width,uint32_t height,bool active=true);
    vsg::Node* temporalCommands()const;
    vsg::vec4 temporalState()const; // history eligibility, phase, raw width, output width
    vsg::Command* gpuInitializationCommands() const;
    vsg::Command* gpuStepCommands() const;
    bool gpuStepRequired() const;
    std::shared_ptr<CloudGpuResources> cloudResources() const {return cloudResources_;}
    vsg::ref_ptr<vsg::Group> node;

    /// Aircraft/rotor/engines interacting with the local GPU cloud field.
    /// Positions/velocities are anchor-ENU metres; consumed by the next
    /// gpuStepCommands() execution. Up to 32 bodies (see GpuVoxelCloud).
    void setColliders(std::vector<GpuCloudCollider> bodies) { colliders_=std::move(bodies); }
    const std::vector<GpuCloudCollider>& colliders() const { return colliders_; }
    /// Destructive impulses (explosions, plumes, blasts) applied to the local
    /// GPU cloud field with energy: they evaporate condensate, heat air and
    /// push a radial shock. Consumed by the next gpuStepCommands() execution.
    void addImpulse(GpuCloudImpulse impulse) {
        impulses_.push_back(impulse);
        if(impulses_.size()>GpuVoxelCloud::maxImpulses())
            impulses_.erase(impulses_.begin());
    }
    void clearImpulses() { impulses_.clear(); }
    const std::vector<GpuCloudImpulse>& impulses() const { return impulses_; }
private:
    std::unique_ptr<CloudTemporal> temporal_;
    std::unique_ptr<WeatherReflection> reflection_;
    std::unique_ptr<CloudLightCache> lightCache_;
    std::unique_ptr<CloudLightField> lightField_;
    std::array<vsg::ref_ptr<vsg::Switch>,3> lightingDraws_;
    std::unique_ptr<Precipitation> precipitation_;
    vsg::ref_ptr<vsg::Switch> environmentCommands_;
    VoxelWeatherFrame lastFrame_;
    struct GpuState;
    std::unique_ptr<GpuState> gpu_;
    std::shared_ptr<CloudGpuResources> cloudResources_;
    std::vector<GpuCloudCollider> colliders_;
    std::vector<GpuCloudImpulse> impulses_;
    vsg::ref_ptr<vsg::vec4Array> parameters_;
};
}
