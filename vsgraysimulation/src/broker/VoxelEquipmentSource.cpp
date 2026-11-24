#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include <vsgraysim/broker/VoxelEquipmentProcess.h>
#include <vsgraysim/broker/BrokerNet.h>
#include <vsgraysim/broker/SimDataBroker.h>
#include <vsgraysim/kernel/TimeTick.h>
#include <vsgraysim/earth/VoxelScenePackage.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
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
    for(const auto& f:j.items())if(f.key()!="format"&&f.key()!="name"&&f.key()!="scene"&&f.key()!="earth_map"&&f.key()!="sky"&&f.key()!="clouds"&&f.key()!="camera"&&f.key()!="earth"&&f.key()!="paused"&&f.key()!="equipment"&&f.key()!="checkpoint")throw std::invalid_argument("unknown scene topic field: "+f.key());
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
    if(equipmentDocument.is_null())throw std::invalid_argument("scene topic has no equipment configuration");
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
    bool initialized=false;onOwner([&]{
    if(scene_&&(runtime_||measurement_process_)){initialized=true;return;}
    try {
        const auto config=project_.equipmentConfig();equipment_config_=config;
        if(project_.document.value("clouds",json::object()).value("cover",.5)!=0)throw std::invalid_argument("voxel equipment currently supports explicit clear-sky land topics only");
        if(project_.document.value("sky",json::object()).value("time_scale",1.)!=1)throw std::invalid_argument("equipment and sky must use the same simulation UTC");
        scene_=project_.createScene();scene_->setPaused(false);fixedStep_=scene_->snapshot()->fixedStep;initialTime_=scene_->snapshot()->timeSeconds();
        if(options.contains("archive_directory")) {
            const auto directory=options.at("archive_directory").get<std::string>();
            if(!fs::path(directory).is_absolute()||!fs::create_directory(directory))throw std::runtime_error("scene archive must be a new private directory");archiveDirectory_=directory;
        } else {
            std::string directory=(fs::temp_directory_path()/"vsgraysim_scene_XXXXXX").string();
            if(!::mkdtemp(directory.data()))throw std::runtime_error("cannot create published scene cache");archiveDirectory_=directory;
        }
        if(options.contains("measurement_worker")){
            auto document=project_.equipmentDocument;document["topic"]=project_.path;
            measurement_process_=std::make_unique<VoxelEquipmentProcess>(*scene_,config,std::move(document),options.at("measurement_worker").get<std::string>(),archiveDirectory_+"/gpu-pool");
            {std::lock_guard lock(publicationMutex_);diagnostics_["gpu_pool"]=measurement_process_->diagnostics();}
        }else runtime_=std::make_unique<earth::VoxelEquipmentRuntime>(*scene_,config,device_.get(),false);
        publishState();initialized=true;
    } catch(const std::exception& error) {
        measurement_process_.reset();runtime_.reset();scene_.reset();std::lock_guard lock(publicationMutex_);error_=error.what();
    }
    });return initialized;
}
void VoxelEquipmentSource::shutdown(){onOwner([this]{onStop();measurement_process_.reset();runtime_.reset();scene_.reset();});}
void VoxelEquipmentSource::onStart(){onOwner([this]{if(runtime_||measurement_process_)SimDataBroker::instance().serverOnline(true);});}
void VoxelEquipmentSource::onStop(){onOwner([this]{if(runtime_||measurement_process_){for(const auto& spec:equipment_config_.equipment)EquipmentRegistry::instance().setActive(spec.sensor.id,false);SimDataBroker::instance().serverOnline(false);}});}
void VoxelEquipmentSource::onTick(const TimeTick& tick) {
    onOwner([&]{if(!scene_||(!runtime_&&!measurement_process_))return;
    try {
        const auto begin=std::chrono::steady_clock::now();
        if(measurement_process_)measurement_process_->ensureStarted(archivePath_);
        const auto physicsBegin=std::chrono::steady_clock::now();
        scene_->advanceTo(initialTime_+tick.sim_time,1);
        const auto advanced=std::chrono::steady_clock::now();
        // Publish geometry before any product tagged with the new version.
        publishState();if(measurement_process_)measurement_process_->advanceMeasurements(archivePath_);else runtime_->advanceMeasurements();
        const auto finished=std::chrono::steady_clock::now();auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
        auto sensors=measurement_process_?measurement_process_->diagnostics():json{{"sensor_ms",runtime_->statistics().sensorMs},{"published",runtime_->statistics().published},{"rejected_windows",runtime_->statistics().rejectedWindows}};
        std::lock_guard lock(publicationMutex_);diagnostics_={{"scene_ms",ms(physicsBegin,advanced)},{"source_tick_ms",ms(begin,finished)},{"sensor_ms",sensors.at("sensor_ms")},{"published",sensors.at("published")},{"rejected_windows",sensors.at("rejected_windows")}};if(measurement_process_)diagnostics_["gpu_pool"]=sensors;
    } catch(const std::exception& error){std::lock_guard lock(publicationMutex_);error_=error.what();}
    });
}
void VoxelEquipmentSource::publishState() {
    const auto state=scene_->snapshot();
    if(archivedContent_!=state->contentRevision) {
        const auto path=fs::path(archiveDirectory_)/("geometry_"+std::to_string(state->contentRevision)+".json");
        const auto pending=path.string()+".pending";
        {std::ofstream output(pending,std::ios::binary);if(!output)throw std::runtime_error("cannot create scene publication");output<<scene_->checkpoint();output.close();if(!output)throw std::runtime_error("cannot complete scene publication");}
        const auto bytes=fs::file_size(pending);if(bytes>256ULL*1024*1024){fs::remove(pending);throw std::runtime_error("published geometry exceeds 256 MiB");}
        fs::rename(pending,path);archivePath_=path.string();archivedContent_=state->contentRevision;
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
    auto telemetry=earth::voxelEquipmentPlatformTelemetry(equipment_config_,state);
    for(const auto& spec:equipment_config_.equipment) {
        bool found=false;for(const auto& p:telemetry)found|=p.at("platform_id")==spec.equipment.platform_id;
        if(!found)telemetry.push_back({{"platform_id",spec.equipment.platform_id},{"name",spec.equipment.platform_name},{"kind",spec.equipment.platform_kind},{"position",{spec.aperture.position.x,spec.aperture.position.y,spec.aperture.position.z}},{"sim_time",state->timeSeconds()},{"flight_control",{{"available",false},{"reason","sensor site has no flight controller"}}}});
    }
    BrokerNet::instance().setEarthContext(context.dump());BrokerNet::instance().setPlatformTelemetry(telemetry.dump());
    std::lock_guard lock(publicationMutex_);published_=state;platforms_=std::move(telemetry);
}
bool VoxelEquipmentSource::hasDataDemand() const {for(const auto& spec:equipment_config_.equipment){auto e=EquipmentRegistry::instance().findById(spec.sensor.id);if(e&&e->powered&&SimDataBroker::instance().isSubscribed(spec.sensor.id))return true;}return false;}
void VoxelEquipmentSource::idleMeasurements(){onOwner([this]{if(measurement_process_){measurement_process_->idle();const auto state=measurement_process_->diagnostics();std::lock_guard lock(publicationMutex_);diagnostics_["gpu_pool"]=state;}});}
bool VoxelEquipmentSource::isHealthy() const {std::lock_guard lock(publicationMutex_);return error_.empty();}
nlohmann::json VoxelEquipmentSource::diagnostics() const {std::lock_guard lock(publicationMutex_);auto out=diagnostics_.is_object()?diagnostics_:json::object();out["error"]=error_;return out;}
earth::VoxelSceneSnapshotPtr VoxelEquipmentSource::snapshot() const {std::lock_guard lock(publicationMutex_);return published_;}
nlohmann::json VoxelEquipmentSource::platforms() const {std::lock_guard lock(publicationMutex_);return platforms_;}
void VoxelEquipmentSource::saveRecovery(const std::string& path) {
    onOwner([&]{if(!scene_||!isHealthy())throw std::runtime_error("cannot checkpoint an unhealthy source");scene_->saveCheckpoint(path);});
}

AsyncVoxelSceneReadView::AsyncVoxelSceneReadView():worker_([this]{
    VoxelSceneReadView view;
    for(;;){json descriptor;{std::unique_lock lock(mutex_);wake_.wait(lock,[this]{return stop_||!pending_.is_null();});if(stop_)return;descriptor=std::move(pending_);pending_=nullptr;}
        try{auto result=view.update(descriptor);std::lock_guard lock(mutex_);
            // An obsolete generation may finish decoding after a restart.
            if(requested_.value("checkpoint_file",std::string())==descriptor.value("checkpoint_file",std::string())){result_=std::move(result);error_.clear();}
        }catch(const std::exception& e){std::lock_guard lock(mutex_);if(requested_==descriptor)error_=e.what();}
    }
}){}
AsyncVoxelSceneReadView::~AsyncVoxelSceneReadView(){{std::lock_guard lock(mutex_);stop_=true;}wake_.notify_one();worker_.join();}
void AsyncVoxelSceneReadView::submit(const json& descriptor){std::lock_guard lock(mutex_);if(requested_==descriptor)return;if(requested_.value("checkpoint_file",std::string())!=descriptor.value("checkpoint_file",std::string()))result_.reset();requested_=descriptor;pending_=descriptor;error_.clear();wake_.notify_one();}
earth::VoxelSceneSnapshotPtr AsyncVoxelSceneReadView::take(){std::lock_guard lock(mutex_);return std::exchange(result_,{});}
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
    const auto& poses=descriptor.at("objects");if(!poses.is_object()||poses.size()!=state->objects.size())throw std::invalid_argument("published voxel object set differs from geometry");
    for(auto& [id,object]:state->objects) {
        const auto& value=poses.at(id);const auto& m=value.at("pose");if(!m.is_array()||m.size()!=16)throw std::invalid_argument("invalid published voxel pose");
        for(int c=0;c<4;++c)for(int r=0;r<4;++r){double v=m[4*c+r].get<double>();if(!std::isfinite(v))throw std::invalid_argument("nonfinite published voxel pose");object.basePose[c][r]=v;}
        object.motionOriginTick=state->tick;object.velocity=vector(value.at("velocity"));object.angularVelocity=vector(value.at("angular_velocity"));
    }
    archivePath_=path;geometry_=std::move(geometry);return state;
}
}
