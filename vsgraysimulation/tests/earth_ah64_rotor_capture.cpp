#include <vsgraysim/earth/VoxelSceneSensors.h>
#include <vsgraysim/broker/VoxelEquipmentSource.h>

#include "earth_aviation_capture.h"
#include "viewport3d/EarthViewFeature.h"
#include "viewport3d/EnvFeatures.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/ViewportConfig.h"
#include "viewport3d/VoxelSceneFeature.h"
#include "viewport3d/VsgQtWidget.h"
#include "../src/earth/VoxelTerrain.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <vsg/all.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
using namespace vsgraysim::earth;
using namespace vsgraysim::viewport3d;
using D=vsg::dvec3;
using json=nlohmann::json;
namespace fs=std::filesystem;
constexpr std::array<const char*,2> rotors{"main-rotor","tail-rotor"};
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void pump(int milliseconds) {
    QElapsedTimer clock;clock.start();
    while(clock.elapsed()<milliseconds) {QApplication::processEvents(QEventLoop::AllEvents,5);QThread::msleep(1);}
}
bool near(const vsg::dmat4& a,const vsg::dmat4& b) {
    for(int i=0;i<4;++i)for(int k=0;k<4;++k)if(std::abs(a[i][k]-b[i][k])>1e-8)return false;
    return true;
}
void verifyRotors(const VoxelSceneSnapshot& snapshot) {
    const auto& root=snapshot.objects.at("body");
    const auto carrier=root.pose(snapshot.tick,snapshot.fixedStep);
    for(auto id:rotors) {
        const auto& part=snapshot.objects.at(id);const auto& drive=part.rotor.value();
        const auto expected=carrier*vsg::translate(drive.pivot)*vsg::rotate(
            vsg::radians(drive.phaseDeg)+snapshot.timeSeconds()*drive.rpm*(vsg::PI/30.),drive.axis);
        require(near(expected,part.pose(snapshot.tick,snapshot.fixedStep)),"rotor phase or hub does not follow the carrier");
    }
}
void verifyPublishedRotorTelemetry(VoxelSceneSnapshotPtr snapshot) {
    VoxelEquipmentConfig config;
    config.platforms.push_back({100,"body","AH-64","helicopter",0.,{}});
    const auto rows=voxelEquipmentPlatformTelemetry(config,snapshot);
    require(rows.size()==1,"helicopter missing from platform telemetry");
    const auto& published=rows.front().at("rotors");
    const auto carrier=snapshot->objects.at("body").pose(snapshot->tick,snapshot->fixedStep);
    for(const auto* id:rotors) {
        const auto& data=published.at(id);
        const auto& rotor=snapshot->objects.at(id);
        require(data["tick"]==snapshot->tick&&data["rpm"]==rotor.rotor->rpm,"rotor telemetry timestamp or speed mismatch");
        const auto pose=carrier*vsg::translate(rotor.rotor->pivot)*
            vsg::rotate(vsg::radians(data["angle_deg"].get<double>()),rotor.rotor->axis);
        require(near(pose,rotor.pose(snapshot->tick,snapshot->fixedStep)),"telemetry angle differs from rendered rotor");
    }
}
void checkCollisionRays(VoxelScene& scene) {
    const auto t=scene.snapshot();
    verifyPublishedRotorTelemetry(t);
    for(auto id:rotors) {
        const auto& part=t->objects.at(id);
        const auto placement=part.pose(t->tick,t->fixedStep);
        const auto axis4=placement*vsg::dvec4(part.rotor->axis,0.);
        const D axis(axis4.x,axis4.y,axis4.z);
        const auto counts=part.volume->dimensions();
        const double cell=part.volume->resolution_m;
        unsigned hits=0;
        for(size_t i=0;i<part.volume->cellCount()&&hits<4;++i) {
            if(!part.volume->occupancy()[i])continue;
            const D point=part.volume->gridOrigin()+(D(i%counts[0],i/counts[0]%counts[1],i/(size_t(counts[0])*counts[1]))+D(.5,.5,.5))*cell;
            if(vsg::length(point)<.65)continue; // hit the blade, away from neighbouring hub surfaces
            const D origin=placement*point+axis*.35;
            const auto hit=scene.raycast(origin,-axis,1.);
            const auto bullet=scene.rigidRaycast(origin,-axis,1.);
            if(hit&&bullet&&hit->object==id&&bullet->object==id&&std::abs(hit->distance-bullet->distance)<.002)++hits;
        }
        require(hits==4,"rotor occupancy and Bullet raycast disagreed");
    }
}
void checkRotatingContact() {
    auto box=[](const char* id,const char* motion,std::array<double,3> p,std::array<int,3> cells,std::array<double,3> origin) {
        return json{{"id",id},{"position_m",p},{"physics",{{"motion",motion}}},
                    {"source",{{"type","box"},{"cell_size_m",.1},{"dimensions",cells},{"origin_m",origin}}}};
    };
    auto blade=box("spinning","prescribed",{0,0,0},{26,2,2},{.4,-.1,-.1});
    blade["rotor_drive"]={{"carrier","mount"},{"pivot_m",{0,2,0}},{"axis",{0,1,0}},{"rpm",60}};
    auto scene=VoxelScene::load({{"format","voxel-scene-v1"},{"fixed_step_seconds",.005},{"gravity_mps2",{0,0,0}},
        {"objects",json::array({box("mount","static",{0,0,0},{2,2,2},{-.1,0,-.1}),blade,
            box("probe","dynamic",{2.,2.,-1.},{2,2,2},{-.1,-.1,-.1})})}});
    scene->setPaused(false);
    bool touched=false;
    for(int step=1;step<=24&&!touched;++step) {
        scene->advanceTo(step*.005);
        for(const auto& contact:scene->snapshot()->contacts)
            if((contact.a=="spinning"&&contact.b=="probe")||(contact.a=="probe"&&contact.b=="spinning"))touched=true;
    }
    require(touched&&vsg::length(scene->snapshot()->objects.at("probe").velocity)>.001,
            "a moving prescribed rotor did not produce a Bullet contact response");
}

// Capture a finite shutter from authoritative sub-time snapshots. The raw
// geometry, ray queries and live viewport continue to use the instantaneous pose.
class Exposure {
public:
    Exposure() {
        for(size_t i=0;i<linear.size();++i) {
            const double x=double(i)/255.;
            linear[i]=uint16_t(std::lround(4095.*(x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4))));
        }
        for(size_t i=0;i<display.size();++i) {
            const double x=double(i)/4095.;
            display[i]=uint8_t(std::lround(255.*(x<=.0031308?12.92*x:1.055*std::pow(x,1./2.4)-.055)));
        }
    }
    void add(const QImage& image) {
        const auto rgb=image.convertToFormat(QImage::Format_RGB888);
        if(count==0){width=rgb.width();height=rgb.height();values.resize(size_t(width)*height*3);}
        require(rgb.width()==width&&rgb.height()==height,"viewport resized during shutter exposure");
        for(int y=0;y<height;++y)for(int x=0;x<width*3;++x)
            values[size_t(y)*width*3+x]+=linear[rgb.constScanLine(y)[x]];
        ++count;
    }
    QImage finish() const {
        QImage result(width,height,QImage::Format_RGB888);
        for(int y=0;y<height;++y)for(int x=0;x<width*3;++x)
            result.scanLine(y)[x]=display[(values[size_t(y)*width*3+x]+count/2)/count];
        return result;
    }
private:
    int width=0,height=0,count=0;
    std::vector<uint16_t> values;
    std::array<uint16_t,256> linear{};
    std::array<uint8_t,4096> display{};
};
}

void runEarthAh64RotorCapture(const QString& scenePath,const QString& output) {
    require(QApplication::platformName()=="xcb","AH-64 capture needs native Vulkan");
    require(QDir().mkpath(output+"/slow")&&QDir().mkpath(output+"/fast"),"could not create AH-64 output");
    checkRotatingContact();
    const auto assets=fs::absolute(scenePath.toStdString());
    auto slow=VoxelScene::open((assets/"ah64-slow.vscene.json").string());
    auto fast=VoxelScene::open((assets/"ah64-fast.vscene.json").string());
    const double height=slow->snapshot()->objects.at("body").basePose[3][1];
    const auto map=fs::absolute("config/earth/taipei-region.map.json");
    const auto chart=simcore::geo::makeEnuToWorldMatrix(slow->snapshot()->anchor);
    Viewport3DView view;
    view.resize(1120,630);
    bool ready=false;
    QString error;
    QObject::connect(&view,&Viewport3DView::vsgReady,&view,[&]{ready=true;view.setAutoFrame(false);});
    QObject::connect(view.widget(),&vsgraysim::gui::VsgQtWidget::initializationFailed,&view,[&](const QString& reason){error=reason;});
    auto cfg=ViewportConfig::fromPreset("earth_display");
    cfg.remove(OceanFeature::kId);
    cfg.add(EarthView::kId,{{"map_json",map.string()},{"lat",25.078},{"lon",121.772},{"alt",0},{"cloud_objects",true}});
    cfg.add(VoxelSceneFeature::kId,{{"external_only",true}});
    view.applyConfig(cfg);
    view.show();
    QElapsedTimer startup;startup.start();
    while(!ready&&error.isEmpty()&&startup.elapsed()<60000)pump(10);
    require(ready&&error.isEmpty(),"AH-64 viewport not ready");
    auto* voxel=view.featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);
    auto* earth=view.featureAs<EarthViewFeature>(EarthView::kId);
    require(voxel&&earth&&earth->surfaceSnapshot()&&earth->surfaceSnapshot()->valid_samples,"missing Taipei or voxel display");
    earth->setCloudPresentationEnabled(false);
    const auto camera=[&](D eye,D centre,double fov) {
        auto* camera=view.camera();auto look=camera->viewMatrix.cast<vsg::LookAt>();
        look->eye=chart*eye;look->center=chart*centre;
        const auto up=chart*vsg::dvec4(0,1,0,0);
        look->up={up.x,up.y,up.z};camera->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=fov;
    };
    const auto capture=[&](VoxelSceneSnapshotPtr state) {
        voxel->bindSnapshot(state);view.context().setSimulationTime(state->timeSeconds());
        const auto serial=view.widget()->property("renderFrameSerial").toULongLong();
        QElapsedTimer limit;limit.start();
        do {view.renderFrame();if(view.widget()->property("renderFrameSerial").toULongLong()>serial)break;pump(17);}
        while(limit.elapsed()<1500);
        require(view.widget()->property("renderFrameSerial").toULongLong()>serial,"AH-64 frame did not present");
        for(auto id:rotors)require(near(voxel->display().placements.at(id)->matrix,state->objects.at(id).pose(state->tick,state->fixedStep)),"display used a different rotor pose");
        const auto img=view.grabFrame();require(!img.isNull(),"AH-64 readback failed");return img;
    };
    const auto save=[&](const QImage& image,const QString& filename){require(image.save(output+"/"+filename),"failed to save AH-64 frame");};
    const auto expose=[&](VoxelScene& scene,double centre,double duration,int samples) {
        Exposure shutter;
        for(int sub=0;sub<samples;++sub) {
            const double offset=duration*((sub+.5)/samples-.5);
            shutter.add(capture(scene.sample(centre+offset)));
        }
        return shutter.finish();
    };
    json report={{"earth_map",map.string()},{"rotating_contact",true},{"parts",json::object()}};
    for(const auto& [id,part]:slow->snapshot()->objects)report["parts"][id]={{"voxel_cm",100*part.volume->resolution_m},{"occupied",part.volume->occupiedCount()}};
    constexpr int fps=60,frames=72,exposureSamples=6,comparisonFrame=25;
    constexpr double shortShutter=1./240.,tailShutter=1./500.,referenceShutter=1./40.;
    constexpr double comparisonTime=.4125;
    const auto overallCamera=[&]{camera({24,height+8,11},{.8,height+2,-7},39.);};
    const auto tailCamera=[&]{camera({13,height+5,-16},{1.4,height+3,-14},36.);};
    for(const auto high:{false,true}) {
        auto& scene=high?*fast:*slow;
        const auto name=high?QString("fast"):QString("slow");
        scene.setPaused(false);
        overallCamera();
        std::array<const VoxelTerrain*,2> geometry{scene.snapshot()->objects.at(rotors[0]).volume.get(),scene.snapshot()->objects.at(rotors[1]).volume.get()};
        for(int frame=0;frame<frames;++frame) {
            const double centre=(frame+.5)/fps;
            scene.advanceTo(centre+scene.snapshot()->fixedStep+(frame==comparisonFrame?referenceShutter*.5:shortShutter*.5),20);
            const auto middle=scene.sample(centre);verifyRotors(*middle);
            for(size_t i=0;i<rotors.size();++i)require(middle->objects.at(rotors[i]).volume.get()==geometry[i],"rotor changed its voxel geometry while spinning");
            const auto image=high?expose(scene,centre,shortShutter,exposureSamples):capture(middle);
            save(image,name+QString("/frame-%1.png").arg(frame,4,10,QLatin1Char('0')));
            if(frame==comparisonFrame) {
                const auto sameTime=scene.sample(comparisonTime);
                if(high) {
                    save(capture(sameTime),"02-high-rpm-sharp.png");
                    save(expose(scene,comparisonTime,shortShutter,exposureSamples),"03-high-rpm-exposure.png");
                    save(expose(scene,comparisonTime,referenceShutter,16),"05-high-rpm-long-shutter.png");
                    tailCamera();
                    save(expose(scene,comparisonTime,tailShutter,exposureSamples),"06-tail-rotor-short-shutter.png");
                    save(expose(scene,comparisonTime,referenceShutter,16),"07-tail-rotor-long-shutter.png");
                    overallCamera();
                } else {
                    save(capture(sameTime),"01-slow-rpm.png");
                    tailCamera();
                    save(capture(sameTime),"04-tail-rotor-close.png");
                    overallCamera();
                }
            }
        }
        checkCollisionRays(scene);
        const auto state=scene.snapshot();
        const auto sensor=makeVoxelSceneSensorData(state);
        for(auto id:rotors) {
            const auto instance=std::find_if(sensor.instances.begin(),sensor.instances.end(),[&](const auto& value){return value.id==id;});
            require(instance!=sensor.instances.end()&&near(instance->transform,state->objects.at(id).pose(state->tick,state->fixedStep)),"sensor did not receive the published rotor pose");
        }
        scene.setPaused(true);
        const auto pending=scene.advanceTo(state->timeSeconds()+.10);
        require(pending.steps==0&&scene.snapshot()==state,"paused rotor changed phase");
        if(high) {
            const auto path=QDir(output).absoluteFilePath("ah64-checkpoint.json").toStdString();
            scene.saveCheckpoint(path);
            auto restored=VoxelScene::open(path);
            verifyRotors(*restored->snapshot());
            vsgraysim::VoxelSceneReadView reader;
            const auto remote=reader.update(vsgraysim::voxelSceneDescriptor(state,path));
            verifyRotors(*remote);
            require(remote->objects.at("main-rotor").pose(remote->tick,remote->fixedStep)==state->objects.at("main-rotor").pose(state->tick,state->fixedStep),"remote rotor lost its pose");
        }
        report[name.toStdString()]={{"main_rpm",state->objects.at(rotors[0]).rotor->rpm},{"tail_rpm",state->objects.at(rotors[1]).rotor->rpm}};
    }
    report["collision_rays_display_sensor_checkpoint_and_remote_pose_verified"]=true;
    report["fps"]=fps;report["frames_per_clip"]=frames;report["fast_capture_shutter_seconds"]=shortShutter;
    report["fast_tail_detail_shutter_seconds"]=tailShutter;report["reference_shutter_seconds"]=referenceShutter;
    report["comparison_simulation_seconds"]=comparisonTime;report["fast_capture_exposure_samples"]=exposureSamples;
    std::ofstream(QDir(output).absoluteFilePath("ah64-acceptance.json").toStdString())<<report.dump(2)<<'\n';
    view.hide();
}
