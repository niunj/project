#pragma once
#include <nlohmann/json.hpp>
#include <vsg/maths/vec3.h>
#include <memory>
#include <vector>
namespace simcore::geo {struct GeoAnchor;}
namespace vsgraysim::earth {
struct OceanHarmonic {double amplitude=0,k=0,dx=1,dz=0,omega=0,phase=0,inverseDepthDenominator=1;};
struct OceanSample {double height=0;vsg::dvec3 normal{0,1,0},velocity{},velocityDx{},velocityDy{},velocityDz{};};
struct OceanSurfaceSample {double height=0;vsg::dvec3 normal{0,1,0},hessian{};};
// Compact, advected wave packet emitted by a moving/displacing rigid body.
// This is a dispersive disturbance approximation, not a Navier–Stokes wake.
struct OceanWake {double x=0,z=0,birth=0,amplitude=0,width=1,speed=2,lifetime=8,k=1,dirX=0,dirZ=-1,bodySpeed=0;};
// Deterministic visual spray event from a swept rigid-body water entry.
// Droplets are optical decoration, not additional rigid collision bodies.
struct OceanSplash {double x=0,z=0,birth=0,height=0,speed=0,radius=1,lifetime=2;uint32_t seed=0;};
// Checkpointed optical foam deposits, independent of the short-lived wave
// packets. They never contribute displacement, mass, drag or colliders.
struct OceanFoam {double x=0,z=0,birth=0,length=1,width=1,dirX=0,dirZ=-1,strength=0,lifetime=24;uint32_t seed=0;};
struct VoxelSceneSnapshot;
struct OceanVoxelState {double waterFraction=0,density=0,temperature=0;vsg::dvec3 velocity{},absorption{},scattering{};};
// Immutable fluid volume in the scene's east/up/south chart. Water is a
// continuous occupancy/density field, never millions of solid cube colliders.
class VoxelOcean {
public:
    static std::shared_ptr<const VoxelOcean> load(const nlohmann::json&,const simcore::geo::GeoAnchor* anchor=nullptr,const std::string& documentPath={});
    nlohmann::json encode()const;
    double meanKinkError(double x,double z,double radius)const;
    std::pair<double,double> meanBounds(double minX,double minZ,double maxX,double maxZ)const;
    vsg::dvec3 meanSurface(double x,double z)const; // height, dH/dx, dH/dz
    OceanSample sample(double x,double z,double seconds,double y=1e30)const;
    OceanSurfaceSample surface(double x,double z,double seconds)const;
    double localCurvature(double x,double z,double seconds,double horizontalRadius)const;
    void refreshBounds();
    static void advanceWakes(VoxelSceneSnapshot&,const VoxelSceneSnapshot* previous=nullptr);
    bool contains(double x,double z)const;
    double densityAt(const vsg::dvec3&,double seconds)const;
    OceanVoxelState voxel(int64_t x,int64_t y,int64_t z,double seconds)const;
    vsg::dvec3 transmission(double waterDistance)const;
    bool intersectSurface(const vsg::dvec3& origin,const vsg::dvec3& direction,double seconds,double limit,double& distance)const;
    double seaLevel=0,bottom=-40,halfExtent=4096,cellSize=.5,density=1025,drag=.85,linearDrag=.2,temperature=291;
    double windSpeed=6,waveHeight=.65,waveLength=24,swellHeight=.25,swellLength=80,windAzimuth=70;
    vsg::dvec3 current{.12,0,.04},absorption{.12,.035,.015},scattering{.006,.012,.018};
    double amplitudeBound=0,slopeBound=0,minimumWavelength=1e30;
    double integrationSpan=2,curvatureBound=0;
    double fogVisibility=0,fogHeight=45; // visibility 0 disables added marine fog
    double baseCurvatureBound=0;
    uint32_t referenceSize=0;double referenceX=0,referenceZ=0,referenceStepX=1,referenceStepZ=1,referenceMin=0,referenceMax=0,referenceJumpX=0,referenceJumpZ=0,referenceSlope=0;
    std::vector<float> referenceHeights; // immutable local chart mean sea/geoid datum, no bathymetry
    static constexpr size_t maximumWakes=24;
    std::vector<OceanWake> wakes;
    static constexpr size_t maximumSplashes=16;
    std::vector<OceanSplash> splashes;
    static constexpr size_t maximumFoam=192;
    std::vector<OceanFoam> foam;
    std::vector<OceanHarmonic> harmonics;
    nlohmann::json provenance=nlohmann::json::object();
};
// Per-force-evaluation cache. Cached phase polynomials have |delta phase|<.5;
// height error is bounded by sum(amplitude)*.5^6/720. Wakes remain exact.
class OceanPressureQuery {
public:
    OceanPressureQuery(const VoxelOcean&,double seconds);
    ~OceanPressureQuery();
    OceanSurfaceSample surface(double x,double z);
    OceanSample sample(double x,double z,double y);
    double curvature(double x,double z,double horizontalRadius,const vsg::dvec3* hessian=nullptr)const;
    double amplitudeBound()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};

}
