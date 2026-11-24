#include <vsgraysim/broker/VoxelEquipmentProcess.h>
#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include <vsgraysim/broker/SensorProductCodec.h>
#include <vsgraysim/broker/SimDataBroker.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <cmath>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <cerrno>
#include <type_traits>
extern char** environ;
namespace vsgraysim {
namespace {
using json=nlohmann::json;namespace fs=std::filesystem;using Clock=std::chrono::steady_clock;
double ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
void writeAll(int fd,const void* value,size_t size){auto* bytes=static_cast<const char*>(value);while(size){const auto n=::write(fd,bytes,size);if(n<0&&errno==EINTR)continue;if(n<=0)throw std::runtime_error("task pipe write failed");bytes+=n;size-=size_t(n);}}
void writeJson(int fd,const json& j){const auto line=j.dump()+"\n";if(line.size()>2*1024*1024)throw std::runtime_error("task descriptor exceeds 2 MiB");writeAll(fd,line.data(),line.size());}
struct PipeReader {
    int fd=-1;std::string buffer;size_t offset=0;
    bool fill(int timeout=30000){if(offset){buffer.erase(0,offset);offset=0;}pollfd descriptor{fd,POLLIN,0};int ready;do{ready=::poll(&descriptor,1,timeout);}while(ready<0&&errno==EINTR);if(ready<=0)throw std::runtime_error("measurement task timeout");char data[65536];ssize_t n;do{n=::read(fd,data,sizeof(data));}while(n<0&&errno==EINTR);if(n<=0)return false;buffer.append(data,size_t(n));return true;}
    json line(int timeout=30000){for(;;){const auto end=buffer.find('\n',offset);if(end!=std::string::npos){auto out=json::parse(buffer.begin()+offset,buffer.begin()+end);offset=end+1;return out;}if(buffer.size()-offset>2*1024*1024)throw std::runtime_error("oversized task line");if(!fill(timeout))throw std::runtime_error("measurement worker pipe closed");}}
    std::vector<uint8_t> bytes(size_t count){if(count>16*1024*1024)throw std::runtime_error("oversized measurement payload");std::vector<uint8_t> out;out.reserve(count);while(count){const size_t available=std::min(count,buffer.size()-offset);if(available){out.insert(out.end(),buffer.data()+offset,buffer.data()+offset+available);offset+=available;count-=available;}else if(!fill())throw std::runtime_error("truncated measurement payload");}return out;}
};
json readJson(const std::string& path,uint64_t limit){if(!fs::is_regular_file(path)||fs::file_size(path)>limit)throw std::runtime_error("invalid task resource: "+path);std::ifstream input(path);return json::parse(input);}
void atomicJson(const fs::path& path,const json& value){const auto tmp=path.string()+".pending";std::ofstream output(tmp);output<<value;output.close();if(!output)throw std::runtime_error("cannot write task resource");fs::rename(tmp,path);}
bool enabled(uint32_t id){auto e=EquipmentRegistry::instance().findById(id);return e&&e->powered&&SimDataBroker::instance().isSubscribed(id);}
}
struct VoxelEquipmentProcess::Impl {
    earth::VoxelScene& scene;earth::VoxelEquipmentConfig config;json document;std::string executable,directory;
    int input=-1,output=-1;pid_t pid=-1;PipeReader reader;uint64_t sequence=0,published=0,rejected=0,jobs=0,restarts=0,stagedRevision=0;double sensorMs=0,roundtripMs=0,initializationMs=0;
    Clock::time_point used=Clock::now();
    json scheduler=json::object();uint32_t lastProducts=0;
    Impl(earth::VoxelScene& s,earth::VoxelEquipmentConfig c,json d,std::string e,std::string root):scene(s),config(std::move(c)),document(std::move(d)),executable(std::move(e)),directory(std::move(root)){}
    void check(){if(pid<=0)return;int status=0;const auto result=::waitpid(pid,&status,WNOHANG);if(result==pid){pid=-1;if(input>=0)::close(input);if(output>=0)::close(output);input=output=-1;reader={};throw std::runtime_error("GPU task process exited unexpectedly, status="+std::to_string(status));}}
    json rpc(json request,int timeout=30000){request["sequence"]=++sequence;writeJson(input,request);auto response=reader.line(timeout);if(response.value("sequence",uint64_t(0))!=sequence||!response.value("ok",false))throw std::runtime_error(response.value("error",std::string("measurement task acknowledgement mismatch")));return response;}
    void stop(){if(input>=0){::close(input);input=-1;}if(output>=0){::close(output);output=-1;}reader={};if(pid>0){int status=0;auto end=Clock::now()+std::chrono::milliseconds(800);while(::waitpid(pid,&status,WNOHANG)==0&&Clock::now()<end)::usleep(10000);if(::waitpid(pid,&status,WNOHANG)==0){::kill(pid,SIGKILL);while(::waitpid(pid,&status,0)<0&&errno==EINTR){}}pid=-1;}stagedRevision=0;}
    void prepare(earth::VoxelSceneSnapshotPtr candidate){if(pid<=0||candidate->contentRevision==scene.snapshot()->contentRevision)return;
        const auto path=fs::path(directory)/("prepare-"+std::to_string(candidate->contentRevision)+"-"+std::to_string(sequence+1)+".json");atomicJson(path,earth::encodeVoxelSceneSnapshot(candidate));
        rpc({{"operation","prepare"},{"descriptor",voxelSceneDescriptor(candidate,path.string())}});stagedRevision=candidate->contentRevision;fs::remove(path);
    }
};
VoxelEquipmentProcess::VoxelEquipmentProcess(earth::VoxelScene& scene,earth::VoxelEquipmentConfig config,json document,std::string exe,std::string directory):impl_(std::make_unique<Impl>(scene,std::move(config),std::move(document),std::move(exe),std::move(directory))){
    if(scene.snapshot()->ocean)throw std::invalid_argument("voxel GPU measurements do not yet support ocean refraction/underwater spectral transport");
    if(!fs::create_directory(impl_->directory))throw std::runtime_error("GPU task directory must be new");
    for(const auto& e:impl_->config.equipment){if(EquipmentRegistry::instance().findById(e.sensor.id))throw std::invalid_argument("equipment id already registered");if(!e.mountObject.empty()&&!scene.snapshot()->objects.count(e.mountObject))throw std::invalid_argument("equipment mount object missing");}
    for(const auto& p:impl_->config.platforms)if(!scene.snapshot()->objects.count(p.object))throw std::invalid_argument("platform object missing");
    for(const auto& e:impl_->config.equipment){SimDataBroker::instance().resetEquipmentChannel(e.sensor.id);EquipmentRegistry::instance().registerEquipment(e.equipment);}
    SimDataBroker::instance().enableHostFrameCache(true);scene.addPublicationParticipant("gpu-measurement-pool",[this](auto candidate){impl_->prepare(std::move(candidate));});
}
VoxelEquipmentProcess::~VoxelEquipmentProcess(){if(impl_){impl_->scene.removePublicationParticipant("gpu-measurement-pool");impl_->stop();for(const auto& e:impl_->config.equipment){EquipmentRegistry::instance().unregisterEquipment(e.sensor.id);SimDataBroker::instance().unsubscribe(e.sensor.id);}}}
void VoxelEquipmentProcess::ensureStarted(const std::string& archive){auto& p=*impl_;p.check();bool demand=false;for(const auto& e:p.config.equipment)demand|=enabled(e.sensor.id);if(!demand||p.pid>0)return;
    ::signal(SIGPIPE,SIG_IGN);
    const auto initialization=Clock::now();
    atomicJson(fs::path(p.directory)/"equipment.json",p.document);atomicJson(fs::path(p.directory)/"initial.json",voxelSceneDescriptor(p.scene.snapshot(),archive));
    int command[2],response[2];if(::pipe2(command,O_CLOEXEC)||::pipe2(response,O_CLOEXEC))throw std::runtime_error("cannot allocate GPU task pipes");
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);posix_spawn_file_actions_adddup2(&actions,command[0],STDIN_FILENO);posix_spawn_file_actions_adddup2(&actions,response[1],STDOUT_FILENO);for(int fd:{command[0],command[1],response[0],response[1]})posix_spawn_file_actions_addclose(&actions,fd);
    const auto config=(fs::path(p.directory)/"equipment.json").string(),initial=(fs::path(p.directory)/"initial.json").string(),parent=std::to_string(::getpid());std::vector<std::string> args{p.executable,"--measurement-worker",config,initial,parent};std::vector<char*> argv;for(auto& arg:args)argv.push_back(arg.data());argv.push_back(nullptr);
    pid_t child=-1;const int result=::posix_spawn(&child,p.executable.c_str(),&actions,nullptr,argv.data(),environ);posix_spawn_file_actions_destroy(&actions);::close(command[0]);::close(response[1]);if(result){::close(command[1]);::close(response[0]);throw std::runtime_error("cannot spawn GPU task process");}
    p.pid=child;p.input=command[1];p.output=response[0];p.reader.fd=p.output;++p.restarts;p.sequence=0;
    try{auto ready=p.reader.line(120000);if(!ready.value("ok",false))throw std::runtime_error(ready.value("error",std::string("GPU process initialization failed")));p.used=Clock::now();p.initializationMs=ms(initialization);}catch(...){p.stop();throw;}
}
void VoxelEquipmentProcess::advanceMeasurements(const std::string& archive){auto& p=*impl_;if(p.pid<=0)return;std::vector<uint32_t> active;for(const auto& e:p.config.equipment)if(enabled(e.sensor.id))active.push_back(e.sensor.id);if(active.empty()&&ms(p.used)>5000){p.stop();return;}
    const auto start=Clock::now();const auto descriptor=voxelSceneDescriptor(p.scene.snapshot(),archive);auto reply=p.rpc({{"operation","advance"},{"descriptor",descriptor},{"enabled",active}});std::vector<wire::SensorProduct> products;uint64_t bytes=0;
    const auto& sizes=reply.at("payload_sizes");if(!sizes.is_array()||sizes.size()>256)throw std::runtime_error("invalid GPU product batch");
    for(const auto& size:sizes){bytes+=size.get<uint64_t>();if(bytes>64*1024*1024)throw std::runtime_error("GPU batch exceeds 64 MiB");const auto payload=p.reader.bytes(size.get<size_t>());wire::SensorProduct product;if(!wire::decodeSensorProduct(payload.data(),payload.size(),product))throw std::runtime_error("invalid GPU product encoding");std::visit([&](const auto& f){const bool known=std::any_of(p.config.equipment.begin(),p.config.equipment.end(),[&](const auto& spec){return spec.sensor.id==f.sensor_id;});const auto history=p.scene.history();const bool version=std::any_of(history.begin(),history.end(),[&](const auto& s){return s->contentRevision==f.surface_revision;});if(!known||!version||!std::isfinite(f.sim_time)||f.sim_time>p.scene.snapshot()->timeSeconds()+1e-9)throw std::runtime_error("GPU product does not match committed truth");},product);products.push_back(std::move(product));}
    for(const auto& product:products)std::visit([&](const auto& f){if(!enabled(f.sensor_id))return;bool accepted=false;using T=std::decay_t<decltype(f)>;auto& broker=SimDataBroker::instance();if constexpr(std::is_same_v<T,RadarFrame>)accepted=broker.publishRadar(f);else if constexpr(std::is_same_v<T,SarFrame>)accepted=broker.publishSar(f);else if constexpr(std::is_same_v<T,IrFrame>)accepted=broker.publishIr(f);else accepted=broker.publishVis(f);if(!accepted)throw std::runtime_error("world broker rejected GPU task product");++p.published;EquipmentRegistry::instance().setActive(f.sensor_id,true);},product);
    for(const auto& e:p.config.equipment)if(!enabled(e.sensor.id))EquipmentRegistry::instance().setActive(e.sensor.id,false);
    p.sensorMs=reply.value("sensor_ms",0.);p.rejected=reply.value("rejected_windows",uint64_t(0));p.scheduler=reply.value("scheduler",json::object());p.lastProducts=uint32_t(products.size());p.roundtripMs=ms(start);if(!active.empty())p.used=Clock::now();++p.jobs;
}
void VoxelEquipmentProcess::idle(){auto& p=*impl_;p.check();bool demand=false;for(const auto& e:p.config.equipment)demand|=enabled(e.sensor.id);if(!demand&&p.pid>0&&ms(p.used)>5000)p.stop();}
json VoxelEquipmentProcess::diagnostics() const {const auto& p=*impl_;return {{"pid",p.pid>0?p.pid:0},{"state",p.pid>0?"ready":"idle"},{"capacity",1},{"kind","gpu_measurements"},{"jobs",p.jobs},{"starts",p.restarts},{"published",p.published},{"rejected_windows",p.rejected},{"sensor_ms",p.sensorMs},{"roundtrip_ms",p.roundtripMs},{"initialization_ms",p.initializationMs},{"last_products",p.lastProducts},{"scheduler",p.scheduler},{"backend",p.pid>0?"Vulkan GPU":"not allocated"}};}

int runVoxelMeasurementWorker(int argc,char** argv){
    if(argc!=5)return 64;::prctl(PR_SET_PDEATHSIG,SIGKILL);if(::getppid()!=std::atoi(argv[4]))return 65;::signal(SIGPIPE,SIG_IGN);
    const int protocol=::dup(STDOUT_FILENO);::dup2(STDERR_FILENO,STDOUT_FILENO);auto logger=spdlog::stderr_color_mt("gpu-task");spdlog::set_default_logger(logger);
    uint64_t sequence=0;
    try{
        auto config=earth::VoxelEquipmentConfig::parse(readJson(argv[2],1024*1024),argv[2]);VoxelSceneReadView view;std::vector<earth::VoxelSceneSnapshotPtr> history{view.update(readJson(argv[3],2*1024*1024))};std::map<std::string,std::function<void(earth::VoxelSceneSnapshotPtr)>> participants;
        earth::VoxelSceneReader source{[&]{return history.back();},[&](double t){return earth::sampleVoxelSceneHistory(history,t);},[&]{return history;},[&](const std::string& key,std::function<void(earth::VoxelSceneSnapshotPtr)> prepare){participants[key]=std::move(prepare);},[&](const std::string& key){participants.erase(key);}};
        SimDataBroker::instance().enableHostFrameCache(true);std::vector<std::vector<uint8_t>> payloads;
        earth::VoxelEquipmentRuntime runtime(source,std::move(config),nullptr,true);auto& broker=SimDataBroker::instance();
        auto collect=[&](const auto& frame){payloads.push_back(wire::encodeSensorProduct(wire::SensorProduct(frame)));};broker.setFrameTap(collect,collect,collect,collect);
        writeJson(protocol,{{"ok",true},{"pid",::getpid()}});PipeReader commands;commands.fd=STDIN_FILENO;
        for(;;){json request;try{request=commands.line(-1);}catch(const std::exception&){break;}sequence=request.at("sequence").get<uint64_t>();const auto op=request.at("operation").get<std::string>();const auto snapshot=view.update(request.at("descriptor"));
            if(op=="prepare"){for(const auto& [key,prepare]:participants)prepare(snapshot);writeJson(protocol,{{"ok",true},{"sequence",sequence}});continue;}
            if(op!="advance")throw std::runtime_error("unknown GPU task operation");
            if(snapshot->tick!=history.back()->tick+1||snapshot->fixedStep!=history.back()->fixedStep)throw std::runtime_error("GPU truth history gap");history.push_back(snapshot);while(history.size()>2&&(history.size()>512||history[1]->timeSeconds()<snapshot->timeSeconds()-2.))history.erase(history.begin());
            const auto active=request.at("enabled").get<std::set<uint32_t>>();for(const auto& e:runtime.config().equipment){if(active.count(e.sensor.id))broker.subscribe(e.sensor.id);else broker.unsubscribe(e.sensor.id);}
            payloads.clear();runtime.advanceMeasurements();json sizes=json::array();for(const auto& bytes:payloads)sizes.push_back(bytes.size());
            const auto stats=runtime.statistics();const auto& gpu=stats.gpu;
            const json scheduler={{"batching",gpu.batching},{"graph_submissions",gpu.graphSubmissions},{"readback_batches",gpu.readbackBatches},{"measurements",gpu.measurements},{"stationary_cpis",gpu.stationaryCpis},{"dynamic_cpis",gpu.dynamicCpis},{"multi_equipment_batches",gpu.multiEquipmentBatches},{"max_batch_size",gpu.maxBatchSize}};
            writeJson(protocol,{{"ok",true},{"sequence",sequence},{"payload_sizes",sizes},{"sensor_ms",stats.sensorMs},{"rejected_windows",stats.rejectedWindows},{"scheduler",scheduler}});for(const auto& bytes:payloads)writeAll(protocol,bytes.data(),bytes.size());
        }
        broker.setFrameTap(nullptr,nullptr,nullptr,nullptr);::close(protocol);return 0;
    }catch(const std::exception& e){try{writeJson(protocol,{{"ok",false},{"sequence",sequence},{"error",e.what()}});}catch(...){}spdlog::error("GPU task process: {}",e.what());::close(protocol);return 2;}
}
}
