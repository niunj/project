#include <vsgraysim/broker/BrokerNet.h>
#include <vsgraysim/layer0_phys/PhysicsEngine.h>
#include "PlatformManager.h"
#include <ShmTransport.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <limits>
#include <poll.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char** environ;
namespace {
using namespace vsgraysim;
using Clock=std::chrono::steady_clock;
using json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Cleanup {~Cleanup(){BrokerNet::instance().stop();SimDataBroker::instance().serverOnline(false);EquipmentRegistry::instance().clear();}};
struct Deadline {Clock::time_point end=Clock::now()+std::chrono::seconds(15);bool valid()const{return Clock::now()<end;}};
void signal(int fd,char c){require(::send(fd,&c,1,MSG_NOSIGNAL)==1,"process signal failed");}
char receive(int fd){pollfd p{fd,POLLIN,0};require(::poll(&p,1,15000)>0,"process signal timeout");char c=0;require(::recv(fd,&c,1,0)==1,"process signal closed");return c;}
BrokerNet::Config config(const std::string& service,bool enabled=true){BrokerNet::Config c;c.enabled=enabled;c.sim_service=service;c.heartbeat_ms=50;c.timeout_ms=1200;c.request_ms=250;return c;}
void contracts(){
    FlightControlCommand source;source.request_id="contract";source.client_id="operator";source.session_id="session";source.platform_id=1;
    const json original=source;require(json(original.get<FlightControlCommand>())==original,"flight protocol round trip");
    for(auto field:{"throttle","elevator","target_speed","platform_id","release","client_id"}) {
        auto malformed=original;malformed[field]=field==std::string("release")?json("true"):json(true);
        bool rejected=false;try{(void)malformed.get<FlightControlCommand>();}catch(const std::exception&){rejected=true;}
        require(rejected,"coerced malformed flight input");
    }
    for(auto value:{-0.1,1.1,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        auto command=source;command.throttle=value;require(!validateFlightControl(command).empty(),"nonfinite/out-of-range throttle accepted");
    }
    auto fractional=original;fractional["platform_id"]=1.25;bool rejected=false;
    try{(void)fractional.get<FlightControlCommand>();}catch(const std::exception&){rejected=true;}require(rejected,"fractional platform identity accepted");
    Cleanup cleanup;auto& net=BrokerNet::instance();net.startSimRole(config("flight_contracts",false));
    SimDataBroker::instance().serverOnline(true);net.setFlightControlAvailable(true);
    source.session_id=net.flightSessionId();net.submitFlightControl(source);
    FlightControlResult result;FlightControlCommand command;
    require(net.flightControlResult(source.request_id,&result)&&result.stage=="queued","submission claimed execution");
    auto premature=flightResult(source,"applied","ok");premature.revision=99;net.completeFlightControl(premature);
    require(net.flightControlResult(source.request_id,&result)&&result.stage=="queued","unconsumed request was falsely acknowledged");
    require(net.pollFlightControl(&command)&&command.platform_id==1,"worker did not receive flight snapshot");
    auto applied=flightResult(command,"applied","ok");applied.sim_time=.1;applied.revision=7;
    net.completeFlightControl(applied);net.submitFlightControl(source);
    require(!net.pollFlightControl(&command),"idempotent command executed twice");
    require(net.flightControlResult(source.request_id,&result)&&result.stage=="applied"&&result.revision==7,"worker acknowledgement lost");
    source.request_id="stale";source.session_id="old_session";net.submitFlightControl(source);
    require(net.flightControlResult(source.request_id,&result)&&result.code=="stale_session","stale simulation identity accepted");
    source.session_id=net.flightSessionId();
    source.request_id="expired_before_execution";net.submitFlightControl(source);
    std::this_thread::sleep_for(std::chrono::milliseconds(1550));
    require(!net.pollFlightControl(&command)&&net.flightControlResult(source.request_id,&result)&&result.stage=="expired",
        "delayed control snapshot executed after its deadline");
    for(int i=0;i<65;++i){source.request_id="bounded_"+std::to_string(i);net.submitFlightControl(source);}
    require(net.flightControlResult(source.request_id,&result)&&result.code=="queue_full","execution queue unbounded");
    const auto oldSceneSession=net.lastSimMeta().scene_session_id;
    const auto oldSession=net.flightSessionId();net.resetFlightControlSession();
    require(net.flightSessionId()!=oldSession&&!net.pollFlightControl(&command),"reset left executable commands in old session");
    require(net.lastSimMeta().scene_session_id==oldSceneSession,"flight release changed scene identity");
    require(net.flightControlResult("bounded_0",&result)&&result.stage=="cancelled","reset did not report cancellation");
    const auto beforeOffline=net.flightSessionId();net.setFlightControlAvailable(false);
    require(net.flightSessionId()!=beforeOffline,"offline transition retained executable session identity");
    const auto offlineSession=net.flightSessionId();net.setFlightControlAvailable(false);
    require(net.flightSessionId()==offlineSession,"offline transition was not idempotent");
    source.request_id="offline";source.session_id=net.flightSessionId();net.submitFlightControl(source);
    require(net.flightControlResult(source.request_id,&result)&&result.code=="control_offline","offline flight command accepted");
    require(net.lastSimMeta().scene_session_id==oldSceneSession,"control offline transition changed scene identity");
    net.clearLinks();require(net.lastSimMeta().scene_session_id!=oldSceneSession,"scenario reset retained scene identity");
    std::cout<<"PASS: strict flight wire types, bounds, queued/applied identity, deduplication, bounded queue, reset and offline cancellation\n";
}
json rawRequest(ipc::ShmTransport& wire,const json& request){const auto bytes=request.dump();std::vector<uint8_t> response;require(wire.request("broker/flight/control",bytes.data(),bytes.size(),response,500),"flight wire RPC failed");return json::parse(response);}
int display(const std::string& service){
    Cleanup cleanup;auto& net=BrokerNet::instance();net.startDisplayRole(config(service));require(net.transportEnabled(),"display transport start");
    Deadline deadline;
    while(deadline.valid()&&(!net.pullSnapshot(150)||!net.lastSimMeta().flight_control_available))std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto initial=net.lastSimMeta();require(initial.flight_control_available&&!initial.session_id.empty(),"flight capability/session missing");
    const auto first=json::parse(initial.platforms_json).at(0);const auto platform=first.at("platform_id").get<uint32_t>();
    FlightControlCommand command;command.request_id="physical_command";command.client_id=net.flightClientId();command.session_id=initial.session_id;command.platform_id=platform;
    command.mode="manual";command.throttle=1;command.elevator=.2;command.aileron=.4;command.rudder=.1;
    const auto start=Clock::now();const auto request=net.submitFlightControl(command);
    require(Clock::now()-start<std::chrono::milliseconds(100),"GUI submission blocked on network RPC");
    FlightControlResult result;bool actual=false;
    while(deadline.valid()){
        const auto meta=net.lastSimMeta();const auto states=json::parse(meta.platforms_json);
        if(net.flightControlResult(request,&result)&&result.stage=="applied"&&!states.empty()){
            const auto& now=states.at(0);
            if(now.at("sim_time").get<double>()>=.5&&now.at("speed").get<double>()>first.at("speed").get<double>()+.1&&
               std::abs(now.at("roll").get<double>()-first.at("roll").get<double>())>.1&&
               std::abs(now.at("pitch").get<double>()-first.at("pitch").get<double>())>.1&&
               now.at("position").at(2).get<double>()<first.at("position").at(2).get<double>()-10){actual=true;break;}
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(actual&&result.revision>0,"remote control did not change physics and acknowledge revision");
    ipc::ShmTransport raw;require(raw.init("flight_wire_"+std::to_string(getpid()))&&raw.start(),"raw test transport start");
    auto duplicate=rawRequest(raw,json(command));require(duplicate.at("result").at("stage")=="applied"&&duplicate.at("result").at("revision")==result.revision,"duplicate flight command did not return original execution");
    auto conflicting=command;conflicting.throttle=.1;auto conflict=rawRequest(raw,json(conflicting));require(conflict.at("result").at("code")=="request_conflict","conflicting request ID was accepted");
    auto malformed=json(command);malformed["throttle"]=true;require(rawRequest(raw,malformed).contains("error"),"wire coerced boolean into throttle");
    signal(3,'A');require(receive(3)=='R',"simulation reset phase");
    while(deadline.valid()&&net.lastSimMeta().session_id==initial.session_id)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(net.lastSimMeta().session_id!=initial.session_id,"same-scene reset did not synchronize new session");
    auto stale=command;stale.request_id="stale_after_reset";auto rejection=rawRequest(raw,json(stale));require(rejection.at("result").at("code")=="stale_session","pre-reset control was accepted after reset");
    raw.stop();signal(3,'D');
    std::cout<<"PASS: exec-separated actuator demand -> worker -> Bullet motion -> telemetry and execution report; dedup/conflict/malformed/reset rejection\n";return 0;
}
void processes(const char* executable){
    std::string directory="/tmp/vsg_flight_link_XXXXXX";require(::mkdtemp(directory.data()),"private IPC directory");
    struct Directory{std::string path;~Directory(){std::error_code error;std::filesystem::remove_all(path,error);}}scratch{directory};
    require(::setenv("VSGRAYSIM_IPC_DIRECTORY",directory.c_str(),1)==0,"set IPC namespace");
    Cleanup cleanup;auto& net=BrokerNet::instance();const auto service="flight_sim_"+std::to_string(getpid());
    net.startSimRole(config(service));require(net.transportEnabled(),"simulation transport start");SimDataBroker::instance().serverOnline(true);net.setFlightControlAvailable(true);
    PhysicsEngine physics;physics.setGravity({0,0,0});physics.configureTimeline(.01,3);
    platform::PlatformManager manager;require(manager.initialize({}),"platform init");manager.setPhysicsEngine(&physics);
    platform::MotionState initial;initial.position={0,1000,0};initial.velocity={0,0,-100};
    simcore::RigidState body;body.position=initial.position;body.velocity=initial.velocity;
    require(physics.createBoxBody("flight_link_aircraft",{8,2,8},body,20000),"aircraft rigid body creation");
    const auto id=manager.createPlatform("flight_link_aircraft",platform::PlatformCategory::AIRCRAFT,{{"motion_authority","dynamic"},{"max_integration_step",.01}},initial);
    require(id>0,"aircraft fixture creation");manager.onStart();
    auto publish=[&](double time){auto state=manager.getAllPlatformsSnapshot();for(auto& value:state)value["sim_time"]=time;net.setSimMeta(service,time,60,true);net.setPlatformTelemetry(state.dump());};publish(0);
    int descriptors[2];require(::socketpair(AF_UNIX,SOCK_STREAM,0,descriptors)==0,"process synchronization socket");
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions,descriptors[1],3);
    if(descriptors[0]!=3)posix_spawn_file_actions_addclose(&actions,descriptors[0]);
    if(descriptors[1]!=3)posix_spawn_file_actions_addclose(&actions,descriptors[1]);
    std::string program=std::filesystem::absolute(executable).string();char* argv[]={program.data(),const_cast<char*>("--display"),const_cast<char*>(service.c_str()),nullptr};
    pid_t child=-1;require(::posix_spawn(&child,program.c_str(),&actions,nullptr,argv,environ)==0,"exec display process");posix_spawn_file_actions_destroy(&actions);::close(descriptors[1]);
    struct Child{pid_t pid;~Child(){if(pid>0){::kill(pid,SIGTERM);int status;while(::waitpid(pid,&status,0)<0&&errno==EINTR){}}}}guard{child};
    Deadline deadline;double time=0;unsigned executionCount=0;bool reset=false,finished=false;
    while(deadline.valid()&&!finished){
        FlightControlCommand command;
        while(net.pollFlightControl(&command)){auto result=manager.applyFlightControl(command,time);net.completeFlightControl(result);++executionCount;}
        if(executionCount&&!reset&&time<.5){TimeTick tick;tick.dt=.01;tick.sim_time=time+.01;manager.onPreTick(tick);physics.step(.01);manager.onPostTick(tick);time+=.01;publish(time);}
        pollfd p{descriptors[0],POLLIN,0};if(::poll(&p,1,1)>0){char event=receive(descriptors[0]);if(event=='A'){require(executionCount==1,"duplicate command executed by physics worker");manager.resetFlightControls();net.resetFlightControlSession();publish(time);reset=true;signal(descriptors[0],'R');}else if(event=='D')finished=true;else throw std::runtime_error("unknown display test signal");}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(finished&&executionCount==1,"flight process test deadline or execution count");
    int status=0;require(::waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"display child failed");guard.pid=-1;::close(descriptors[0]);manager.shutdown();
}
}
int main(int argc,char** argv){try{if(argc==3&&std::string(argv[1])=="--display")return display(argv[2]);contracts();processes(argv[0]);return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
