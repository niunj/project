#include "vsgraysim/broker/BrokerNet.h"
#include "vsgraysim/broker/EquipmentInfo.h"
#include "vsgraysim/broker/SimDataBroker.h"
#include "IpcPaths.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <execinfo.h>

extern char** environ;
namespace {
using namespace vsgraysim;
using Clock=std::chrono::steady_clock;
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
constexpr uint32_t kImageSide=512;
float radianceAt(size_t index) { return .125f+float((index*13)%1024)*.125f; }
nlohmann::json earthFixture() {
    return {{"geo_anchor",{{"lat",25.078},{"lon",121.772},{"alt",10.5},{"heading_deg",23.0}}},
        {"earth",{{"map","/opt/vsgraysim/earth/mission.map.json"},{"spacing_m",25.0}}},
        {"terrain_ref","/opt/vsgraysim/earth/mission.map.json"},
        {"simulation",{{"epoch_utc","2026-11-06T02:15:00Z"}}},
        {"atmosphere",{{"cloud_cover",0.7},{"rain",0.4}}},{"ocean",{{"sea_level",1.3}}},
        {"source_scenario","/opt/vsgraysim/scenes/mission.json"},{"surface_revision",uint64_t(0xfeed123456789abcULL)}};
}
struct Deadline {
    Clock::time_point end=Clock::now()+std::chrono::seconds(10);
    int remaining() const { return std::max(0,int(std::chrono::duration_cast<std::chrono::milliseconds>(end-Clock::now()).count())); }
};
void sendByte(int fd,char value) { require(::send(fd,&value,1,MSG_NOSIGNAL)==1,"cross-process synchronization write failed"); }
bool ready(int fd,int timeout) {
    pollfd descriptor{fd,POLLIN,0};
    const int result=::poll(&descriptor,1,timeout);
    if(result<0 && errno==EINTR) return false;
    require(result>=0,"cross-process synchronization poll failed");
    return result>0;
}
char readByte(int fd,Deadline& deadline) {
    require(deadline.remaining()>0 && ready(fd,deadline.remaining()),"cross-process test deadline exceeded");
    char value=0; require(::recv(fd,&value,1,0)==1,"cross-process peer closed unexpectedly"); return value;
}
void expectByte(int fd,char expected,Deadline& deadline) { require(readByte(fd,deadline)==expected,"cross-process peer reported a failed phase"); }
BrokerNet::Config configuration(const std::string& service) {
    BrokerNet::Config result; result.enabled=true; result.transport="shm"; result.sim_service=service;
    result.heartbeat_ms=100; result.request_ms=150; result.timeout_ms=1500; return result;
}
struct NetworkCleanup {
    ~NetworkCleanup() { BrokerNet::instance().stop(); SimDataBroker::instance().serverOnline(false); EquipmentRegistry::instance().clear(); }
};
struct Directory {
    std::string path="/tmp/vsg_product_link_XXXXXX";
    Directory() { require(::mkdtemp(path.data())!=nullptr,"cannot create isolated IPC namespace"); }
    ~Directory() { std::error_code error; std::filesystem::remove_all(path,error); }
};
struct Descriptors {
    int value[2]{-1,-1};
    ~Descriptors(){ for(int descriptor:value) if(descriptor>=0) ::close(descriptor); }
};
struct Child {
    pid_t pid=-1;
    ~Child(){ if(pid>0) { ::kill(pid,SIGTERM); int status=0; while(::waitpid(pid,&status,0)<0 && errno==EINTR) {} } }
};

int display(const std::string& service,uint32_t first) {
    Deadline deadline; NetworkCleanup cleanup;
    auto& net=BrokerNet::instance(); auto& broker=SimDataBroker::instance();
    expectByte(3,'R',deadline);
    net.startDisplayRole(configuration(service)); require(net.transportEnabled(),"display transport did not start");
    bool found=false;
    while(deadline.remaining()>0) {
        if(net.pullSnapshot(150) && net.lastSimMeta().scene==service) { found=true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    require(found && net.simOnline(),"display did not discover the isolated simulation process");
    for(uint32_t id:{first,first+1,first+2}) {
        auto equipment=EquipmentRegistry::instance().findById(id);
        require(equipment && equipment->remote,"discovery did not produce remote equipment identities");
        require(net.linkAttach(id),"remote equipment link was rejected");
    }
    const auto meta=net.lastSimMeta();
    require(meta.online && meta.running && meta.scene==service && meta.sim_time==12.5,"simulation heartbeat metadata changed in transit");
    const auto platforms=nlohmann::json::parse(meta.platforms_json);
    require(nlohmann::json::parse(meta.earth_json)==earthFixture(),
        "Earth anchor, absolute dataset references, epoch, weather or surface revision changed across exec heartbeat");
    require(platforms.size()==1 && platforms.at(0).at("platform_id")==77 && platforms.at(0).at("position").at(1)==1250.5 &&
        platforms.at(0).at("velocity").at(0)==80 && platforms.at(0).at("speed")==80 && platforms.at(0).at("yaw")==90 &&
        platforms.at(0).at("sim_time")==12.5,
        "platform telemetry was not transported in the simulation heartbeat");
    sendByte(3,'A');
    IrFrame infrared; RadarFrame radar; SarFrame sar; bool received=false;
    while(deadline.remaining()>0) {
        if(broker.latestIr(first,&infrared) && broker.latestRadar(first+1,&radar) && broker.latestSar(first+2,&sar)) { received=true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(received,"typed measurement data did not arrive over the process link");
    require(infrared.surface_revision==earthFixture().at("surface_revision").get<uint64_t>() && infrared.frame_id==0 && infrared.stage==0 && infrared.width==kImageSide && infrared.height==kImageSide && infrared.radiance.size()==size_t(kImageSide)*kImageSide,
        "P0 IR frame identity, stage or physical radiance changed in transit");
    for(size_t pixel=0;pixel<infrared.radiance.size();++pixel) require(infrared.radiance[pixel]==radianceAt(pixel),"large IR frame was truncated or corrupted, including final pixels");
    require(radar.frame_id==0 && radar.stage==2 && radar.sample_rate_hz==30e6 && radar.prf==40 && radar.coherent_pulses==2 && radar.rd_map.size()==4 && radar.rd_map[0]==std::complex<float>(1,2),
        "P2 RD frame lost sampling/CPI metadata or complex data");
    require(sar.frame_id==0 && sar.stage==0 && sar.sample_rate_hz==20e6f && sar.range_min==5 && sar.echo_data.size()==4 && sar.echo_data[1]==std::complex<float>(3,4),
        "P0 SAR raw IQ lost sampling/gate metadata or data");
    sendByte(3,'F'); expectByte(3,'C',deadline);
    ControlCommand command; command.type=ControlCommand::Type::PowerOff; command.equipment_id=first; command.equipment_type=SensorType::IR;
    net.sendControl(command); sendByte(3,'P'); expectByte(3,'O',deadline);
    for(uint32_t id:{first,first+1,first+2}) require(net.linkDetach(id),"remote detach failed");
    std::this_thread::sleep_for(std::chrono::milliseconds(150)); // finish any already-delivered callback
    const auto before=broker.channelStats(first+2).published_frames;
    sendByte(3,'D'); expectByte(3,'G',deadline); expectByte(3,'Q',deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    SarFrame after; require(broker.latestSar(first+2,&after) && after.frame_id!=999 && broker.channelStats(first+2).published_frames==before,
        "unlinked equipment continued receiving forwarded measurements");
    net.stop(); sendByte(3,'X');
    std::cout<<"PASS: display received discovery, telemetry, P0 IR/P2 RD/P0 SAR, controlled power and detached\n";
    return 0;
}

int simulation(const char* executable) {
    Directory directory;
    require(::setenv("VSGRAYSIM_IPC_DIRECTORY",directory.path.c_str(),1)==0,"cannot select isolated IPC namespace");
    // Validate path handling without creating any socket outside this namespace.
    sockaddr_un address{};
    require(!ipc::unixSocketAddress(address,"/"+std::string(200,'x')),"overlong socket path was truncated");
    require(ipc::ipcSocketPath(directory.path,"../user_service").empty(),"process name escaped IPC namespace");
    const std::string service="vsg_link_"+std::to_string(::getpid());
    const uint32_t first=0x71000000u+(uint32_t(::getpid())&0x000fffffu)*4;
    const std::string identifier=std::to_string(first);
    std::atomic<uint32_t> gate{0}; NetworkCleanup cleanup; Descriptors synchronization; Child child;
    require(::socketpair(AF_UNIX,SOCK_STREAM,0,synchronization.value)==0,"cannot create process coordination socket");
    posix_spawn_file_actions_t actions; require(::posix_spawn_file_actions_init(&actions)==0,"spawn actions init");
    const int childDescriptor=synchronization.value[1];
    int setup=::posix_spawn_file_actions_adddup2(&actions,childDescriptor,3);
    if(!setup && childDescriptor!=3) setup=::posix_spawn_file_actions_addclose(&actions,childDescriptor);
    if(!setup && synchronization.value[0]!=3 && synchronization.value[0]!=childDescriptor) setup=::posix_spawn_file_actions_addclose(&actions,synchronization.value[0]);
    if(setup) { ::posix_spawn_file_actions_destroy(&actions); throw std::runtime_error("spawn descriptor actions failed"); }
    char* arguments[]={const_cast<char*>(executable),const_cast<char*>("--display"),const_cast<char*>(service.c_str()),const_cast<char*>(identifier.c_str()),nullptr};
    const int spawned=::posix_spawn(&child.pid,executable,&actions,nullptr,arguments,environ);
    ::posix_spawn_file_actions_destroy(&actions);
    require(spawned==0,"could not exec isolated display process");
    ::close(synchronization.value[1]); synchronization.value[1]=-1;
    const int control=synchronization.value[0]; Deadline deadline;
    auto& broker=SimDataBroker::instance(); auto& net=BrokerNet::instance();
    broker.serverOnline(true);
    for(uint32_t offset=0;offset<3;++offset) {
        EquipmentInfo info; info.equipment_id=first+offset; info.platform_id=77; info.platform_name="fixture aircraft";
        info.type=offset==0 ? SensorType::IR : offset==1 ? SensorType::RADAR : SensorType::SAR; info.name="isolated sensor"; info.powered=true;
        EquipmentRegistry::instance().registerEquipment(info);
    }
    net.startSimRole(configuration(service)); require(net.transportEnabled(),"simulation transport did not start");
    net.setLinkGate([&](const std::set<uint32_t>& linked) {
        uint32_t mask=0; for(uint32_t offset=0;offset<3;++offset) if(linked.count(first+offset)) mask|=1u<<offset;
        gate.store(mask,std::memory_order_release);
    });
    net.setSimMeta(service,12.5,40,true);
    net.setPlatformTelemetry(R"([{"platform_id":77,"name":"fixture aircraft","position":[100,1250.5,-25],"velocity":[80,0,0],"speed":80,"yaw":90,"pitch":0,"roll":2.5,"sim_time":12.5}])");
    net.setEarthContext(earthFixture().dump());
    sendByte(control,'R'); expectByte(control,'A',deadline);
    require(gate.load(std::memory_order_acquire)==7,"simulation link gate did not open for all selected sensors");
    IrFrame infrared; infrared.surface_revision=earthFixture().at("surface_revision"); infrared.sensor_id=first; infrared.resize(kImageSide,kImageSide); infrared.stage=0; for(size_t pixel=0;pixel<infrared.radiance.size();++pixel) infrared.radiance[pixel]=radianceAt(pixel);
    RadarFrame radar; radar.sensor_id=first+1; radar.resize(2,2); radar.stage=2; radar.sample_rate_hz=30e6; radar.prf=40; radar.coherent_pulses=2; radar.rd_map={{1,2},{3,4},{5,6},{7,8}};
    SarFrame sar; sar.sensor_id=first+2; sar.resize(2,2); sar.stage=0; sar.sample_rate_hz=20e6f; sar.range_min=5; sar.echo_data={{1,2},{3,4},{5,6},{7,8}};
    while(!ready(control,10)) {
        require(deadline.remaining()>0,"display did not receive measurements before deadline");
        require(broker.publishIr(infrared) && broker.publishRadar(radar) && broker.publishSar(sar),"simulation rejected valid fixture measurements");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    expectByte(control,'F',deadline); sendByte(control,'C'); expectByte(control,'P',deadline);
    while(broker.isEquipmentPowered(first) && deadline.remaining()>0) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    require(!broker.isEquipmentPowered(first),"display control only changed local UI state, not simulation power");
    sendByte(control,'O'); expectByte(control,'D',deadline);
    require(gate.load(std::memory_order_acquire)==0 && !net.linkAllows(first) && !net.linkAllows(first+1) && !net.linkAllows(first+2),"simulation gate stayed open after remote detach");
    sendByte(control,'G'); sar.frame_id=999;
    for(int attempt=0;attempt<3;++attempt) require(broker.publishSar(sar),"unlinked publication fixture was not accepted locally");
    sendByte(control,'Q'); expectByte(control,'X',deadline);
    int status=0; bool exited=false;
    while(deadline.remaining()>0) {
        const pid_t result=::waitpid(child.pid,&status,WNOHANG);
        if(result==child.pid) { child.pid=-1; exited=true; break; }
        require(result>=0 || errno==EINTR,"waitpid failed"); std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(exited && WIFEXITED(status) && WEXITSTATUS(status)==0,"display child failed or exceeded deadline");
    net.stop();
    require(!std::filesystem::exists(directory.path+"/"+service+".sock"),"simulation socket was not released");
    std::cout<<"PASS: real exec-separated BrokerNet discovery, typed data, telemetry, power control, link gate and isolated cleanup\n";
    return 0;
}
}
int main(int argc,char** argv) {
    std::set_terminate([]{void* frames[40]; const int n=::backtrace(frames,40);::backtrace_symbols_fd(frames,n,2);std::_Exit(90);});
    try {
        if(argc==4 && std::string(argv[1])=="--display") return display(argv[2],uint32_t(std::stoul(argv[3])));
        const auto executable=std::filesystem::read_symlink("/proc/self/exe").string();
        return simulation(executable.c_str());
    } catch(const std::exception& error) {
        if(argc>1 && std::string(argv[1])=="--display") { const char failure='E'; ::send(3,&failure,1,MSG_NOSIGNAL); }
        std::cerr<<"FAIL: "<<error.what()<<'\n'; return 1;
    }
}
