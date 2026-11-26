#include "SimulationEngineWorker.h"
#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include <vsgraysim/broker/BrokerNet.h>
#include <vsgraysim/broker/SimDataBroker.h>
#include <vsgraysim/kernel/TimingBus.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include <QCryptographicHash>

namespace vsgraysim::gui {
namespace {
using json=nlohmann::json;
using Clock=std::chrono::steady_clock;
double millis(Clock::time_point a,Clock::time_point b=Clock::now()){return std::chrono::duration<double,std::milli>(b-a).count();}
void atomicJson(const std::filesystem::path& file,const json& value) {
    auto pending=file;pending+=".pending";
    std::ofstream out(pending);out<<value;out.close();
    if(!out)throw std::runtime_error("cannot write worker status");
    std::filesystem::rename(pending,file);
}
template<class T> std::string digest(const std::vector<T>& values){return QCryptographicHash::hash(QByteArray::fromRawData(reinterpret_cast<const char*>(values.data()),qsizetype(values.size()*sizeof(T))),QCryptographicHash::Sha256).toHex().toStdString();}
json products(){
    auto out=json::array();auto& broker=SimDataBroker::instance();
    for(const auto& e:EquipmentRegistry::instance().listOnline()){
        const auto id=e.equipment_id;const auto st=broker.channelStats(id);json p={{"id",id},{"received",st.published_frames},{"frame_id",st.last_frame_id}};
        VisFrame v;IrFrame i;RadarFrame r;SarFrame s;
        if(const auto f=broker.sonarSnapshot(id))p.update({{"sim_time",f->sim_time},{"tick",f->tick},
            {"surface_revision",f->surface_revision},{"bottom",uint8_t(f->bottom)},{"water_depth_m",f->water_depth_m},
            {"clearance_m",f->clearance_m},{"two_way_ms",f->two_way_ms},
            {"water_column_enabled",f->water_column_enabled},{"echo_range_m",f->echo_range_m}});
        else if(broker.latestVis(id,&v))p.update({{"payload_sha256",digest(v.intensity)},{"sim_time",v.sim_time},{"surface_revision",v.surface_revision}});
        else if(broker.latestIr(id,&i))p.update({{"payload_sha256",digest(i.radiance)},{"sim_time",i.sim_time},{"surface_revision",i.surface_revision}});
        else if(broker.latestRadar(id,&r)){
            p.update({{"payload_sha256",digest(r.rd_map)},{"sim_time",r.sim_time},{"surface_revision",r.surface_revision}});
            if(r.ground)p.update({{"ground_samples",r.ground->size()},{"ground_sha256",digest(*r.ground)}});
        }
        else if(broker.latestSar(id,&s))p.update({{"payload_sha256",digest(s.echo_data)},{"sim_time",s.sim_time},{"surface_revision",s.surface_revision}});
        out.push_back(p);
    }return out;
}
}
int runSimulationEngineWorker(int argc,char** argv) {
    // No widget, swapchain or shared GUI device exists in this process.
    if(argc<5)return 64;
    const std::filesystem::path directory=argv[3];const std::string generation=argv[4];
    if(!directory.is_absolute()||!std::filesystem::is_directory(directory))return 64;
    auto logger=spdlog::stderr_color_mt("engine-worker");spdlog::set_default_logger(logger);
    json status={{"generation",generation},{"pid",::getpid()},{"state","loading"},{"stage","project"},{"initialized",false},{"running",false},{"voxel_source",true},{"ack",0}};
    const auto started=Clock::now();
    auto publish=[&]{status["uptime_ms"]=millis(started);atomicJson(directory/"status.json",status);};
    try {
        publish();auto project=VoxelSceneProject::open(argv[2]);const auto equipment=project.equipmentConfig();
        if(argc>5){std::ifstream in(argv[5]);project.document["checkpoint"]=json::parse(in);}
        status["stage"]="terrain_geometry_and_sensor_gpu";publish();
        auto& net=BrokerNet::instance();net.startSimRole();
        net.setSimMeta(std::filesystem::path(project.path).filename().string(),0,0,false);
        auto source=std::make_shared<VoxelEquipmentSource>(std::move(project));
        if(!source->initialize({{"archive_directory",(directory/"geometry").string()},{"measurement_worker",std::filesystem::canonical("/proc/self/exe").string()}}))throw std::runtime_error(source->diagnostics().dump());
        TimingBus bus;bus.configureFixedStep(source->fixedStep(),1);bus.registerSubsystem(source,0);
        if(!bus.initialize())throw std::runtime_error("worker timing bus initialization failed");
        bus.start();bus.setPaused(true);
        const auto initialTick=source->snapshot()->tick;
        const auto recovery=directory/"recovery.json";
        auto checkpoint=[&]{auto begin=Clock::now();source->saveRecovery(recovery.string());status["checkpoint_ms"]=millis(begin);status["recovery_checkpoint"]=recovery.string();status["recovery_tick"]=source->snapshot()->tick;};
        checkpoint();status["load_ms"]=millis(started);status["initialized"]=true;status["state"]="paused";status["stage"]="ready";
        bool running=false,requested=false,quit=false;uint64_t stopAtTick=0;
        auto demand=[&]{return net.hasTaskDemand();};uint64_t ack=0;std::string input;double tickMs=0;
        std::deque<double> timings,cycles,measurementBatches,measurementRoundtrips;
        auto update=[&]{
            const auto state=source->snapshot();net.setSimMeta(std::filesystem::path(argv[2]).filename().string(),state->timeSeconds(),tickMs>0?1000./tickMs:0,running);
            const auto meta=net.lastSimMeta();status["running"]=running;status["sim_time"]=state->timeSeconds();status["tick"]=state->tick;status["frames"]=state->tick-initialTick;
            status["fixed_step_seconds"]=state->fixedStep;status["content_revision"]=state->contentRevision;status["source"]=source->diagnostics();status["platforms"]=source->platforms();status["earth"]=json::parse(meta.earth_json);status["scene_session_id"]=meta.scene_session_id;
            status["equipment"]=json::array();
            for(const auto& e:EquipmentRegistry::instance().listOnline()) {
                const auto s=SimDataBroker::instance().channelStats(e.equipment_id);
                status["equipment"].push_back({{"id",e.equipment_id},{"name",e.name},{"frames",s.published_frames},{"subscribed",SimDataBroker::instance().isSubscribed(e.equipment_id)},{"powered",e.powered}});
            }
            status["worker_tick_ms"]=tickMs;
            status["subscription_leases"]=net.subscriptionStatus();
            if(!timings.empty()){auto sorted=std::vector<double>(timings.begin(),timings.end());std::sort(sorted.begin(),sorted.end());double sum=0;for(double value:sorted)sum+=value;status["worker_tick_mean_ms"]=sum/sorted.size();status["worker_tick_samples"]=sorted.size();status["worker_tick_p50_ms"]=sorted[(sorted.size()-1)/2];status["worker_tick_p95_ms"]=sorted[size_t((sorted.size()-1)*.95)];}
            if(!cycles.empty()){auto sorted=std::vector<double>(cycles.begin(),cycles.end());std::sort(sorted.begin(),sorted.end());double sum=0;for(double value:sorted)sum+=value;status["simulation_cycle_mean_ms"]=sum/sorted.size();status["simulation_cycle_samples"]=sorted.size();status["simulation_cycle_p50_ms"]=sorted[(sorted.size()-1)/2];status["simulation_cycle_p95_ms"]=sorted[size_t((sorted.size()-1)*.95)];}
            auto distribution=[](const std::deque<double>& values){json result={{"samples",values.size()}};if(values.empty())return result;auto sorted=std::vector<double>(values.begin(),values.end());std::sort(sorted.begin(),sorted.end());double sum=0;for(double value:sorted)sum+=value;result.update({{"mean_ms",sum/sorted.size()},{"p50_ms",sorted[(sorted.size()-1)/2]},{"p95_ms",sorted[size_t((sorted.size()-1)*.95)]}});return result;};
            status["measurement_batches"]=distribution(measurementBatches);status["measurement_roundtrips"]=distribution(measurementRoundtrips);
            if(!running)status["channels"]=products();
            status["run_requested"]=requested;
            const auto& sourceStatus=status.at("source");
            const bool visualOnly=sourceStatus.value("visual_only",false);
            const auto gpu=sourceStatus.value("gpu_pool",json{{"pid",0},{"state","idle"},{"capacity",1}});
            json workers=json::array();
            workers.push_back({{"kind","world_time_and_physics"},{"pid",::getpid()},{"backend","CPU / existing collision precision"},{"state",running?"running":requested?"waiting_lease":"paused"}});
            const bool gpuTask=!visualOnly&&!equipment.equipment.empty();
            if(gpuTask)
                workers.push_back({{"kind","gpu_measurements"},{"pid",gpu.value("pid",0)},{"backend",gpu.value("backend",std::string("not allocated"))},{"state",gpu.value("state",std::string("idle"))}});
            status["process_pool"]={{"gpu_capacity",gpuTask?1:0},{"gpu_allocation","on data demand; shared scene across equipment jobs"},{"workers",std::move(workers)}};
            status["tasks"]=json::array();const auto worldState=running?"running":requested?"waiting_lease":"paused";
            status["tasks"].push_back({{"id","world.time"},{"type","time_advance"},{"state",worldState},{"pid",::getpid()},{"backend","CPU"},{"period_seconds",state->fixedStep},{"published",state->tick}});
            for(const auto& platform:equipment.platforms)status["tasks"].push_back({{"id","platform."+std::to_string(platform.id)},{"name",platform.name},{"type","platform_motion"},{"state",worldState},{"pid",::getpid()},{"backend","CPU / authoritative physics"},{"period_seconds",state->fixedStep},{"published",state->tick}});
            for(const auto& unit:equipment.navigation)
                status["tasks"].push_back({{"id","navigation."+std::to_string(unit.id)},{"name",unit.name},
                    {"type","navigation_truth"},{"state",worldState},{"pid",::getpid()},{"backend","CPU / scenario snapshot"},
                    {"period_seconds",state->fixedStep},{"published",state->tick}});
            for(const auto& unit:equipment.sounders){
                const auto id=unit.equipment.equipment_id;
                const auto info=EquipmentRegistry::instance().findById(id);
                const auto& broker=SimDataBroker::instance();
                status["tasks"].push_back({{"id","equipment."+std::to_string(id)},{"name",unit.equipment.name},
                    {"type","sonar_depth"},{"pid",::getpid()},{"backend","CPU / scene seabed"},
                    {"state",visualOnly?"preview":!info||!info->powered?"off":!broker.isSubscribed(id)?"waiting_lease":worldState},
                    {"period_seconds",unit.periodSeconds},{"published",broker.channelStats(id).published_frames}});
            }
            for(const auto& spec:equipment.equipment){
                const auto id="equipment."+std::to_string(spec.sensor.id);
                if(visualOnly){
                    status["tasks"].push_back({{"id",id},{"name",spec.equipment.name},{"type","equipment_visual"},
                        {"state",worldState},{"pid",::getpid()},{"backend","CPU / scenario clock"},
                        {"period_seconds",state->fixedStep},{"published",state->tick}});
                    continue;
                }
                const auto info=EquipmentRegistry::instance().findById(spec.sensor.id);
                const bool active=info&&info->powered&&SimDataBroker::instance().isSubscribed(spec.sensor.id);
                const auto counts=SimDataBroker::instance().channelStats(spec.sensor.id);
                const auto type=spec.equipment.type;
                status["tasks"].push_back({{"id",id},{"name",spec.equipment.name},
                    {"type",type==SensorType::IR?"infrared":type==SensorType::RADAR?"radar":type==SensorType::SAR?"sar":"optical"},
                    {"state",!info||!info->powered?"off":!active?"waiting_lease":!requested?"paused":gpu.value("pid",0)>0?"enabled":"queued"},
                    {"pid",gpu.value("pid",0)},{"backend","Vulkan GPU"},
                    {"period_seconds",spec.sensor.period_seconds},{"published",counts.published_frames}});
            }
            status["task_types"]={{{"type","time_advance"},{"supported",true}},{{"type","platform_motion"},{"supported",true}},{{"type","infrared"},{"supported",true}},{{"type","radar"},{"supported",true}},{{"type","optical"},{"supported",true}},{{"type","sar"},{"supported",true},{"product","P0 echo acquisition"}},{{"type","isar"},{"supported",false},{"reason","voxel runtime does not yet provide ISAR motion compensation and imaging"}}};
            status["task_types"].push_back({{"type","navigation_truth"},{"supported",true},
                {"product","WGS84 coordinate and scene-frame velocity in platform telemetry"}});
            status["task_types"].push_back({{"type","sonar_depth"},{"supported",true},
                {"product","vertical sounding of designated scene seabed"}});
            status["ack"]=ack;publish();
        };
        auto tick=[&]{const auto begin=Clock::now();if(!bus.isRunning())bus.start();bus.setPaused(false);if(!bus.tick()||!source->isHealthy())throw std::runtime_error(source->diagnostics().dump());bus.setPaused(!running);tickMs=millis(begin);timings.push_back(tickMs);if(timings.size()>600)timings.pop_front();const auto pool=source->diagnostics().value("gpu_pool",json::object());if(pool.value("last_products",0)>0){measurementBatches.push_back(pool.value("sensor_ms",0.));measurementRoundtrips.push_back(pool.value("roundtrip_ms",0.));if(measurementBatches.size()>600){measurementBatches.pop_front();measurementRoundtrips.pop_front();}}};
        const auto flags=::fcntl(STDIN_FILENO,F_GETFL,0);::fcntl(STDIN_FILENO,F_SETFL,flags|O_NONBLOCK);
        const auto wallStep=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(source->fixedStep()));
        auto next=Clock::now(),published=Clock::now();update();
        while(!quit) {
            const auto cycleBegin=Clock::now();bool advanced=false;
            source->servicePresentationCommands();
            ControlCommand control;for(unsigned n=0;n<64&&SimDataBroker::instance().pollControl(&control);++n){}
            char bytes[4096];ssize_t count;
            while((count=::read(STDIN_FILENO,bytes,sizeof(bytes)))>0){input.append(bytes,size_t(count));if(input.size()>65536)throw std::runtime_error("worker command buffer exceeded");}
            if(count==0)break; // Parent closed its command pipe: never orphan a worker.
            size_t end;unsigned handled=0;
            while((end=input.find('\n'))!=std::string::npos && handled++<32) {
                auto line=input.substr(0,end);input.erase(0,end+1);
                const auto command=json::parse(line);
                if(command.at("generation")!=generation)continue;
                const auto seq=command.at("seq").get<uint64_t>();if(seq<=ack)continue;
                const auto op=command.at("command").get<std::string>();status.erase("command_error");
                if(op=="play"||op.rfind("play_to:",0)==0){stopAtTick=op=="play"?0:std::stoull(op.substr(8));if(op!="play"&&(!stopAtTick||stopAtTick<=source->snapshot()->tick||stopAtTick-source->snapshot()->tick>100000)){stopAtTick=0;status["command_error"]="target tick must be in the future and within 100000 steps";}else {if(!bus.isRunning())bus.start();requested=true;running=demand();status["state"]=running?"running":"waiting_for_leases";bus.setPaused(!running);next=Clock::now();}}
                else if(op=="pause"){stopAtTick=0;requested=false;running=false;bus.setPaused(true);checkpoint();status["state"]="paused";}
                else if(op=="step"){requested=false;running=false;if(demand()){tick();checkpoint();status["state"]="paused";}else status["command_error"]="single step requires an active task lease";}
                else if(op=="checkpoint")checkpoint();
                else if(op=="reset_metrics"){timings.clear();cycles.clear();measurementBatches.clear();measurementRoundtrips.clear();}
                else if(op=="stop"){requested=false;running=false;bus.stop();checkpoint();status["state"]="stopped";}
                else if(op=="shutdown"){requested=false;running=false;quit=true;bus.stop();status["state"]="stopped";}
                else throw std::runtime_error("unknown worker command: "+op);
                ack=seq;update();published=Clock::now();
            }
            const bool shouldRun=requested&&demand();if(shouldRun!=running){running=shouldRun;bus.setPaused(!running);next=Clock::now();status["state"]=running?"running":requested?"waiting_for_leases":"paused";}
            if(running && Clock::now()>=next){
                tick();advanced=true;next+=wallStep;
                // Acquisitions are periodic bursts, not a constant-cost step.
                // Keep a bounded deadline debt so inexpensive steps can recover
                // from one expensive acquisition. Every numerical step remains
                // exactly fixed; never enlarge dt or discard a physics tick.
                const auto oldest=Clock::now()-4*wallStep;
                if(next<oldest){next=oldest;status["deadline_reanchors"]=status.value("deadline_reanchors",uint64_t(0))+1;}
                status["pacing_policy"]="bounded_catch_up_4";
                if(stopAtTick&&source->snapshot()->tick>=stopAtTick){stopAtTick=0;requested=false;running=false;bus.setPaused(true);checkpoint();status["state"]="paused";update();published=Clock::now();}
            }
            if(millis(published)>=100){source->idleMeasurements();update();published=Clock::now();}
            if(advanced){cycles.push_back(millis(cycleBegin));if(cycles.size()>600)cycles.pop_front();}
            pollfd pipe{STDIN_FILENO,POLLIN,0};::poll(&pipe,1,running?1:10);
        }
        bus.stop();source.reset();net.stop();return 0;
    } catch(const std::exception& error) {
        status["state"]="fault";status["running"]=false;status["error"]=error.what();try{publish();}catch(...){}
        spdlog::error("engine worker: {}",error.what());BrokerNet::instance().stop();return 2;
    }
}
}
