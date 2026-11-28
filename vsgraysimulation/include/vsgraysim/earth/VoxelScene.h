#pragma once
#include <vsgraysim/earth/EarthSurface.h>
#include <vsgraysim/earth/SceneWeather.h>
#include <vsg/nodes/Group.h>
#include <vsg/maths/box.h>
#include <vsg/maths/quat.h>
#include <vsg/nodes/MatrixTransform.h>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <set>
#include <future>
#include <atomic>

namespace vsgraysim::earth {
class VoxelTerrain;
class VoxelTerrainPhysics;
class VoxelEarthPatch;
class VoxelOcean;
class VoxelSensorMaterials;
class SurfaceClusterDisplay;
struct VoxelBodySettings {
    std::string motion="prescribed"; // prescribed / static / dynamic
    bool fracture=false,anchorAll=false;
    double fractureEnergyPerArea=1000.; // J/m² per face bond
    double friction=.6,restitution=.1,linearDamping=.02,angularDamping=.02;
    double ballastMassKg=0; // optional explicit point mass, separate from occupied material
    vsg::dvec3 ballastPosition{};
    double crushRadius=0., crushYieldSpeed=4., crushMaxStrain=.65;
};
struct VoxelRotorDrive {
    std::string carrier;
    vsg::dvec3 pivot{}; // carrier-local metres, with the rotor mesh centred at its hub
    vsg::dvec3 axis{0.,1.,0.};
    double rpm=0.,phaseDeg=0.;
    double startSeconds=0.,spinUpSeconds=0.;
    bool waterJet=false;
    double rpmAt(double seconds) const;
    double angleAt(double seconds) const; // radians
};
struct VoxelPoseKeyframe {
    double seconds;
    vsg::dvec3 position;
    vsg::dquat rotation;
};
enum class VoxelMotionResponse { None, Waves, Gusts };
struct VoxelSceneObject {
    std::string id,role;
    std::shared_ptr<const VoxelTerrain> volume;
    std::shared_ptr<const VoxelTerrainPhysics> collision;
    std::shared_ptr<const VoxelEarthPatch> earth;
    EarthSurfaceSnapshotPtr geometry;
    std::string terrainMode="voxel"; // hd (static clipped DEM) / voxel
    vsg::dmat4 basePose;
    vsg::dvec3 velocity{};
    vsg::dvec3 angularVelocity{}; // world radians/s; velocity is model-origin velocity
    VoxelBodySettings body;
    std::optional<VoxelRotorDrive> rotor;
    std::shared_ptr<const std::vector<VoxelPoseKeyframe>> poseTrack;
    VoxelMotionResponse motionResponse=VoxelMotionResponse::None;
    std::set<uint32_t> anchors;
    std::set<uint64_t> brokenBonds; // cell*3 + positive axis
    std::string parentId;
    uint64_t motionOriginTick=0;
    uint64_t crushedAtTick=0;
    nlohmann::json material,provenance;
    std::shared_ptr<const VoxelSensorMaterials> sensorMaterials;
    vsg::dmat4 pose(uint64_t tick,double fixedStep) const;
    double massKg() const;
    vsg::dbox localBounds() const;
    vsg::dvec3 localCentreOfMass() const;
    const EarthSurfaceSnapshot& surface() const;
};
struct VoxelContact {
    std::string a,b;
    vsg::dvec3 pointA{},pointB{},normalOnB{};
    double impulseNs=0,penetrationM=0;
};
/// Visit changed column spans and their geometry neighbors for the same static
/// Earth patch. Returns false if the caller must rebuild all derived data.
bool visitVoxelEarthChangedColumns(const VoxelSceneObject& before,const VoxelSceneObject& after,
                                  const std::function<void(const vsg::dbox&)>& visit);
struct VoxelSceneSnapshot {
    std::shared_ptr<const VoxelOcean> ocean;
    uint64_t revision=0,contentRevision=0,tick=0;
    double fixedStep=0.02;
    simcore::geo::GeoAnchor anchor;
    nlohmann::json utcOrigin={{"year",2026},{"month",9},{"day",10},{"hours_utc",4.}};
    SceneWeather weather;
    std::map<std::string,VoxelSceneObject> objects;
    vsg::dvec3 gravity{0,-9.81,0};
    uint64_t nextFragment=1;
    std::vector<VoxelContact> contacts;
    nlohmann::json journal=nlohmann::json::array();
    nlohmann::json loadDiagnostics=nlohmann::json::array(); // transient preparation results, not physical/checkpoint state
    nlohmann::json scenePresentation; // transient read-only broker data, bound to tick and revision
    double sampleTime=-1.; // transient interpolated measurement state, never a checkpoint
    double timeSeconds() const {return sampleTime>=0?sampleTime:double(tick)*fixedStep;}
};
using VoxelSceneSnapshotPtr=std::shared_ptr<const VoxelSceneSnapshot>;
// Read-only trajectory access also used across a process boundary. Sampling
// retains the exact existing interpolation; no consumer integrates physics.
VoxelSceneSnapshotPtr sampleVoxelSceneHistory(const std::vector<VoxelSceneSnapshotPtr>&,double);
nlohmann::json encodeVoxelSceneSnapshot(VoxelSceneSnapshotPtr);
struct VoxelSceneReader {
    std::function<VoxelSceneSnapshotPtr()> snapshot;
    std::function<VoxelSceneSnapshotPtr(double)> sample;
    std::function<std::vector<VoxelSceneSnapshotPtr>()> history;
    std::function<void(const std::string&,std::function<void(VoxelSceneSnapshotPtr)>)> addParticipant;
    std::function<void(const std::string&)> removeParticipant;
};
struct VoxelSceneHit {std::string object;double distance=0;vsg::dvec3 position{},normal{};};

/// Owner-thread edits and stepping; snapshots may be retained/read concurrently.
/// A failed transaction or publication preparation leaves state and events intact.
/// Face-bond fracture and occupied-volume Bullet bodies share this publication.
class VoxelScene {
public:
    static std::unique_ptr<VoxelScene> load(const nlohmann::json&,const std::string& documentPath={});
    struct Preparation {
        std::shared_ptr<std::atomic<bool>> cancellation=std::make_shared<std::atomic<bool>>(false);
        std::future<nlohmann::json> document;
        Preparation()=default;Preparation(Preparation&&)=default;Preparation& operator=(Preparation&&)=delete;
        ~Preparation(){if(cancellation)cancellation->store(true);}
    };
    struct ImportPreparation {
        std::shared_ptr<std::atomic<bool>> cancellation=std::make_shared<std::atomic<bool>>(false);
        std::future<VoxelSceneSnapshotPtr> snapshot;
        ImportPreparation()=default;ImportPreparation(ImportPreparation&&)=default;
        ~ImportPreparation(){if(cancellation)cancellation->store(true);}
    };
    static ImportPreparation prepareImportAsync(const nlohmann::json&,unsigned timeoutMs=300000);
    static Preparation prepareFileAsync(const std::string& path,unsigned timeoutMs=300000);
    static std::unique_ptr<VoxelScene> open(const std::string& path);
    ~VoxelScene();
    VoxelSceneSnapshotPtr snapshot() const;
    VoxelSceneSnapshotPtr sample(double simulationSeconds) const;
    std::vector<VoxelSceneSnapshotPtr> history() const;
    bool paused() const;
    void setPaused(bool);
    /// Paused authoring only. Commands: add/remove/place/material/sensor_materials, target_properties, sensor_paint and terrain_brush.
    void edit(const nlohmann::json& commands,uint64_t expectedRevision,VoxelSceneSnapshotPtr prepared={});
    // Undo a paused authoring transaction using its pre-edit snapshot.
    void restoreAuthored(VoxelSceneSnapshotPtr,uint64_t expectedRevision);
    /// Commit immutable assets prepared off-thread into the existing paused world.
    void importPrepared(VoxelSceneSnapshotPtr,uint64_t expectedRevision);
    /// Future tick: damage, settle, fracture (face-bond plane), or impulse.
    uint64_t schedule(const nlohmann::json& event);
    void cancel(uint64_t sequence);
    struct Advance {
        uint64_t steps=0,pendingSteps=0,events=0,crushedBodies=0;
        double buoyancyMs=0,waterDragMs=0,waterMs=0,crushMs=0;
        double damageCarveMs=0, damageVolumeMs=0, topologyRefreshMs=0, collisionRefreshMs=0;
        double terrainBvhMs=0;
        size_t terrainBvhRebuilt=0,terrainBvhReused=0;
        double terrainMeshMs=0,terrainMeshStitchMs=0;
        size_t terrainMeshRebuilt=0,terrainMeshReused=0;
    };
    Advance advanceTo(double absoluteSimulationSeconds,uint32_t stepBudget=8);
    /// May throw to reject a candidate, e.g. if a render/sensor build fails.
    /// Prepare resources only; mount them after observing snapshot().revision.
    void setPreparePublication(std::function<void(VoxelSceneSnapshotPtr)>);
    void addPublicationParticipant(const std::string& key,std::function<void(VoxelSceneSnapshotPtr)>);
    void removePublicationParticipant(const std::string& key);
    std::optional<VoxelSceneHit> rigidRaycast(const vsg::dvec3&,const vsg::dvec3&,double);
    nlohmann::json checkpoint() const;
    void saveCheckpoint(const std::string& path) const;
    std::optional<VoxelSceneHit> raycast(const vsg::dvec3& origin,const vsg::dvec3& direction,double distance) const;
private:
    VoxelScene();
    struct Impl;std::unique_ptr<Impl> impl_;
};
VoxelSceneReader readVoxelScene(VoxelScene&);
struct VoxelSceneDisplay {
    vsg::ref_ptr<vsg::Group> node;
    std::map<std::string,vsg::ref_ptr<vsg::MatrixTransform>> placements;
    struct Drawing{EarthSurfaceSnapshotPtr source;nlohmann::json material;vsg::ref_ptr<vsg::Node> node;bool externalLighting=false,terrain=false;};
    std::map<std::string,Drawing> drawings;
    std::optional<size_t> contextDrawable;
    std::shared_ptr<SurfaceClusterDisplay> clusters;
    size_t reusedDrawings=0,builtDrawings=0;
    uint64_t contentRevision=0;
    void sync(const VoxelSceneSnapshot&);
};
VoxelSceneDisplay makeVoxelSceneDisplay(const VoxelSceneSnapshot&,const VoxelSceneDisplay* reuse=nullptr,bool externalOceanLighting=false);
std::optional<VoxelSceneHit> raycastVoxelScene(const VoxelSceneSnapshot&,const vsg::dvec3&,const vsg::dvec3&,double,
    bool terrainOnly=false,std::string_view objectId={});
}
