#include "PlatformManager.h"
#include "AircraftMotion.h"
#include <vsgraysim/broker/FlightControl.h>
#include <vsgraysim/layer0_phys/PhysicsEngine.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <limits>
using namespace vsgraysim;
using namespace vsgraysim::platform;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
FlightControlCommand command(uint32_t id,const std::string& client="pilot") {
    FlightControlCommand c;c.request_id="request";c.client_id=client;c.session_id="session";c.platform_id=id;
    c.mode="manual";c.throttle=1;c.elevator=.2;c.aileron=.4;c.rudder=.1;return c;
}
void advance(PlatformManager& manager,PhysicsEngine& physics,double& time,double dt=.1) {
    TimeTick tick;tick.dt=dt;time+=dt;tick.sim_time=time;manager.onPreTick(tick);physics.step(dt);manager.onPostTick(tick);
}
void realExecution() {
    PhysicsEngine physics;physics.setGravity({0,0,0});
    PlatformManager manager;manager.initialize({});manager.setPhysicsEngine(&physics);
    MotionState initial;initial.position={0,1000,0};initial.velocity={0,0,-100};
    const auto id=manager.createPlatform("aircraft",PlatformCategory::AIRCRAFT,{{"altitude_hold",true}},initial);
    require(std::abs(manager.getPlatformSnapshot(id)["speed"].get<double>()-100)<1e-5,"fallback body discarded initial velocity");
    const auto initial_control=manager.getPlatformSnapshot(id)["flight_control"];
    require(std::abs(initial_control["command"]["throttle"].get<double>()-.2)<1e-6 && initial_control["limits"]["max_speed"]==300,"initial command ignored current motion or omitted airframe limits");
    auto other=initial;other.position.x=1000;
    const auto second=manager.createPlatform("uav",PlatformCategory::AIRCRAFT,{},other);
    manager.onStart();
    RoutePlan route;route.waypoints={{0,{0,1000,0},100},{100,{0,1000,-10000},100}};
    require(manager.assignRoute(id,route),"route setup failed");
    auto c=command(id);auto result=manager.applyFlightControl(c,0);
    require(result.stage=="applied" && result.revision==1,"manual control was not installed");
    auto before=manager.getPlatformSnapshot(id);
    require(before["flight_control"]["actual"].empty(),"actuator state fabricated before first controlled tick");
    require(manager.applyFlightControl(command(id,"other"),0).code=="control_owned","second controller stole control");
    double time=0;for(int i=0;i<5;++i)advance(manager,physics,time);
    auto after=manager.getPlatformSnapshot(id);
    require(after["speed"].get<double>()>100.1 && after["pitch"].get<double>()>.1 && after["roll"].get<double>()>.1,"normalized controls did not change Bullet motion or route overwrote manual control");
    require(after["position"][2].get<double>()<-10 && after["position"][1].get<double>()>1000,"elevator did not generate climb");
    require(after["flight_control"]["actual"]["throttle"].get<double>()<1,"throttle slew limit bypassed");
    require(manager.getPlatformSnapshot(second)["flight_control"]["owner_client_id"]=="","per-platform ownership leaked");
    auto invalid=c;invalid.elevator=std::numeric_limits<double>::quiet_NaN();
    require(manager.applyFlightControl(invalid,time).stage=="rejected","nonfinite demand accepted");
    auto hold=c;hold.mode="altitude_hold";hold.target_altitude=0;hold.target_speed=100;
    require(manager.applyFlightControl(hold,time).code=="outside_flight_envelope","invalid hold target accepted");
    hold.target_altitude=1100;hold.target_heading=30;
    require(manager.applyFlightControl(hold,time).stage=="applied","hold control rejected");
    hold.release=true;
    require(manager.applyFlightControl(hold,time).code=="released","explicit release failed");
    const auto released=manager.getPlatformSnapshot(id)["flight_control"];
    require(released["mode"]=="stabilized" && released["owner_client_id"]=="" && released["failsafe"]==true,"release returned to an old route");
    require(std::abs(released["command"]["throttle"].get<double>()-(after["speed"].get<double>()-50)/250)<1e-6,"release retained a stale full-throttle demand");
    auto c2=command(id,"newpilot");require(manager.applyFlightControl(c2,time).stage=="applied","release did not permit handover");
    std::this_thread::sleep_for(std::chrono::milliseconds(2050));
    manager.updateFlightControlLeases(time); // No simulation tick: paused lease must expire.
    const auto expired=manager.getPlatformSnapshot(id)["flight_control"];
    require(expired["mode"]=="stabilized" && expired["owner_client_id"]=="" && expired["revision"].get<uint64_t>()>released["revision"].get<uint64_t>(),"paused stale manual control did not expire");
    const auto held_altitude=expired["command"]["target_altitude"].get<double>();
    require(std::abs(held_altitude-manager.getPlatformSnapshot(id)["position"][1].get<double>())<1e-6,"failsafe captured an old altitude");
    manager.applyFlightControl(c2,time);manager.resetFlightControls();
    require(manager.getPlatformSnapshot(id)["flight_control"]["mode"]=="stabilized","reset retained live control");
    auto route_command=command(second);route_command.mode="route";
    require(manager.applyFlightControl(route_command,time).code=="route_unavailable","missing route accepted");
    route_command.platform_id=id;
    require(manager.applyFlightControl(route_command,1000).code=="route_expired","expired route resumed");
    require(manager.applyFlightControl(route_command,time).stage=="applied","valid route did not resume");
    std::cout<<"PASS: real Bullet execution, rate-limited inputs, route override, hold, ownership, release, paused lease expiry and reset\n";
}

void gravityAndRoute() {
    PhysicsEngine physics;physics.setGravity({0,-9.81,0});
    PlatformManager manager;manager.initialize({});manager.setPhysicsEngine(&physics);manager.onStart();
    MotionState initial;initial.position={0,1000,0};initial.velocity={0,0,-100};
    const auto id=manager.createPlatform("gravity_aircraft",PlatformCategory::AIRCRAFT,{},initial);
    auto hold=command(id);hold.mode="altitude_hold";hold.target_speed=100;hold.target_altitude=1000;hold.target_heading=0;
    require(manager.applyFlightControl(hold,0).stage=="applied","gravity hold installation failed");
    double time=0;for(int i=0;i<200;++i)advance(manager,physics,time,.05);
    auto state=manager.getPlatformSnapshot(id);
    require(std::abs(state["position"][1].get<double>()-1000)<5 && std::abs(state["speed"].get<double>()-100)<3,"hold diverged under real nonzero gravity");
    // A climbing, accelerating route must drive both targets, not just its UI mode.
    RoutePlan route;route.waypoints={{0,{0,1000,0},100},{60,{0,1600,-7200},180}};
    manager.assignRoute(id,route);auto follow=command(id);follow.mode="route";
    require(manager.applyFlightControl(follow,time).stage=="applied","climbing route was not installed");
    const double previous_altitude=state["position"][1].get<double>();
    for(int i=0;i<200;++i)advance(manager,physics,time,.05);
    state=manager.getPlatformSnapshot(id);
    require(state["speed"].get<double>()>120 && state["position"][1].get<double>()>previous_altitude+30,"aircraft route ignored its commanded speed or altitude");
    require(!state["flight_control"]["actual"].empty(),"route controller omitted real actuator feedback");
    std::cout<<"PASS: altitude hold under gravity and climbing/accelerating aircraft route execution\n";
}
void capabilitiesAndParameters() {
    PlatformManager manager;manager.initialize({});MotionState initial;initial.position={0,1000,0};initial.velocity={0,0,-100};
    const auto disabled=manager.createPlatform("disabled",PlatformCategory::AIRCRAFT,{{"flight_control_enabled",false}},initial);
    const auto kinematic=manager.createPlatform("prescribed",PlatformCategory::AIRCRAFT,{{"motion_authority","kinematic"}},initial);
    const auto ground=manager.createPlatform("ground",PlatformCategory::GROUND_VEHICLE,{},initial);
    require(manager.applyFlightControl(command(disabled),0).code=="disabled","disabled aircraft controlled");
    require(manager.applyFlightControl(command(kinematic),0).code=="kinematic_authority","prescribed trajectory controlled");
    require(manager.applyFlightControl(command(ground),0).code=="unsupported_platform","ground platform accepted flight controls");
    require(manager.getPlatformSnapshot(kinematic)["flight_control"]["available"]==false,"unavailable capability falsely advertised");
    const auto count=manager.platformCount();bool rejected=false;
    try{manager.createPlatform("invalid",PlatformCategory::AIRCRAFT,{{"min_speed",300},{"max_speed",200}},initial);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected && manager.platformCount()==count,"invalid envelope registered a partial platform");
    AircraftMotion a,b;a.initialize({},initial);b.initialize({},initial);MotionState sa=initial,sb=initial;vsgraysim::platform::ControlCommand controls;
    controls.flight_mode="manual";controls.throttle=.9;controls.aileron=.4;controls.elevator=.2;controls.rudder=.1;EnvironmentContext env;
    for(int i=0;i<100;++i)a.update(.01,env,controls,sa);
    for(int i=0;i<200;++i)b.update(.005,env,controls,sb);
    require(vsg::length(sa.position-sb.position)<.5 && std::abs(sa.roll-sb.roll)<.5 && std::abs(sa.pitch-sb.pitch)<.5,"manual control depends materially on integration cadence");
    std::cout<<"PASS: capability rejection, transactional envelope validation and controller timestep consistency\n";
}
}
int main(){try{realExecution();gravityAndRoute();capabilitiesAndParameters();return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
