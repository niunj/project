#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include <vsgraysim/broker/VoxelEquipmentProcess.h>
#include <vsgraysim/broker/BrokerNet.h>
#include <vsgraysim/broker/SimDataBroker.h>
#include <vsgraysim/kernel/TimeTick.h>
#include <vsgraysim/earth/VoxelScenePackage.h>
#include <vsgraysim/earth/ScenePresentation.h>
#include <vsgraysim/earth/SceneAcoustics.h>
#include <simcore/Orientation.h>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <set>
#include <stdexcept>
#include <unistd.h>

namespace vsgraysim {
namespace {
using json=nlohmann::json;
namespace fs=std::filesystem;
json read(const std::string& path, uint64_t budget) {
    if(!fs::is_regular_file(path)||fs::file_size(path)>budget)throw std::invalid_argument("missing or oversized scene resource: "+path);
    std::ifstream input(path);return json::parse(input);
}
std::string resolve(const json& value,const fs::path& directory) {
    auto path=fs::path(value.get<std::string>());if(path.is_relative())path=directory/path;
    path=fs::weakly_canonical(path);if(!fs::is_regular_file(path))throw std::invalid_argument("missing scene resource: "+path.string());return path.string();
}
json matrixJson(const vsg::dmat4& m) {auto out=json::array();for(int c=0;c<4;++c)for(int r=0;r<4;++r)out.push_back(m[c][r]);return out;}
vsg::dvec3 vector(const json& j) {
    if(!j.is_array()||j.size()!=3)throw std::invalid_argument("invalid published velocity");
    vsg::dvec3 out(j[0].get<double>(),j[1].get<double>(),j[2].get<double>());
    if(!std::isfinite(vsg::length(out)))throw std::invalid_argument("nonfinite published velocity");return out;
}
}
json voxelSceneDescriptor(earth::VoxelSceneSnapshotPtr state,const std::string& archive){
    json poses=json::object();for(const auto& [id,o]:state->objects)poses[id]={{"pose",matrixJson(o.pose(state->tick,state->fixedStep))},{"velocity",{o.velocity.x,o.velocity.y,o.velocity.z}},{"angular_velocity",{o.angularVelocity.x,o.angularVelocity.y,o.angularVelocity.z}}};
    return {{"checkpoint_file",archive},{"revision",state->revision},{"content_revision",state->contentRevision},{"tick",state->tick},{"fixed_step_seconds",state->fixedStep},{"objects",poses}};
}
VoxelSceneProject VoxelSceneProject::open(const std::string& path) {
    VoxelSceneProject out;out.path=fs::absolute(path).lexically_normal().string();out.document=read(out.path,256ULL*1024*1024);
    if(out.document.value("format",std::string())==earth::VoxelScenePackage::format){out.packageManifest=earth::VoxelScenePackage::inspect(out.path);out.document=out.packageManifest.at("topic");out.document["scene"]=out.path;out.document["earth_map"]=earth::VoxelScenePackage::resolveResource(out.document.at("earth_map"),out.path);}
    auto& j=out.document;if(j.at("format")!="earth-scene-topic-v1")throw std::invalid_argument("expected an Earth scene topic");
    for(const auto& f:j.items())if(f.key()!="format"&&f.key()!="name"&&f.key()!="scene"&&f.key()!="earth_map"&&f.key()!="sky"&&f.key()!="clouds"&&f.key()!="camera"&&f.key()!="earth"&&f.key()!="paused"&&f.key()!="equipment"&&f.key()!="checkpoint"&&f.key()!="terrain_paging"&&f.key()!="acoustics")throw std::invalid_argument("unknown scene topic field: "+f.key());
    if(j.contains("acoustics"))earth::validateAcousticTopic(j.at("acoustics"));
    if(j.contains("terrain_paging") && j.at("terrain_paging")!="quad-v1")
        throw std::invalid_argument("unsupported scene topic terrain paging layout");
    const auto directory=fs::path(out.path).parent_path();
    if(!out.packageManifest.is_null())out.scenePath=out.path;
    else {auto source=fs::path(j.at("scene").get<std::string>());if(source.is_relative())source=directory/source;out.scenePath=j.contains("checkpoint")&&!fs::is_regular_file(source)?out.path:resolve(j.at("scene"),directory);}
    out.mapPath=resolve(j.at("earth_map"),directory);
    if(fs::file_size(out.scenePath)>256ULL*1024*1024)throw std::invalid_argument("topic source scene exceeds 256 MiB");
    j["scene"]=out.scenePath;j["earth_map"]=out.mapPath;
    if(j.contains("checkpoint")&&j.at("checkpoint").at("format")!="voxel-scene-checkpoint-v1")throw std::invalid_argument("invalid authored voxel checkpoint");
    if(j.contains("equipment")) {
        if(j["equipment"].is_string()) {
            const auto configPath=resolve(j["equipment"],directory);out.equipmentDocument=read(configPath,1024*1024);
            // Resolve the config's old topic before moving the document; the
            // enclosing project intentionally overrides that reference below.
            earth::VoxelEquipmentConfig::parse(out.equipmentDocument,configPath);
        } else out.equipmentDocument=j["equipment"];
        // Embedded devices belong to the enclosing project. Do not persist a
        // stale back-reference which changes on each Save As/reopen.
        out.equipmentDocument.erase("topic");out.equipmentConfig();j["equipment"]=out.equipmentDocument;
    }
    return out;
}
earth::VoxelEquipmentConfig VoxelSceneProject::equipmentConfig() const {
    if(equipmentDocument.is_null()){
        if(document.contains("acoustics")){
            earth::VoxelEquipmentConfig config;
            config.topicPath=path;config.visualOnly=true;
            return config;
        }
        throw std::invalid_argument("scene topic has no equipment configuration");
    }
    auto j=equipmentDocument;j["topic"]=path;return earth::VoxelEquipmentConfig::parse(j,path);
}
std::unique_ptr<earth::VoxelScene> VoxelSceneProject::createScene() const {
    return document.contains("checkpoint")?earth::VoxelScene::load(document.at("checkpoint"),path):earth::VoxelScene::open(scenePath);
}
nlohmann::json VoxelSceneProject::saved(const json& checkpoint) const {
    if(checkpoint.at("format")!="voxel-scene-checkpoint-v1")throw std::invalid_argument("cannot save an invalid voxel checkpoint");
    auto out=document;out["checkpoint"]=checkpoint;out["paused"]=true;
    if(!equipmentDocument.is_null())out["equipment"]=equipmentDocument;return out;
}

void applyVoxelInitialPositions(earth::VoxelScene& scene, const json& positions) {
    if (positions.empty()) return;
    const auto state = scene.snapshot();
    auto edits = json::array();
    for (const auto& [id, coordinates] : positions.items()) {
        const auto found = state->objects.find(id);
        if (found == state->objects.end()) throw std::invalid_argument("unknown layout object: " + id);
        const auto& object = found->second;
        if (object.role != "target" || object.poseTrack || object.rotor || !object.parentId.empty())
            throw std::invalid_argument("object does not accept a layout position: " + id);
        auto pose = object.pose(state->tick, state->fixedStep);
        for (size_t axis = 0; axis < 3; ++axis) pose[3][axis] = coordinates.at(axis).get<double>();
        edits.push_back({{"kind","place"},{"object",id},{"pose",matrixJson(pose)},
                         {"velocity_mps",{object.velocity.x,object.velocity.y,object.velocity.z}}});
    }
    scene.edit(edits, state->revision);
}

void applyVoxelModelMaterials(earth::VoxelScene& scene, const json& bindings) {
    if (bindings.empty()) return;
    const auto state = scene.snapshot();
    auto edits = json::array();
    for (const auto& [id, binding] : bindings.items()) {
        const auto found = state->objects.find(id);
        if (found == state->objects.end() || found->second.role != "target")
            throw std::invalid_argument("invalid model material target: " + id);
        edits.push_back({{"kind","sensor_paint"},{"object",id},{"region",nullptr},{"material",binding.at("material")}});
    }
    scene.edit(edits, state->revision);
}

struct VoxelEquipmentSource::AcousticPublication {
    earth::SceneAcoustics model;
    json readings=json::array();
    std::map<std::string,earth::AcousticMeter> meters;
    double last=-1.;
};

VoxelEquipmentSource::VoxelEquipmentSource(VoxelSceneProject project,vsg::Device* device):project_(std::move(project)),device_(device) {
    owner_=std::thread([this]{for(;;){std::function<void()> task;{std::unique_lock lock(taskMutex_);taskWake_.wait(lock,[this]{return stopping_||!tasks_.empty();});if(stopping_&&tasks_.empty())return;task=std::move(tasks_.front());tasks_.pop_front();}task();}});
}
void VoxelEquipmentSource::onOwner(std::function<void()> action) {
    if(std::this_thread::get_id()==owner_.get_id()){action();return;}
    auto task=std::make_shared<std::packaged_task<void()>>(std::move(action));auto result=task->get_future();
    {std::lock_guard lock(taskMutex_);if(stopping_||tasks_.size()>=8)throw std::runtime_error("voxel owner queue unavailable");tasks_.push_back([task]{(*task)();});}taskWake_.notify_one();result.get();
}
VoxelEquipmentSource::~VoxelEquipmentSource(){shutdown();{std::lock_guard lock(taskMutex_);stopping_=true;}taskWake_.notify_one();if(owner_.joinable())owner_.join();std::error_code error;if(!archiveDirectory_.empty())fs::remove_all(archiveDirectory_,error);}
bool VoxelEquipmentSource::initialize(const json& options) {
    return initialize(options,{});
}
bool VoxelEquipmentSource::initialize(const json& options,
    const std::function<void(earth::VoxelScene&, VoxelSceneProject&)>& prepare) {
    bool initialized=false;onOwner([&]{
    if(scene_){initialized=true;return;}
    try {
        scene_=project_.createScene();
        if (prepare) {
            scene_->setPaused(true);
            prepare(*scene_,project_);
        }
        const auto config=project_.equipmentConfig();equipment_config_=config;
        if(!config.visualOnly&&!config.equipment.empty()&&project_.document.value("clouds",json::object()).value("cover",.5)!=0)throw std::invalid_argument("GPU voxel equipment currently supports explicit clear-sky land topics only");
        if(project_.document.value("sky",json::object()).value("time_scale",1.)!=1)throw std::invalid_argument("equipment and sky must use the same simulation UTC");
        if(options.contains("initial_positions") || options.contains("initial_model_bindings")){
            scene_->setPaused(true);
            applyVoxelInitialPositions(*scene_,options.value("initial_positions",json::object()));
            applyVoxelModelMaterials(*scene_,options.value("initial_model_bindings",json::object()));
        }
        scene_->setPaused(false);fixedStep_=scene_->snapshot()->fixedStep;initialTime_=scene_->snapshot()->timeSeconds();
        for(const auto& platform:config.platforms)
            if(!scene_->snapshot()->objects.count(platform.object))throw std::invalid_argument("platform scene object is missing: "+platform.object);
        for(const auto& sounder:config.sounders){
            const auto snapshot=scene_->snapshot();
            const auto bed=snapshot->objects.find(sounder.seabedObject);
            if(!snapshot->ocean||bed==snapshot->objects.end()||bed->second.role!="terrain"||bed->second.body.motion!="static")
                throw std::invalid_argument("depth sounder requires an ocean and a designated static seabed terrain");
            const auto id=sounder.equipment.equipment_id;
            if(!config.visualOnly&&EquipmentRegistry::instance().findById(id))throw std::invalid_argument("depth sounder ID already registered");
        }
        if(config.visualOnly) {
            for(const auto& equipment:config.equipment)
                if(!equipment.mountObject.empty()&&!scene_->snapshot()->objects.count(equipment.mountObject))
                    throw std::invalid_argument("equipment mount object is missing: "+equipment.mountObject);
        }
        for(const auto& p:config.platforms)if(!p.displayRig.empty()) {
            const auto snap=scene_->snapshot();
            const auto& body=snap->objects.at(p.object);
            if(!body.volume||body.role!="target"||body.provenance.value("up_axis",std::string("y"))!="y"||!fs::is_regular_file(fs::path(p.displayRig)/"m1a2-rig.json"))
                throw std::invalid_argument("missing M1A2 rig or scene object");
            const auto source=body.provenance.value("path",std::string());
            if(fs::path(source).filename()!="M1A2.obj")
                throw std::invalid_argument("M1A2 presentation requires the M1A2 voxel asset");
            auto& visual=presentation_[p.id];
            visual.initialVolume=body.volume;
            visual.position=body.pose(snap->tick,snap->fixedStep)*vsg::dvec3{};
            visual.tick=snap->tick;
        }
        if(options.contains("archive_directory")) {
            const auto directory=options.at("archive_directory").get<std::string>();
            if(!fs::path(directory).is_absolute()||!fs::create_directory(directory))throw std::runtime_error("scene archive must be a new private directory");archiveDirectory_=directory;
        } else {
            std::string directory=(fs::temp_directory_path()/"vsgraysim_scene_XXXXXX").string();
            if(!::mkdtemp(directory.data()))throw std::runtime_error("cannot create published scene cache");archiveDirectory_=directory;
        }
        if(config.visualOnly){
            std::lock_guard lock(publicationMutex_);
            diagnostics_={{"visual_only",true}};
        }else if(!config.equipment.empty()&&options.contains("measurement_worker")){
            auto document=project_.equipmentDocument;document["topic"]=project_.path;
            measurement_process_=std::make_unique<VoxelEquipmentProcess>(*scene_,config,std::move(document),options.at("measurement_worker").get<std::string>(),archiveDirectory_+"/gpu-pool");
            {std::lock_guard lock(publicationMutex_);diagnostics_["gpu_pool"]=measurement_process_->diagnostics();}
        }else if(!config.equipment.empty())runtime_=std::make_unique<earth::VoxelEquipmentRuntime>(*scene_,config,device_.get(),false);
        if(!config.visualOnly)for(const auto& sounder:config.sounders){
            const auto id=sounder.equipment.equipment_id;
            SimDataBroker::instance().resetEquipmentChannel(id);
            EquipmentRegistry::instance().registerEquipment(sounder.equipment);
            registeredSounders_.insert(id);
        }
        publishState();initialized=true;
    } catch(const std::exception& error) {
        unregisterSounders();measurement_process_.reset();runtime_.reset();scene_.reset();std::lock_guard lock(publicationMutex_);error_=error.what();
    }
    });return initialized;
}
void VoxelEquipmentSource::shutdown(){onOwner([this]{
    onStop();unregisterSounders();measurement_process_.reset();runtime_.reset();scene_.reset();presentation_.clear();acoustics_.reset();
    std::lock_guard lock(publicationMutex_);
    presentationState_=nullptr;
    published_.reset();
});}
bool VoxelEquipmentSource::hasPresentationControls() const {
    for(const auto& platform:equipment_config_.platforms)if(!platform.displayRig.empty())return true;
    return false;
}
namespace {
double courseSpeedLimit(const earth::VoxelEquipmentPlatform& platform,const earth::VoxelSceneSnapshot& state) {
    const auto found=state.objects.find(platform.object);
    if(found==state.objects.end())return 0;
    const auto& object=found->second;
    if(object.body.motion!="prescribed"||object.poseTrack||object.rotor||object.body.fracture||!object.parentId.empty())return 0;
    if(platform.modelId=="skywalker-x8")return 35;
    if(platform.modelId=="surface-vessel")return 12;
    return 0;
}
}
json VoxelEquipmentSource::prescribedControls() const {
    const auto state=snapshot();
    auto controls=json::object();
    if(state)for(const auto& platform:equipment_config_.platforms)
        if(const auto limit=courseSpeedLimit(platform,*state);limit>0)
            controls[std::to_string(platform.id)] = {{"max_speed_mps", limit}};
    return controls;
}

void VoxelEquipmentSource::setPrescribedCourse(uint32_t platformId,double heading,double speed) {
    onOwner([&]{
        if(!scene_)throw std::logic_error("world is not ready");
        const auto state=scene_->snapshot();
        const auto platform=std::find_if(equipment_config_.platforms.begin(),equipment_config_.platforms.end(),
            [platformId](const auto& candidate){return candidate.id==platformId;});
        const double limit=platform==equipment_config_.platforms.end()?0:courseSpeedLimit(*platform,*state);
        if(limit<=0)throw std::invalid_argument("platform does not support prescribed course control");
        if(!std::isfinite(heading)||heading<0||heading>=360||!std::isfinite(speed)||speed<0||speed>limit)
            throw std::invalid_argument("course heading or speed out of range");
        const auto& object=state->objects.at(platform->object);
        const auto actual=object.pose(state->tick,state->fixedStep);
        vsg::dvec3 position,scale;
        vsg::dquat rotation;
        vsg::decompose(actual,position,rotation,scale);
        const auto angles=simcore::headingAngles(rotation);
        const double localHeading=heading-state->anchor.heading_deg;
        const auto course=simcore::headingRotation(localHeading-platform->modelHeadingDeg,angles.y,angles.z);
        const double radians=simcore::geo::deg2rad(localHeading);
        const json command={{"kind","place"},{"object",platform->object},
            {"pose",matrixJson(vsg::translate(position)*vsg::rotate(course))},
            {"velocity_mps",{speed*std::sin(radians),object.velocity.y,-speed*std::cos(radians)}},
            {"angular_velocity_rad_s",{0,0,0}}};
        // The world owner already runs this command between fixed steps.
        // Reuse the same pose transaction and collision representation as authoring.
        const bool originallyPaused=scene_->paused();
        scene_->setPaused(true);
        try {
            scene_->edit(json::array({command}),state->revision);
        } catch(...) {
            scene_->setPaused(originallyPaused);
            throw;
        }
        scene_->setPaused(originallyPaused);
        publishState();
    });
}

void VoxelEquipmentSource::onStart(){
    onOwner([this]{
        if(scene_){
            SimDataBroker::instance().serverOnline(true);
            BrokerNet::instance().setFlightControlAvailable(hasPresentationControls());
        }
    });
}
void VoxelEquipmentSource::onStop(){onOwner([this]{if(scene_){BrokerNet::instance().setFlightControlAvailable(false);for(const auto& spec:equipment_config_.equipment)EquipmentRegistry::instance().setActive(spec.sensor.id,false);for(const auto id:registeredSounders_)EquipmentRegistry::instance().setActive(id,false);SimDataBroker::instance().serverOnline(false);}});}

void VoxelEquipmentSource::unregisterSounders() {
    for(const auto id:registeredSounders_){
        EquipmentRegistry::instance().unregisterEquipment(id);
        SimDataBroker::instance().resetEquipmentChannel(id);
    }
    registeredSounders_.clear();
    soundingTicks_.clear();
}

void VoxelEquipmentSource::advanceSounders(const earth::VoxelSceneSnapshot& state) {
    auto& broker=SimDataBroker::instance();
    auto& registry=EquipmentRegistry::instance();
    for(const auto& sounder:equipment_config_.sounders){
        const auto id=sounder.equipment.equipment_id;
        if(!registeredSounders_.count(id))continue;
        const auto info=registry.findById(id);
        if(!info||!info->powered||!broker.isSubscribed(id)){
            registry.setActive(id,false);
            continue;
        }
        const auto period=uint64_t(std::ceil(sounder.periodSeconds/state.fixedStep-1e-9));
        const auto last=soundingTicks_.find(id);
        if(last!=soundingTicks_.end()&&state.tick-last->second<period)continue;
        const auto position=sounder.origin(state);
        registry.setActive(id,position&&broker.publishSonar(sounder.sample(state,*position)));
        if(position)soundingTicks_[id]=state.tick;
    }
}
void VoxelEquipmentSource::onTick(const TimeTick& tick) {
    onOwner([&]{if(!scene_)return;
    try {
        const auto begin=std::chrono::steady_clock::now();
        if(measurement_process_)measurement_process_->ensureStarted(archivePath_);
        const auto physicsBegin=std::chrono::steady_clock::now();
        const auto advance=scene_->advanceTo(initialTime_+tick.sim_time,1);
        const auto advanced=std::chrono::steady_clock::now();
        // Publish geometry before any product tagged with the new version.
        publishState();
        const auto published=std::chrono::steady_clock::now();
        if(!registeredSounders_.empty())advanceSounders(*scene_->snapshot());
        if(measurement_process_)measurement_process_->advanceMeasurements(archivePath_);
        else if(runtime_)runtime_->advanceMeasurements();
        const auto finished=std::chrono::steady_clock::now();auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
        auto sensors=json{{"sensor_ms",0.},{"published",0},{"rejected_windows",0}};
        if(measurement_process_)sensors=measurement_process_->diagnostics();
        else if(runtime_){
            const auto stats=runtime_->statistics();
            sensors={{"sensor_ms",stats.sensorMs},{"published",stats.published},{"rejected_windows",stats.rejectedWindows}};
        }
        std::lock_guard lock(publicationMutex_);diagnostics_={{"scene_ms",ms(physicsBegin,advanced)},{"scene_publication_ms",ms(advanced,published)},{"source_tick_ms",ms(begin,finished)},{"sensor_ms",sensors.at("sensor_ms")},{"published",sensors.at("published")},{"rejected_windows",sensors.at("rejected_windows")},{"visual_only",equipment_config_.visualOnly}};if(measurement_process_)diagnostics_["gpu_pool"]=sensors;
        if(advance.events||advance.crushedBodies)
            diagnostics_["topology_timings_ms"]={{"carve",advance.damageCarveMs},{"volume",advance.damageVolumeMs},
                {"surface",advance.topologyRefreshMs},{"collision",advance.collisionRefreshMs},
                {"terrain_mesh",advance.terrainMeshMs},{"terrain_mesh_stitch",advance.terrainMeshStitchMs},
                {"terrain_mesh_rebuilt",advance.terrainMeshRebuilt},{"terrain_mesh_reused",advance.terrainMeshReused},
                {"terrain_bvh",advance.terrainBvhMs},{"terrain_bvh_rebuilt",advance.terrainBvhRebuilt},
                {"terrain_bvh_reused",advance.terrainBvhReused}};
    } catch(const std::exception& error){std::lock_guard lock(publicationMutex_);error_=error.what();}
    });
}
FlightControlResult VoxelEquipmentSource::applyPresentation(const FlightControlCommand& command) {
    auto result=flightResult(command,"rejected","presentation_unavailable","No presentation controller for this platform");
    const auto state=scene_->snapshot();
    result.sim_time=state->timeSeconds();
    auto it=presentation_.find(command.platform_id);
    if(!command.presentation_pose||it==presentation_.end())return result;
    const auto& platform=*std::find_if(equipment_config_.platforms.begin(),equipment_config_.platforms.end(),
        [&](const auto& p){return p.id==command.platform_id;});
    auto& visual=it->second;
    const auto object=state->objects.find(platform.object);
    if(object==state->objects.end()||object->second.volume!=visual.initialVolume)return result;
    const auto now=std::chrono::steady_clock::now();
    if(now<visual.lease&&visual.owner!=command.client_id) {
        result.code="presentation_busy";
        result.message="Another console is adjusting the display pose";
        return result;
    }
    visual.pose=*command.presentation_pose;
    visual.owner=command.client_id;
    visual.lastRequest=command.request_id;
    visual.lease=now+std::chrono::milliseconds(1500);
    result.stage="applied";
    result.code="presentation_applied";
    result.message="Display pose recorded at the current scene tick";
    result.revision=++visual.revision;
    return result;
}
void VoxelEquipmentSource::servicePresentationCommands() {
    if(!hasPresentationControls())return;
    auto& net=BrokerNet::instance();
    std::vector<FlightControlCommand> commands;
    FlightControlCommand command;
    for(int count=0;count<32&&net.pollFlightControl(&command);++count)commands.push_back(std::move(command));
    if(commands.empty())return;
    onOwner([&]{
        for(const auto& pending:commands) {
            const auto result=applyPresentation(pending);
            if(result.stage=="applied")publishState();
            net.completeFlightControl(result);
        }
    });
}

void VoxelEquipmentSource::publishState() {
    const auto state=scene_->snapshot();
    if(archivedContent_!=state->contentRevision) {
        const auto path=fs::path(archiveDirectory_)/("geometry_"+std::to_string(state->contentRevision)+".json");
        const auto pending=path.string()+".pending";
        try {
            std::ofstream output(pending,std::ios::binary);
            if(!output)throw std::runtime_error("cannot create scene publication");
            output<<scene_->checkpoint()<<'\n';output.close();
            if(!output)throw std::runtime_error("cannot complete scene publication");
            if(fs::file_size(pending)>256ULL*1024*1024)throw std::runtime_error("published geometry exceeds 256 MiB");
            fs::rename(pending,path);
        } catch(...) {
            std::error_code error;fs::remove(pending,error);throw;
        }
        const auto bytes=fs::file_size(path);
        archivePath_=path.string();archivedContent_=state->contentRevision;
        archivedState_=state;archivedPaused_=scene_->paused();
        archives_.push_back({archivePath_,bytes});archiveBytes_+=bytes;
        // Bounded local geometry cache. A lagging reader whose old archive was
        // reclaimed must use the newest descriptor rather than mismatched data.
        while(archives_.size()>8||archiveBytes_>512ULL*1024*1024){fs::remove(archives_.front().first);archiveBytes_-=archives_.front().second;archives_.pop_front();}
    }
    const auto descriptor=voxelSceneDescriptor(state,archivePath_);
    const auto& a=state->anchor;const auto& u=state->utcOrigin;
    std::ostringstream epoch;epoch<<std::setfill('0')<<std::setw(4)<<u.at("year").get<int>()<<'-'<<std::setw(2)<<u.at("month").get<int>()<<'-'<<std::setw(2)<<u.at("day").get<int>()<<"T00:00:00Z";
    // hours_utc is kept explicitly; no truncation to integer hours or silently
    // assigning a saved nonzero scene time a new epoch.
    const double hours=u.at("hours_utc").get<double>();const auto seconds=std::llround(hours*3600);
    epoch.str("");epoch.clear();epoch<<std::setfill('0')<<std::setw(4)<<u.at("year").get<int>()<<'-'<<std::setw(2)<<u.at("month").get<int>()<<'-'<<std::setw(2)<<u.at("day").get<int>()<<'T'<<std::setw(2)<<seconds/3600<<':'<<std::setw(2)<<(seconds/60)%60<<':'<<std::setw(2)<<seconds%60<<'Z';
    auto atmosphere=project_.document.value("sky",json::object());atmosphere["cloud_cover"]=0;atmosphere["wind_speed"]=0;atmosphere["visibility"]=equipment_config_.visibilityM;
    json context={{"geo_anchor",{{"lat",a.lat_deg},{"lon",a.lon_deg},{"alt",a.alt_m},{"heading_deg",a.heading_deg}}},{"earth",{{"map",project_.mapPath}}},{"simulation",{{"epoch_utc",epoch.str()}}},{"atmosphere",atmosphere},{"source_scenario",project_.path},{"voxel_scene",descriptor}};
    context["utc_origin"]=u;
    if(project_.document.contains("camera"))context["voxel_camera"]=project_.document.at("camera");
    if(project_.packageManifest.contains("terrain_pages"))context["terrain_display_paging"]=true;
    auto telemetry=earth::voxelEquipmentPlatformTelemetry(equipment_config_,state);
    for(auto& platform:telemetry) {
        const auto id=platform.at("platform_id").get<uint32_t>();
        auto it=presentation_.find(id);
        if(it==presentation_.end())continue;
        auto& visual=it->second;
        const auto& object=state->objects.at(platform.at("scene_object").get<std::string>());
        if(object.volume!=visual.initialVolume)continue;
        const auto matrix=object.pose(state->tick,state->fixedStep);
        const auto pos=matrix*vsg::dvec3{};
        const auto axis=matrix*vsg::dvec4(0.,0.,1.,0.);
        const auto delta=pos-visual.position;
        if(state->tick>visual.tick)
            visual.travel+=vsg::dot(delta,vsg::dvec3(axis.x,axis.y,axis.z));
        visual.tick=state->tick;
        visual.position=pos;
        const auto& spec=*std::find_if(equipment_config_.platforms.begin(),equipment_config_.platforms.end(),
            [&](const auto& p){return p.id==id;});
        platform["presentation"]={{"model","m1a2"},{"rig",spec.displayRig},{"tick",state->tick},
            {"content_revision",state->contentRevision},{"revision",visual.revision},{"last_request_id",visual.lastRequest},
            {"yaw_deg",visual.pose.yawDeg},{"pitch_deg",visual.pose.pitchDeg},{"travel_m",visual.travel}};
    }
    for(const auto& spec:equipment_config_.equipment) {
        bool found=false;for(const auto& p:telemetry)found|=p.at("platform_id")==spec.equipment.platform_id;
        if(!found)telemetry.push_back({{"platform_id",spec.equipment.platform_id},{"name",spec.equipment.platform_name},{"kind",spec.equipment.platform_kind},{"position",{spec.aperture.position.x,spec.aperture.position.y,spec.aperture.position.z}},{"sim_time",state->timeSeconds()},{"flight_control",{{"available",false},{"reason","sensor site has no flight controller"}}}});
    }
    auto equipmentState=earth::voxelEquipmentStateTelemetry(equipment_config_,*state);
    auto presentation=earth::scenePresentation(equipment_config_,*state,equipmentState,snapshot().get());
    if(const auto acousticTopic=project_.document.find("acoustics");acousticTopic!=project_.document.end()){
        earth::appendAcousticSources(*acousticTopic,*state,presentation);
        if(!acoustics_)acoustics_=std::make_unique<AcousticPublication>();
        acoustics_->model.ingest(*state,presentation);
        const double now=state->timeSeconds();
        if(acoustics_->last<0.||now-acoustics_->last>=.2-1e-9){
            acoustics_->last=now;
            acoustics_->readings=json::array();
            for(const auto& probe:presentation.at("acoustic_probes")){
                const auto p=vector(probe.at("position_m"));
                const auto resolved=acoustics_->model.listen(*state,p);
                auto reading=resolved.encode();
                reading.erase("paths");
                auto& meter=acoustics_->meters[probe.at("id").get<std::string>()];
                meter.observe(now,resolved);
                reading.update(meter.encode());
                reading["id"]=probe.at("id");reading["tick"]=state->tick;reading["sample_s"]=now;
                acoustics_->readings.push_back(std::move(reading));
            }
        }
        presentation["acoustic_readings"]=acoustics_->readings;
    }
    earth::retainSceneSoundEvents(presentation,presentationState_,*state);
    for(auto& row:telemetry)
        for(const auto& status:presentation.at("platforms"))
            if(row.at("platform_id")==status.at("id"))row["model_state"]=status.at("state");
    auto visualList=json::array();
    for(const auto& sample:telemetry)if(sample.contains("presentation"))visualList.push_back(sample);
    if(!visualList.empty())context["voxel_scene"]["presentation"]=std::move(visualList);
    auto equipment=equipmentVisuals(*state);
    if(!equipment.at("items").empty())context["voxel_scene"]["equipment_visuals"]=std::move(equipment);
    context["voxel_scene"]["equipment_states"]=std::move(equipmentState);
    context["voxel_scene"]["scene_presentation"]=presentation;
    if(!equipment_config_.connections.empty()) {
        const auto history=scene_->history();
        auto messages=json::array();
        for(auto it=history.rbegin();it!=history.rend();++it) {
            if((*it)->tick>=state->tick)continue;
            messages=earth::voxelModelStateMessages(equipment_config_,**it,*state);
            break;
        }
        context["voxel_scene"]["model_state_messages"]=std::move(messages);
    }
    BrokerNet::instance().setEarthContext(context.dump());BrokerNet::instance().setPlatformTelemetry(telemetry.dump());
    std::lock_guard lock(publicationMutex_);published_=state;platforms_=std::move(telemetry);presentationState_=std::move(presentation);
}
bool VoxelEquipmentSource::hasDataDemand() const {
    const auto demanded=[](uint32_t id){
        const auto e=EquipmentRegistry::instance().findById(id);
        return e&&e->powered&&SimDataBroker::instance().isSubscribed(id);
    };
    return std::any_of(equipment_config_.equipment.begin(),equipment_config_.equipment.end(),
               [&](const auto& spec){return demanded(spec.sensor.id);})||
           std::any_of(equipment_config_.sounders.begin(),equipment_config_.sounders.end(),
               [&](const auto& sounder){return demanded(sounder.equipment.equipment_id);});
}
void VoxelEquipmentSource::idleMeasurements(){onOwner([this]{if(measurement_process_){measurement_process_->idle();const auto state=measurement_process_->diagnostics();std::lock_guard lock(publicationMutex_);diagnostics_["gpu_pool"]=state;}});}
bool VoxelEquipmentSource::isHealthy() const {std::lock_guard lock(publicationMutex_);return error_.empty();}
nlohmann::json VoxelEquipmentSource::diagnostics() const {std::lock_guard lock(publicationMutex_);auto out=diagnostics_.is_object()?diagnostics_:json::object();out["error"]=error_;return out;}
earth::VoxelSceneSnapshotPtr VoxelEquipmentSource::snapshot() const {std::lock_guard lock(publicationMutex_);return published_;}
nlohmann::json VoxelEquipmentSource::platforms() const {std::lock_guard lock(publicationMutex_);return platforms_;}
nlohmann::json VoxelEquipmentSource::equipmentVisuals(const earth::VoxelSceneSnapshot& state) const {
    return earth::voxelEquipmentVisualTelemetry(equipment_config_,state);
}
nlohmann::json VoxelEquipmentSource::presentationState(const earth::VoxelSceneSnapshot& state) const {
    std::lock_guard lock(publicationMutex_);
    if(!published_||published_.get()!=&state)return nullptr;
    return presentationState_;
}
void VoxelEquipmentSource::saveRecovery(const std::string& path) {
    onOwner([&]{
        if(!scene_||!isHealthy())throw std::runtime_error("cannot checkpoint an unhealthy source");
        // This source owns event scheduling. Once a revision is published,
        // another owner operation that changes the checkpoint swaps the scene snapshot.
        if(archivedState_.lock()==scene_->snapshot()&&archivedPaused_==scene_->paused()) {
            const auto destination=fs::absolute(path);
            const auto temporary=destination.string()+".link-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            std::error_code error;
            if(fs::equivalent(archivePath_,destination,error))return;
            error.clear();
            fs::create_hard_link(archivePath_,temporary,error);
            if(!error) {
                fs::rename(temporary,destination,error);
                if(!error)return;
                fs::remove(temporary,error);
            }
        }
        scene_->saveCheckpoint(path);
    });
}

AsyncVoxelSceneReadView::AsyncVoxelSceneReadView():worker_([this]{
    VoxelSceneReadView view;
    for(;;){json descriptor;{std::unique_lock lock(mutex_);wake_.wait(lock,[this]{return stop_||!pending_.is_null();});if(stop_)return;descriptor=std::move(pending_);pending_=nullptr;}
        try{auto result=view.update(descriptor);std::lock_guard lock(mutex_);
            // An obsolete generation may finish decoding after a restart.
            if(requested_.value("checkpoint_file",std::string())==descriptor.value("checkpoint_file",std::string())){result_=std::move(result);resultPresentation_=descriptor.value("presentation",json::array());resultEquipmentVisuals_=descriptor.value("equipment_visuals",json::object());error_.clear();}
        }catch(const std::exception& e){std::lock_guard lock(mutex_);if(requested_==descriptor)error_=e.what();}
    }
}){}
AsyncVoxelSceneReadView::~AsyncVoxelSceneReadView(){{std::lock_guard lock(mutex_);stop_=true;}wake_.notify_one();worker_.join();}
void AsyncVoxelSceneReadView::submit(const json& descriptor){std::lock_guard lock(mutex_);if(requested_==descriptor&&error_.empty())return;if(requested_.value("checkpoint_file",std::string())!=descriptor.value("checkpoint_file",std::string()))result_.reset();requested_=descriptor;pending_=descriptor;error_.clear();wake_.notify_one();}
earth::VoxelSceneSnapshotPtr AsyncVoxelSceneReadView::take(json* presentation,json* equipmentVisuals){
    std::lock_guard lock(mutex_);
    if(presentation&&result_)*presentation=std::move(resultPresentation_);
    if(equipmentVisuals&&result_)*equipmentVisuals=std::move(resultEquipmentVisuals_);
    return std::exchange(result_,{});
}
std::string AsyncVoxelSceneReadView::error() const {std::lock_guard lock(mutex_);return error_;}

earth::VoxelSceneSnapshotPtr VoxelSceneReadView::update(const json& descriptor) {
    const auto path=descriptor.at("checkpoint_file").get<std::string>();const auto content=descriptor.at("content_revision").get<uint64_t>();
    auto geometry=geometry_;
    if(path!=archivePath_||!geometry||geometry->contentRevision!=content) {
        auto decoded=earth::VoxelScene::load(read(path,256ULL*1024*1024),path);geometry=decoded->snapshot();
        if(geometry->contentRevision!=content)throw std::invalid_argument("published voxel geometry revision mismatch");
    }
    auto state=std::make_shared<earth::VoxelSceneSnapshot>(*geometry);
    state->tick=descriptor.at("tick").get<uint64_t>();state->revision=descriptor.at("revision").get<uint64_t>();
    if(!content||content>1000000000000ULL||state->tick>1000000000000ULL||state->revision>1000000000000ULL||state->revision<content||descriptor.at("fixed_step_seconds").get<double>()!=state->fixedStep)throw std::invalid_argument("invalid published voxel time/revision");
    const auto audio=descriptor.find("scene_presentation");
    if(audio!=descriptor.end()&&audio->is_object()&&audio->contains("version")&&audio->at("version")==1&&
       audio->contains("tick")&&audio->at("tick")==state->tick&&audio->contains("revision")&&audio->at("revision")==state->revision)
        state->scenePresentation=*audio;
    const auto& poses=descriptor.at("objects");if(!poses.is_object()||poses.size()!=state->objects.size())throw std::invalid_argument("published voxel object set differs from geometry");
    for(auto& [id,object]:state->objects) {
        const auto& value=poses.at(id);const auto& m=value.at("pose");if(!m.is_array()||m.size()!=16)throw std::invalid_argument("invalid published voxel pose");
        for(int c=0;c<4;++c)for(int r=0;r<4;++r){double v=m[4*c+r].get<double>();if(!std::isfinite(v))throw std::invalid_argument("nonfinite published voxel pose");object.basePose[c][r]=v;}
        object.motionOriginTick=state->tick;object.velocity=vector(value.at("velocity"));object.angularVelocity=vector(value.at("angular_velocity"));
    }
    archivePath_=path;geometry_=std::move(geometry);return state;
}
}
