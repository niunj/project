#include <simcore/Orientation.h>
#include <vsgraysim/layer0_phys/PhysicsEngine.h>
#include <vsgraysim/layer4_sched/SensorScheduler.h>
#include <vsgraysim/core/ScenarioClockConfig.h>
#include <vsgraysim/Simulation.h>
#include <vsgraysim/layer2_rt/RayTracer.h>
#include <vsgraysim/layer3b_signal/SensorSignalProcessor.h>
#include <vsgraysim/layer3b_signal/CameraProjection.h>
#include <vsgraysim/broker/EquipmentDataSource.h>
#include <vsgraysim/broker/SimDataBroker.h>
#include <simcore/ObjectId.h>
#include "PlatformManager.h"
#include "PlatformMotionFactory.h"
#include <btBulletDynamicsCommon.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <cstdlib>

namespace {
void check(bool ok,const char* text) { if (!ok) throw std::runtime_error(text); }
void rotationsAndHistory() {
    using namespace simcore;
    check(vsg::length(headingRotation(90)*vsg::dvec3(0,0,-1)-vsg::dvec3(1,0,0))<1e-12,"heading points opposite to motion");
    for (double yaw:{0.,25.,90.,181.,359.}) for (double pitch:{-70.,0.,40.}) for (double roll:{-50.,0.,35.}) {
        const auto q=headingRotation(yaw,pitch,roll);
        const auto a=headingAngles(q);
        const auto r=headingRotation(a.x,a.y,a.z);
        check(vsg::length(q*vsg::dvec3(1,2,3)-r*vsg::dvec3(1,2,3))<1e-10,"attitude roundtrip");
    }
    RigidStateHistory history;
    RigidState a,b,out; a.time=0; b.time=nanosecondsPerSecond;
    b.position={1,0,0}; b.velocity={2,0,0}; b.orientation=headingRotation(90);
    a.angular_velocity=b.angular_velocity={0,-3.141592653589793/2,0};
    check(history.push(a)&&history.push(b)&&history.sample(nanosecondsPerSecond/2,out),"rigid history sampling");
    check(std::abs(out.position.x-.25)<1e-12 && std::abs(out.velocity.x-1)<1e-12,"accelerating trajectory");
    check(vsg::length(out.orientation*vsg::dvec3(0,0,-1)-headingRotation(45)*vsg::dvec3(0,0,-1))<1e-10,"attitude not interpolated");
    const auto v=out.pointVelocity({2,0,0})-out.velocity;
    check(std::abs(vsg::length(v)-3.141592653589793)<1e-10,"rotating mount lever-arm velocity");
    check(!history.sample(2*nanosecondsPerSecond,out),"stale world truth extrapolated");
}
vsg::ref_ptr<vsg::VertexIndexDraw> cube() {
    auto draw=vsg::VertexIndexDraw::create();
    auto vertices=vsg::vec3Array::create({{0,0,0},{2,0,0},{2,4,0},{0,4,0},{0,0,6},{2,0,6},{2,4,6},{0,4,6}});
    auto indices=vsg::ushortArray::create({0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5});
    draw->assignArrays({vertices}); draw->assignIndices(indices); draw->indexCount=indices->size(); draw->instanceCount=1;
    return draw;
}
void physics() {
    using namespace vsgraysim;
    PhysicsEngine physics; physics.setGravity({0,0,0});
    RigidBodyDef def; def.object_id="vehicle"; def.start_position={20000,10,30000};
    def.start_rotation_deg={20,90,10}; def.mass=1000; def.use_convex_hull_shape=true;
    auto node=physics.createRigidBodyFromMesh(cube(),def);
    check(bool(node)&&physics.bodyLocalTriangles(0).size()==36,"indexed collision triangles lost");
    check(vsg::length(physics.position("vehicle")-def.start_position)<1e-4,"model origin confused with COM");
    const auto direction=physics.rotation("vehicle")*vsg::dvec3(0,0,-1);
    check(vsg::length(direction-simcore::headingRotation(90,20,10)*vsg::dvec3(0,0,-1))<1e-6,"initial body attitude");
    const auto old=physics.snapshot();
    physics.setVelocity("vehicle",{1,0,0});
    physics.step(.2);
    check(std::abs(physics.position("vehicle").x-20000.2)<1e-4,"elapsed physics time was truncated or lost far from origin");
    check(old->time==0 && std::abs(old->bodies.at("vehicle").position.x-20000)<1e-5,"published snapshot mutated");
    simcore::RigidState state;
    check(physics.sampleBody("vehicle",.125,state)&&std::abs(state.position.x-20000.125)<1e-4,"physics substep truth unavailable");
    const auto prior=physics.position("vehicle");
    physics.shiftOrigin({19000,0,29000});
    check(vsg::length(physics.position("vehicle")-prior)<1e-4,"rebase changed canonical world pose");
    auto* body=physics.nativeBody("vehicle");
    platform::PlatformManager manager; manager.initialize({}); manager.setPhysicsEngine(&physics);
    platform::MotionState initial; initial.position=def.start_position;
    auto pid=manager.createPlatform("vehicle",platform::PlatformCategory::GROUND_VEHICLE,{{"motion_authority","dynamic"}},initial);
    check(pid>0 && physics.nativeBody("vehicle")==body && physics.transformNode("vehicle")==node,
          "platform replaced the scene collider or visual transform");
    check(physics.world()->getDynamicsWorld()->getNumCollisionObjects()==1,"duplicate collider after platform attachment");
    manager.shutdown();
    check(physics.bodyExists("vehicle"),"platform teardown destroyed the scene body's ownership");
    physics.addGroundPlane(0);
    double height; vsg::dvec3 normal;
    check(physics.groundAt(20000,30000,height,normal)&&std::abs(height)<1e-3 && normal.y>.99,
          "ground query after floating-origin rebase hit a moving platform or misplaced plane");
    simcore::RigidState load; load.position={20020,20,30000};
    physics.createBoxBody("load",{.5,.5,.5},load,10);
    physics.applyCentralForce("load",{10,0,0}); physics.step(.2);
    check(std::abs(physics.velocity("load").x-.2)<1e-5,"force was lost after the first explicit physics substep");
    physics.setGravity({0,-9.81,0});
    simcore::RigidState drop; drop.position={20010,5,30000};
    physics.createBoxBody("drop",{.5,.5,.5},drop,10); physics.step(2);
    check(std::abs(physics.position("drop").y-.5)<.03 && std::abs(physics.velocity("drop").y)<.05,
          "rebased ground did not collide with a falling body");

    PhysicsEngine kinematic; kinematic.setGravity({0,0,0});
    simcore::RigidState start,target; start.position={0,5,0};
    kinematic.createBoxBody("drone",{1,1,1},start,10); kinematic.setKinematic("drone",true);
    target=start; target.time=simcore::nanosecondsPerSecond; target.position={10,5,0}; target.orientation=simcore::headingRotation(90);
    kinematic.setKinematicTarget("drone",target); kinematic.step(1);
    check(kinematic.sampleBody("drone",.5,state)&&std::abs(state.position.x-5)<1e-5,"prescribed motion not sampled on physics substeps");
    check(vsg::length(state.orientation*vsg::dvec3(0,0,-1)-simcore::headingRotation(45)*vsg::dvec3(0,0,-1))<1e-5,"kinematic attitude history");
    kinematic.removeBody("drone");
    check(!kinematic.bodyExists("drone") && !kinematic.sampleBody("drone",.5,state),"removed body's truth remains accessible");
}
void pulses() {
    using namespace vsgraysim;
    auto rules=std::make_shared<CrossInductionRules>();
    rules->loadConfigFromJson({{"sensor_list",{{{"sensor_id",1},{"type","radar"},{"tx_params",{{"power",1},{"pulse_rate",1000},{"wavelength",.03},{"beam_width",.1},{"duty_cycle",.1}}}}}}});
    std::vector<double> baseline;
    for (int frameRate:{30,60,144}) {
        SensorScheduler scheduler(rules); simcore::PeriodicTimeline frames(frameRate);
        std::vector<double> times;
        for (int i=1;i<=frameRate;++i) {
            const double end=simcore::toSeconds(frames.at(i)), begin=simcore::toSeconds(frames.at(i-1));
            scheduler.buildFrameSchedule(end-begin,end);
            for (const auto& tx:scheduler.schedule()) {
                check(tx.absolute_time>=begin && tx.absolute_time<end,"pulse outside half-open frame");
                check(tx.pulse_index==times.size(),"pulse ordinal reset each frame");
                times.push_back(tx.absolute_time);
            }
        }
        check(times.size()==1000,"configured PRF changed with simulation frame rate");
        if (baseline.empty()) baseline=times; else check(times==baseline,"pulse timestamps depend on rendering rate");
    }
}
void historicalGeometryAndMaterials() {
    using namespace vsgraysim;
    PhysicsEngine physics; physics.setGravity({0,0,0});
    RigidBodyDef moving; moving.object_id="moving"; moving.mass=100;
    moving.start_position={0,0,-20}; moving.initial_velocity={0,0,2}; moving.use_convex_hull_shape=true;
    physics.createRigidBodyFromMesh(cube(),moving);
    auto parameters=std::make_shared<PhysParamManager>();
    nlohmann::json a={{"object_id","moving"},{"face_count",12}}, b={{"object_id","fixed"},{"face_count",12},
        {"vis_params",{{"albedo",.8}}}};
    a["face_groups"]=nlohmann::json::array();
    for(int i=0;i<12;++i) a["face_groups"].push_back({{"group_id",std::to_string(i)},
        {"face_range",{i,i}},{"vis_params",{{"albedo",double(i+1)/20}}}});
    parameters->loadObjectConfigFromJson(a); parameters->loadObjectConfigFromJson(b); parameters->buildGeoUnitArray();
    check(parameters->findFace("moving",0)->geo_unit_id!=parameters->findFace("fixed",0)->geo_unit_id,
          "local face ids collided across objects");
    parameters->updateDynamicParams("fixed",0,25);
    check(parameters->findFace("moving",0)->rcs_modify_factor==1 && parameters->findFace("moving",0)->temp_modify_offset==0,
          "material update leaked into another object's local face zero");
    auto rules=std::make_shared<CrossInductionRules>();
    RayTracer tracer(std::make_shared<AtmosphereModel>(),nullptr,rules);
    tracer.setPhysicsEngine(&physics); tracer.setPhysicalParameters(parameters); tracer.setAtmosphereEnabled(false);
    physics.step(1);
    RayTraceConfig ray; ray.sensor_id=30; ray.sensor_type=SensorType::VIS;
    ray.origin={.7f,1.2f,10}; ray.direction={0,0,-1}; ray.wavelength=550e-9f; ray.max_distance=100; ray.num_rays=1;
    ray.measurement_time=0;
    auto early=tracer.traceSensor(ray);
    check(early.size()==1 && std::abs(early[0].time_delay*299792458.-24)<1e-4,
          "dynamic-only scene used Bullet's frame-end pose");
    check(early[0].object_id==simcore::objectId("moving") && early[0].geo_unit_id==parameters->findFace("moving",2)->geo_unit_id &&
          std::abs(early[0].amplitude-.15)<1e-6,"BVH reordering lost the hit primitive/material or passive response");
    ray.measurement_time=.5;
    auto middle=tracer.traceSensor(ray);
    check(middle.size()==1 && std::abs(middle[0].time_delay*299792458.-23)<1e-4,"per-measurement target motion was ignored");
    ray.measurement_time=1; ray.beam_width=.02f; ray.num_rays=20; ray.sample_index=123;
    const auto first=tracer.traceSensor(ray);
    auto other=ray; other.measurement_time=.5; other.sample_index=122;
    const auto batched=tracer.traceFrame({ray,other});
    check(!first.empty() && batched.size()>=first.size(),"batch geometry query lost hits");
    for(std::size_t i=0;i<first.size();++i) check(first[i].position.x==batched[i].position.x &&
        first[i].position.z==batched[i].position.z && first[i].i_amplitude==batched[i].i_amplitude,
        "ray samples depend on host batching or worker scheduling");
    for(int batch=0;batch<300;++batch)
        check(tracer.traceFrame({other,other}).size()==2*tracer.traceSensor(other).size(),
              "parallel ray-query batch was incomplete");
    ray.measurement_time=1.1;
    bool rejected=false;
    try { tracer.traceFrame({ray,other}); } catch(const std::runtime_error&) { rejected=true; }
    check(rejected,"future target truth was replaced with the latest pose");

    RigidBodyDef fixed; fixed.object_id="fixed"; fixed.start_position={10,0,-40};
    physics.createRigidBodyFromMesh(cube(),fixed);
    ray=other; ray.origin={10.7f,1.2f,10}; ray.beam_width=0; ray.num_rays=1; ray.measurement_time=1;
    auto late=tracer.traceSensor(ray);
    check(late.size()==1 && late[0].object_id==simcore::objectId("fixed") && std::abs(late[0].amplitude-.8)<1e-6,
          "late static geometry or per-object materials were omitted");
    physics.setKinematic("fixed",true);
    simcore::RigidState target; target.time=simcore::fromSeconds(2); target.position={30,0,-40};
    physics.setKinematicTarget("fixed",target); physics.step(1);
    ray.measurement_time=2;
    check(tracer.traceSensor(ray).empty(),"static BVH remained after a body switched motion authority");
    physics.removeBody("fixed");
    check(tracer.traceSensor(ray).empty(),"deleted static geometry still blocks sensor rays");

    SignalFormationConfig camera;
    camera.sensor_position={100,20,30}; camera.sensor_direction={1,0,0}; camera.sensor_up={0,0,1};
    CameraProjection projection(camera);
    double x,y,depth;
    check(projection.point({110,20,30},x,y,depth)&&std::abs(x)<1e-12&&std::abs(y)<1e-12&&depth==10,
          "camera projection ignored translated/rotated aperture");
    check(projection.point({110,20,31},x,y,depth)&&y==-1,"camera roll or image row convention was lost");
    check(!projection.point({90,20,30},x,y,depth),"camera accepted a point behind its aperture");
    IrThermalProcessor previousFrames,isolatedFrame;
    camera.image_width=8; camera.image_height=6; camera.frame_id=16;
    previousFrames.process({},camera,31);
    camera.frame_id=17;
    check(previousFrames.process({},camera,31).samples==isolatedFrame.process({},camera,31).samples,
          "detector noise depends on skipped measurements or host batching");
}
void scenarioClocks() {
    using namespace vsgraysim;
    const auto clock=ScenarioClockConfig::fromJson({{"frame_dt",.02},{"physics_step",.005},
        {"history_seconds",12},{"epoch_utc","2024-02-29T23:59:59Z"},{"max_frames",42}});
    const auto saved=clock.toJson();
    check(saved["max_frames"]==42 && saved["physics_step"]==.005,"editor discarded simulation settings");
    check(simcore::parseUtcEpoch(clock.epoch_utc).day==29,"valid leap-day epoch rejected");
    for (const auto& invalid:{nlohmann::json{{"frame_dt",0}},nlohmann::json{{"physics_step",-1}},
          nlohmann::json{{"history_seconds",.001}},nlohmann::json{{"epoch_utc","2023-02-29T12:00:00Z"}}}) {
        bool rejected=false;
        try { ScenarioClockConfig::fromJson(invalid); } catch(const std::exception&) { rejected=true; }
        check(rejected,"invalid scene clock accepted");
    }
}
void headlessWiring() {
    using namespace vsgraysim;
    struct Scratch {
        std::filesystem::path root;
        Scratch() {
            auto pattern=(std::filesystem::temp_directory_path()/"vsgraysim_truth_XXXXXX").string();
            const auto* created=mkdtemp(pattern.data());
            if (!created) throw std::runtime_error("cannot create isolated scenario fixture");
            root=created;
        }
        ~Scratch() { std::error_code ec; std::filesystem::remove_all(root,ec); }
    } scratch;
    const auto mesh=scratch.root/"cube.obj", scene=scratch.root/"scene.json";
    {
        std::ofstream out(mesh);
        out<<"v 0 0 0\nv 2 0 0\nv 2 4 0\nv 0 4 0\nv 0 0 6\nv 2 0 6\nv 2 4 6\nv 0 4 6\n"
              "f 1 3 2\nf 1 4 3\nf 5 6 7\nf 5 7 8\nf 1 2 6\nf 1 6 5\nf 4 8 7\nf 4 7 3\nf 1 5 8\nf 1 8 4\nf 2 3 7\nf 2 7 6\n";
    }
    nlohmann::json j;
    j["simulation"]={{"frame_dt",.1},{"physics_step",.01},{"history_seconds",2},{"use_gpu_rt",false},
        {"epoch_utc","2024-02-29T23:59:59Z"}};
    j["objects"]={{{"object_id","vehicle"},{"mesh_path",mesh.string()},{"face_count",12},
        {"physics",{{"mass",1000},{"initial_position",{100,10,0}},{"initial_rotation_deg",{0,90,0}},{"convex_hull",true}}}}};
    j["sensors"]["sensor_list"]={{{"sensor_id",7},{"type","radar"},{"platform_object","vehicle"},
        {"tx_params",{{"power",0},{"pulse_rate",1000},{"wavelength",.03},{"beam_width",.1},{"duty_cycle",.1}}},
        {"aperture",{{"position_offset",{2,0,0}},{"direction",{0,0,-1}}}}}};
    for(int id:{8,9}) j["sensors"]["sensor_list"].push_back({{"sensor_id",id},{"type","ir"},
        {"platform_object","vehicle"},{"tx_params",{{"power",0},{"wavelength",10e-6}}},
        {"signal_params",{{"image_width",8},{"image_height",6},{"integration_time",.005},
            {"frame_interval_s",id==8 ? .02 : .025}}},
        {"aperture",{{"position_offset",{0,10,0}},{"direction",{0,0,-1}}}}});
    { std::ofstream out(scene); out<<j; }
    Simulation sim;
    check(sim.initFromScenario(scene.string()),"headless scenario did not initialize");
    Simulation::SensorPose pose;
    check(sim.sampleSensorPose(7,0,pose)&&vsg::length(pose.position-vsg::dvec3(100,10,2))<1e-5,
          "headless engine did not mount sensor in the platform frame");
    check(vsg::length(pose.direction-vsg::dvec3(1,0,0))<1e-6,"headless boresight ignored platform attitude");
    sim.physics()->setGravity({0,0,0}); sim.physics()->setAngularVelocity("vehicle",{0,-1,0});
    sim.physics()->setVelocity("vehicle",{1,0,0});
    check(sim.step(.1,.1)&&sim.sampleSensorPose(7,.05,pose),"engine sensor history unavailable inside a frame");
    check(vsg::length(pose.direction-simcore::headingRotation(90+.05*180/3.141592653589793)*vsg::dvec3(0,0,-1))<1e-4,
          "sensor used frame-end attitude for historical measurement");
    check(!sim.sampleSensorPose(7,.2,pose),"engine extrapolated future measurement truth");
    check(sim.physics()->snapshot()->epoch_utc=="2024-02-29T23:59:59Z","world snapshot lost the scenario epoch");
    check(sim.completedFrames().size()==9,"passive exposures were skipped or collapsed into one host frame");
    std::map<uint32_t,uint64_t> counts;
    double previous=0;
    for(const auto& frame:sim.completedFrames()) {
        check(frame.sim_time>=previous && frame.frame_id==counts[frame.sensor_id]++,"completed measurement order or per-sensor sequence");
        check(std::abs(frame.sim_time-(frame.frame_id+1)*(frame.sensor_id==8 ? .02 : .025))<1e-12,
              "image time depends on host frame boundaries");
        previous=frame.sim_time;
    }
    auto& broker=SimDataBroker::instance();
    struct ResetBroker { SimDataBroker& broker; ~ResetBroker() { broker.setFrameTap({},{},{},{}); broker.serverOnline(false); } } reset{broker};
    broker.serverOnline(true);
    std::vector<std::pair<uint32_t,double>> received;
    broker.setFrameTap({},{},[&](const IrFrame& frame) { received.emplace_back(frame.sensor_id,frame.sim_time); },{});
    EquipmentDataSource source(&sim); check(source.initialize({}),"data source did not initialize");
    TimeTick tick; tick.dt=.1; tick.sim_time=.2;
    source.onTick(tick);
    check(source.lastTickOk() && received.size()==9 && broker.channelStats(8).published_frames==5 &&
          broker.channelStats(9).published_frames==4,"data source delivered only the last image of a host frame");
    for(std::size_t i=1;i<received.size();++i) check(received[i].second>=received[i-1].second,"broker delivery time order");
    check(sim.runHeadless(2)==2 && sim.physics()->snapshot()->time==simcore::fromSeconds(.4),
          "headless continuation reset time or reported a cumulative frame count");
    check(sim.traceSensor(7,1) && sim.physics()->snapshot()->time==simcore::fromSeconds(.5),
          "single-sensor diagnostic rewound the running physical world");
    auto rules=std::make_shared<CrossInductionRules>();
    PhysicsEngine empty;
    RayTracer tracer(std::make_shared<AtmosphereModel>(),nullptr,rules);
    tracer.setPhysicsEngine(&empty); tracer.setAtmosphereEnabled(false);
    RayTraceConfig ray; ray.sensor_type=SensorType::RADAR; ray.sensor_id=7; ray.origin={0,0,0};
    ray.direction={0,0,-1}; ray.num_rays=1; ray.tx_power=1; ray.wavelength=.03f; ray.max_distance=1000;
    check(tracer.traceSensor(ray).empty(),"empty space generated a fabricated target");
    const auto atmosphereFile=scratch.root/"atmosphere.json";
    {
        std::ofstream out(atmosphereFile);
        out<<nlohmann::json{{"layers",{
            {{"height_min",0},{"height_max",50},{"ext_coeff",.1},{"scatter_coeff",0},{"refract_index",1}},
            {{"height_min",50},{"height_max",100},{"ext_coeff",.5},{"scatter_coeff",0},{"refract_index",1}}}}};
    }
    auto atmosphere=std::make_shared<AtmosphereModel>(); atmosphere->loadConfig(atmosphereFile.string());
    PhysicsEngine ground; ground.addGroundPlane();
    RayTracer transport(atmosphere,nullptr,rules); transport.setPhysicsEngine(&ground);
    ray.origin={100,2,0}; ray.direction={0,-1,0}; ray.sensor_type=SensorType::IR;
    ray.wavelength=10e-6f; ray.band_mask=BAND_IR;
    auto passive=transport.traceSensor(ray);
    check(passive.size()==1 && std::abs(passive[0].atm_atten-std::exp(-.2))<1e-6,
          "passive transport doubled the path, overran a short ray, or used horizontal position as altitude");
    ray.sensor_type=SensorType::RADAR; ray.wavelength=.03f; ray.band_mask=BAND_RADAR;
    auto active=transport.traceSensor(ray);
    check(active.size()==1 && std::abs(active[0].atm_atten-std::exp(-.4))<1e-6,"active radar lost two-way propagation");
    // The long RTX route exposed a half-nanosecond drift after 43,430 steps.
    // Exercise the normal engine entry point past that boundary without GPU work.
    const auto clockScene=scratch.root/"long-clock.json";
    nlohmann::json clockJson={{"simulation",{{"sensor_backend","cpu"},{"use_gpu_rt",false},
        {"frame_dt",.02},{"physics_step",.01}}},{"objects",nlohmann::json::array()},
        {"sensors",{{"sensor_list",nlohmann::json::array()}}}};
    {std::ofstream out(clockScene);out<<clockJson;}
    Simulation clock;
    check(clock.initFromScenario(clockScene.string()),"long clock scenario initialization failed");
    for(int i=1;i<=60000;++i) {
        check(clock.step(.02),"long simulation clock failed");
        check(clock.physics()->snapshot()->time==simcore::TimePoint(i)*20000000,
              "repeated simulation steps drifted from the nanosecond schedule");
    }
    j["simulation"]["frame_dt"]=0;
    { std::ofstream out(scene); out<<j; }
    check(!sim.initFromScenario(scene.string()),"invalid imported scene clock accepted");
}
void realPulseData() {
    using namespace vsgraysim;
    RadarPulseProcessor empty;
    SignalFormationConfig cfg;
    cfg.fs=1e6f; cfg.chirp_duration=8e-6f; cfg.chirp_bandwidth=5e5f;
    cfg.range_gates=32; cfg.doppler_bins=8; cfg.carrier_hz=10e9; cfg.prf=1000;
    cfg.pulses_in_frame=1;
    const auto zero=empty.process({},cfg,1);
    check(!zero.empty()&&zero.coherent_pulses==1,"empty real pulse was discarded instead of advancing CPI");
    for (auto value:zero.samples) check(std::abs(value)==0,"empty pulse reused target data");
    RadarPulseProcessor stationary;
    ScatterPoint point{}; point.position={30,0,300}; point.i_amplitude=1; point.amplitude=1;
    SignalFrame frame;
    for (int i=0;i<8;++i) { cfg.sim_time=double(i)/cfg.prf; cfg.frame_id=i; frame=stationary.process({point},cfg,1); }
    auto peak=std::max_element(frame.samples.begin(),frame.samples.end(),[](auto a,auto b){return std::norm(a)<std::norm(b);});
    check(std::norm(*peak)>0 && std::size_t(peak-frame.samples.begin())/frame.width==0,
          "stationary off-axis target acquired invented Doppler or transposed axes");
    check(std::abs(frame.range_axis[1]-299792458.0f/(2*frame.fs))<1e-3,"CPU RD sampling metadata disagrees with the data");
    check(frame.coherent_pulses==8 && std::abs(frame.sim_time-.007)<1e-12,"CPI metadata lost the pulse timestamp");
    cfg.chirp_bandwidth*=.5f; cfg.sim_time=.008;
    const auto changed=stationary.process({point},cfg,1);
    RadarPulseProcessor fresh;
    const auto expected=fresh.process({point},cfg,1);
    check(changed.coherent_pulses==1 && changed.samples==expected.samples,
          "waveform change retained stale matched filter or CPI profiles");
    cfg.doppler_bins=7;
    bool rejected=false;
    try { fresh.process({point},cfg,1); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"non-radix-2 Doppler dimension allowed unsafe FFT indexing");
}
}
int main() {
    try { rotationsAndHistory(); physics(); pulses(); historicalGeometryAndMaterials(); scenarioClocks(); headlessWiring(); realPulseData(); std::cout<<"PASS: world truth, attitude, COM, rebasing, ownership, kinematic motion, PRF, scene clocks, headless wiring and real-pulse data\n"; }
    catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
