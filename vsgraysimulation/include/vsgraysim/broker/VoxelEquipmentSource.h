#pragma once
#pragma push_macro("slots")
#undef slots
#include <vsgraysim/earth/VoxelEquipmentRuntime.h>
#pragma pop_macro("slots")
#include <vsgraysim/subsystems/ISubsystem.h>
#include <mutex>
#include <map>
#include <chrono>
#include <vsgraysim/broker/FlightControl.h>
#include <condition_variable>
#include <deque>
#include <thread>

namespace vsgraysim {
class VoxelEquipmentProcess;
void applyVoxelInitialPositions(earth::VoxelScene&, const nlohmann::json&);
void applyVoxelModelMaterials(earth::VoxelScene&, const nlohmann::json& bindings);
nlohmann::json voxelSceneDescriptor(earth::VoxelSceneSnapshotPtr,const std::string& archive);
// The existing Earth topic is the handoff document. References are normalized
// before a Save As; an optional checkpoint preserves authored voxel state.
struct VoxelSceneProject {
    std::string path, scenePath, mapPath;
    nlohmann::json document, equipmentDocument, packageManifest;
    static VoxelSceneProject open(const std::string&);
    earth::VoxelEquipmentConfig equipmentConfig() const;
    std::unique_ptr<earth::VoxelScene> createScene() const;
    nlohmann::json saved(const nlohmann::json& checkpoint) const;
};

// Official TimingBus source, not a display-owned demonstration. All methods
// which mutate the scene run under the host's simulation-owner lock.
class VoxelEquipmentSource final : public ISimulationSubsystem {
public:
    explicit VoxelEquipmentSource(VoxelSceneProject,vsg::Device* sharedDevice=nullptr);
    ~VoxelEquipmentSource() override;
    std::string name() const override { return "VoxelEquipmentSource"; }
    bool initialize(const nlohmann::json&) override;
    bool initialize(const nlohmann::json&, const std::function<void(earth::VoxelScene&, VoxelSceneProject&)>& prepare);
    void shutdown() override;
    void onPreTick(const TimeTick&) override {}
    void onTick(const TimeTick&) override;
    void onPostTick(const TimeTick&) override {}
    void onStart() override;
    void onStop() override;
    int tickPriority() const override { return 0; }
    void* getMutableProduct(const std::string&) override { return nullptr; }
    const void* getConstProduct(const std::string&) const override { return nullptr; }
    bool isHealthy() const override;
    nlohmann::json diagnostics() const override;
    earth::VoxelSceneSnapshotPtr snapshot() const;
    nlohmann::json platforms() const;
    nlohmann::json equipmentVisuals(const earth::VoxelSceneSnapshot&) const;
    nlohmann::json presentationState(const earth::VoxelSceneSnapshot&) const;
    double fixedStep() const { return fixedStep_; }
    bool hasDataDemand() const;
    void idleMeasurements();
    void servicePresentationCommands();
    bool hasPresentationControls() const;
    nlohmann::json prescribedControls() const;
    void setPrescribedCourse(uint32_t platformId,double headingDeg,double speedMps);
    // Serialized on the physics owner; existing atomic checkpoint format,
    // including pending events and solver state, is retained without resampling.
    void saveRecovery(const std::string& path);
private:
    void onOwner(std::function<void()>);
    void publishState();
    void advanceSounders(const earth::VoxelSceneSnapshot& state);
    void unregisterSounders();
    struct PresentationState {
        std::shared_ptr<const void> initialVolume;
        vsg::dvec3 position;
        double travel = 0.;
        PlatformPoseDemand pose;
        uint64_t revision = 0, tick = 0;
        std::string owner, lastRequest;
        std::chrono::steady_clock::time_point lease;
    };
    std::map<uint32_t,PresentationState> presentation_;
    FlightControlResult applyPresentation(const FlightControlCommand&);

    VoxelSceneProject project_;
    vsg::ref_ptr<vsg::Device> device_;
    std::unique_ptr<earth::VoxelScene> scene_;
    std::unique_ptr<earth::VoxelEquipmentRuntime> runtime_;
    std::unique_ptr<VoxelEquipmentProcess> measurement_process_;
    earth::VoxelEquipmentConfig equipment_config_;
    std::set<uint32_t> registeredSounders_;
    std::map<uint32_t,uint64_t> soundingTicks_;
    std::string archiveDirectory_, archivePath_;
    uint64_t archivedContent_=0;
    std::weak_ptr<const earth::VoxelSceneSnapshot> archivedState_;
    bool archivedPaused_=true;
    std::deque<std::pair<std::string,uint64_t>> archives_;
    uint64_t archiveBytes_=0;
    double initialTime_=0, fixedStep_=.02;
    mutable std::mutex publicationMutex_;
    earth::VoxelSceneSnapshotPtr published_;
    nlohmann::json platforms_=nlohmann::json::array(), diagnostics_;
    nlohmann::json presentationState_;
    struct AcousticPublication;
    std::unique_ptr<AcousticPublication> acoustics_;
    std::string error_;
    std::mutex taskMutex_;
    std::condition_variable taskWake_;
    std::deque<std::function<void()>> tasks_;
    bool stopping_=false;
    std::thread owner_;
};

// A cache of published geometry plus exact source transforms. Loading a
// checkpoint decodes existing geometry; this consumer never advances physics.
class VoxelSceneReadView {
public:
    earth::VoxelSceneSnapshotPtr update(const nlohmann::json& descriptor);
private:
    std::string archivePath_;
    earth::VoxelSceneSnapshotPtr geometry_;
};

// One bounded latest-value mailbox, not one thread/task per heartbeat. CPU
// checkpoint decoding happens here; callers bind GPU drawables on the UI thread.
class AsyncVoxelSceneReadView {
public:
    AsyncVoxelSceneReadView();
    ~AsyncVoxelSceneReadView();
    void submit(const nlohmann::json&);
    earth::VoxelSceneSnapshotPtr take(nlohmann::json* presentation=nullptr,nlohmann::json* equipmentVisuals=nullptr);
    std::string error() const;
private:
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    nlohmann::json resultPresentation_,resultEquipmentVisuals_;
    nlohmann::json pending_,requested_=nlohmann::json::object();
    earth::VoxelSceneSnapshotPtr result_;
    std::string error_;
    bool stop_=false;
    std::thread worker_;
};
}
