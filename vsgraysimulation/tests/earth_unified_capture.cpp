#include <vsgraysim/earth/VoxelSensorSession.h>
#include <vsgraysim/earth/VoxelSceneSensors.h>
#include <vsgraysim/earth/VoxelOcean.h>
#include <vsgraysim/earth/VoxelEarthPatch.h>
#include <vsgraysim/earth/VoxelScenePackage.h>
#include <vsgraysim/earth/ScenePresentation.h>
#include <vsgraysim/earth/SceneAcoustics.h>
#include "audio/SceneAudioMixer.h"
#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include <vsgraysim/kernel/TimeTick.h>
#include <sys/resource.h>
#include <vsgraysim/earth/EarthDataService.h>
#include <vsgraysim/earth/SurfaceQuality.h>
#include <vsgraysim/earth/SurfaceClusters.h>
#include <rocky_env/GpuContext.h>
#include <rocky_env/EnvPhysicsService.h>
#include "display/PanelProcessing.h"
#include "display/EquipmentSession.h"
#include "display/SonarDisplayPanel.h"
#include "../src/earth/VoxelSensorMaterials.h"
#include "earth_unified_capture.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/ViewportConfig.h"
#include "viewport3d/EarthViewFeature.h"
#include "viewport3d/VoxelSceneFeature.h"
#include "viewport3d/EnvFeatures.h"
#include "viewport3d/VsgQtWidget.h"
#include <simcore/GeoNavigation.h>
#include "../src/earth/VoxelTerrain.h"
#include "../src/earth/VoxelDynamics.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMainWindow>
#include <QDockWidget>
#include <QLabel>
#include <QPainter>
#include <QToolBar>
#include <QTimer>
#include <QThread>
#include <vsg/all.h>

#include <vsgXchange/all.h>
#include <vsgXchange/assimp.h>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <limits>
#include <algorithm>

namespace {
using namespace vsgraysim;using namespace vsgraysim::earth;using namespace vsgraysim::viewport3d;
using namespace rockyenv::sensor;using json=nlohmann::json;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void pump(int ms){QElapsedTimer timer;timer.start();do{QApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}while(timer.elapsed()<ms);}
vsg::dvec3 occupiedCentre(const VoxelSceneObject& o) {
    const auto n=o.volume->dimensions();const auto origin=o.volume->gridOrigin();const auto target=o.localCentreOfMass();
    vsg::dvec3 result=target;double best=std::numeric_limits<double>::infinity();
    for(size_t i=0;i<o.volume->cellCount();++i)if(o.volume->occupancy()[i]) {
        const auto p=origin+vsg::dvec3(i%n[0]+.5,i/n[0]%n[1]+.5,i/(n[0]*n[1])+.5)*o.volume->resolution_m;
        const double d=vsg::length(p-target);if(d<best){best=d;result=p;}
    }
    return result;
}
double percentile95(std::vector<double> times) {
    require(!times.empty(), "missing timing samples");
    std::sort(times.begin(), times.end());
    return times[size_t(std::ceil(.95 * times.size())) - 1];
}

void runTerrainCollision(VoxelSceneFeature& feature,Viewport3DView& view,const QString& output) {
    auto& scene=*feature.runtime();
    const auto initial=scene.snapshot();
    const auto terrain=std::find_if(initial->objects.begin(),initial->objects.end(),[](const auto& entry){
        return entry.second.earth&&entry.second.parentId.empty();
    });
    require(terrain!=initial->objects.end()&&initial->objects.size()==1&&initial->tick==0,
            "terrain collision capture requires an initial terrain-only topic");
    const auto& terrainId=terrain->first;
    const auto& bounds=terrain->second.surface();
    const double seamX=(bounds.min_x+bounds.max_x)*.5, seamZ=(bounds.min_z+bounds.max_z)*.5;
    const vsg::dvec3 down{0,-1,0};
    const auto height=[&](double x,double z) {
        const auto hit=raycastVoxelScene(*scene.snapshot(),{x,bounds.max_height+100,z},down,5000,true);
        require(bool(hit),"missing terrain collision fixture ground");
        return hit->position.y;
    };
    double z=seamZ-350, leastSlope=std::numeric_limits<double>::infinity();
    for(double at=seamZ-760;at<seamZ-350;at+=5) {
        const double spread=std::abs(height(seamX-3,at)-height(seamX+3,at))+
                            std::abs(height(seamX,at-2)-height(seamX,at+2));
        if(spread<leastSlope){z=at;leastSlope=spread;}
    }
    const double seamY=height(seamX,z);
    const double craterY=height(seamX,seamZ);
    const auto insertBody=[&](const char* id,const vsg::dvec3& p,double restitution,double velocity,bool fracture) {
        scene.edit(json::array({{{"kind","add"},{"object",{
            {"id",id},{"position_m",{p.x,p.y,p.z}},{"velocity_mps",{0,velocity,0}},
            {"material",{{"color_rgba",{.88,.33,.11,1.}}}},
            {"source",{{"type","box"},{"dimensions",{6,2,4}},{"origin_m",{-3,-1,-2}},{"cell_size_m",1}}},
            {"physics",{{"motion","dynamic"},{"friction",.65},{"restitution",restitution},
                          {"linear_damping",0.},{"fracture",fracture}}}
        }}}}),scene.snapshot()->revision);
    };
    // Set ground restitution for this controlled rebound test. Both runs
    // use the same contact material, geometry and integration interval.
    scene.edit(json::array({{{"kind","physics"},{"object",terrainId},{"merge",true},
        {"physics",{{"restitution",.75}}}}}),scene.snapshot()->revision);
    insertBody("seam-probe",{seamX,seamY+1.25,z},0.,-2.,false);
    scene.schedule({{"kind","damage"},{"object",terrainId},{"tick",1},{"space","object"},
        {"point_m",{seamX,craterY,seamZ}},{"radius_m",220.}});
    const auto basis=simcore::geo::makeEnuToWorldMatrix(initial->anchor);
    const auto camera=view.context().camera()->viewMatrix.cast<vsg::LookAt>();
    const auto up=basis*vsg::dvec4(0,1,0,0);camera->up={up.x,up.y,up.z};
    const auto directory=std::filesystem::absolute(output.toStdString());
    std::filesystem::create_directories(directory);
    view.setAutoFrame(false);view.setIdleSuspend(false);
    const auto drawFrame=[&] {
        QThread::msleep(25);
        view.context().setSimulationTime(scene.snapshot()->timeSeconds());
        const auto serial=view.widget()->property("renderFrameSerial").toULongLong();
        QElapsedTimer timer;timer.start();
        view.markActivity();view.renderFrame();view.context().viewer()->deviceWaitIdle();
        require(view.widget()->property("renderFrameSerial").toULongLong()==serial+1,"terrain validation failed to submit a native frame");
        return timer.nsecsElapsed()/1e6;
    };
    const auto capture=[&](const char* file,vsg::dvec3 focus,vsg::dvec3 offset) {
        camera->eye=basis*(focus+offset);camera->center=basis*focus;
        drawFrame();
        const auto image=view.grabFrame();
        require(!image.isNull()&&image.save(QString::fromStdString((directory/file).string())),"terrain collision Vulkan readback failed");
    };
    capture("01-before.png",{seamX,craterY,seamZ},{-510,460,585});
    const double dt=initial->fixedStep;
    const auto step=[&](int tick) {
        scene.setPaused(false);
        QElapsedTimer timer;timer.start();
        const auto result=scene.advanceTo(tick*dt,1);
        const double simulationMs=timer.nsecsElapsed()/1e6;
        scene.setPaused(true);
        require(result.steps==1&&!result.pendingSteps,"terrain collision capture missed a step");
        return json{{"tick",tick},{"simulation_ms",simulationMs},{"terrain_bvh_ms",result.terrainBvhMs},
            {"collider_refresh_ms",result.collisionRefreshMs},{"rebuilt",result.terrainBvhRebuilt},{"reused",result.terrainBvhReused},
            {"terrain_mesh_ms",result.terrainMeshMs},{"terrain_stitch_ms",result.terrainMeshStitchMs},
            {"mesh_rebuilt",result.terrainMeshRebuilt},{"mesh_reused",result.terrainMeshReused}};
    };
    json events=json::array({step(1)});
    const double floor=height(seamX,seamZ);
    require(craterY-floor>20.,"damage did not excavate the canonical surface");
    capture("02-crater.png",{seamX,craterY,seamZ},{-510,460,585});
    events[0]["render_atlas_reuse"]=feature.terrainAtlasReuseStats();
    const vsg::dvec3 hinge{seamX+2,floor+4,seamZ+30};
    const auto hingePosition=std::array{hinge.x,hinge.y,hinge.z};
    const double hingeHalfAngle=3.*vsg::PI/8.;
    scene.edit(json::array({{{"kind","add"},{"object",{
        {"id","hinged-part"},{"material",{{"color_rgba",{.28,.64,.9,1.}}}},
        {"pose_track",json::array({{{"seconds",0.},{"position_m",hingePosition},{"rotation_xyzw",{0,0,0,1}}},
            {{"seconds",1.8},{"position_m",hingePosition},{"rotation_xyzw",{0,std::sin(hingeHalfAngle),0,std::cos(hingeHalfAngle)}}}})},
        {"source",{{"type","box"},{"dimensions",{6,1,2}},{"origin_m",{0,0,-1}},{"cell_size_m",1}}}
    }}}}),scene.snapshot()->revision);
    insertBody("drop",{seamX,floor+4,seamZ},.85,-6.,true);
    scene.schedule({{"kind","fracture"},{"object","drop"},{"tick",2},{"space","object"},
        {"point_m",{0,0,0}},{"normal",{1,0,0}},{"radius_m",12.},{"energy_j",1e6}});
    events.push_back(step(2));
    const auto detached=scene.snapshot()->journal.back().at("fragments").get<std::vector<std::string>>();
    require(detached.size()==1,"drop did not generate two fracture bodies");
    const auto fracture=scene.snapshot();
    for(const auto& id:{std::string("drop"),detached.front()}) {
        const auto& body=fracture->objects.at(id);
        const auto centre=body.localCentreOfMass();
        // Separate the cut faces before landing. This prevents a fragment
        // hitting its sibling from being mistaken for a terrain rebound.
        scene.schedule({{"kind","impulse"},{"object",id},{"tick",3},{"space","object"},
            {"point_m",{centre.x,centre.y,centre.z}},
            {"impulse_ns",{std::copysign(body.massKg(),centre.x),0,0}}});
    }
    const auto scheduleDamage=[&](int tick,double x,double z,double radius) {
        scene.schedule({{"kind","damage"},{"object",terrainId},{"tick",tick},{"space","object"},
            {"point_m",{x,height(x,z),z}},{"radius_m",radius}});
    };
    scheduleDamage(20,seamX+555,seamZ,170.);
    scheduleDamage(50,seamX-620,seamZ+670,130.);
    size_t left=0,right=0,across=0,fragmentContacts=0;
    double rebound=0.,maxRangeDelta=0.;
    int firstLanding=0;
    json flight=json::array();
    double previousVerticalSpeed=fracture->objects.at(detached.front()).velocity.y;
    const auto hingeVolume=fracture->objects.at("hinged-part").volume;
    std::unique_ptr<VoxelScene> restored;
    size_t restoredSteps=0,animationChecks=0;
    for(int tick=3;tick<=100;++tick) {
        const auto elapsed=step(tick);
        if(tick==20||tick==50) {
            events.push_back(elapsed);
            capture(tick==20?"06-second-cut.png":"07-third-cut.png",{seamX,craterY,seamZ},{-1020,920,1170});
            events.back()["render_atlas_reuse"]=feature.terrainAtlasReuseStats();
        }
        const auto state=scene.snapshot();
        if(tick==3) {
            restored=VoxelScene::load(scene.checkpoint());restored->setPaused(false);
        } else {
            restored->advanceTo(tick*dt,1);
            const auto fresh=restored->snapshot();
            for(const auto& [id,body]:state->objects) {
                const auto& replay=fresh->objects.at(id);
                require(body.basePose==replay.basePose&&body.velocity==replay.velocity&&body.angularVelocity==replay.angularVelocity,
                        "incremental terrain collision and cold checkpoint replay produced different body states");
            }
            require(state->contacts.size()==fresh->contacts.size(),"cold collision tree changed support contact count");
            ++restoredSteps;
        }
        bool contactLeft=false,contactRight=false;
        for(const auto& contact:state->contacts) {
            if(contact.a!=terrainId&&contact.b!=terrainId)continue;
            const auto& id=contact.a==terrainId?contact.b:contact.a;
            const auto point=contact.a==terrainId?contact.pointA:contact.pointB;
            if(id=="seam-probe") {contactLeft|=point.x<seamX-.001;contactRight|=point.x>seamX+.001;}
            if(std::find(detached.begin(),detached.end(),id)!=detached.end()) {
                ++fragmentContacts;
            }
        }
        left+=contactLeft;right+=contactRight;across+=contactLeft&&contactRight;
        if(tick==20||tick==40||tick==75)for(double x:{seamX-3.,seamX-.02,seamX+.02,seamX+3.,seamX+125.}) {
            const vsg::dvec3 origin{x,craterY+160,seamZ+9.25};
            const auto visual=scene.raycast(origin,down,750),rigid=scene.rigidRaycast(origin,down,750);
            require(visual&&rigid&&visual->object==rigid->object,"cut/seam collision hit disagrees with the visible triangles");
            maxRangeDelta=std::max(maxRangeDelta,std::abs(visual->distance-rigid->distance));
        }
        for(const auto& id:detached) {
            const auto& body=state->objects.at(id);
            if(previousVerticalSpeed<-.4&&body.velocity.y>.4) {
                const auto pose=body.pose(tick,dt);
                const auto box=body.localBounds();
                double distance=std::numeric_limits<double>::infinity();
                for(double x:{box.min.x,box.max.x})for(double y:{box.min.y,box.max.y})for(double z:{box.min.z,box.max.z}) {
                    const auto p=pose*vsg::dvec3(x,y,z);
                    distance=std::min(distance,std::abs(p.y-height(p.x,p.z)));
                }
                // A contact can occur inside the fixed step and be released
                // by its final substep. Check the measured reversal at the floor.
                if(distance<.2){rebound=std::max(rebound,body.velocity.y);if(!firstLanding)firstLanding=tick;}
            }
            previousVerticalSpeed=body.velocity.y;
            flight.push_back({{"tick",tick},{"id",id},{"y",body.pose(tick,dt)[3].y},{"vy",body.velocity.y}});
        }
        if(tick==8||tick==22||tick==55)capture(tick==8?"03-falling.png":tick==22?"04-rebound.png":"05-followup.png",
            {seamX,floor+2,seamZ},{-21,15,30});
        if(tick==10||tick==50||tick==90) {
            const auto& animated=state->objects.at("hinged-part");
            const auto pose=animated.pose(tick,dt);
            const auto tip=pose*vsg::dvec3(5.8,.4,0);
            require(tick==50||(tick==10?tip.x>seamX:tip.x<seamX),"hinged part did not cross the terrain block seam");
            const auto testRay=pose*vsg::dvec3(4,3,0);
            const auto visible=scene.raycast(testRay,down,5),rigid=scene.rigidRaycast(testRay,down,5);
            require(animated.volume==hingeVolume&&visible&&rigid&&visible->object=="hinged-part"&&rigid->object==visible->object&&
                    std::abs(visible->distance-rigid->distance)<.001,"animated voxel part lost its occupied geometry or collision pose");
            capture(tick==10?"hinge-start.png":tick==50?"hinge-mid.png":"hinge-end.png",hinge+vsg::dvec3(2,0,-2),{-8,10,13});
            require(feature.display().placements.at("hinged-part")->matrix==pose,"animated voxel display pose differs from its collision pose");
            ++animationChecks;
        }
    }
    if(!(left&&right&&across&&firstLanding&&rebound>.4&&maxRangeDelta<.03))
        throw std::runtime_error("seam/fragment validation: "+json{{"left",left},{"right",right},{"both",across},
            {"fragment_contacts",fragmentContacts},{"rebound_mps",rebound},{"range_error_m",maxRangeDelta},
            {"seam_z",z},{"seam_height_spread_m",leastSlope}}.dump());
    GpuSensorSpec optical;
    optical.id=901;optical.period_seconds=dt;optical.output=ProductStage::Scene;
    optical.profile.width=9;optical.profile.height=7;optical.profile.fov_deg=1.;
    optical.profile.detector.integration_ms=1.;optical.profile.band=BandSpec::make(BandType::TV);
    auto infrared=optical;infrared.id=902;infrared.profile.band=BandSpec::make(BandType::MWIR);
    VoxelSensorSession sensors(scene,{optical,infrared},view.context().window()->getOrCreateDevice(),"terrain-collision-capture");
    const auto rayOrigin=[&](uint32_t id){return vsg::dvec3(seamX+(id==optical.id?-.02:.02),craterY+160,seamZ+9.25);};
    scheduleDamage(103,seamX,seamZ+9.25,58.);
    size_t products=0;
    std::set<uint64_t> measuredRevisions;
    double gpuRangeError=0.;
    for(int tick=101;tick<=107;++tick) {
        const auto elapsed=step(tick);
        if(tick==103)events.push_back(elapsed);
        sensors.advanceTo(tick*dt,[&](uint32_t id,double){
            GpuAperture aperture;aperture.position=rayOrigin(id);aperture.forward=down;aperture.up={0,0,-1};return aperture;
        },{},[&](GpuMeasurementProduct&& product) {
            constexpr size_t pixel=(3*9+4)*4+3;
            const auto truth=scene.sample((product.start_time+product.end_time)*.5);
            const auto hit=raycastVoxelScene(*truth,rayOrigin(product.sensor_id),down,750);
            require(hit&&hit->object==terrainId&&product.rgba.size()>pixel,"GPU sensor missed the canonical crater floor");
            gpuRangeError=std::max(gpuRangeError,std::abs(product.rgba[pixel]*1000.-hit->distance));
            measuredRevisions.insert(truth->contentRevision);
            ++products;
        });
    }
    require(products>=6&&measuredRevisions.size()==2&&gpuRangeError<.05,"optical/IR and collision geometry revision mismatch");
    capture("08-sensor-cut.png",{seamX,craterY,seamZ},{-510,460,585});
    events.back()["render_atlas_reuse"]=feature.terrainAtlasReuseStats();
    std::vector<double> stableFrames;
    for(int i=0;i<5;++i)drawFrame();
    for(int i=0;i<32;++i)stableFrames.push_back(drawFrame());
    const json report={{"step_events",events},{"seam_left_ticks",left},{"seam_right_ticks",right},
        {"seam_both_ticks",across},{"fragment_contacts",fragmentContacts},{"first_landing_tick",firstLanding},
        {"rebound_mps",rebound},{"collision_range_max_error_m",maxRangeDelta},{"crater_depth_m",craterY-floor},
        {"gpu_sensor_products",products},{"gpu_range_max_error_m",gpuRangeError},
        {"gpu_measured_revisions",measuredRevisions},{"stable_vulkan_frame_p95_ms",percentile95(stableFrames)},
        {"stable_vulkan_frames_ms",std::move(stableFrames)},
        {"restored_steps_identical",restoredSteps},{"animation_pose_checks",animationChecks},
        {"flight",std::move(flight)}};
    std::ofstream(directory/"terrain-collision.json")<<report.dump(2)<<'\n';
}

void runPageTraversal(VoxelSceneFeature& feature, Viewport3DView& view, const QString& topic,
                      const QString& output, double firstFrameMs, double physicsReadyMs) {
    auto& scene = *feature.runtime();
    const auto initial = scene.snapshot();
    const auto terrain = std::find_if(initial->objects.begin(), initial->objects.end(),
        [](const auto& entry) { return entry.second.earth && entry.second.parentId.empty(); });
    require(terrain != initial->objects.end(), "terrain traversal requires an Earth patch");
    const auto& root = terrain->second;
    const auto box = root.earth->bounds();
    const auto n = root.volume->dimensions();
    const double cell = root.volume->resolution_m;
    const double x = box.min.x + (n[0] / 2) * cell, z = box.min.z + (n[2] / 2) * cell;
    const auto basis = simcore::geo::makeEnuToWorldMatrix(initial->anchor);
    auto* camera = view.context().camera()->viewMatrix.cast<vsg::LookAt>().get();
    const auto out = std::filesystem::absolute(output.toStdString());
    std::filesystem::create_directories(out);
    const vsg::dvec3 downward{0,-1,0};
    const auto ground = [&](double atX, double atZ) {
        const auto hit = raycastVoxelScene(*scene.snapshot(), {atX, box.max.y + 20, atZ},
                                          downward, box.max.y - box.min.y + 21, true);
        require(bool(hit), "ground missing at terrain seam");
        return hit->position.y;
    };
    auto setCamera = [&](double u) {
        const auto atX = x + u * cell * 8, atZ = z + u * cell * 8;
        const auto y = ground(atX, atZ);
        camera->eye = basis * vsg::dvec3(atX - 70, y + 220, atZ + 150);
        camera->center = basis * vsg::dvec3(atX, y, atZ);
        const auto up = basis * vsg::dvec4(0,1,0,0);
        camera->up = {up.x, up.y, up.z};
    };
    QElapsedTimer lastDraw;
    const auto pace = [&] {
        // Let the widget's native cadence expire before starting a timed sample.
        const auto remaining = lastDraw.isValid() ? 17 - lastDraw.elapsed() : 17;
        if (remaining > 0) QThread::msleep(static_cast<unsigned long>(remaining));
    };
    const auto render = [&] {
        view.context().setSimulationTime(scene.snapshot()->timeSeconds());
        const auto serial = view.widget()->property("renderFrameSerial").toULongLong();
        view.markActivity();
        view.renderFrame();
        view.context().viewer()->deviceWaitIdle();
        require(view.widget()->property("renderFrameSerial").toULongLong() == serial + 1,
                "terrain benchmark sample did not submit a native frame");
        lastDraw.start();
    };
    const auto pacedRender = [&] { pace(); render(); };
    const auto shot = [&](const char* name) {
        pacedRender();
        const auto image = view.grabFrame();
        require(!image.isNull() && image.save(QString::fromStdString((out / name).string())),
                "terrain traverse Vulkan readback failed");
        lastDraw.start();
    };
    view.setAutoFrame(false);
    view.setIdleSuspend(false);
    json residency = json::array();
    const auto expectPages = VoxelScenePackage::inspect(topic.toStdString()).contains("terrain_pages");
    if (expectPages) {
        for (double location : {-4.5, 4.5, -4.5}) {
            const auto atX = x + location * cell * 8, atZ = z + location * cell * 8;
            const auto y = ground(atX, atZ);
            camera->eye = basis * vsg::dvec3(atX, y + 120, atZ);
            camera->center = basis * vsg::dvec3(atX, y, atZ);
            const auto up = basis * vsg::dvec4(0,0,-1,0);
            camera->up = {up.x, up.y, up.z};
            for (int frame = 0; frame < 12; ++frame) pacedRender();
            const auto stats = feature.terrainDisplayPageStats();
            require(stats.contains(terrain->first), "paged package did not activate runtime terrain geometry pages");
            residency.push_back(stats.at(terrain->first));
            if (residency.size() == 1) shot("page-first.png");
            if (residency.size() == 3) shot("page-return.png");
        }
        if (residency[0].at("resident").get<size_t>() == 0 ||
            residency[0].at("resident_geometry_bytes") >= residency[0].at("unpartitioned_geometry_bytes") ||
            residency[1].at("evictions") <= residency[0].at("evictions") ||
            residency[2].at("loads") <= residency[1].at("loads"))
            throw std::runtime_error("moving the camera did not unload and reload terrain geometry: " + residency.dump());
    }
    setCamera(-1);
    for (int i = 0; i < 5; ++i) pacedRender();
    std::vector<double> frameMs, completeMs;
    const auto stableUtc = scene.snapshot()->utcOrigin;
    for (int i = 0; i < 41; ++i) {
        setCamera((i - 20) / 20.);
        pace();
        QElapsedTimer sample;
        sample.start();
        render();
        frameMs.push_back(sample.nsecsElapsed() / 1e6);
        if (i == 0) shot("approach.png");
        if (i == 20) shot("seam.png");
        if (i == 40) shot("exit.png");
    }

    const auto y = ground(x, z);
    scene.edit(json::array({{{"kind", "add"}, {"object", {
        {"id", "seam-probe"}, {"position_m", {x, y + 2.03, z}},
        {"source", {{"type", "box"}, {"dimensions", {2,2,2}},
                    {"origin_m", {-2,-2,-2}}, {"cell_size_m", 2}}},
        {"physics", {{"motion", "dynamic"}, {"friction", .6}}}
    }}}}), scene.snapshot()->revision);
    GpuSensorSpec ir;
    ir.id = 901;
    ir.period_seconds = initial->fixedStep;
    ir.output = ProductStage::Scene;
    ir.profile.width = 9;
    ir.profile.height = 7;
    ir.profile.fov_deg = 4;
    ir.profile.detector.integration_ms = 1;
    ir.profile.band = BandSpec::make(BandType::MWIR);
    auto vis = ir;
    vis.id = 902;
    vis.profile.band = BandSpec::make(BandType::TV);
    QElapsedTimer gpuSetup;
    gpuSetup.start();
    VoxelSensorSession sensors(scene, {ir, vis}, view.context().window()->getOrCreateDevice(), "terrain-page-acceptance");
    const double sensorReadyMs = gpuSetup.nsecsElapsed() / 1e6;
    vsg::dvec3 sensing;
    const auto aperture = [&](uint32_t, double) {
        GpuAperture a;
        a.position = sensing;
        a.forward = downward;
        a.up = {0,0,-1};
        return a;
    };
    const auto environment = [](uint32_t, double, const BandSpec& band) {
        EnvAnswer answer;
        answer.sigma_per_km = 0;
        answer.sun_irradiance = band.type == BandType::TV ? 100 : 0;
        answer.sun_el_deg = 40;
        return answer;
    };
    size_t contacts = 0, measurements = 0;
    double maxSensorError = 0;
    for (int i = 0; i < 41; ++i) {
        const double u = (i - 20) / 20.;
        setCamera(u);
        sensing = {x + u * cell * 8, y + 450, z + u * cell * 8};
        const auto next = scene.snapshot()->timeSeconds() + initial->fixedStep;
        pace();
        QElapsedTimer sample;
        sample.start();
        scene.setPaused(false);
        const auto tick = scene.advanceTo(next, 1);
        scene.setPaused(true);
        require(tick.steps == 1 && !tick.pendingSteps, "terrain traversal missed a simulation step");
        sensors.advanceTo(next, aperture, environment, [&](GpuMeasurementProduct&& product) {
            const auto truth = scene.sample((product.start_time + product.end_time) * .5);
            const auto hit = raycastVoxelScene(*truth, sensing, downward, 2000);
            constexpr size_t pixel = (3 * 9 + 4) * 4 + 3;
            require(hit && product.rgba.size() > pixel, "terrain traversal has no sensor range");
            maxSensorError = std::max(maxSensorError, std::abs(product.rgba[pixel] * 1000. - hit->distance));
            ++measurements;
        });
        render();
        completeMs.push_back(sample.nsecsElapsed() / 1e6);
        contacts += std::count_if(scene.snapshot()->contacts.begin(), scene.snapshot()->contacts.end(),
            [&](const auto& c) { return c.a == terrain->first || c.b == terrain->first; });
    }
    require(contacts > 0 && measurements >= 80 && maxSensorError < .05,
            "terrain seam ground contact or GPU range consistency failed");

    QElapsedTimer update;
    update.start();
    scene.edit(json::array({{{"kind", "terrain_brush"}, {"object", terrain->first},
        {"brush", {{"operation", "lower"}, {"center_m", {x, y, z}}, {"radius_m", cell * 1.5}, {"amount_m", 1}}}}}),
        scene.snapshot()->revision);
    const double brushMs = update.nsecsElapsed() / 1e6;
    scene.schedule({{"kind", "damage"}, {"object", terrain->first}, {"tick", scene.snapshot()->tick + 1},
        {"point_m", {x, y - cell * .25, z}}, {"radius_m", cell * 1.4}});
    update.restart();
    scene.setPaused(false);
    scene.advanceTo(scene.snapshot()->timeSeconds() + initial->fixedStep, 1);
    scene.setPaused(true);
    const double damageMs = update.nsecsElapsed() / 1e6;
    const auto damaged = scene.checkpoint();
    const auto removed = damaged.at("journal").back().at("removed_voxels").get<size_t>();
    require(removed > 0, "terrain traversal damage removed no voxel");
    setCamera(0);
    shot("damaged.png");
    rusage memory{};
    getrusage(RUSAGE_SELF, &memory);
    const auto project = VoxelSceneProject::open(std::filesystem::absolute(topic.toStdString()).string());
    const auto file = (out / "damaged.vsimscene").string();
    VoxelScenePackage::save(file, project.document, damaged);
    require(VoxelScene::open(file)->checkpoint() == damaged, "terrain edits lost on reopening the traversal package");
    const auto report = json{{"first_visible_frame_ms", firstFrameMs}, {"physical_scene_ready_ms", physicsReadyMs},
        {"sensor_setup_ms", sensorReadyMs}, {"steady_render_p95_ms", percentile95(frameMs)},
        {"full_cycle_p95_ms", percentile95(completeMs)}, {"peak_process_rss_kib", memory.ru_maxrss},
        {"render_frames", frameMs.size()}, {"full_cycles", completeMs.size()},
        {"timed_native_frame_submissions", frameMs.size() + completeMs.size()}, {"brush_update_ms", brushMs},
        {"damage_update_ms", damageMs}, {"removed_voxels", removed}, {"bullet_contacts", contacts},
        {"gpu_measurements", measurements}, {"gpu_range_max_error_m", maxSensorError},
        {"stable_utc", stableUtc}, {"seam_xz_m", {x, z}}, {"cell_size_m", cell},
        {"damaged_checkpoint_roundtrip", true}, {"runtime_geometry_residency", residency},
        {"peak_render_sample_ms", *std::max_element(frameMs.begin(), frameMs.end())}};
    std::ofstream(out / "traverse.json") << report.dump(2) << '\n';
}

void runThermalCapture(VoxelScene& scene, Viewport3DView& view, const QString& output) {
    const auto path = std::filesystem::absolute(output.toStdString());
    std::filesystem::create_directories(path);
    const auto initial = scene.snapshot();
    const auto& ground = initial->objects.at("taipei-terrain");
    const auto& shelter = initial->objects.at("equipment-shelter");
    require(ground.sensorMaterials && shelter.sensorMaterials, "missing authored thermal material charts");
    const auto chart = ground.sensorMaterials->encode();
    std::set<int> classes;
    for (const auto& run : chart.at("cell_runs")) classes.insert(run.at(2).get<int>());
    require(classes.size() >= 2, "RGB classification did not generate multiple land cover classes");
    std::vector<GpuSensorSpec> specifications;
    for (uint32_t i=0;i<4;++i) {
        GpuSensorSpec sensor;
        sensor.id = i+1;
        sensor.period_seconds = .2;
        sensor.output = ProductStage::Scene;
        sensor.profile.width = 256;
        sensor.profile.height = 192;
        sensor.profile.fov_deg = i<2 ? 42 : 24;
        sensor.profile.detector.integration_ms = 4;
        sensor.profile.band = BandSpec::make(i%2 ? BandType::LWIR : BandType::MWIR);
        specifications.push_back(sensor);
    }
    VoxelSensorSession sensors(scene,specifications,view.context().window()->getOrCreateDevice(),"thermal-field-acceptance");
    auto aperture = [](uint32_t id,double) {
        GpuAperture a;
        a.position = id<=2 ? vsg::dvec3(-200,1350,810) : vsg::dvec3(-7,489,-7);
        const auto point = id<=2 ? vsg::dvec3(-200,400,200) : vsg::dvec3(-16,477.7,-16);
        a.forward = vsg::normalize(point-a.position);
        return a;
    };
    const auto model = simcore::geo::makeEnuToWorldMatrix(initial->anchor);
    const auto environment = [&](uint32_t id,double t,const BandSpec& band) {
        const auto state = scene.sample(t);
        const auto utc = sceneUtc(state->utcOrigin,t);
        rockyenv::EnvFrame frame;
        frame.model = model;
        frame.cam_pos_enu = aperture(id,t).position;
        frame.cam_pos_world = model*frame.cam_pos_enu;
        frame.time_sec = t;
        frame.year=utc.year;frame.month=utc.month;frame.day=utc.day;frame.hours_utc=utc.hours;
        const auto weather = state->weather.enabled ? state->weather.sample(t) : SceneWeather::preset("clear");
        frame.cloud_cover=weather.cover;frame.wind_speed=weather.windSpeed;frame.visibility=weather.visibility;
        frame.precipitation=weather.precipitation();frame.cloud_base_m=weather.cloudBase;frame.overcast=weather.overcast;
        EnvPhysicsService physics;
        physics.update(frame);
        EnvRequest request;
        request.band=band;request.sim_time=t;request.hours_utc=utc.hours;
        request.lat=state->anchor.lat_deg;request.lon=state->anchor.lon_deg;
        request.alt=state->anchor.alt_m+frame.cam_pos_enu.y;
        return physics.answerAtmosphere(request);
    };
    auto stableRevision = initial->contentRevision;
    std::map<uint32_t,GpuMeasurementProduct> products;
    auto stepTo = [&](double seconds) {
        while (scene.snapshot()->timeSeconds()+1e-8 < seconds) {
            const auto end = std::min(seconds,scene.snapshot()->timeSeconds()+.2);
            scene.setPaused(false);
            scene.advanceTo(end,32);
            scene.setPaused(true);
            sensors.advanceTo(scene.snapshot()->timeSeconds(),aperture,environment,[&](GpuMeasurementProduct&& product) {
                products[product.sensor_id] = std::move(product);
            });
        }
        require(sensors.contentRevision()==stableRevision && scene.snapshot()->contentRevision==stableRevision,
                "weather or heat changed geometry revision");
        require(products.size()==4,"missing a band or camera product");
    };
    auto editEnvironment = [&](double utc,bool wet) {
        auto origin = scene.snapshot()->utcOrigin;
        origin["hours_utc"] = utc;
        auto weather = SceneWeather::preset(wet ? "overcast" : "clear");
        weather.wetness = wet ? .85 : 0;
        if (wet) weather.temperature=291.15;
        scene.edit(json::array({{{"kind","environment"},{"utc_origin",origin},
            {"weather",{{"initial",SceneWeather::encodeState(weather)}}}}}),scene.snapshot()->revision);
    };
    const auto save = [&](const std::string& phase, bool captureEarth) {
        json result = json::object();
        for (uint32_t i=1;i<=4;++i) {
            const auto& product = products.at(i);
            const auto& profile = specifications.at(i-1).profile;
            const auto filename = phase+(i<=2 ? "-terrain" : "-target")+(i%2 ? "-mwir" : "-lwir");
            require(product.rgba.size()==size_t(product.width)*product.height*4,"bad P0 product shape");
            std::vector<float> radiance,valid,temperatures;
            float minimumEmissivity = 1.f, maximumEmissivity = 0.f;
            radiance.reserve(product.rgba.size()/4);
            for (size_t j=0;j<product.rgba.size();j+=4) {
                require(std::isfinite(product.rgba[j]),"nonfinite P0 radiance");
                radiance.push_back(product.rgba[j]);
                if (product.rgba[j+3] >= 1000.f) continue;
                valid.push_back(product.rgba[j]);
                temperatures.push_back(product.rgba[j+1]);
                minimumEmissivity = std::min(minimumEmissivity,product.rgba[j+2]);
                maximumEmissivity = std::max(maximumEmissivity,product.rgba[j+2]);
            }
            require(valid.size() > 100, "too few surface pixels for thermal capture");
            auto quantile = [](std::vector<float>& a,double fraction) {
                auto mid=a.begin()+size_t(fraction*(a.size()-1));
                std::nth_element(a.begin(),mid,a.end());
                return *mid;
            };
            result[filename]={{"p05_surface_k",quantile(temperatures,.05)},{"p95_surface_k",quantile(temperatures,.95)},
                {"maximum_surface_k",*std::max_element(temperatures.begin(),temperatures.end())},
                {"min_surface_emissivity",minimumEmissivity},{"max_surface_emissivity",maximumEmissivity},
                {"median_surface_radiance",quantile(valid,.5)},{"surface_pixels",valid.size()},
                {"acquisition_end_s",product.end_time}};
            std::ofstream floats(path/(filename+".f32"),std::ios::binary);
            floats.write(reinterpret_cast<const char*>(product.rgba.data()),product.rgba.size()*sizeof(float));
            require(bool(floats),"cannot write P0 radiance");
            for (const bool automatic : {false,true}) {
                const auto image = gui::display::infraredRadianceImage(radiance,product.width,product.height,
                    profile.band.lambda_min*1e6,profile.band.lambda_max*1e6,automatic,false);
                require(image.save(QString::fromStdString((path/(filename+(automatic?"-auto.png":"-fixed.png"))).string())),
                        "infrared panel image write failed");
            }
        }
        for (uint32_t i : {1u,3u}) {
            const auto& mw = products.at(i);
            const auto& lw = products.at(i+1);
            double maximum = 0;
            for (size_t j=0;j<mw.rgba.size();j+=4)
                if (mw.rgba[j+3]<1000 && lw.rgba[j+3]<1000)
                    maximum = std::max(maximum,std::abs(double(mw.rgba[j+1])-lw.rgba[j+1]));
            require(maximum<.05,"MWIR/LWIR did not use the same material thermal state");
            result[i==1 ? "terrain_band_temperature_max_delta_k" : "target_band_temperature_max_delta_k"] = maximum;
        }
        if (captureEarth) {
            view.context().setSimulationTime(scene.snapshot()->timeSeconds());
            view.renderFrame();
            const auto native = view.grabFrame();
            require(!native.isNull() && native.save(QString::fromStdString((path/(phase+"-earth.png")).string())),
                    "native Earth screenshot failed");
        }
        return result;
    };
    view.setAutoFrame(false);
    view.setIdleSuspend(false);
    json results;
    stepTo(.4);results["noon_cold"]=save("noon-cold",true);
    stepTo(2.2);results["noon_warm"]=save("noon-warm",true);
    stepTo(4.4);results["noon_cool"]=save("noon-cool",false);
    editEnvironment(11,false);stepTo(4.6);results["dusk"]=save("dusk",true);
    editEnvironment(20,false);stepTo(4.8);results["dawn"]=save("dawn",true);
    editEnvironment(4,true);stepTo(5.0);results["wet"]=save("wet",true);
    auto maximum = [&](const char* sceneKey,const char* product) {
        return results.at(sceneKey).at(product).at("maximum_surface_k").get<float>();
    };
    require(maximum("noon_warm","noon-warm-target-lwir") >
                maximum("noon_cold","noon-cold-target-lwir")+8 &&
            maximum("noon_warm","noon-warm-target-lwir") >
                maximum("noon_cool","noon-cool-target-lwir")+8,
            "local heat source did not warm up and cool down");
    const auto& landNoon = results["noon_cold"]["noon-cold-terrain-lwir"];
    const auto& landWet = results["wet"]["wet-terrain-lwir"];
    require(landNoon.at("max_surface_emissivity").get<float>()-landNoon.at("min_surface_emissivity").get<float>()>.02f,
            "terrain classes did not affect P0 emissivity");
    require(landNoon.at("p95_surface_k").get<float>()>landWet.at("p95_surface_k").get<float>()+1,
            "wet weather did not affect P0 temperature");
    require(landWet.at("median_surface_radiance").get<float>() > .4f*landNoon.at("median_surface_radiance").get<float>(),
            "wet path extinguished warm terrain without thermal emission");
    std::ofstream(path/"thermal-report.json") << json{{"results",results},{"thermal_classes",classes.size()+1},
        {"unchanged_geometry_revision",stableRevision},{"resolution",{256,192}},{"format","P0 RGBA Float32 little endian"}}.dump(2);
}

void runG63Motion(VoxelSceneFeature& feature,Viewport3DView& view,const QString& topic,const QString& output) {
    using D=vsg::dvec3;
    const auto project=VoxelSceneProject::open(topic.toStdString());
    const auto path=std::filesystem::absolute(output.toStdString());
    const auto referencePath=std::filesystem::path(project.scenePath).parent_path()/"g63-motion-reference.json";
    json reference;
    std::ifstream(referencePath)>>reference;
    const auto& assets=reference.at("parts");
    auto& scene=*feature.runtime();
    const auto initial=scene.snapshot();
    const auto chart=simcore::geo::makeEnuToWorldMatrix(initial->anchor);
    const auto coordinate=[](const json& v){return D{v.at(0),v.at(1),v.at(2)};};
    const auto close=[](const vsg::dmat4& a,const vsg::dmat4& b){
        for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)if(std::abs(a[c][r]-b[c][r])>1e-7)return false;
        return true;
    };
    const auto wheelIDs=std::array{"wheel-front-left","wheel-front-right","wheel-rear-left","wheel-rear-right"};
    const auto doorIDs=std::array{"door-front-left","door-front-right"};
    for(const auto& [id,data]:assets.items()){
        const auto& part=initial->objects.at(id);
        require(part.volume->resolution_m==reference.at("cell_size_m")&&part.volume->occupiedCount()>0,
                "G 63 body or joint part lost the reviewed voxel resolution");
        require(part.poseTrack!=nullptr,"G 63 part does not use the scene pose timeline");
    }
    for(const auto* id:doorIDs)require(initial->objects.at(id).surface().hasMaterialBlend,
        "the front door does not carry its glass in the material pass");
    require(!initial->objects.at("g63-body").surface().emissive_rgba.empty(),"G 63 lamp atlas is missing");
    const auto runtimeConfig=project.equipmentConfig();
    auto camera=view.context().camera()->viewMatrix.cast<vsg::LookAt>();
    view.context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=38.;
    view.setAutoFrame(false);
    view.setIdleSuspend(false);
    const bool movie=std::getenv("VSGRAYSIM_G63_FRAMES")!=nullptr;
    std::filesystem::create_directories(path);
    if(movie)std::filesystem::create_directories(path/"frames");
    std::vector<double> renderMs,stepMs;
    json report={{"topic",project.path},{"utc",initial->utcOrigin},{"cell_size_m",reference.at("cell_size_m")},
        {"source_dem_m",initial->objects.at("terrain").surface().dem_spacing_m},
        {"source_imagery_m",initial->objects.at("terrain").surface().imagery_spacing_m},
        {"parts",assets},{"shots",json::array()},{"material_ray_hits",json::array()}};
    auto shot=[&](const std::filesystem::path& file,const vsg::dmat4& body,bool passenger,bool cabin=false){
        const double side=passenger?-1.:1.;
        const D eye=cabin?D{side*3.75,1.97,-2.50}:D{side*4.75,2.08,3.8};
        camera->eye=chart*(body*eye);
        camera->center=chart*(body*(cabin?D{side*.82,1.02,.43}:D{0,.98,.25}));
        const auto up=chart*vsg::dvec4(0,1,0,0);
        camera->up={up.x,up.y,up.z};
        const auto first=view.context().viewer()->getFrameStamp()->frameCount;
        QElapsedTimer timer;timer.start();
        while(timer.elapsed()<3000&&view.context().viewer()->getFrameStamp()->frameCount==first){
            view.markActivity();
            view.renderFrame();
            view.context().viewer()->deviceWaitIdle();
        }
        require(view.context().viewer()->getFrameStamp()->frameCount>first,"G 63 native frame was not submitted");
        renderMs.push_back(timer.nsecsElapsed()/1e6);
        QImage image;
        timer.restart();
        while(timer.elapsed()<8000&&image.isNull()){
            pump(10);
            image=view.grabFrame();
        }
        require(!image.isNull()&&image.save(QString::fromStdString(file.string())),"could not save G 63 Vulkan image");
    };
    const std::map<size_t,std::string> stills={{0,"01-closed"},{31,"02-rolling"},{54,"03-front-steered"},
        {90,"04-stopped-closed"},{124,"05-left-door-open"},{155,"06-both-doors-open"},{205,"08-closed-again"}};
    std::map<std::string,vsg::dmat4> stoppedPoses;
    const auto& frames=reference.at("frames");
    for(size_t i=0;i<frames.size();++i){
        if(!movie&&!stills.count(i))continue;
        const auto& frame=frames.at(i);
        const double seconds=frame.at("seconds");
        QElapsedTimer step;step.start();
        scene.setPaused(false);
        const auto advanced=scene.advanceTo(seconds,1024);
        scene.setPaused(true);
        stepMs.push_back(step.nsecsElapsed()/1e6);
        const auto s=scene.snapshot();
        require(!advanced.pendingSteps&&std::abs(s->timeSeconds()-seconds)<1e-9,"G 63 time source fell behind");
        view.context().setSimulationTime(s->timeSeconds());
        const auto pose=[&](const auto& id){return s->objects.at(id).pose(s->tick,s->fixedStep);};
        const auto body=pose("g63-body");
        const auto& angles=frame.at("parts");
        for(const auto* id:wheelIDs){
            const auto& data=assets.at(id);
            const auto& wheel=angles.at(id);
            const auto steer=vsg::rotate(wheel.at("steer_rad").get<double>(),D{0,1,0});
            const auto mount=body*vsg::translate(coordinate(data.at("pivot_m")))*steer;
            const auto spin=vsg::rotate(wheel.at("roll_rad").get<double>(),coordinate(data.at("axis")));
            require(close(pose(id),mount*spin),"steering and rolling do not share the wheel hub");
            if(std::string_view(id).find("front")!=std::string_view::npos)
                require(close(pose("caliper-"+std::string(id).substr(6)),mount),"front caliper rolled with the tire");
        }
        for(const auto* id:doorIDs){
            const auto& data=assets.at(id);
            const auto door=body*vsg::translate(coordinate(data.at("pivot_m")))*
                vsg::rotate(angles.at(id).at("swing_rad").get<double>(),D{0,1,0});
            require(close(pose(id),door),"glass and door trim did not follow the door hinge");
            if(i!=155)continue;
            for(const auto& [material,points]:data.at("material_probes").items()){
                const double side=id==doorIDs.front()?1.:-1.;
                const auto ray=door*vsg::dvec4(material=="glass"?side:-side,0,0,0);
                const D outward{ray.x,ray.y,ray.z};
                bool hitMaterial=false;
                for(const auto& coords:points){
                    const auto candidate=door*coordinate(coords);
                    const auto start=candidate+outward*.08;
                    const auto hit=raycastVoxelScene(*s,start,-outward,.14,false,id);
                    const auto bullet=scene.rigidRaycast(start,-outward,.14);
                    if(hit&&bullet&&bullet->object==id&&std::abs(hit->distance-.08)<.003&&
                       std::abs(hit->distance-bullet->distance)<.003){
                        report["material_ray_hits"].push_back({{"door",id},{"component",material},
                            {"cpu_distance_m",hit->distance},{"bullet_distance_m",bullet->distance}});
                        hitMaterial=true;
                        break;
                    }
                }
                require(hitMaterial,"opened glass/liner rays could not agree with the authoritative door collider");
            }
        }
        if(const auto it=stills.find(i);it!=stills.end()){
            shot(path/(it->second+".png"),body,false);
            if(i==155){
                shot(path/"07-passenger-door-open.png",body,true);
                shot(path/"09-driver-door-liner.png",body,false,true);
                shot(path/"10-passenger-door-liner.png",body,true,true);
            }
            report["shots"].push_back({{"name",it->second},{"seconds",seconds},{"tick",s->tick},{"joints",angles}});
        }
        if(movie)shot(path/"frames"/QString("%1.png").arg(i,4,10,QChar('0')).toStdString(),body,i>=147&&i<176);
        for(const auto& [id,data]:assets.items()){
            require(close(feature.display().placements.at(id)->matrix,pose(id)),"G 63 displayed geometry lagged the scene snapshot");
            if(i==90)stoppedPoses.emplace(id,pose(id));
            if(i==205)require(close(stoppedPoses.at(id),pose(id)),"G 63 moved while parked or did not return to its closed pose");
        }
        if(i==54){
            const auto published=voxelEquipmentPlatformTelemetry(runtimeConfig,s);
            require(published.size()==1&&published.front().at("model_id")=="mercedes-amg-g63",
                "G 63 state did not reach the standard platform telemetry adapter");
            report["turn_platform"]=published.front();
        }
    }
    report["render_submit_wait_p95_ms"]=percentile95(renderMs);
    report["scene_step_p95_ms"]=percentile95(stepMs);
    report["rendered_frames"]=renderMs.size();
    report["movie_frame_seconds"]=.06;
    std::ofstream(path/"g63-motion-verification.json")<<report.dump(2)<<'\n';
    std::cout<<"PASS: G 63 Taipei Vulkan motion, fixed-resolution part poses, wheel/steering and door material rays\n";
}

void runDeckTakeoff(VoxelSceneFeature& feature,Viewport3DView& view,const QString& topic,const QString& output) {
    using D=vsg::dvec3;
    const auto project=VoxelSceneProject::open(topic.toStdString());
    const auto path=std::filesystem::absolute(output.toStdString());
    json reference;
    std::ifstream(std::filesystem::path(project.scenePath).parent_path()/"deck-takeoff-reference.json")>>reference;
    const auto platformConfig=project.equipmentConfig();
    auto& scene=*feature.runtime();
    const auto initial=scene.snapshot();
    const auto chart=simcore::geo::makeEnuToWorldMatrix(initial->anchor);
    auto camera=view.context().camera()->viewMatrix.cast<vsg::LookAt>();
    view.context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=39.;
    view.setAutoFrame(false);
    view.setIdleSuspend(false);
    std::filesystem::create_directories(path);
    const bool movie=std::getenv("VSGRAYSIM_DECK_TAKEOFF_FRAMES")!=nullptr;
    if(movie)std::filesystem::create_directories(path/"frames");
    const std::map<int,std::string> stills={{0,"01-stopped"},{90,"02-spooling"},{162,"03-ready"},
        {197,"04-liftoff"},{230,"05-hover"},{320,"06-departure"}};
    const double liftoff=reference.at("liftoff_s");
    const double hover=reference.at("hover_s");
    const double departure=reference.at("departure_s");
    const int fps=movie?50:25;
    const double frameTime=1./fps;
    json report={{"utc_origin",initial->utcOrigin},{"geo_anchor",{initial->anchor.lat_deg,initial->anchor.lon_deg,initial->anchor.alt_m}},
        {"source_dem_m",initial->objects.at("taipei-coast").surface().dem_spacing_m},
        {"source_imagery_m",initial->objects.at("taipei-coast").surface().imagery_spacing_m},
        {"fps",fps},{"shots",json::array()}};
    const auto poseOf=[](const VoxelSceneSnapshot& state,const char* id){
        return state.objects.at(id).pose(state.tick,state.fixedStep);
    };
    const auto checkMatrix=[](const vsg::dmat4& actual,const vsg::dmat4& expected,const char* message){
        for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)
            require(std::abs(actual[c][r]-expected[c][r])<1e-7,message);
    };
    const auto rest=vsg::inverse(poseOf(*initial,"vessel"))*poseOf(*initial,"body");
    const auto total=int(std::lround(reference.at("end_s").get<double>()/frameTime));
    std::vector<double> renderMs,stepMs;
    for(int i=0;i<=total;++i){
        const int stillIndex=movie?(i%2==0?i/2:-1):i;
        const auto named=stills.find(stillIndex);
        const bool still=named!=stills.end();
        const double seconds=i*frameTime;
        QElapsedTimer timing;
        timing.start();
        scene.setPaused(false);
        const auto advanced=scene.advanceTo(seconds,2);
        scene.setPaused(true);
        if(advanced.steps)stepMs.push_back(timing.nsecsElapsed()/1e6/advanced.steps);
        const auto state=scene.snapshot();
        require(!advanced.pendingSteps&&std::abs(state->timeSeconds()-seconds)<1e-9,"deck takeoff missed its simulation tick");
        const auto ship=poseOf(*state,"vessel");
        const auto body=poseOf(*state,"body");
        const auto relative=vsg::inverse(ship)*body;
        if(seconds<liftoff)checkMatrix(relative,rest,"parked helicopter slipped on deck");
        if(seconds>=hover&&seconds<departure)
            require(std::abs(body[3][1]-rest[3][1]-4.)<.001,"helicopter did not hold its hover altitude");
        if(i==total)require(relative[3][0]>14.&&relative[3][1]-rest[3][1]>12.,"helicopter did not clear the deck");
        if(!movie&&!still)continue;
        json sample={{"seconds",seconds},{"tick",state->tick},{"height_above_deck_m",relative[3][1]-rest[3][1]},
            {"gear_ray_gaps_m",json::array()}};
        if(still){
            const auto rows=voxelEquipmentPlatformTelemetry(platformConfig,state);
            require(rows.size()==2&&rows.back().at("model_id")=="ah64d","deck takeoff platform telemetry missing");
            const auto& rotors=rows.back().at("rotors");
            for(const auto* id:{"main-rotor","tail-rotor"}){
                const auto& item=state->objects.at(id);
                const auto& data=rotors.at(id);
                const auto speed=data.at("rpm").get<double>();
                require(data.at("tick")==state->tick&&std::abs(vsg::length(item.angularVelocity)-std::abs(speed)*vsg::PI/30.)<1e-8,
                    "rotor speed and angular velocity disagree");
                checkMatrix(poseOf(*state,id),body*vsg::translate(item.rotor->pivot)*
                    vsg::rotate(vsg::radians(data.at("angle_deg").get<double>()),item.rotor->axis),
                    "rotor hub or phase does not match published state");
            }
            const double expectedMain=stillIndex==0?0.:stillIndex==90?143.:286.;
            require(std::abs(rotors.at("main-rotor").at("rpm").get<double>()-expectedMain)<1e-8,
                "main rotor did not go through the configured spin-up");
            const double expectedTail=-1130.*expectedMain/286.;
            require(std::abs(rotors.at("tail-rotor").at("rpm").get<double>()-expectedTail)<1e-8,
                "tail rotor did not follow its independent signed speed");
            sample["rotors"]=rotors;
            const auto presentation=scenePresentation(platformConfig,*state,{{"items",json::array()}});
            const auto status=presentation.at("platforms").back().at("state");
            const char* expectedState=stillIndex==0?"landed":seconds<liftoff?"spooling_up":
                seconds<hover?"climbing":seconds<departure?"hovering":"flying";
            require(status==expectedState,"presentation state does not match the takeoff timeline");
            sample["platform_state"]=status;
            int rotorSounds=0;
            for(const auto& emitter:presentation.at("loops"))if(emitter.contains("rpm")){
                const auto mount=emitter.at("mount_object").get<std::string>();
                require(emitter.at("rpm")==rotors.at(mount).at("rpm"),"sound speed and rotor telemetry differ");
                ++rotorSounds;
            }
            require(rotorSounds==(stillIndex==0?0:2),"takeoff lost a rotor sound or played while stopped");
            if(seconds<liftoff)for(const auto& gear:reference.at("gear_contacts_local_m")){
                const auto sole=body*D(gear.at(0),gear.at(1),gear.at(2));
                const auto start=sole+D(0,.4,0);
                const auto deck=raycastVoxelScene(*state,start,{0,-1,0},1.,false,"vessel");
                require(bool(deck),"no occupied flight deck under landing gear");
                const double gap=sole.y-deck->position.y;
                require(gap>0.&&gap<.035,"parked landing gear does not align with the flight deck");
                const auto belowWheel=sole-D(0,gap*.5,0);
                const auto bullet=scene.rigidRaycast(belowWheel,{0,-1,0},1.);
                require(bullet&&bullet->object=="vessel"&&std::abs(bullet->position.y-deck->position.y)<.004,
                    "flight deck collision and scene ray are inconsistent");
                sample["gear_ray_gaps_m"].push_back(gap);
            }
        }
        const auto up=chart*vsg::dvec4(0,1,0,0);
        camera->eye=chart*(ship*D{-22.,17.4,-61.});
        const double climb=std::max(0.,body[3][1]-rest[3][1]-4.);
        camera->center=chart*(ship*D{3.,15.3+.74*climb,-41.});
        camera->up={up.x,up.y,up.z};
        view.context().setSimulationTime(state->timeSeconds());
        const auto serial=view.widget()->property("renderFrameSerial").toULongLong();
        timing.restart();
        do {
            view.markActivity();
            view.renderFrame();
            view.context().viewer()->deviceWaitIdle();
        } while(view.widget()->property("renderFrameSerial").toULongLong()==serial&&timing.elapsed()<2500);
        require(view.widget()->property("renderFrameSerial").toULongLong()>serial,"takeoff did not submit a native frame");
        renderMs.push_back(timing.nsecsElapsed()/1e6);
        for(const auto* id:{"vessel","body","main-rotor","tail-rotor"})
            checkMatrix(feature.display().placements.at(id)->matrix,poseOf(*state,id),"rendered ship or aircraft pose lagged simulation");
        const auto image=view.grabFrame();
        require(!image.isNull(),"Vulkan deck takeoff readback failed");
        const auto save=[&](const std::filesystem::path& file) {
            require(image.save(QString::fromStdString(file.string())),"cannot save deck takeoff screenshot");
        };
        if(still){
            save(path/(named->second+".png"));
            sample["name"]=named->second;
            report["shots"].push_back(std::move(sample));
        }
        if(movie)save(path/"frames"/QString("%1.png").arg(i,4,10,QChar('0')).toStdString());
    }
    report["native_frame_wall_p95_ms"]=percentile95(renderMs);
    report["scene_step_wall_p95_ms"]=percentile95(stepMs);
    std::ofstream(path/"acceptance.json")<<report.dump(2)<<'\n';
    std::cout<<"PASS: Taipei Vulkan deck takeoff, gear alignment, hover, rotor telemetry and render poses\n";
}

void runSonarCrossing(VoxelEquipmentSource& source,VoxelSceneFeature& feature,Viewport3DView& view,
                      const VoxelSceneProject& project,const QString& output) {
    using D=vsg::dvec3;
    using namespace vsgraysim::gui::display;
    const auto config=project.equipmentConfig();
    require(config.sounders.size()==1,"crossing requires a single depth sounder");
    const auto& sounder=config.sounders.front();
    const auto subject=std::find_if(config.platforms.begin(),config.platforms.end(),
        [](const auto& platform){return platform.kind=="submersible";});
    require(subject!=config.platforms.end(),"crossing requires a submersible");
    const auto& subId=subject->object;
    const bool diesel=subject->modelId=="cc0-diesel-submarine";
    const int lastFrame=diesel?700:300;
    const auto port=sounder.equipment.equipment_id;
    EquipmentSession session({SensorType::SONAR,EquipmentWorkRole::Search,true,1,QStringLiteral("船载声呐")});
    require(session.bind(port,sounder.equipment.name)&&source.hasDataDemand(),"sonar console did not subscribe");
    SonarDisplayPanel panel(nullptr);
    panel.resize(462,540);
    panel.setStyleSheet(QStringLiteral(
        "#sonar_survey_panel{background:#14242e;} QLabel{color:#e0eef4;}"
        "QPushButton{color:#e0eef4;background:#253c49;border:1px solid #4d6b78;padding:7px;}"));
    panel.ensurePolished();
    auto camera=view.context().camera()->viewMatrix.cast<vsg::LookAt>();
    auto perspective=view.context().camera()->projectionMatrix.cast<vsg::Perspective>();
    const auto initial=source.snapshot();
    const auto& submersible=initial->objects.at(subId);
    const auto propeller=initial->objects.find("diesel-propeller");
    const auto bounds=submersible.localBounds();
    const auto centre=diesel?(bounds.min+bounds.max)/2.:D{};
    const auto dimensions=bounds.max-bounds.min;
    vsg::dbox visible;
    for(const auto& point:submersible.surface().vertices())visible.add(point);
    if(propeller!=initial->objects.end()){
        const auto& part=propeller->second;
        const auto relative=vsg::inverse(submersible.pose(initial->tick,initial->fixedStep))*
            part.pose(initial->tick,initial->fixedStep);
        for(const auto& point:part.surface().vertices())visible.add(relative*point);
    }
    const auto visibleExtent=visible.max-visible.min;
    if(diesel)require(vsg::length(visibleExtent-D{65.265280,12.9,10.3})<.04,
        "diesel submarine length, height or beam changed during voxel import");
    const auto chart=simcore::geo::makeEnuToWorldMatrix(initial->anchor);
    const auto up=chart*vsg::dvec4(0,1,0,0);
    camera->up={up.x,up.y,up.z};
    const auto path=std::filesystem::absolute(output.toStdString());
    const bool movie=std::getenv("VSGRAYSIM_SONAR_FRAMES")!=nullptr;
    std::filesystem::create_directories(path);
    if(movie)std::filesystem::create_directories(path/"frames");
    std::vector<double> renders,steps;
    json report={{"seabed","synthetic demonstration terrain"},{"fps",25},{"soundings",json::array()},
        {"source_dem_m",initial->objects.at("taipei-coast").surface().dem_spacing_m},
        {"source_imagery_m",initial->objects.at("taipei-coast").surface().imagery_spacing_m},
        {"platform_model",subject->modelId},{"submersible_cell_m",submersible.volume->resolution_m},
        {"submersible_grid_extent_local_m",{dimensions.x,dimensions.y,dimensions.z}},
        {"submersible_visible_extent_local_m",{visibleExtent.x,visibleExtent.y,visibleExtent.z}},
        {"submersible_surface_triangles",submersible.surface().indices().size()/3}};
    uint64_t lastId=0,lastTick=0;
    unsigned echoes=0;
    bool bottomBefore=false,bottomAfter=false;
    view.setAutoFrame(false);
    view.setIdleSuspend(false);
    const auto pose=[](const VoxelSceneSnapshot& snap,const std::string& id){
        return snap.objects.at(id).pose(snap.tick,snap.fixedStep);
    };
    for(int i=0;i<=lastFrame;++i){
        const auto now=uint64_t(i*2);
        QElapsedTimer measured;
        measured.start();
        do {
            if(i>0)++lastTick;
            source.onTick(TimeTick{lastTick,lastTick*initial->fixedStep,initial->fixedStep});
        } while(lastTick<now);
        steps.push_back(measured.nsecsElapsed()/1e6/(i==0?1.:2.));
        const auto snap=source.snapshot();
        require(source.isHealthy()&&snap->tick==now,"sonar source fell behind its simulation clock");
        session.tick(snap->contentRevision);
        const auto echo=session.sonarSnapshot();
        require(echo&&echo->water_column_enabled&&echo->tick<=now,"formal sonar console lost the sounder frame");
        bool firstEcho=false,firstRecovery=false;
        if(echo->frame_id!=lastId){
            lastId=echo->frame_id;
            require(echo->tick==now&&echo->valid(),"sounding frame departed from the rendered scene tick");
            const auto origin=sounder.origin(*snap);
            require(origin.has_value(),"no submerged transducer mount");
            if(now==0){
                const auto hull=raycastVoxelScene(*snap,*origin,{0,1,0},2.,false,sounder.hostObject);
                require(bool(hull),"transducer is not mounted beneath the ship hull");
                report["transducer_to_hull_m"]=hull->distance;
            }
            auto hit=raycastVoxelScene(*snap,*origin,{0,-1,0},sounder.rangeMetres,false,subId);
            if(propeller!=initial->objects.end()){
                const auto shaftHit=raycastVoxelScene(*snap,*origin,{0,-1,0},sounder.rangeMetres,false,propeller->first);
                if(shaftHit&&(!hit||shaftHit->distance<hit->distance))hit=shaftHit;
            }
            require(bool(hit)==(echo->echo_range_m>0.),"visible submersible geometry disagrees with the sounder return");
            if(hit){
                require(std::abs(hit->distance-echo->echo_range_m)<1e-7&&echo->bottom==BottomReturn::NoBottom,
                    "water-column echo depth or bottom occlusion is inconsistent");
                firstEcho=echoes++==0;
            }else if(echo->bottom==BottomReturn::Valid){
                if(echoes==0)bottomBefore=true;
                else if(!bottomAfter){bottomAfter=true;firstRecovery=true;}
            }
            report["soundings"].push_back({{"tick",echo->tick},{"seconds",echo->sim_time},
                {"echo_range_m",echo->echo_range_m},{"bottom_depth_m",echo->water_depth_m},
                {"bottom_valid",echo->bottom==BottomReturn::Valid}});
        }
        panel.tick(&session);
        const auto midShot=diesel?"03-diesel-echo.png":"03-alvin-echo.png";
        const char* still=i==0?"01-surface.png":firstEcho?"02-entered-beam.png":
            i==lastFrame/2?midShot:firstRecovery?"04-bottom-restored.png":nullptr;
        if(!movie&&!still)continue;
        const auto ship=pose(*snap,sounder.hostObject);
        const auto sub=pose(*snap,subId)*centre;
        if(i<53){
            perspective->fieldOfViewY=43.;
            camera->eye=chart*(ship*D{42.,13.,34.});
            camera->center=chart*(ship*D{0.,5.7,0.});
        }else if(diesel){
            const double progress=(i-53.)/(lastFrame-53.);
            const double close=std::pow(std::sin(vsg::PI*progress),4.);
            const auto focus=sub+D{9.*close,-1.2+2.6*close,0.};
            perspective->fieldOfViewY=44.;
            camera->eye=chart*(focus+D{(28.-53.*progress)*(1.-close)+10.*close,
                10.-2.5*close,56.-39.*close});
            camera->center=chart*focus;
        }else{
            const double progress=(i-53.)/(lastFrame-53.);
            perspective->fieldOfViewY=43.;
            camera->eye=chart*(sub+D{6.8+1.8*progress,4.5,-7.6+9.*progress});
            camera->center=chart*(sub+D{.15,.15,0.});
        }
        feature.bindSnapshot(snap,source.presentationState(*snap));
        view.context().setSimulationTime(snap->timeSeconds());
        view.markActivity();
        measured.restart();
        view.renderFrame();
        const auto image=view.grabFrame();
        renders.push_back(measured.nsecsElapsed()/1e6);
        require(!image.isNull(),"underwater native Vulkan readback failed");
        require(feature.display().placements.at(subId)->matrix==pose(*snap,subId),
            "Vulkan display does not use the sonar snapshot pose");
        const QImage console=panel.grab().toImage();
        QImage combined(image.width()+console.width(),image.height(),QImage::Format_RGB888);
        QPainter painter(&combined);
        painter.drawImage(0,0,image);
        painter.drawImage(image.width(),0,console);
        painter.end();
        const auto save=[&](const std::filesystem::path& file){
            require(combined.save(QString::fromStdString(file.string())),"cannot save sonar crossing frame");
        };
        if(still)save(path/still);
        if(movie)save(path/"frames"/QString("%1.png").arg(i,4,10,QChar('0')).toStdString());
    }
    require(bottomBefore&&echoes>=4&&bottomAfter,"crossing did not produce seabed, submersible return and seabed recovery");
    report["water_column_frames"]=echoes;
    report["native_render_and_readback_p95_ms"]=percentile95(renders);
    report["simulation_source_step_p95_ms"]=percentile95(steps);
    std::ofstream(path/"acceptance.json")<<report.dump(2)<<'\n';
    std::cout<<"PASS: native Taipei submersible echo crossing, formal sounder subscription and Qt panel\n";
}

void runDieselPropeller(VoxelEquipmentSource& source,VoxelSceneFeature& feature,Viewport3DView& view,
                        const QString& output) {
    using D=vsg::dvec3;
    const auto initial=source.snapshot();
    constexpr auto id="diesel-propeller";
    const auto& initialPart=initial->objects.at(id);
    require(initialPart.rotor&&initial->tick==0&&initial->fixedStep==.02,"propeller capture needs a fresh 50 Hz scene");
    const auto& drive=*initialPart.rotor;
    require(drive.carrier=="diesel-submarine"&&drive.axis==D(1,0,0),"propeller shaft does not follow the reviewed asset");
    const auto directory=std::filesystem::absolute(output.toStdString());
    std::filesystem::create_directories(directory/"frames");
    const auto chart=simcore::geo::makeEnuToWorldMatrix(initial->anchor);
    const auto up=chart*vsg::dvec4(0,1,0,0);
    auto camera=view.context().camera()->viewMatrix.cast<vsg::LookAt>();
    camera->up={up.x,up.y,up.z};
    view.context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=41.;
    view.setAutoFrame(false);
    view.setIdleSuspend(false);
    const std::map<uint64_t,const char*> stills{{0,"01-stopped.png"},{90,"02-spooling.png"},{190,"03-running.png"}};
    json report={{"fps",50},{"shaft_local_m",{drive.pivot.x,drive.pivot.y,drive.pivot.z}},
        {"propeller_cell_m",initialPart.volume->resolution_m},{"utc_origin",initial->utcOrigin},
        {"samples",json::array()}};
    std::array<bool,32> firstMask{};
    bool rotatingSurface=false;
    std::vector<double> renders;
    for(uint64_t tick=0;tick<=300;++tick){
        source.onTick(TimeTick{tick,tick*initial->fixedStep,initial->fixedStep});
        const auto snapshot=source.snapshot();
        require(source.isHealthy()&&snapshot->tick==tick,"propeller capture lost a scene tick");
        const auto& part=snapshot->objects.at(id);
        const auto carrier=snapshot->objects.at(drive.carrier).pose(tick,snapshot->fixedStep);
        const auto shaft=carrier*drive.pivot;
        const auto rotorPose=part.pose(tick,snapshot->fixedStep);
        const auto platform=source.platforms().front();
        const auto& telemetry=platform.at("rotors").at(id);
        const double rpm=telemetry.at("rpm");
        const auto angle=vsg::radians(telemetry.at("angle_deg").get<double>());
        const auto expected=carrier*vsg::translate(drive.pivot)*vsg::rotate(angle,drive.axis);
        for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row)
            require(std::abs(rotorPose[col][row]-expected[col][row])<1e-8,"shaft pose differs from published phase");
        require(telemetry.at("tick")==tick&&std::abs(part.angularVelocity.x-rpm*vsg::PI/30.)<1e-8,
            "propeller angular velocity differs from published RPM");
        if(tick==190||tick==192){
            std::array<bool,32> mask{};
            for(size_t i=0;i<mask.size();++i){
                const double phase=2.*vsg::PI*i/mask.size();
                const auto origin=shaft+D{-2.,1.43*std::cos(phase),1.43*std::sin(phase)};
                mask[i]=bool(raycastVoxelScene(*snapshot,origin,{1,0,0},3.,false,id));
            }
            if(tick==190)firstMask=mask;
            else rotatingSurface=firstMask!=mask;
        }
        camera->eye=chart*(shaft+D{-4.8,1.4,2.9});
        camera->center=chart*shaft;
        feature.bindSnapshot(snapshot,source.presentationState(*snapshot));
        view.context().setSimulationTime(snapshot->timeSeconds());
        view.markActivity();
        QElapsedTimer measured;measured.start();
        view.renderFrame();
        const auto image=view.grabFrame();
        renders.push_back(measured.nsecsElapsed()/1e6);
        require(feature.display().placements.at(id)->matrix==rotorPose,"rendered propeller missed its simulation pose");
        const auto save=[&](const auto& path){
            require(!image.isNull()&&image.save(QString::fromStdString(path.string())),"Vulkan propeller frame was not saved");
        };
        save(directory/"frames"/QString("%1.png").arg(tick,4,10,QChar('0')).toStdString());
        if(const auto named=stills.find(tick);named!=stills.end()){
            const double expectedRpm=tick==0?0.:tick==90?45.:90.;
            require(std::abs(rpm-expectedRpm)<1e-8,"propeller did not pass through the preview speed stages");
            const auto jets=oceanPropellerJets(*snapshot);
            double flowSpeed=0.;
            double plumeLength=0.,stationaryLength=0.;
            if(drive.waterJet&&rpm>0.){
                require(jets.size()==1&&vsg::length(jets.front().hub-shaft)<1e-8&&
                    std::abs(jets.front().spin-rpm*vsg::PI/30.)<1e-8&&jets.front().axis.x<-.99,
                    "propeller jet disagrees with the submerged shaft");
                flowSpeed=jets.front().speed;
                require(flowSpeed>0.,"rotating propeller did not drive any water movement");
                plumeLength=vsg::length(jets.front().plume);
                auto stationary=*snapshot;
                stationary.objects.at(id).velocity=snapshot->ocean->current;
                const auto stationaryJets=oceanPropellerJets(stationary);
                require(stationaryJets.size()==1,"stationary shaft did not stir the water");
                stationaryLength=vsg::length(stationaryJets.front().plume);
                require(plumeLength>stationaryLength,"plume did not elongate with the platform's advance");
                if(tick==190){
                    camera->eye=chart*(shaft+D{-7.2,3.2,14.});
                    camera->center=chart*(shaft+D{-4.2,0.,0.});
                    const auto sideShot=[&](VoxelSceneSnapshotPtr shot,const char* filename){
                        feature.bindSnapshot(std::move(shot));
                        view.markActivity();
                        view.renderFrame();
                        const auto side=view.grabFrame();
                        require(!side.isNull()&&side.save(QString::fromStdString((directory/filename).string())),
                            "could not save same-camera propeller side comparison");
                    };
                    sideShot(std::make_shared<VoxelSceneSnapshot>(std::move(stationary)),"04-side-zero-through-water-speed.png");
                    sideShot(snapshot,"05-side-underway.png");
                    camera->eye=chart*(shaft+D{-4.8,1.4,2.9});
                    camera->center=chart*shaft;
                }
            }else require(jets.empty(),"stationary or disabled propeller emitted a jet");
            save(directory/named->second);
            report["samples"].push_back({{"seconds",snapshot->timeSeconds()},{"rpm",rpm},
                {"angle_deg",telemetry.at("angle_deg")},{"jet_speed_mps",flowSpeed},
                {"plume_extent_m",plumeLength},{"stationary_plume_extent_m",stationaryLength}});
        }
    }
    require(rotatingSurface,"propeller rays did not follow the moving blade surfaces");
    report["rotating_surface_ray_verified"]=rotatingSurface;
    report["native_render_and_readback_p95_ms"]=percentile95(renders);
    std::ofstream(directory/"propeller-acceptance.json")<<report.dump(2)<<'\n';
    std::cout<<"PASS: diesel propeller shaft, spin-up, telemetry, moving ray surfaces and native Vulkan capture\n";
}

void runEnvironmentMotion(VoxelEquipmentSource& source,VoxelSceneFeature& feature,Viewport3DView& view,
                          const QString& output) {
    using D=vsg::dvec3;
    const auto initial=source.snapshot();
    const bool ship=initial->objects.count("survey-vessel");
    const auto id=ship?"survey-vessel":"aircraft";
    const auto& subject=initial->objects.at(id);
    require(subject.poseTrack&&subject.poseTrack->size()==2,"wave/gust capture requires a two-key route");
    const auto& from=subject.poseTrack->front();
    const auto& to=subject.poseTrack->back();
    const D velocity=(to.position-from.position)/(to.seconds-from.seconds);
    const auto chart=simcore::geo::makeEnuToWorldMatrix(initial->anchor);
    const auto up=chart*vsg::dvec4(0,1,0,0);
    const auto camera=view.context().camera()->viewMatrix.cast<vsg::LookAt>();
    camera->up={up.x,up.y,up.z};
    view.context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=40.;
    view.setAutoFrame(false);
    view.setIdleSuspend(false);
    const auto directory=std::filesystem::absolute(output.toStdString());
    const bool enabled=subject.motionResponse!=VoxelMotionResponse::None;
    const bool hideHullSpray=ship&&std::getenv("VSGRAYSIM_CAPTURE_WITHOUT_HULL_SPRAY");
    std::filesystem::create_directories(directory/"frames");
    json report={{"object",id},{"response_enabled",enabled},{"fps",25},{"utc_origin",initial->utcOrigin},
        {"nominal_speed_mps",vsg::length(velocity)},{"hull_spray_visible",!hideHullSpray},{"samples",json::array()}};
    double maxHeave=0.,maxRoll=0.,maxPitch=0.;
    size_t minimumFoam=0;
    unsigned splashCount=0;
    double maxSplashOriginError=0.;
    std::vector<double> rendered,advanced;
    for(uint64_t frame=0;frame<=150;++frame){
        const uint64_t tick=frame*2;
        QElapsedTimer clock;
        clock.start();
        if(tick)
            for(uint64_t i=tick-1;i<=tick;++i)
                source.onTick(TimeTick{i,i*initial->fixedStep,initial->fixedStep});
        const auto snapshot=source.snapshot();
        advanced.push_back(clock.nsecsElapsed()/1e6);
        require(source.isHealthy()&&snapshot->tick==tick,"water/air motion capture missed its simulation tick");
        const auto& object=snapshot->objects.at(id);
        const auto pose=object.pose(tick,snapshot->fixedStep);
        const auto nominal=from.position+velocity*snapshot->timeSeconds();
        const auto relative=vsg::inverse(vsg::rotate(from.rotation))*pose;
        const double heave=pose[3][1]-nominal.y;
        const double roll=vsg::degrees(std::atan2(relative[0][1],relative[0][0]));
        const double pitch=vsg::degrees(std::atan2(-relative[2][1],relative[1][1]));
        maxHeave=std::max(maxHeave,std::abs(heave));
        maxRoll=std::max(maxRoll,std::abs(roll));
        maxPitch=std::max(maxPitch,std::abs(pitch));
        if(ship&&tick==200)minimumFoam=snapshot->ocean->foam.size();
        unsigned newSplashCount=0;
        if(ship)for(const auto& splash:snapshot->ocean->splashes){
            if(std::abs(splash.birth-snapshot->timeSeconds())>1e-8||vsg::length(splash.drift)<.01)continue;
            const auto wave=snapshot->ocean->surface(splash.x,splash.z,splash.birth);
            maxSplashOriginError=std::max(maxSplashOriginError,std::abs(splash.height-wave.height));
            const D point{splash.x,splash.height,splash.z};
            const auto local=vsg::inverse(pose)*point;
            const auto bounds=object.localBounds();
            const double side=local.x-(bounds.min.x+bounds.max.x)*.5;
            const auto outward=vsg::normalize(D{pose[0][0],0.,pose[0][2]})*std::copysign(1.,side);
            const auto hit=raycastVoxelScene(*snapshot,point+outward*.6,-outward,1.2,false,id);
            require(hit&&vsg::length(hit->position-point)<.35,"wave spray did not originate at the displaced hull");
            ++newSplashCount;
        }
        splashCount+=newSplashCount;
        // Match the comparison camera on the nominal trajectory, independent of platform movement.
        camera->eye=chart*(nominal+(ship?D{26.,11.,-36.}:D{16.,5.,-23.}));
        camera->center=chart*(nominal+(ship?D{0.,2.,0.}:D{0.,0.,1.}));
        if(hideHullSpray){
            auto before=std::make_shared<VoxelSceneSnapshot>(*snapshot);
            auto water=std::make_shared<VoxelOcean>(*snapshot->ocean);
            auto& splashes=water->splashes;
            splashes.erase(std::remove_if(splashes.begin(),splashes.end(),
                [](const auto& splash){return vsg::length(splash.drift)>.01;}),splashes.end());
            before->ocean=std::move(water);
            feature.bindSnapshot(std::move(before));
        }else feature.bindSnapshot(snapshot);
        view.context().setSimulationTime(snapshot->timeSeconds());
        view.markActivity();
        clock.restart();
        view.renderFrame();
        const auto image=view.grabFrame();
        rendered.push_back(clock.nsecsElapsed()/1e6);
        require(feature.display().placements.at(id)->matrix==pose,"rendered pose disagreed with the canonical platform motion");
        require(!image.isNull()&&image.save(QString::fromStdString(
            (directory/"frames"/QString("%1.png").arg(frame,4,10,QChar('0')).toStdString()).string())),
            "native wave/gust capture failed");
        report["samples"].push_back({{"seconds",snapshot->timeSeconds()},{"heave_m",heave},
            {"bank_deg",roll},{"pitch_deg",pitch},{"new_hull_splashes",newSplashCount}});
    }
    require(enabled?(maxHeave>.075&&maxRoll>.2&&maxPitch>.1):
        (maxHeave<1e-8&&maxRoll<1e-8&&maxPitch<1e-8),"expected motion response was not measured");
    require(!ship||minimumFoam>0,"moving ship did not leave foam");
    if(ship&&enabled)
        require(splashCount>0&&maxSplashOriginError<1e-7,"bow spray did not follow the same wave surface as the hull");
    report.update({{"peak_heave_m",maxHeave},{"peak_bank_deg",maxRoll},{"peak_pitch_deg",maxPitch},
        {"foam_deposits_at_4s",minimumFoam},{"wave_spray_events",splashCount},{"max_spray_origin_height_error_m",maxSplashOriginError},
        {"native_render_and_readback_p95_ms",percentile95(rendered)},
        {"two_source_ticks_p95_ms",percentile95(advanced)}});
    std::ofstream(directory/"environment-motion.json")<<report.dump(2)<<'\n';
    std::cout<<"PASS: water/air motion route, canonical display pose and native Vulkan capture\n";
}

void runMarineAudio(VoxelEquipmentSource& source,VoxelSceneFeature& feature,Viewport3DView& view,
                   const QString& output) {
    using D=vsg::dvec3;
    const auto initial=source.snapshot();
    constexpr uint64_t lastTick=800;
    constexpr unsigned samplesPerTick=audio::SceneAudioMixer::sampleRate/50;
    require(initial->ocean&&initial->fixedStep==.02&&initial->tick==0,"marine audio route requires a fresh 50 Hz ocean");
    const auto path=std::filesystem::absolute(output.toStdString());
    std::filesystem::create_directories(path/"frames");
    const auto chart=simcore::geo::makeEnuToWorldMatrix(initial->anchor);
    const auto up=chart*vsg::dvec4(0,1,0,0);
    const auto camera=view.context().camera()->viewMatrix.cast<vsg::LookAt>();
    camera->up={up.x,up.y,up.z};
    view.context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=48.;
    view.setAutoFrame(false);
    view.setIdleSuspend(false);
    constexpr const char* shipId="survey-vessel";
    constexpr const char* shaftId="diesel-propeller";
    struct Shot {double seconds;D eye,target;};
    const std::array<Shot,9> shots{{
        {0.,{-26.,9.2,-24.},{0.,4.1,-3.}},
        {2.3,{-11.,2.2,-13.},{0.,1.5,-6.}},
        {4.2,{-11.,.8,-13.},{-2.,.1,-2.}},
        {6.1,{-13.,-1.2,-10.},{-7.,-3.,3.}},
        {8.4,{-15.,-5.8,-4.},{-6.7,-9.,10.}},
        {10.8,{-12.,-9.9,2.},{-2.,-13.45,10.}},
        {12.8,{-5.7,-11.7,5.4},{5.,-13.45,10.}},
        {14.5,{3.8,-12.1,6.6},{9.,-13.45,9.8}},
        {16.,{7.9,-12.4,7.4},{13.75,-13.45,9.6}},
    }};
    const auto route=[&](double time){
        const auto right=std::upper_bound(shots.begin(),shots.end(),time,
            [](double t,const Shot& shot){return t<shot.seconds;});
        if(right==shots.end())return shots.back();
        const auto i=size_t(right-shots.begin()-1);
        const auto& a=shots[i];const auto& b=shots[i+1];
        const auto& before=shots[i?i-1:i];const auto& after=shots[std::min(i+2,shots.size()-1)];
        const double duration=b.seconds-a.seconds,u=(time-a.seconds)/duration;
        const auto hermite=[&](D p0,D p1,D pm,D p2){
            const D m0=(p1-pm)*(duration/(b.seconds-before.seconds));
            const D m1=(p2-p0)*(duration/(after.seconds-a.seconds));
            return p0*((1.+2.*u)*(1.-u)*(1.-u))+m0*(u*(1.-u)*(1.-u))+
                p1*(u*u*(3.-2.*u))+m1*(u*u*(u-1.));
        };
        return Shot{time,hermite(a.eye,b.eye,before.eye,after.eye),
            hermite(a.target,b.target,before.target,after.target)};
    };
    SceneAcoustics solver;
    audio::SceneAudioMixer mixer;
    std::array<int16_t,2*samplesPerTick> block{};
    std::vector<int16_t> pcm;
    pcm.reserve(size_t(lastTick+2)*block.size());
    std::vector<double> renders,soundTimes,sourceTimes;
    json samples=json::array();
    double previousMix=0.,jump=0.,firstWet=-1.,lastEyeAir=-1.;
    unsigned mixed=0,engineAir=0,engineWater=0,shaftWater=0;
    unsigned frameCount=0;
    for(uint64_t tick=0;tick<=lastTick+1;++tick){
        const double time=tick*initial->fixedStep;
        QElapsedTimer clock;clock.start();
        source.onTick(TimeTick{tick,time,initial->fixedStep});
        sourceTimes.push_back(clock.nsecsElapsed()/1e6);
        const auto snapshot=source.snapshot();
        require(source.isHealthy()&&snapshot->tick==tick,"marine audio lost the scene clock");
        const auto presentation=source.presentationState(*snapshot);
        const auto shot=route(std::min(time,16.));
        const auto last=route(std::max(0.,time-initial->fixedStep));
        const auto right=vsg::normalize(vsg::cross(shot.target-shot.eye,D{0,1,0}));
        clock.restart();
        solver.ingest(*snapshot,presentation,"marine-dive");
        const auto reading=solver.listen(*snapshot,shot.eye,(shot.eye-last.eye)/initial->fixedStep,false);
        mixer.apply(audio::acousticFrame(reading,{"marine-dive",tick,true,{}},{right.x,right.y,right.z}));
        mixer.render(block.data(),block.size());
        soundTimes.push_back(clock.nsecsElapsed()/1e6);
        pcm.insert(pcm.end(),block.begin(),block.end());
        const double mix=reading.submergedMix;
        jump=std::max(jump,std::abs(previousMix-mix));previousMix=mix;
        if(mix>.05&&mix<.95)++mixed;
        if(mix<.05)lastEyeAir=time;
        if(firstWet<0.&&reading.underwater)firstWet=time;
        json motors=json::array();
        const auto& shaft=snapshot->objects.at(shaftId);
        const auto shaftPosition=shaft.pose(tick,initial->fixedStep)*D{};
        for(const auto& emitter:reading.paths){
            if(emitter.cue=="ship_engine")++engineAir;
            else if(emitter.cue=="sea_ship_water")++engineWater;
            else if(emitter.cue=="sea_propeller"){
                ++shaftWater;
                require(emitter.underwater&&vsg::length(emitter.sourcePosition-shaftPosition)<.30,
                    "shaft audio did not originate from the moving propeller axial hub");
            }else continue;
            if(tick%25==0)motors.push_back({{"cue",emitter.cue},{"distance_m",emitter.distance},
                {"pitch",emitter.pitch},{"delay_s",emitter.delay}});
        }
        if(tick%25==0){
            const auto& ship=snapshot->objects.at(shipId);
            samples.push_back({{"seconds",time},{"water_mix",mix},{"spl_db",reading.dbSpl()},
                {"pressure_reference_pa",reading.referencePressure()},
                {"ship_ground_speed_mps",std::hypot(ship.velocity.x,ship.velocity.z)},
                {"shaft_rpm",shaft.rotor->rpmAt(time)},{"paths",std::move(motors)}});
        }
        if(tick%2!=0)continue;
        camera->eye=chart*shot.eye;camera->center=chart*shot.target;
        feature.bindSnapshot(snapshot,presentation);
        view.context().setSimulationTime(time);
        view.markActivity();
        clock.restart();
        view.renderFrame();
        const auto image=view.grabFrame();
        renders.push_back(clock.nsecsElapsed()/1e6);
        require(feature.display().placements.at(shaftId)->matrix==shaft.pose(tick,initial->fixedStep),
            "marine audio and native frame used different shaft states");
        require(!image.isNull()&&image.save(QString::fromStdString(
            (path/"frames"/QString("%1.png").arg(frameCount++,4,10,QChar('0')).toStdString()).string())),
            "marine audio native frame was not saved");
    }
    require(engineAir>50&&engineWater>50&&shaftWater>80&&mixed>=20&&jump<.10&&firstWet>4.&&firstWet<7.,
        "marine dive lost a medium, motor or gradual crossing");
    require(pcm.size()==size_t(frameCount)*2*audio::SceneAudioMixer::sampleRate/25&&
        std::any_of(pcm.begin(),pcm.end(),[](int16_t value){return std::abs(int(value))>100;}),
        "picture clock and audio sample count differ or stereo is silent");
    std::ofstream wave(path/"marine-stereo.wav",std::ios::binary);
    const auto word=[&](uint32_t n,int width){for(int i=0;i<width;++i)wave.put(char((n>>(8*i))&255u));};
    const auto bytes=uint32_t(pcm.size()*sizeof(int16_t));
    wave.write("RIFF",4);word(36+bytes,4);wave.write("WAVEfmt ",8);word(16,4);
    word(1,2);word(2,2);word(audio::SceneAudioMixer::sampleRate,4);
    word(audio::SceneAudioMixer::sampleRate*4,4);word(4,2);word(16,2);
    wave.write("data",4);word(bytes,4);
    for(auto value:pcm)word(uint16_t(value),2);
    require(bool(wave),"synchronized stereo WAV could not be saved");
    json report={{"picture_frames",frameCount},{"video_fps",25},{"audio_frames",pcm.size()/2},
        {"audio_hz",audio::SceneAudioMixer::sampleRate},{"first_submerged_s",firstWet},
        {"last_mostly_air_s",lastEyeAir},{"mixed_states",mixed},{"max_medium_mix_delta_per_tick",jump},
        {"ship_air_paths",engineAir},{"ship_water_paths",engineWater},{"shaft_paths",shaftWater},
        {"utc_origin",initial->utcOrigin},{"native_render_readback_p95_ms",percentile95(renders)},
        {"source_tick_p95_ms",percentile95(sourceTimes)},{"acoustic_and_pcm_p95_ms",percentile95(soundTimes)},
        {"samples",std::move(samples)}};
    std::ofstream(path/"marine-audio-acceptance.json")<<report.dump(2)<<'\n';
    std::cout<<"PASS: Vulkan marine dive and stereo PCM share the simulation clock\n";
}
}
void runEarthTopicStillCapture(const QString& topic, const QString& output, EarthTopicCapture mode) {
    require(QApplication::platformName()=="xcb", "topic capture needs a native X11 surface");
    auto config = ViewportConfig::fromTopicFile(std::filesystem::absolute(topic.toStdString()).string());
    config.add(VoxelSceneFeature::kId, {{"paused", true}, {"advance_from_frames", false}});
    const bool external=mode==EarthTopicCapture::SonarCrossing||mode==EarthTopicCapture::DieselPropeller||
        mode==EarthTopicCapture::EnvironmentMotion||mode==EarthTopicCapture::MarineAudio;
    if(external)config.add(VoxelSceneFeature::kId,{{"external_only",true}});

    QElapsedTimer startup;
    startup.start();
    double firstFrameMs = -1;
    QMainWindow window;
    window.setFixedSize(960, 540);
    auto* view = new Viewport3DView(&window);
    window.setCentralWidget(view);
    QString failure;
    QObject::connect(view->widget(), &gui::VsgQtWidget::initializationFailed, &window,
                     [&](const QString& error) { failure = error; });
    view->applyConfig(config);
    view->setAutoFrame(true, 30);
    window.show();
    QElapsedTimer timer;
    timer.start();
    VoxelSceneFeature* feature = nullptr;
    while (timer.elapsed() < 90000 && failure.isEmpty()) {
        view->markActivity();
        pump(10);
        if (!view->isReady()) continue;
        if (firstFrameMs < 0 && view->widget()->property("renderFrameSerial").toULongLong() > 0)
            firstFrameMs = startup.nsecsElapsed() / 1e6;
        feature = view->featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);
        if (feature && (external || feature->runtime() || !feature->lastError().empty())) break;
    }
    require(failure.isEmpty(), failure.toStdString().c_str());
    if(external){
        require(feature,"external scene viewport was not ready");
        const auto project=VoxelSceneProject::open(topic.toStdString());
        VoxelEquipmentSource source(project);
        require(source.initialize(json::object()),"formal scene source could not load the topic");
        source.onStart();
        const auto state=source.snapshot();
        feature->bindSnapshot(state,source.presentationState(*state));
        view->context().setSimulationTime(state->timeSeconds());
        pump(2000);
        if(mode==EarthTopicCapture::DieselPropeller)runDieselPropeller(source,*feature,*view,output);
        else if(mode==EarthTopicCapture::EnvironmentMotion)runEnvironmentMotion(source,*feature,*view,output);
        else if(mode==EarthTopicCapture::MarineAudio)runMarineAudio(source,*feature,*view,output);
        else runSonarCrossing(source,*feature,*view,project,output);
        window.hide();
        pump(20);
        return;
    }
    require(feature && feature->runtime(), feature ? feature->lastError().c_str() : "topic scene not ready");
    const double physicsReadyMs = startup.nsecsElapsed() / 1e6;
    const auto snapshot = feature->runtime()->snapshot();
    view->context().setSimulationTime(snapshot->timeSeconds());
    timer.restart();
    while (timer.elapsed() < 3000) {
        view->markActivity();
        pump(10);
    }
    require(feature->runtime()->snapshot() == snapshot, "paused topic changed physical state");
    if (mode == EarthTopicCapture::TerrainTraverse) {
        runPageTraversal(*feature, *view, topic, output, firstFrameMs, physicsReadyMs);
    } else if (mode == EarthTopicCapture::TerrainCollision) {
        runTerrainCollision(*feature,*view,output);
    } else if (mode == EarthTopicCapture::Thermal) {
        runThermalCapture(*feature->runtime(), *view, output);
    } else if(mode==EarthTopicCapture::G63Motion) {
        runG63Motion(*feature,*view,topic,output);
    } else if(mode==EarthTopicCapture::DeckTakeoff) {
        runDeckTakeoff(*feature,*view,topic,output);
    } else {
        const auto frame = view->grabFrame();
        require(!frame.isNull() && frame.save(output), "could not save topic Vulkan framebuffer");
    }
    window.hide();
    pump(20);
}

void runEarthTargetDestruction(const QString& output,int maxDimension,const QString& comparisonAsset,double unitsToMetres) {
    require(maxDimension>=16&&maxDimension<=512,"target resolution must be in [16,512]");
    require(QApplication::platformName()=="xcb","target acceptance needs a real native surface");
    const auto directory=std::filesystem::absolute(output.toStdString());
    std::filesystem::create_directories(directory);
    const bool compare=!comparisonAsset.isEmpty();
    const auto asset=std::filesystem::absolute(compare?comparisonAsset.toStdString():"models/MQ-9/MQ-9.vsgb");
    const auto map=std::filesystem::absolute(compare?"config/earth/taipei-region.map.json":"config/earth/default.map.json");
    json definition={{"format","voxel-scene-v1"},{"fixed_step_seconds",.02},
        {"geo_anchor",{{"lat",compare?25.0747:25.078},{"lon",compare?121.5422:121.772},{"alt",compare?20.:364.}}},
        {"objects",json::array({
            {{"id","terrain"},{"role","terrain"},{"source",{{"type","earth"},{"map",map.string()},
                {"earth",{{"coverage",{{"min_x",-80},{"max_x",80},{"min_z",-80},{"max_z",80}}},
                          {"grid_size",81},{"imagery_size",128},{"strict",true}}},
                {"cell_size_m",4},{"bottom_m",-32},{"mode","hd"}}}},
            {{"id","aircraft"},{"role","target"},{"position_m",{0,30,0}},
             {"source",{{"type","mesh"},{"path",asset.string()},{"max_dimension",maxDimension},
                        {"up_axis",compare?"y":"z"},{"units_to_metres",unitsToMetres},
                        {"surface_detail",compare?"source_clipped":"corner"},{"fill_interior",!compare}}},
             {"physics",{{"motion","static"},{"fracture",true},{"support","none"},{"restitution",.08},{"friction",.72}}},
             {"material",{{"density_kg_m3",320},{"color_rgba",{1.,1.,1.,1.}},{"temperature_k",315}}}}
        })}};
    auto prepared=VoxelScene::load(definition);
    const auto initial=prepared->snapshot();
    const auto& aircraft=initial->objects.at("aircraft");
    const auto box=aircraft.localBounds();
    const auto com=aircraft.localCentreOfMass();
    const double span=vsg::length(box.max-box.min);
    const auto ground=raycastVoxelScene(*initial,{0,5000,0},{0,-1,0},10000,true);
    require(bool(ground),"destruction fixture has no physical DEM ground");
    double groundY=ground->position.y;
    if(compare)for(double x:{-6.,0.,6.})for(double z:{-6.,0.,6.}){
        const auto sample=raycastVoxelScene(*initial,{x,5000,z},{0,-1,0},10000,true);
        require(bool(sample),"model comparison pad has no DEM coverage");
        groundY=std::max(groundY,sample->position.y);
    }
    const auto centre=(box.min+box.max)/2.;
    const vsg::dvec3 offset=compare?vsg::dvec3{-centre.x,groundY+.10-box.min.y,-centre.z}:
                                          vsg::dvec3{-com.x,groundY+8.-box.min.y,-com.z};
    prepared->edit(json::array({{{"kind","place"},{"object","aircraft"},{"position_m",
                    {offset.x,offset.y,offset.z}}}}),initial->revision);
    const auto chart=simcore::geo::makeEnuToWorldMatrix(initial->anchor);
    QMainWindow window;window.setFixedSize(960,540);
    auto* view=new Viewport3DView(&window);window.setCentralWidget(view);
    bool ready=false;QString failure;
    QObject::connect(view,&Viewport3DView::vsgReady,&window,[&]{ready=true;view->setAutoFrame(false);});
    QObject::connect(view->widget(),&gui::VsgQtWidget::initializationFailed,&window,[&](const QString& text){failure=text;});
    auto config=ViewportConfig::fromPreset("earth_display");config.remove(OceanFeature::kId);
    config.add(EarthView::kId,{{"map_json",map.string()},{"lat",initial->anchor.lat_deg},{"lon",initial->anchor.lon_deg},{"alt",initial->anchor.alt_m},
                              {"cloud_objects",true},{"view_height",1200}});
    config.add(CloudFeature::kId,{{"cover",.45},{"wind_speed",6},{"stratus_altitude",1900},{"cumulus_altitude",3200}});
    config.add(VoxelSceneFeature::kId,{{"checkpoint",prepared->checkpoint()},{"paused",true},
                                      {"advance_from_frames",false},{"frame_scene",false}});
    view->applyConfig(config);window.show();
    QElapsedTimer deadline;deadline.start();
    while(!ready&&failure.isEmpty()&&deadline.elapsed()<45000)pump(10);
    require(ready&&failure.isEmpty(),"target scene viewport failed to initialise");
    auto* feature=view->featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);
    auto& scene=*feature->runtime();
    auto camera=view->context().camera()->viewMatrix.cast<vsg::LookAt>();
    const vsg::dvec3 focus=offset+com-vsg::dvec3(0,3,0);
    camera->eye=chart*(focus+vsg::dvec3(.80,.56,.95)*span*1.65);
    camera->center=chart*focus;
    const auto up=chart*vsg::dvec4(0,1,0,0);camera->up={up.x,up.y,up.z};
    view->context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=43.;
    json report={{"asset",asset.string()},{"cell_size_m",aircraft.volume->resolution_m},
                 {"initial_target_voxels",aircraft.volume->occupiedCount()},{"frames",json::array()},
                 {"geo_anchor",{{"lat",initial->anchor.lat_deg},{"lon",initial->anchor.lon_deg},{"alt",initial->anchor.alt_m}}},{"source_dem_spacing_m",initial->objects.at("terrain").surface().dem_spacing_m},
                 {"source_imagery_spacing_m",initial->objects.at("terrain").surface().imagery_spacing_m}};
    std::vector<double> frameTimes;
    auto render=[&]{view->setAutoFrame(false);view->setIdleSuspend(false);view->markActivity();view->context().setSimulationTime(scene.snapshot()->timeSeconds());
        auto viewer=view->context().viewer();const auto first=viewer->getFrameStamp()->frameCount;QElapsedTimer timeout;timeout.start();
        while(timeout.elapsed()<3000){QThread::msleep(21);QElapsedTimer elapsed;elapsed.start();view->renderFrame();viewer->deviceWaitIdle();const double ms=elapsed.nsecsElapsed()/1e6;
            if(viewer->getFrameStamp()->frameCount>first){frameTimes.push_back(ms);return ms;}}
        throw std::runtime_error("target timing did not submit a native frame");};
    auto capture=[&](const std::string& name) {
        render();QImage image;QElapsedTimer timeout;timeout.start();
        while(image.isNull()&&timeout.elapsed()<10000){view->setAutoFrame(false);view->setIdleSuspend(false);view->markActivity();pump(10);QThread::msleep(50);image=view->grabFrame();if(image.isNull()){window.showNormal();window.raise();render();}}
        require(!image.isNull(),"target native framebuffer capture failed");
        require(image.save(QString::fromStdString((directory/(name+".png")).string())),"target snapshot save failed");
    };
    if(compare) {
        auto options=vsg::Options::create(*view->context().options());
        options->sceneCoordinateConvention=vsg::CoordinateConvention::Y_UP;
        options->setValue(vsgXchange::assimp::two_sided,true);
        options->setValue(vsgXchange::assimp::culling,false);
        auto raw=vsgXchange::assimp::create()->read(asset.string(),options).cast<vsg::Node>();
        require(bool(raw),"original mesh viewer could not load comparison asset");
        auto sourceTransform=vsg::MatrixTransform::create(chart*vsg::translate(offset)*
                                vsg::scale(unitsToMetres,unitsToMetres,unitsToMetres));
        sourceTransform->addChild(raw);
        auto sourceSwitch=vsg::Switch::create();
        sourceSwitch->addChild(true,sourceTransform);
        auto world=view->context().worldRoot();
        world->addChild(sourceSwitch);
        vsg::GeometryInfo padGeometry;
        padGeometry.dx={12,0,0};padGeometry.dy={0,0,-12};
        padGeometry.position={0,float(groundY+.10),0};
        padGeometry.color=vsg::vec4(.27f,.29f,.31f,1.f);
        auto pad=vsg::MatrixTransform::create(chart);
        pad->addChild(vsg::Builder::create()->createQuad(padGeometry));
        world->addChild(pad);
        auto placement=feature->display().placements.at("aircraft");
        auto voxelChildren=std::move(placement->children);
        auto perspective=view->context().camera()->projectionMatrix.cast<vsg::Perspective>();
        perspective->fieldOfViewY=38.;
        const auto focus=offset+centre;
        const std::array eyes{focus+vsg::dvec3(.71,.34,.91)*span,
                              focus+vsg::dvec3(.72,.32,-.91)*span};
        camera->center=chart*focus;
        report["units_to_metres"]=unitsToMetres;
        report["utc_origin"]=initial->utcOrigin;
        report["map"]=map.string();
        report["voxel_grid_bounds_m"]={box.max.x-box.min.x,box.max.y-box.min.y,box.max.z-box.min.z};
        report["surface_vertices"]=aircraft.surface().vertices().size();
        report["surface_triangles"]=aircraft.surface().indices().size()/3;
        report["target_local_m"]={focus.x,focus.y,focus.z};
        report["camera_eyes_local_m"]=json::array();
        for(const auto& eye:eyes)report["camera_eyes_local_m"].push_back({eye.x,eye.y,eye.z});
        view->context().requestCompile();
        for(size_t i=0;i<eyes.size();++i){
            camera->eye=chart*eyes[i];capture("original-"+std::to_string(i+1));
        }
        sourceSwitch->setAllChildren(false);
        placement->children=std::move(voxelChildren);
        view->context().requestCompile();
        for(size_t i=0;i<eyes.size();++i){
            camera->eye=chart*eyes[i];capture("voxel-"+std::to_string(i+1));
        }
        require(scene.snapshot()->tick==initial->tick,"comparison changed simulation time");
        std::ofstream(directory/"comparison.json")<<report.dump(2)<<'\n';
        window.hide();pump(20);return;
    }
    scene.saveCheckpoint((directory/"intact-checkpoint.json").string());
    capture("intact");
    view->context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=20.;capture("intact-detail");
    view->context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=43.;
    // Render the exact original triangle mesh and source UV atlas at
    // the identical geodetic placement, lighting and camera as its voxels.
    struct SourceTriangles : vsg::Visitor {
        using vsg::Visitor::apply;
        struct Batch{vsg::ref_ptr<vsg::Data> image;std::vector<vsg::vec3> p,n;std::vector<vsg::vec2> uv;std::vector<vsg::vec4> color;};
        std::map<const vsg::Data*,Batch> batches;
        void apply(vsg::Node& n)override{n.traverse(*this);}
        void apply(vsg::VertexIndexDraw& draw)override {
            auto image=draw.getObject<vsg::Data>("voxel.import.image");auto& batch=batches[image];batch.image=image;
            const auto* p=draw.arrays[0]->data->cast<vsg::dvec3Array>();const auto* uv=draw.getObject<vsg::vec2Array>("voxel.import.uv");const auto* colors=draw.getObject<vsg::vec4Array>("voxel.import.color");const auto* normals=draw.getObject<vsg::vec3Array>("voxel.import.normal");
            for (size_t t=0; t+2<p->size(); t+=3) {
                const auto cross=vsg::cross((*p)[t+1]-(*p)[t],(*p)[t+2]-(*p)[t]);const auto normal=vsg::length(cross)>1e-12?vsg::normalize(cross):vsg::dvec3(0,0,1);
                for (size_t i=t; i<t+3; ++i) {batch.p.emplace_back((*p)[i]);batch.n.emplace_back(normals?vsg::dvec3((*normals)[i]):normal);batch.uv.push_back((*uv)[i]);batch.color.push_back((*colors)[i]);}
            }
        }
    } source;
    auto raw=vsg::read_cast<vsg::Node>(asset.string());require(bool(raw),"original target reference failed to load");raw->accept(source);
    auto original=vsg::Group::create();
    for(const auto& [image,batch]:source.batches) {
        auto p=vsg::vec3Array::create(batch.p.size());std::copy(batch.p.begin(),batch.p.end(),p->begin());
        auto n=vsg::vec3Array::create(batch.n.size());std::copy(batch.n.begin(),batch.n.end(),n->begin());
        auto uv=vsg::vec2Array::create(batch.uv.size());std::copy(batch.uv.begin(),batch.uv.end(),uv->begin());
        auto color=vsg::vec4Array::create(batch.color.size());std::copy(batch.color.begin(),batch.color.end(),color->begin());
        auto indices=vsg::uintArray::create(batch.p.size());for(size_t i=0;i<indices->size();++i)(*indices)[i]=i;
        auto draw=vsg::VertexIndexDraw::create();draw->assignArrays({p,n,uv,color});draw->assignIndices(indices);draw->indexCount=indices->size();draw->instanceCount=1;
        auto builder=vsg::Builder::create();builder->shaderSet=vsg::createPhongShaderSet();
        auto material=vsg::PhongMaterialValue::create();material->value().diffuse=vsg::vec4(1,1,1,1);material->value().specular=vsg::vec4(0,0,0,1);
        for(auto& binding:builder->shaderSet->descriptorBindings)if(binding.name=="material")binding.data=material;
        vsg::StateInfo state;state.image=batch.image;state.instance_colors_vec4=false;auto group=builder->createStateGroup(state);group->addChild(draw);original->addChild(group);
    }
    require(bool(original),"original target reference failed to load");
    auto originalPose=vsg::MatrixTransform::create(chart*vsg::translate(offset)*vsg::rotate(-vsg::PI*.5,vsg::dvec3(1,0,0)));originalPose->addChild(original);
    auto reference=vsg::Switch::create();reference->addChild(true,originalPose);view->context().worldRoot()->addChild(reference);
    auto placement=feature->display().placements.at("aircraft");auto voxelChildren=placement->children;placement->children.clear();
    view->context().requestCompile();capture("original");
    view->context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=20.;capture("original-detail");
    view->context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=43.;
    reference->setAllChildren(false);placement->children=voxelChildren;
    const auto point=occupiedCentre(scene.snapshot()->objects.at("aircraft"));
    scene.schedule({{"kind","damage"},{"object","aircraft"},{"tick",1},{"space","object"},
                    {"point_m",{point.x,point.y,point.z}},{"radius_m",span*.115},
                    {"blast_impulse_ns",aircraft.massKg()*5.}});
    GpuSensorSpec ir;ir.id=191;ir.period_seconds=.08;ir.output=ProductStage::Scene;ir.profile.width=9;ir.profile.height=7;ir.profile.fov_deg=2;
    ir.profile.detector.integration_ms=1;ir.profile.band=BandSpec::make(BandType::MWIR);auto tv=ir;tv.id=192;tv.profile.band=BandSpec::make(BandType::TV);
    VoxelSensorSession measurements(scene,{ir,tv},view->context().window()->getOrCreateDevice(),"target-contact-sensors");
    auto aperture=[&](uint32_t,double seconds){const auto state=scene.sample(seconds);const auto& o=state->objects.at("aircraft");const auto centre=o.pose(state->tick,state->fixedStep)*o.localCentreOfMass();GpuAperture a;a.position={centre.x+.137,ground->position.y+50.,centre.z+.193};a.forward={0,-1,0};a.up={0,0,-1};return a;};
    auto environment=[](uint32_t,double,const BandSpec&){EnvAnswer e;e.sigma_per_km=0;e.sky_radiance=0;e.sun_irradiance=100;e.sun_el_deg=90;e.sun_az_deg=0;return e;};
    std::map<uint32_t,size_t> measuredFrames;double maximumGpuRangeError=0;
    auto sensor=makeVoxelSceneSensorData(scene.snapshot());
    size_t contacts=0,referenceContactFrames=0,referenceContacts=0;double maximumRayError=0,maximumPenetration=0,maximumComSpeed=0,maximumAngularSpeed=0;
    VoxelDynamics contactIndex,contactReference(false);
    std::vector<double> physicsTimes,sensorTimes,completeTimes,steadyPhysicsTimes,steadyCompleteTimes;
    for(int frame=0;frame<70;++frame) {
        QElapsedTimer physicsTime;physicsTime.start();scene.setPaused(false);scene.advanceTo(.02+frame*.08,8);scene.setPaused(true);physicsTimes.push_back(physicsTime.nsecsElapsed()/1e6);
        const auto s=scene.snapshot();
        if(frame==0||frame==12||frame==69)std::cout<<"advance "<<(frame?80:20)<<" ms at frame "<<frame<<": "<<physicsTimes.back()<<" ms"<<std::endl;
        contacts+=s->contacts.size();
        for(const auto& c:s->contacts)maximumPenetration=std::max(maximumPenetration,c.penetrationM);
        size_t voxels=0;
        for(const auto& [id,o]:s->objects)if(o.role=="target")voxels+=o.volume->occupiedCount();
        const double renderMs=render();
        const auto& display=feature->display();
        if(sensor.snapshot->contentRevision!=s->contentRevision) {
            sensor=makeVoxelSceneSensorData(s);
            for(const auto& instance:sensor.instances) {
                auto group=display.placements.at(instance.id)->children.at(0).cast<vsg::StateGroup>();auto draw=group->children.at(0).cast<vsg::VertexIndexDraw>();
                const auto* a=draw->arrays[0]->data.cast<vsg::vec3Array>().get();const auto* b=instance.geometry->arrays[0]->data.cast<vsg::vec3Array>().get();
                require(a&&b&&a->size()==b->size()&&std::equal(a->begin(),a->end(),b->begin()),"native target display/sensor vertices differ");
                const auto* ai=draw->indices->data.cast<vsg::uintArray>().get();const auto* bi=instance.geometry->indices->data.cast<vsg::uintArray>().get();
                require(ai&&bi&&ai->size()==bi->size()&&std::equal(ai->begin(),ai->end(),bi->begin()),"native target display/sensor triangles differ");
            }
        }
        for(auto& instance:sensor.instances) {
            const auto& o=s->objects.at(instance.id);instance.transform=o.pose(s->tick,s->fixedStep);
            require(instance.transform==display.placements.at(instance.id)->matrix,"target display/sensor transforms disagree");
            if(o.body.motion=="dynamic") {const auto offset=o.pose(s->tick,s->fixedStep)*vsg::dvec4(o.localCentreOfMass(),0.);maximumComSpeed=std::max(maximumComSpeed,vsg::length(o.velocity+vsg::cross(o.angularVelocity,vsg::dvec3(offset.x,offset.y,offset.z))));maximumAngularSpeed=std::max(maximumAngularSpeed,vsg::length(o.angularVelocity));}
        }
        std::vector<GpuMeasurementProduct> products;QElapsedTimer sensorTime;sensorTime.start();
        measurements.advanceTo(s->timeSeconds(),aperture,environment,[&](GpuMeasurementProduct&& product){products.push_back(std::move(product));});
        sensorTimes.push_back(sensorTime.nsecsElapsed()/1e6);completeTimes.push_back(physicsTimes.back()+renderMs+sensorTimes.back());
        if(frame){steadyPhysicsTimes.push_back(physicsTimes.back());steadyCompleteTimes.push_back(completeTimes.back());}
        for(const auto& product:products) {
            ++measuredFrames[product.sensor_id];for(float value:product.rgba)require(std::isfinite(value),"native target sensor produced a nonfinite result");
            if(product.sensor_id!=ir.id)continue;
            const double t=.5*(product.start_time+product.end_time);const auto a=aperture(ir.id,t);const auto truth=scene.sample(t);const auto hit=raycastVoxelScene(*truth,a.position,a.forward,3000);
            const size_t centre=(3*9+4)*4;require(bool(hit)==(product.rgba[centre+1]>1),"native target IR hit mask disagrees with historical geometry");
            if(hit){maximumGpuRangeError=std::max(maximumGpuRangeError,std::abs(product.rgba[centre+3]*1000-hit->distance));require(maximumGpuRangeError<.03,"native target IR range disagrees with historical displayed geometry");require(std::abs(product.rgba[centre+1]-truth->objects.at(hit->object).material.at("temperature_k").get<double>())<.15,"native target IR used stale fragment material");}
        }
        for(int z=-3;z<=3;++z)for(int x=-3;x<=3;++x) {
            // Avoid rays lying exactly on a voxel edge: Bullet uses float while DDA uses double.
            const vsg::dvec3 origin{3.*x+.137,ground->position.y+50.,3.*z+.193};
            const auto a=scene.raycast(origin,{0,-1,0},100),b=scene.rigidRaycast(origin,{0,-1,0},100);
            require(bool(a)==bool(b),"damaged target lost geometric collision agreement");
            if(a&&b)maximumRayError=std::max(maximumRayError,std::abs(a->distance-b->distance));
            if(a&&!(a->object==b->object&&std::abs(a->distance-b->distance)<.03)) {
                std::cerr<<"probe "<<x<<","<<z<<" at "<<s->timeSeconds()<<" DDA "<<a->object<<" "<<a->distance<<" Bullet "<<b->object<<" "<<b->distance<<std::endl;
                throw std::runtime_error("target ray and collider differ after destruction");
            }
        }
        const auto removed=s->journal.at(0).at("removed_voxels").get<size_t>();
        require(voxels+removed==aircraft.volume->occupiedCount(),"target destruction lost surviving voxel material");
        if(frame==0||frame==9||frame==12||frame==20||frame==35||frame==50||frame==69) {
            auto indexed=*s,referenceState=*s;contactIndex.detect(indexed,1.);contactReference.detect(referenceState,1.);
            auto signature=[](const std::vector<VoxelContact>& input){std::vector<std::array<double,10>> out;for(const auto& c:input)out.push_back({c.pointA.x,c.pointA.y,c.pointA.z,c.pointB.x,c.pointB.y,c.pointB.z,c.normalOnB.x,c.normalOnB.y,c.normalOnB.z,c.penetrationM});std::sort(out.begin(),out.end());return out;};
            const auto a=signature(indexed.contacts),b=signature(referenceState.contacts);
            if(a.size()!=b.size()){std::cerr<<"native raw contact oracle frame "<<frame<<": indexed="<<a.size()<<" reference="<<b.size()<<std::endl;throw std::runtime_error("native fine target index lost or invented an original Bullet contact");}
            for(size_t i=0;i<a.size();++i)for(size_t k=0;k<a[i].size();++k)require(std::abs(a[i][k]-b[i][k])<.00005,"native fine target index changed an original Bullet contact");
            ++referenceContactFrames;referenceContacts+=a.size();
        }
        const auto name=QString("debris-%1").arg(frame,3,10,QChar('0')).toStdString();
        capture(name);
        report["frames"].push_back({{"seconds",s->timeSeconds()},{"bodies",s->objects.size()},
            {"surviving_voxels",voxels},{"contacts",s->contacts.size()},
            {"advance_simulation_ms",frame?80:20},{"advance_wall_ms",physicsTimes.back()},
            {"render_wall_ms",renderMs},{"sensor_wall_ms",sensorTimes.back()},{"complete_wall_ms",completeTimes.back()}});
        if(frame==0)capture("damaged");
        if(frame==12)capture("fragments-airborne");
        if(frame==69)capture("fragments-ground");
    }
    require(scene.snapshot()->objects.size()>2,"aircraft damage did not produce disconnected fragments");
    require(contacts>0,"target fragments did not contact the DEM ground");
    require(measuredFrames[ir.id]>50&&measuredFrames[tv.id]>50,"native target IR/TV acquisition cadence stopped after fracture");
    report["contacts_observed"]=contacts;report["events"]=scene.snapshot()->journal;
    report["reference_contact_frames"]=referenceContactFrames;report["reference_raw_contacts"]=referenceContacts;
    report["utc_origin"]=initial->utcOrigin;
    report["maximum_collision_ray_error_m"]=maximumRayError;report["range_probes_per_frame"]=49;std::sort(physicsTimes.begin(),physicsTimes.end());report["advance_80ms_median_ms"]=physicsTimes[physicsTimes.size()/2];
    report["advance_80ms_p95_ms"]=physicsTimes[size_t(.95*(physicsTimes.size()-1))];report["maximum_gpu_ir_range_error_m"]=maximumGpuRangeError;
    report["ir_frames"]=measuredFrames[ir.id];report["tv_frames"]=measuredFrames[tv.id];report["maximum_penetration_m"]=maximumPenetration;report["maximum_com_speed_mps"]=maximumComSpeed;report["maximum_angular_speed_rad_s"]=maximumAngularSpeed;
    std::sort(frameTimes.begin(),frameTimes.end());report["frame_ms_median"]=frameTimes[frameTimes.size()/2];report["frame_ms_p95"]=frameTimes[size_t(.95*(frameTimes.size()-1))];
    auto timing=[&](const char* name,std::vector<double> samples){std::sort(samples.begin(),samples.end());report[std::string(name)+"_median_ms"]=samples[samples.size()/2];report[std::string(name)+"_p95_ms"]=samples[size_t(.95*(samples.size()-1))];report[std::string(name)+"_maximum_ms"]=samples.back();};
    timing("sensor_acquire",sensorTimes);timing("complete_simulation",completeTimes);
    timing("steady_advance_80ms",steadyPhysicsTimes);timing("steady_complete_80ms",steadyCompleteTimes);
    size_t withinBudget=0;double activeWall=0;std::vector<double> steadyRender,lateContact;
    for(size_t i=1;i<report["frames"].size();++i){const auto& row=report["frames"][i];const double total=row["complete_wall_ms"].get<double>();withinBudget+=total<=80.;activeWall+=total;steadyRender.push_back(row["render_wall_ms"].get<double>());if(row["seconds"].get<double>()>=4.&&row["contacts"].get<size_t>())lateContact.push_back(total);}
    timing("steady_render",steadyRender);if(!lateContact.empty())timing("late_contact_complete_80ms",lateContact);
    report["steady_complete_within_80ms_frames"]=withinBudget;report["steady_complete_frame_count"]=steadyCompleteTimes.size();
    report["steady_complete_active_wall_ms"]=activeWall;report["steady_complete_realtime_factor"]=(steadyCompleteTimes.size()*80.)/activeWall;
    report["destruction_first_20ms_wall_ms"]=report["frames"][0]["advance_wall_ms"];
    report["complete_timing_scope"]="scene advance and publication (including topology/sensor preparation) + synchronized display render + actual IR/TV acquisition; excludes screenshot encoding, pacing sleeps and acceptance-only geometry/ray checks";
    scene.saveCheckpoint((directory/"target-checkpoint.json").string());
    std::ofstream(directory/"target-acceptance.json")<<report.dump(2)<<'\n';
    std::cout<<"PASS: Earth model voxelization, blast fragments, DEM contacts and consistent target sensors"<<std::endl;
    window.hide();pump(20);
}
void runUnifiedEarth(const QString& sceneFile,const QString& contextMap,const QString& output,bool interactive) {
    const auto validated=VoxelScene::open(sceneFile.toStdString());const auto initial=validated->snapshot();const auto model=simcore::geo::makeEnuToWorldMatrix(initial->anchor);
    QMainWindow window;window.resize(920,580);window.setWindowTitle(QStringLiteral("地球场景集成验证 — 影像/高程/目标/体素云/测量"));
    auto* view=new Viewport3DView(&window);window.setCentralWidget(view);bool ready=false;QString failure;
    QObject::connect(view,&Viewport3DView::vsgReady,view,[&]{ready=true;});
    QObject::connect(view->context().widget(),&gui::VsgQtWidget::initializationFailed,view,[&](const QString& text){failure=text;});
    auto config=ViewportConfig::fromPreset("earth_display");config.remove(OceanFeature::kId);
    config.add(EarthView::kId,{{"map_json",std::filesystem::absolute(contextMap.toStdString()).string()},{"lat",initial->anchor.lat_deg},{"lon",initial->anchor.lon_deg},{"alt",initial->anchor.alt_m},
        {"scene_context",true},{"view_height",1200},{"cloud_grid",{{"width",32},{"height",48},{"depth",32},{"pressure_iterations",8},{"cell_size_m",24}}}});
    config.add(SkyFeature::kId,initial->utcOrigin);
    config.add(CloudFeature::kId,{{"cover",1.0},{"stratus_altitude",450},{"cumulus_altitude",650},{"stratus_size",4000},{"wind_speed",8},{"wind_az",70}});
    config.add(VoxelSceneFeature::kId,{{"checkpoint",validated->checkpoint()},{"paused",true},{"advance_from_frames",false},{"frame_scene",false}});
    view->applyConfig(config);
    auto* dock=new QDockWidget(QStringLiteral("统一场景状态"),&window);auto* status=new QLabel;status->setWordWrap(true);status->setMinimumWidth(230);dock->setWidget(status);window.addDockWidget(Qt::LeftDockWidgetArea,dock);
    auto* tools=window.addToolBar(QStringLiteral("地球仿真"));
    window.show();QElapsedTimer waiting;waiting.start();while(!ready&&failure.isEmpty()&&waiting.elapsed()<45000)pump(10);
    require(failure.isEmpty(),failure.toStdString().c_str());require(ready&&view->isReady(),"Earth integrated viewport initialization timed out");view->setAutoFrame(false);
    auto* feature=view->featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);auto* globe=view->featureAs<EarthViewFeature>(EarthView::kId);auto* sky=view->featureAs<SkyFeature>(SkyFeature::kId);
    require(feature&&feature->runtime()&&globe&&globe->showsSceneContext()&&sky,"Earth test did not retain globe, physical scene and sky");auto& scene=*feature->runtime();
    if(auto* native=view->context().widget()->view()) {
        native->viewDependentState->maxShadowDistance=2000.;native->viewDependentState->shadowMapBias=.0005;
    }
    require(globe->surfaceSnapshot()->orbital&&!globe->surfaceSnapshot()->imagery_rgba.empty(),"Earth context requires a native volume with global imagery roots");
    auto* camera=view->context().camera()->viewMatrix.cast<vsg::LookAt>().get();require(camera,"Earth camera missing");
    auto pose=[&](vsg::dvec3 eye,vsg::dvec3 focus){camera->eye=model*eye;camera->center=model*focus;const auto up=model*vsg::dvec4(0,1,0,0);camera->up={up.x,up.y,up.z};};
    auto render=[&]{view->context().setSimulationTime(scene.snapshot()->timeSeconds());view->renderFrame();view->context().viewer()->deviceWaitIdle();};
    std::filesystem::path directory=output.toStdString();if(!interactive)std::filesystem::create_directories(directory);
    auto capture=[&](const char* name){render();pump(160);auto image=view->grabFrame();require(!image.isNull(),"Earth integrated framebuffer missing");require(image.save(QString::fromStdString((directory/(std::string(name)+".png")).string())),"Earth integrated capture failed");return image.convertToFormat(QImage::Format_RGB32);};
    if(!interactive) {
        const auto orbit=simcore::geo::initialCamera(initial->anchor,16000000.);camera->eye=orbit.eye;camera->center=orbit.center;camera->up=orbit.up;capture("earth-orbit");
    }
    pose({180,130,240},{-20,90,0});view->context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=75;
    for(int i=0;i<3;++i)render();
    auto resources=globe->cloudResources();require(resources&&resources->field&&resources->field->ready(),"Earth renderer did not own a physical GPU cloud field");
    auto binding=globe->sceneCloud(model);require(binding->resources==resources,"Earth sensor bound a separate cloud instance");
    GpuSensorSpec ir;ir.id=91;ir.period_seconds=initial->fixedStep;ir.output=ProductStage::Scene;ir.profile.width=9;ir.profile.height=7;ir.profile.fov_deg=4;
    ir.profile.detector.integration_ms=1;ir.profile.band=BandSpec::make(BandType::MWIR);auto tv=ir;tv.id=92;tv.profile.band=BandSpec::make(BandType::TV);
    VoxelSensorSession clear(scene,{ir,tv},view->context().window()->getOrCreateDevice(),"earth-clear-reference");
    VoxelSensorSession measured(scene,{ir,tv},view->context().window()->getOrCreateDevice(),"earth-shared-cloud",binding);
    std::map<uint32_t,std::vector<GpuMeasurementProduct>> reference,products;
    auto aperture=[&](uint32_t,double seconds){const auto s=scene.sample(seconds);const auto& o=s->objects.at("ground-MQ9");const auto p=o.pose(s->tick,s->fixedStep)*occupiedCentre(o);GpuAperture a;a.position={p.x,1500,p.z};a.forward={0,-1,0};a.up={0,0,-1};return a;};
    auto environment=[&](uint32_t,double time,const BandSpec& band){EnvAnswer e;e.sigma_per_km=0;e.sky_radiance=0;e.sun_irradiance=band.type==BandType::TV?100:0;
        auto* sun=sky->sunProvider();sun->syncDateTime(initial->utcOrigin.at("year"),initial->utcOrigin.at("month"),initial->utcOrigin.at("day"),initial->utcOrigin.at("hours_utc").get<double>()+time/3600);
        rockyenv::EnvSunInfo light;if(sun->sunInfo(model,light)){e.sun_el_deg=light.elevationDeg;e.sun_az_deg=std::atan2(light.dirENU.x,-light.dirENU.z)*180/3.141592653589793;}
        return e;};
    auto refresh=[&]{const auto s=scene.snapshot();status->setText(QString::fromStdString("Earth + OSG targets + GPU clouds\n\nTick "+std::to_string(s->tick)+" / "+std::to_string(s->timeSeconds())+" s\nTerrain: "+s->objects.at("earth-terrain").terrainMode+"\nBodies: "+std::to_string(s->objects.size())+"\nContacts: "+std::to_string(s->contacts.size())+"\nCloud revision: "+std::to_string(resources->field->revision())+"\nAircraft wakes: "+std::to_string(globe->sceneCloudColliders())+"\nIR/TV frames: "+std::to_string(products[91].size())+" / "+std::to_string(products[92].size())+"\n\nSame geometry, shared cloud image.\nCloud held over each measurement interval."));};
    double minimumTransmission=1.;size_t contacts=0,maxWakes=0;uint64_t frames=0;
    auto step=[&] {
        const auto next=scene.snapshot()->timeSeconds()+initial->fixedStep;scene.setPaused(false);scene.advanceTo(next,1);scene.setPaused(true);
        // The cloud at the previous boundary is held through this acquisition
        // interval. Drain measurements before executing the next native cloud
        // step, avoiding the use of a future preview field for old exposures.
        clear.advanceTo(next,aperture,environment,[&](auto&& p){reference[p.sensor_id].push_back(std::move(p));});
        measured.advanceTo(next,aperture,environment,[&](auto&& p){products[p.sensor_id].push_back(std::move(p));});
        const size_t centre=(3*9+4)*4;const auto& p=products[91].back();const auto& ref=reference[91].back();const auto time=(p.start_time+p.end_time)*.5;const auto a=aperture(91,time);
        const auto hit=raycastVoxelScene(*scene.sample(time),a.position,a.forward,3000);require(hit&&std::abs(p.rgba[centre+3]*1000-hit->distance)<.02,"Earth combined measurement range disagrees with historical terrain/target geometry");
        require(std::abs(p.rgba[centre+3]-ref.rgba[centre+3])<1e-6&&std::abs(p.rgba[centre+1]-ref.rgba[centre+1])<.01,"shared clouds changed geometric range or target temperature");
        if(ref.rgba[centre]>1e-5)minimumTransmission=std::min(minimumTransmission,double(p.rgba[centre]/ref.rgba[centre]));
        globe->setCloudPresentationEnabled(interactive);render();globe->setCloudPresentationEnabled(true);
        contacts+=scene.snapshot()->contacts.size();maxWakes=std::max(maxWakes,globe->sceneCloudColliders());++frames;refresh();
    };
    auto action=[&](const QString& text,auto callback){auto* a=tools->addAction(text);QObject::connect(a,&QAction::triggered,&window,[&,callback]{try{callback();render();refresh();}catch(const std::exception& e){status->setText(QString::fromUtf8(e.what()));}});};
    action(QStringLiteral("单步"),[&]{step();});action(QStringLiteral("高清/体素"),[&]{const auto s=scene.snapshot();scene.edit(json::array({{{"kind","terrain_mode"},{"object","earth-terrain"},{"mode",s->objects.at("earth-terrain").terrainMode=="hd"?"voxel":"hd"}}}),s->revision);});
    action(QStringLiteral("地形毁伤"),[&]{scene.schedule({{"kind","damage"},{"object","earth-terrain"},{"tick",scene.snapshot()->tick+1},{"point_m",{-20,0,0}},{"radius_m",12}});});
    action(QStringLiteral("目标毁伤"),[&]{const auto s=scene.snapshot();const auto p=occupiedCentre(s->objects.at("ground-MQ9"));scene.schedule({{"kind","damage"},{"object","ground-MQ9"},{"tick",s->tick+1},{"space","object"},{"point_m",{p.x,p.y,p.z}},{"radius_m",1.044718724489212}});});
    action(QStringLiteral("云开关"),[&]{view->setFeatureEnabled(CloudFeature::kId,!view->featureAs<CloudFeature>(CloudFeature::kId)->enabled());});
    action(QStringLiteral("地球轨道"),[&]{auto p=simcore::geo::initialCamera(initial->anchor,16000000.);camera->eye=p.eye;camera->center=p.center;camera->up=p.up;});
    action(QStringLiteral("工作区"),[&]{pose({180,130,240},{-20,90,0});});
    QTimer timer;bool playing=false;QObject::connect(&timer,&QTimer::timeout,&window,[&]{try{step();}catch(const std::exception& e){timer.stop();playing=false;status->setText(QString::fromUtf8(e.what()));}});
    action(QStringLiteral("运行/暂停"),[&]{playing=!playing;if(playing)timer.start(50);else timer.stop();});refresh();
    if(interactive){QEventLoop loop;QObject::connect(&window,&QMainWindow::destroyed,&loop,&QEventLoop::quit);QObject::connect(qApp,&QApplication::lastWindowClosed,&loop,&QEventLoop::quit);loop.exec();return;}
    pump(50);render();const auto pausedImage=capture("earth-hd-target-cloud");
    if(std::getenv("VSGRAYSIM_EARTH_SCENE_DIAGNOSTIC")) {
        view->setFeatureEnabled(CloudFeature::kId,false);capture("earth-hd-target-clear");
        std::cout<<"DIAGNOSTIC: Earth visual captures only; full integrated acceptance was not executed\n";view->hide();pump(20);return;
    }
    const auto pausedRevision=resources->field->revision();render();render();require(resources->field->revision()==pausedRevision,"native cloud evolved while the unified Earth clock was paused");
    const auto repeated=capture("earth-paused-repeat");require(repeated.size()==pausedImage.size(),"Earth paused viewport changed dimensions");
    size_t changed=0,sampled=0;
    for(int y=10;y<repeated.height()/3;++y)for(int x=10;x<repeated.width()-10;++x) {
        const auto a=pausedImage.pixel(x,y),b=repeated.pixel(x,y);++sampled;
        if(std::abs(qRed(a)-qRed(b))+std::abs(qGreen(a)-qGreen(b))+std::abs(qBlue(a)-qBlue(b))>3)++changed;
    }
    require(changed<=sampled/200,"paused native Earth cloud image flickers without physical evolution");
    for(int tick=1;tick<=50;++tick){step();if(tick==12)capture("earth-voxel-target-cloud");if(tick==24)capture("earth-crater-target-cloud");if(tick==40)capture("earth-hd-damaged-target-cloud");if(tick%10==0)std::cout<<"Earth integrated tick="<<tick<<" cloud="<<resources->field->revision()<<" wake_bodies="<<maxWakes<<" IR transmittance="<<minimumTransmission<<std::endl;}
    require(contacts>0&&maxWakes>0,"Earth scene failed to couple target contact and visible aircraft to cloud forcing");
    require(scene.snapshot()->objects.size()>initial->objects.size(),"imported OSGB target did not fracture in Earth scene");
    require(scene.snapshot()->journal[1]["removed_voxels"].get<size_t>()>0,"Earth terrain damage was not executed");
    require(minimumTransmission<.99,"native Earth cloud had no effect on the shared sensor path");
    require(scene.snapshot()->objects.at("earth-terrain").terrainMode=="hd"&&globe->weather()&&globe->weather()->clouds,"Earth context or physical cloud disappeared after terrain mode changes");
    const auto flight=scene.snapshot()->objects.at("flying-AH64");const auto point=flight.pose(scene.snapshot()->tick,initial->fixedStep)*flight.localCentreOfMass();pose(point+vsg::dvec3(-70,35,110),point);capture("earth-aircraft-in-cloud");
    scene.saveCheckpoint((directory/"earth-physical-checkpoint.json").string());
    json report={{"program","sim_earth_gui_tests"},{"scene",std::filesystem::absolute(sceneFile.toStdString()).string()},{"earth_context",std::filesystem::absolute(contextMap.toStdString()).string()},
        {"vulkan_device",std::string(view->context().window()->getOrCreateDevice()->getPhysicalDevice()->getProperties().deviceName)},
        {"source_dem_spacing_m",scene.snapshot()->objects.at("earth-terrain").provenance.at("dem_spacing_m")},
        {"source_imagery_spacing_m",scene.snapshot()->objects.at("earth-terrain").provenance.at("imagery_spacing_m")},
        {"tick",scene.snapshot()->tick},{"physical_frames",frames},{"contacts_observed",contacts},{"cloud_revision",resources->field->revision()},{"cloud_wake_bodies",maxWakes},{"minimum_ir_transmission",minimumTransmission},
        {"ir_frames",products[91].size()},{"tv_frames",products[92].size()},{"geometry_revision",scene.snapshot()->contentRevision},{"shared_cloud_image",true},
        {"paused_cloud_changed_pixels",changed},{"paused_cloud_compared_pixels",sampled},
        {"cloud_colour_draws","captured probe frames; physical cloud compute executes at every fixed step"},{"cloud_time_model","held between fixed boundaries; measurements drain before next cloud step"},
        {"cloud_wake_model","resolved bounding wake envelope; rigid contact and measurement geometry remain voxel-exact"},{"gpu_cloud_field_checkpointed",false}};
    std::ofstream(directory/"earth-scene-report.json")<<report.dump(2)<<'\n';
    std::cout<<"PASS: Earth program integrated real imagery/DEM, globe context, OSGB targets, terrain HD/voxel damage, rigid contacts, fractured targets, shared GPU cloud/aircraft wakes and sensor range/extinction\n";
    view->hide();pump(20);
}

void runEarthTerrainRegionCapture(const QString& output,bool highResolution) {
    require(QApplication::platformName()=="xcb","terrain region capture needs a native X11 surface");
    const auto directory=std::filesystem::absolute(output.toStdString());std::filesystem::create_directories(directory);
    const auto file=std::filesystem::absolute(highResolution?"config/earth/taipei-80km-hires.vscene.json":"config/earth/taipei-80km.vscene.json");
    QElapsedTimer importTimer;importTimer.start();
    auto prepared=VoxelScene::open(file.string());const auto initial=prepared->snapshot();const auto& terrain=initial->objects.at("taipei-terrain");
    const double importMs=importTimer.nsecsElapsed()/1e6;
    const auto chart=simcore::geo::makeEnuToWorldMatrix(initial->anchor);const auto box=terrain.earth->bounds();
    require(box.max.x-box.min.x==80000&&box.max.z-box.min.z==80000,"terrain working set must be exactly 80 by 80 km");
    auto smooth=prepared->checkpoint(),blocks=smooth;blocks["earth_patches"]["earth-patch-1"]["voxel_surface"]="blocks";
    auto baseline=highResolution?VoxelScene::open(std::filesystem::absolute("config/earth/taipei-80km-coarse.vscene.json").string()):nullptr;
    if(baseline)blocks=baseline->checkpoint();
    QMainWindow window;window.setFixedSize(1024,640);auto* view=new Viewport3DView(&window);window.setCentralWidget(view);
    bool ready=false;QString failure;
    QObject::connect(view,&Viewport3DView::vsgReady,&window,[&]{ready=true;view->setAutoFrame(false);});
    QObject::connect(view->widget(),&gui::VsgQtWidget::initializationFailed,&window,[&](const QString& text){failure=text;});
    auto config=ViewportConfig::fromPreset("earth_display");config.remove(OceanFeature::kId);config.remove(CloudFeature::kId);
    config.add(EarthView::kId,{{"map_json",std::filesystem::absolute(highResolution?"config/earth/taipei-coarse.map.json":"config/earth/taipei-region.map.json").string()},
        {"lat",initial->anchor.lat_deg},{"lon",initial->anchor.lon_deg},{"alt",initial->anchor.alt_m},{"scene_context",true},{"view_height",60000}});
    config.add(SkyFeature::kId,initial->utcOrigin);
    config.add(VoxelSceneFeature::kId,{{"checkpoint",blocks},{"paused",true},{"advance_from_frames",false},{"frame_scene",false},{"terrain_atmosphere",false}});
    view->applyConfig(config);window.show();QElapsedTimer deadline;deadline.start();
    while(!ready&&failure.isEmpty()&&deadline.elapsed()<60000)pump(10);
    require(ready&&failure.isEmpty(),"terrain viewport failed to initialize");
    auto* feature=view->featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);require(feature&&feature->runtime(),"terrain runtime missing");
    auto camera=view->context().camera()->viewMatrix.cast<vsg::LookAt>();
    const auto up4=chart*vsg::dvec4(0,1,0,0);const vsg::dvec3 up(up4.x,up4.y,up4.z);
    auto pose=[&](vsg::dvec3 eye,vsg::dvec3 focus,double fov){camera->eye=chart*eye;camera->center=chart*focus;camera->up=up;view->context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=fov;};
    auto render=[&]{view->setAutoFrame(false);view->setIdleSuspend(false);view->markActivity();QThread::msleep(21);view->context().setSimulationTime(0);QElapsedTimer t;t.start();view->renderFrame();view->context().viewer()->deviceWaitIdle();return t.nsecsElapsed()/1e6;};
    auto capture=[&](const std::string& name){render();QImage image;QElapsedTimer timeout;timeout.start();while(image.isNull()&&timeout.elapsed()<10000){pump(10);QThread::msleep(50);image=view->grabFrame();}require(!image.isNull(),"terrain readback failed");require(image.save(QString::fromStdString((directory/(name+".png")).string())),"terrain image save failed");};
    vsg::dvec3 peak(0,-1e9,0),gentle(0,0,0);double flat=1e9;
    const auto& surface=baseline?baseline->snapshot()->objects.at("taipei-terrain").surface():terrain.surface();
    for(size_t i=0;i<surface.vertices().size();++i) {
        const auto& p=surface.vertices()[i];if(std::abs(p.x)>28000||std::abs(p.z)>28000)continue;
        if(p.y>peak.y)peak=p;
        if(p.y>20&&p.y<500&&!surface.surface_normals.empty()&&surface.surface_normals[i].y>.98) {
            const double score=1.-surface.surface_normals[i].y+vsg::length(vsg::dvec3(p.x,0,p.z))*1e-8;
            if(score<flat){flat=score;gentle=p;}
        }
    }
    require(peak.y>500&&flat<1e8,"real DEM must contain both mountain relief and gently sloping land");
    struct Camera {const char* name;vsg::dvec3 eye,focus;double fov;};
    std::array<Camera,4> cameras{{
        {"overview",{60000,90000,85000},{0,300,0},55},
        {"ridge",peak+vsg::dvec3(2400,1700,3500),peak-vsg::dvec3(1800,250,1300),45},
        {"gentle",gentle+vsg::dvec3(1800,900,2500),gentle-vsg::dvec3(1800,100,1200),50},
        {"boundary",{-37000,6500,22000},{-40000,200,12000},48}
    }};
    json report={{"region","Taipei 80 x 80 km"},{"area_km2",6400},{"anchor",{{"lat",initial->anchor.lat_deg},{"lon",initial->anchor.lon_deg},{"alt",initial->anchor.alt_m}}},
        {"source",terrain.earth->provenance()},{"grid_cell_size_m",terrain.volume->resolution_m},{"terrain_quality",surfaceQuality(terrain.surface()).json()},{"cameras",json::array()},
        {"comparison",highResolution?"existing coarse DEM/RGB versus public GLO-30 DSM/Sentinel-2; same camera and UTC":"blocks versus smooth DEM"},{"source_import_ms",importMs}};
    for(const auto& c:cameras)report["cameras"].push_back({{"name",c.name},{"eye",{c.eye.x,c.eye.y,c.eye.z}},{"focus",{c.focus.x,c.focus.y,c.focus.z}},{"fov_y",c.fov}});
    auto probe=[&]{double maximum=0;auto& scene=*feature->runtime();for(int z=-2;z<=2;++z)for(int x=-2;x<=2;++x){const vsg::dvec3 p{12000.*x+73,15000,12000.*z+137};const auto a=scene.raycast(p,{0,-1,0},30000),b=scene.rigidRaycast(p,{0,-1,0},30000);require(bool(a)==bool(b),"regional DEM collider hit mask mismatch");if(a){require(a->object==b->object,"regional collider hit a different object");maximum=std::max(maximum,std::abs(a->distance-b->distance));}}require(maximum<.03,"regional DEM collision/range error exceeds 3 cm");return maximum;};
    for(const std::string phase:{"before","after"}) {
        if(phase=="after") {
            if(highResolution) {
                auto* map=view->featureAs<EarthViewFeature>(EarthView::kId);
                const auto path=std::filesystem::absolute("config/earth/taipei-hires.map.json").string();
                map->configure({{"map_json",path}});map->bindSurface(EarthDataService::loadMap(path,initial->anchor),false);
            }
            view->configureFeature(VoxelSceneFeature::kId,{{"checkpoint",smooth},{"paused",true},{"frame_scene",false}});
        }
        report[phase]["maximum_collision_range_error_m"]=probe();
        double demError=0;const auto& heights=(phase=="before"?blocks:smooth).at("earth_patches").at("earth-patch-1").at("heights_m");
        const int pitch=int(std::lround(std::sqrt(double(heights.size()))));
        for(int j=0;j<8;++j)for(int i=0;i<8;++i){const int x=(pitch-1)*(16+i*32)/256,z=(pitch-1)*(16+j*32)/256;const double step=80000./(pitch-1);const auto hit=feature->runtime()->raycast({-40000+x*step,15000,-40000+z*step},{0,-1,0},30000);require(bool(hit),"DEM node disappeared from the physical surface");demError=std::max(demError,std::abs(hit->position.y-heights.at(z*pitch+x).get<double>()));}
        const auto& current=feature->runtime()->snapshot()->objects.at("taipei-terrain");
        report[phase]["terrain_quality"]=surfaceQuality(current.surface()).json();
        report[phase]["atlas_size"]={current.surface().imagery_width,current.surface().imagery_height};
        report[phase]["height_grid_size"]=pitch;
        report[phase]["maximum_dem_node_error_m"]=demError;
        if(phase=="after")require(demError<.005,"smooth voxel terrain flattened or displaced a source DEM sample");
        require(!feature->runtime()->raycast({50000,15000,0},{0,-1,0},30000),"display-only context leaked into terrain physics");

        for(const auto& c:cameras){pose(c.eye,c.focus,c.fov);capture(phase+"-"+c.name);}
        pose(cameras[1].eye,cameras[1].focus,cameras[1].fov);for(int i=0;i<4;++i)render();std::vector<double> times;for(int i=0;i<12;++i)times.push_back(render());std::sort(times.begin(),times.end());
        report[phase]["render_median_ms"]=times[times.size()/2];report[phase]["render_p95_ms"]=times[size_t(.95*(times.size()-1))];
    }
    report["presentation"]="DEM inspection: terrain atmospheric veiling disabled; matching full-atmosphere ridge snapshot also provided";
    pose(cameras[1].eye,cameras[1].focus,cameras[1].fov);view->configureFeature(VoxelSceneFeature::kId,{{"terrain_atmosphere",true}});capture("after-ridge-atmosphere");view->configureFeature(VoxelSceneFeature::kId,{{"terrain_atmosphere",false}});
    // Same camera flight from mountain flank into gentler terrain; all positions
    // come from the real DEM, and interpolation introduces no synthetic relief.
    for(int i=0;i<24;++i){const double t=i/23.,u=t*t*(3-2*t);const auto& a=cameras[1];const auto& b=cameras[2];pose(a.eye*(1-u)+b.eye*u,a.focus*(1-u)+b.focus*u,a.fov*(1-u)+b.fov*u);capture(QString("flight-%1").arg(i,3,10,QChar('0')).toStdString());}
    auto& scene=*feature->runtime();const auto original=scene.snapshot();const auto h=scene.raycast({peak.x,15000,peak.z},{0,-1,0},30000);require(bool(h),"regional destruction probe missing");
    scene.schedule({{"kind","damage"},{"object","taipei-terrain"},{"tick",1},{"space","object"},{"point_m",{peak.x,h->position.y,peak.z}},{"radius_m",1500}});
    QElapsedTimer damageTimer;damageTimer.start();scene.setPaused(false);scene.advanceTo(.02,1);scene.setPaused(true);const double damageMs=damageTimer.nsecsElapsed()/1e6;render();
    const auto crater=scene.raycast({peak.x,15000,peak.z},{0,-1,0},30000);require(crater&&crater->position.y<h->position.y-100,"regional damage did not remove the actual DEM surface");
    pose(peak+vsg::dvec3(3500,5500,4500),peak-vsg::dvec3(0,400,0),45);capture("crater-voxel");
    scene.schedule({{"kind","terrain_mode"},{"object","taipei-terrain"},{"tick",2},{"mode","hd"}});scene.setPaused(false);scene.advanceTo(.04,1);scene.setPaused(true);render();
    const auto hd=scene.raycast({peak.x,15000,peak.z},{0,-1,0},30000);require(hd&&std::abs(hd->position.y-crater->position.y)<.01,"HD mode resurrected or moved damaged terrain");capture("crater-hd");
    report["damage"]={{"events",scene.snapshot()->journal},{"original_height_m",h->position.y},{"crater_height_m",crater->position.y},{"hd_height_m",hd->position.y},{"maximum_collision_range_error_m",probe()},{"advance_and_prepare_ms",damageMs}};
    scene.saveCheckpoint((directory/"terrain-checkpoint.json").string());std::ofstream(directory/"terrain-acceptance.json")<<report.dump(2)<<'\n';
    std::cout<<"PASS: native 80 km DEM/imagery, smooth voxel terrain, same cameras, collider/range probes and persistent crater across display modes"<<std::endl;
    window.hide();pump(20);
}

void runEarthUrbanCapture(const QString& baseline, const QString& district, const QString& output,
                         const QString& inspection,bool compareClusters) {
    namespace fs=std::filesystem;
    require(QApplication::platformName()=="xcb","urban capture requires a native Vulkan window");
    const auto directory=fs::absolute(output.toStdString());
    fs::create_directories(directory);
    const auto basePath=fs::absolute(baseline.toStdString()).string();
    const auto urbanPath=fs::absolute(district.toStdString()).string();
    const auto map=fs::absolute("config/earth/taipei-urban-context.map.json").string();
    json plan;
    if(!inspection.isEmpty()) {
        std::ifstream file(inspection.toStdString());
        require(bool(file),"street inspection file is missing");
        file>>plan;
    }
    const bool street=!plan.is_null();
    auto xyz=[](const json& point) {
        const auto p=point.get<std::array<double,3>>();
        return vsg::dvec3(p[0],p[1],p[2]);
    };
    auto source=VoxelScene::open(urbanPath);
    const auto built=source->snapshot();
    const auto before=compareClusters?built:VoxelScene::open(basePath)->snapshot();
    const auto chart=simcore::geo::makeEnuToWorldMatrix(built->anchor);
    require(chart==simcore::geo::makeEnuToWorldMatrix(before->anchor)&&before->utcOrigin==built->utcOrigin,
            "urban comparison requires identical anchor and UTC");
    std::string building;
    vsg::dvec3 roof;
    double score=0.;
    for(const auto& [id,object]:built->objects)if(object.provenance.contains("district_provenance")) {
        const auto& record=object.provenance.at("district_provenance");
        const auto p=record.at("roof_probe_m").get<std::array<double,3>>();
        const double area=record.at("area_m2");
        const double candidate=record.at("height_m").get<double>()*std::min(1.,area/1700.);
        if(candidate>score){score=candidate;building=id;roof={p[0],p[1],p[2]};}
    }
    require(!building.empty(),"urban scene has no compiled footprints");
    const auto ground=built->objects.at("district-ground").earth->bounds();
    const double span=ground.max.x-ground.min.x;
    const vsg::dvec3 center((ground.min.x+ground.max.x)*.5,35.,(ground.min.z+ground.max.z)*.5);
    const auto upper=chart*vsg::dvec4(0,1,0,0);

    QMainWindow window;
    window.setFixedSize(1024,640);
    auto* view=new Viewport3DView(&window);
    window.setCentralWidget(view);
    bool ready=false;
    QString failure;
    QObject::connect(view,&Viewport3DView::vsgReady,&window,[&]{ready=true;view->setAutoFrame(false);});
    QObject::connect(view->widget(),&gui::VsgQtWidget::initializationFailed,&window,[&](const QString& text){failure=text;});
    auto config=ViewportConfig::fromPreset("earth_display");
    config.remove(OceanFeature::kId);config.remove(CloudFeature::kId);
    config.add(EarthView::kId,{{"map_json",map},{"lat",built->anchor.lat_deg},{"lon",built->anchor.lon_deg},
        {"alt",built->anchor.alt_m},{"scene_context",true},{"view_height",1500}});
    config.add(SkyFeature::kId,built->utcOrigin);
    config.add(VoxelSceneFeature::kId,{{"scene_json",basePath},{"paused",true},{"frame_scene",false},
                                     {"advance_from_frames",false},{"terrain_atmosphere",false}});
    if(compareClusters)config.add(VoxelSceneFeature::kId,{{"clustered_rendering",false}});
    view->applyConfig(config);window.show();
    QElapsedTimer deadline;deadline.start();
    while(!ready&&failure.isEmpty()&&deadline.elapsed()<60000)pump(10);
    require(ready&&failure.isEmpty(),"urban viewport failed to initialize");
    auto* feature=view->featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);
    auto camera=view->context().camera()->viewMatrix.cast<vsg::LookAt>();
    require(feature&&feature->runtime()&&camera,"urban native view is incomplete");
    auto pose=[&](vsg::dvec3 eye,vsg::dvec3 focus,double fov){
        camera->eye=chart*eye;camera->center=chart*focus;camera->up={upper.x,upper.y,upper.z};
        view->context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=fov;
    };
    auto render=[&] {
        view->setAutoFrame(false);view->setIdleSuspend(false);view->markActivity();
        view->context().setSimulationTime(feature->runtime()->snapshot()->timeSeconds());
        auto viewer=view->context().viewer();
        const auto first=view->widget()->property("renderFrameSerial").toULongLong();
        QElapsedTimer timeout;timeout.start();
        while(timeout.elapsed()<3000) {
            QThread::msleep(22);
            QElapsedTimer timer;timer.start();view->renderFrame();viewer->deviceWaitIdle();
            if(view->widget()->property("renderFrameSerial").toULongLong()>first)return timer.nsecsElapsed()/1e6;
        }
        throw std::runtime_error("urban test did not submit a Vulkan frame");
    };
    auto capture=[&](const std::string& name) {
        render();
        QImage image;QElapsedTimer timeout;timeout.start();
        while(image.isNull()&&timeout.elapsed()<10000){pump(10);QThread::msleep(35);image=view->grabFrame();}
        require(!image.isNull()&&image.save(QString::fromStdString((directory/(name+".png")).string())),"urban native readback failed");
        return image.convertToFormat(QImage::Format_RGB32);
    };
    auto stats=[](std::vector<double> samples) {
        std::sort(samples.begin(),samples.end());
        return json{{"samples",samples.size()},{"median",samples[samples.size()/2]},
                    {"p95",samples[size_t(std::ceil(samples.size()*.95))-1]}};
    };
    auto renderStatistics=[&] {
        std::vector<double> timings;
        for(int i=0;i<24;++i)timings.push_back(render());
        return stats(timings);
    };
    const auto nearEye=roof+vsg::dvec3(140,62,160),nearTarget=roof-vsg::dvec3(0,22,0);
    const auto highEye=center+vsg::dvec3(.55,.65,.85)*span;
    auto plannedPose=[&](const json& p){pose(xyz(p.at("eye_m")),xyz(p.at("target_m")),p.at("fov_y"));};
    json report={{"utc",built->utcOrigin},{"building_probe",building},{"objects",built->objects.size()},
                 {"span_m",span},{"capture", "1024x640 X11 Vulkan. Stable render includes device idle; excludes capture and forced inter-frame sleep"},
                 {"cameras",{{"overview_eye_m",{highEye.x,highEye.y,highEye.z}},{"overview_target_m",{center.x,center.y,center.z}},
                              {"detail_eye_m",{nearEye.x,nearEye.y,nearEye.z}},{"detail_target_m",{nearTarget.x,nearTarget.y,nearTarget.z}}}}};
    if(street)report["cameras"]=plan.at("cameras");
    if(compareClusters) {
        const auto& display=feature->display();
        require(display.contextDrawable.has_value(),"cluster comparison did not mount the surrounding Taipei terrain");
        const auto context=display.node->children.at(*display.contextDrawable).cast<vsg::StateGroup>();
        const auto draw=context->children.front().cast<vsg::VertexIndexDraw>();
        const auto vertices=draw->arrays.front()->data.cast<vsg::vec3Array>();
        require(vertices->size()<draw->indexCount,"the terrain context did not reuse surface vertices");
        report["terrain_context"]={{"vertices",vertices->size()},{"triangles",draw->indexCount/3},
                                   {"vertices_if_triangle_expanded",draw->indexCount}};
    }
    std::map<std::string,QImage> references;
    auto readClusters=[&] {
        const auto& clusters=feature->display().clusters;
        require(view->widget()->supportsClusteredDraws(),"cluster comparison Vulkan graphics queue lacks compute support");
        require(!feature->runtime()->snapshot()->ocean,"cluster comparison needs the land scene");
        require(bool(clusters),"cluster comparison did not activate GPU clustered rendering");
        GpuContext device;device.initialize(false,view->context().window()->getOrCreateDevice());
        uint64_t available=0,visible=0,triangles=0,lowerTriangles=0,selected=0,selectedLower=0;
        const auto batches=clusters->indirectDraws();
        for(const auto& draw:batches) {
            std::vector<VkDrawIndexedIndirectCommand> data(draw.count+1);
            device.readbackBatch({{nullptr,draw.buffer.get(),VK_IMAGE_LAYOUT_UNDEFINED,data.size()*sizeof(data.front()),data.data()}});
            available+=draw.count;triangles+=draw.sourceTriangles;lowerTriangles+=draw.lowerLodTriangles;
            uint64_t batchIndices=0;
            for(size_t i=1;i<data.size();++i) {
                const auto& command=data[i];
                require(command.indexCount%3==0&&command.instanceCount==1&&command.firstInstance==0&&
                        uint64_t(command.firstIndex)+command.indexCount<=3*(draw.sourceTriangles+draw.lowerLodTriangles),
                        "cluster GPU produced an invalid indirect draw");
                visible+=command.indexCount!=0;selected+=command.indexCount/3;
                selectedLower+=command.indexCount!=0&&command.firstIndex>=3*draw.sourceTriangles;
                batchIndices+=command.indexCount;
            }
            require(data.front().indexCount==batchIndices&&data.front().firstIndex==0&&data.front().instanceCount==1,
                    "cluster dispatch did not compact its selected indices into the combined draw");
        }
        require(triangles>0&&selected>0&&selected<triangles&&visible<available&&selectedLower>0&&lowerTriangles<triangles,
                "cluster compute did not cull visible-cut geometry or build a terrain LOD");
        return json{{"draw_batches",batches.size()},{"input_clusters",available},{"visible_clusters",visible},{"triangles_input",triangles},
                    {"triangles_selected",selected},{"lower_lod_visible_clusters",selectedLower},
                    {"triangles_with_all_lower_levels",lowerTriangles}};
    };
    for(const std::string phase:{"before","after"}) {
        if(phase=="after") {
            QElapsedTimer timer;timer.start();
            if(compareClusters)view->configureFeature(VoxelSceneFeature::kId,{{"clustered_rendering",true}});
            else view->configureFeature(VoxelSceneFeature::kId,{{"scene_json",urbanPath},{"paused",true},{"frame_scene",false}});
            require(feature->runtime()->snapshot()->objects.size()==built->objects.size(),"district package did not mount");
            report["district_load_and_viewport_prepare_ms"]=timer.nsecsElapsed()/1e6;
        }
        if(street) {
            for(const auto& camera:plan.at("cameras")) {
                plannedPose(camera);for(int i=0;i<4;++i)render();
                const auto name=camera.at("name").get<std::string>();
                const auto image=capture(phase+"-"+name);
                if(compareClusters&&phase=="before")references.emplace(name,image);
                if(compareClusters&&phase=="after") {
                    render();
                    auto result=readClusters();
                    const auto& reference=references.at(name);
                    require(reference.size()==image.size(),"cluster comparison changed output dimensions");
                    uint64_t difference=0;
                    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
                        const QRgb a=reference.pixel(x,y),b=image.pixel(x,y);
                        difference+=abs(qRed(a)-qRed(b))+abs(qGreen(a)-qGreen(b))+abs(qBlue(a)-qBlue(b));
                    }
                    result["mean_rgb_error_8bit"]=double(difference)/(3.*image.width()*image.height());
                    require(result.at("mean_rgb_error_8bit").get<double>()<6.,"clustered display diverged visually from the canonical surface");
                    report["clustered_views"][name]=std::move(result);
                }
                if(compareClusters)report["stable_render_by_view_ms"][name][phase]=renderStatistics();
            }
            plannedPose(plan.at("cameras").front());
        } else {
            pose(highEye,center,42.);for(int i=0;i<4;++i)render();capture(phase+"-overview");
            pose(nearEye,nearTarget,46.);for(int i=0;i<4;++i)render();capture(phase+"-detail");
        }
        if(compareClusters)report[phase+"_stable_render_ms"]=report.at("stable_render_by_view_ms").at("street").at(phase);
        else report[phase+"_stable_render_ms"]=renderStatistics();
    }
    auto& scene=*feature->runtime();
    const auto initialProbe=street?xyz(plan.at("road_point_m")):roof+vsg::dvec3(.03,0,.03);
    const auto origin=initialProbe+vsg::dvec3(0,30,0);
    const vsg::dvec3 down(0,-1,0);
    auto probe=[&](const vsg::dvec3& origin) {
        const auto hit=scene.raycast(origin,down,600.),rigid=scene.rigidRaycast(origin,down,600.);
        require(hit&&rigid&&hit->object==rigid->object&&std::abs(hit->distance-rigid->distance)<.03,
                "urban authoritative surface and collision disagree");
        return *hit;
    };
    const auto surfaceBefore=probe(origin);
    const auto damageObject=street?plan.at("terrain_id").get<std::string>():building;
    require(surfaceBefore.object==damageObject&&std::abs(surfaceBefore.position.y-initialProbe.y)<.08,
            "urban geometry lost its authored surface height");
    size_t n=0;
    if(street) {
        auto groundHeight=[&](const json& value) {
            const auto point=xyz(value);
            const auto hit=probe(point+vsg::dvec3(0,20,0));++n;
            require(hit.object==damageObject&&std::abs(hit.position.y-point.y)<.09,
                    "road cross-section does not match the published voxel surface");
            return hit.position.y;
        };
        const double edge=groundHeight(plan.at("road_edge_m")),walk=groundHeight(plan.at("sidewalk_m"));
        const double start=groundHeight(plan.at("road_section_endpoints_m")[0]),end=groundHeight(plan.at("road_section_endpoints_m")[1]);
        require(walk-edge>.06&&walk-edge<.35,"the designed curb height is missing in collision geometry");
        report["road_section"]={{"edge_m",edge},{"walkway_m",walk},{"edge_to_walkway_rise_m",walk-edge},
                                {"route_start_m",start},{"route_end_m",end},
                                {"section_grade_ratio",plan.at("longitudinal_grade_ratio")}};
    }
    for(int z=-3;z<=3;++z)for(int x=-3;x<=3;++x) {
        probe(center+vsg::dvec3(x*span/8.+.31,240.,z*span/8.+.17));++n;
    }
    auto sensorData=makeVoxelSceneSensorData(scene.snapshot());
    for(const auto& instance:sensorData.instances) {
        const auto& drawing=feature->display().drawings.at(instance.id);
        const auto node=drawing.node.cast<vsg::StateGroup>();
        auto draw=node->children.front().cast<vsg::VertexIndexDraw>();
        const auto a=draw->arrays[0]->data.cast<vsg::vec3Array>(),b=instance.geometry->arrays[0]->data.cast<vsg::vec3Array>();
        const auto ai=draw->indices->data.cast<vsg::uintArray>(),bi=instance.geometry->indices->data.cast<vsg::uintArray>();
        require(a&&b&&a->size()==b->size()&&std::equal(a->begin(),a->end(),b->begin())&&ai&&bi&&ai->size()==bi->size()
                &&std::equal(ai->begin(),ai->end(),bi->begin()),"urban renderer and sensor geometry differ");
    }
    report["sensor_instances"]=sensorData.instances.size();
    report["sensor_texture_layers"]=sensorData.textures.layers.size();
    if(!sensorData.textures.layers.empty()) {
        const auto layer=sensorData.textures.layers.front().cast<vsg::ubvec4Array2D>();
        report["sensor_texture_bytes"]=size_t(layer->width())*layer->height()*4*sensorData.textures.layers.size();
    }
    report["collision_probes"]=n+1;
    sensorData={};
    QElapsedTimer timer;
    auto advance=[&]() {
        const auto state=scene.snapshot();
        timer.start();scene.setPaused(false);const auto step=scene.advanceTo((state->tick+1)*state->fixedStep,1);scene.setPaused(true);
        require(step.steps==1,"district simulation missed a fixed step");return timer.nsecsElapsed()/1e6;
    };
    std::vector<double> steps;
    for(int i=0;i<24;++i)steps.push_back(advance());
    report["stable_simulation_step_without_equipment_ms"]=stats(steps);
    GpuSensorSpec infrared;
    infrared.id=81;infrared.period_seconds=.02;infrared.output=ProductStage::Scene;
    infrared.profile.width=9;infrared.profile.height=7;infrared.profile.fov_deg=2.;
    infrared.profile.detector.integration_ms=1.;infrared.profile.band=BandSpec::make(BandType::MWIR);
    auto visible=infrared;visible.id=82;visible.profile.band=BandSpec::make(BandType::TV);
    timer.start();
    VoxelSensorSession measurements(scene,{infrared,visible},view->context().window()->getOrCreateDevice(),"urban-sensors");
    report["sensor_initial_prepare_ms"]=timer.nsecsElapsed()/1e6;
    auto aperture=[&](uint32_t,double){GpuAperture a;a.position=origin;a.forward=down;a.up={0,0,-1};return a;};
    auto environment=[](uint32_t,double,const BandSpec&){EnvAnswer e;e.sigma_per_km=0;e.sky_radiance=0;e.sun_irradiance=100;e.sun_el_deg=70;e.sun_az_deg=0;return e;};
    size_t irFrames=0,tvFrames=0;
    double maxGpuError=0.;
    auto measure=[&] {
        timer.start();
        measurements.advanceTo(scene.snapshot()->timeSeconds(),aperture,environment,[&](GpuMeasurementProduct&& product) {
            const size_t pixel=(3*9+4)*4;
            require(product.rgba.size()>pixel+3,"urban GPU product has no centre pixel");
            if(product.sensor_id==infrared.id) {
                const auto truth=scene.sample(.5*(product.start_time+product.end_time));
                const auto hit=raycastVoxelScene(*truth,origin,down,600.);
                require(hit&&product.rgba[pixel+1]>1,"urban GPU range lost the authored surface");
                maxGpuError=std::max(maxGpuError,std::abs(product.rgba[pixel+3]*1000-hit->distance));
                ++irFrames;
            } else {
                const double response=product.rgba[pixel]+product.rgba[pixel+1]+product.rgba[pixel+2];
                require(std::isfinite(response)&&response>0.,"urban GPU optical return is empty");
                ++tvFrames;
            }
        });
        return timer.nsecsElapsed()/1e6;
    };
    std::vector<double> complete;
    for(int i=0;i<12;++i) {const double simulationMs=advance();complete.push_back(simulationMs+measure());}
    report["stable_simulation_with_two_gpu_products_ms"]=stats(complete);
    const auto undamaged=scene.snapshot();
    if(street) {
        plannedPose(plan.at("crater_camera"));capture("before-damage");
    }
    scene.schedule({{"kind","damage"},{"object",damageObject},{"tick",undamaged->tick+1},{"space","scene"},
                    {"point_m",{surfaceBefore.position.x,surfaceBefore.position.y,surfaceBefore.position.z}},
                    {"radius_m",street?4.5:8.5}});
    const auto damageMs=advance();
    const auto surfaceAfter=probe(origin);
    require(surfaceAfter.position.y<surfaceBefore.position.y-2.,"urban voxel removal did not change the physical surface");
    report["damage"]={{"advance_publish_ms",damageMs},{"render_after_update_ms",render()},
                      {street?"road_before_m":"roof_before_m",surfaceBefore.position.y},
                      {street?"road_after_m":"roof_after_m",surfaceAfter.position.y}};
    measure();
    const auto previousFrames=irFrames;
    for(int i=0;i<3;++i) {advance();measure();}
    require(previousFrames>0&&irFrames>previousFrames&&tvFrames>0&&maxGpuError<.03,
            "urban GPU measurements failed after geometry publication");
    report["gpu_sensor"]={{"visible_frames",tvFrames},{"infrared_frames",irFrames},
                          {"maximum_range_error_m",maxGpuError}};
    capture("after-damage");
    const auto checkpoint=scene.checkpoint();
    auto reopened=VoxelScene::load(checkpoint);
    require(reopened->checkpoint()==checkpoint,"urban checkpoint failed damage round-trip");
    std::ofstream log(directory/(compareClusters?"cluster-acceptance.json":street?"street-acceptance.json":"urban-acceptance.json"));log<<report.dump(2)<<'\n';
    require(bool(log),"could not save urban acceptance report");
    std::cout<<"PASS: native Taipei district capture, fixed-step damage, collision and canonical sensor geometry\n";
    window.hide();pump(20);
}
