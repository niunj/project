#include "earth_aviation_capture.h"
#include "viewport3d/EarthViewFeature.h"
#include "viewport3d/EngagementVisualFeature.h"
#include "viewport3d/EnvFeatures.h"
#include "viewport3d/PlatformMotionFeature.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/ViewportConfig.h"
#include "viewport3d/VsgQtWidget.h"
#include "viewport3d/VoxelSceneFeature.h"
#include "display/EarthPanel.h"
#include <vsgraysim/broker/BrokerNet.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <QProcess>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <vsg/all.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace vsgraysim::earth;
using namespace vsgraysim::viewport3d;
using D=vsg::dvec3;
using json=nlohmann::json;
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
void pump(int milliseconds) {
    QElapsedTimer time;time.start();
    while(time.elapsed()<milliseconds) {QApplication::processEvents(QEventLoop::AllEvents,5);QThread::msleep(1);}
}
double ease(double x) {x=std::clamp(x,0.,1.);return x*x*(3.-2.*x);}
D locationOnLand(const EarthSurfaceSnapshot& surface) {
    D result;
    double best=-1.;
    for(int z=1100;z<=7500;z+=180)for(int x=-4000;x<=3000;x+=180) {
        const auto s=surface.sampleLocal(x,z);
        if(s.status==SurfaceStatus::NoData||s.height<7.||s.height>170.)continue;
        const double rank=s.normal.y-.0000005*std::abs(z-3000);
        if(rank>best){best=rank;result={double(x),s.height,double(z)};}
    }
    require(best>0.,"could not find terrain for the tracked vehicle");
    return result;
}
}

void runEarthPlatformMotionCapture(const QString& tankAssetPath,const QString& output,const QString& shipAssetPath,bool displayVehicle) {
    require(QApplication::platformName()=="xcb","platform clips require native Vulkan");
    require(QDir().mkpath(output),"could not create platform capture directory");
    const auto originalMap=std::filesystem::absolute("config/earth/taipei-region.map.json");
    json localMap;
    std::ifstream(originalMap)>>localMap;
    // Offshore camera anchor uses a smaller strict footprint inside the
    // same Taipei raster, with both the land and water shots in bounds.
    localMap["earth"]["coverage"]={{"min_x",-37000},{"max_x",37000},{"min_z",-37000},{"max_z",37000}};
    localMap["earth"]["grid_size"]=513;
    const auto map=QDir(output).absoluteFilePath("coast-preview.map.json").toStdString();
    std::ofstream(map)<<localMap.dump(2)<<'\n';
    const bool tankOnly=shipAssetPath.isEmpty();
    const auto assets=displayVehicle?std::filesystem::path{}:std::filesystem::absolute(tankAssetPath.toStdString());
    require(displayVehicle||std::filesystem::is_regular_file(assets/"m1a2-rig.json"),"prepare the M1A2 rig with tools/prepare_m1a2_motion.py");
    Viewport3DView view;view.resize(1120,630);
    bool ready=false;QString error;
    QObject::connect(&view,&Viewport3DView::vsgReady,&view,[&]{ready=true;view.setAutoFrame(false);});
    QObject::connect(view.widget(),&vsgraysim::gui::VsgQtWidget::initializationFailed,&view,[&](const QString& e){error=e;});
    auto config=ViewportConfig::fromPreset("earth_display");
    config.add(EarthView::kId,{{"map_json",map},{"lat",25.22},{"lon",121.8},{"alt",19.8389225},{"scene_context",false}});
    config.add(SkyFeature::kId,{{"year",2026},{"month",11},{"day",18},{"hours_utc",4.}});
    config.add(CloudFeature::kId,{{"cover",.18},{"wind_speed",4.3},{"wind_az",68.}});
    config.add(OceanFeature::kId,{{"ocean_size",8000},{"wind_speed",5.0}});
    view.applyConfig(config);view.show();
    QElapsedTimer startup;startup.start();
    while(!ready&&error.isEmpty()&&startup.elapsed()<60000)pump(10);
    require(ready&&error.isEmpty(),"platform capture viewport did not initialize");
    auto* earth=view.featureAs<EarthViewFeature>(EarthView::kId);
    require(earth&&earth->surfaceSnapshot()&&earth->surfaceSnapshot()->valid_samples,"missing Taipei terrain in platform preview");
    earth->setCloudPresentationEnabled(false);
    const auto land=locationOnLand(*earth->surfaceSnapshot());
    const auto pose=[&](D eye,D centre,double fov) {
        const auto chart=view.context().contentModel();
        auto* camera=view.camera();auto look=camera->viewMatrix.cast<vsg::LookAt>();
        look->eye=chart*eye;look->center=chart*centre;
        const auto up=chart*vsg::dvec4(0,1,0,0);look->up={up.x,up.y,up.z};
        camera->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=fov;
    };
    const auto render=[&](double t) {
        view.context().setSimulationTime(t);
        const auto serial=view.widget()->property("renderFrameSerial").toULongLong();
        QElapsedTimer limit;limit.start();
        do {view.renderFrame();if(view.widget()->property("renderFrameSerial").toULongLong()>serial)break;pump(16);}
        while(limit.elapsed()<1600);
        require(view.widget()->property("renderFrameSerial").toULongLong()>serial,"platform preview frame did not present");
    };
    const auto shoot=[&](const QString& name) {
        const auto pixels=view.grabFrame();
        require(!pixels.isNull()&&pixels.save(output+"/"+name),"platform preview image write failed");
    };
    constexpr int fps=24,frames=84;
    json report={{"source",earth->surfaceSnapshot()->source},{"utc","2026-11-18T04:00:00Z"},
        {"fps",fps},{"frames_per_clip",frames},{"purpose","viewport demonstration"},{"clips",json::object()}};
    const std::vector<std::string> subjects=displayVehicle?std::vector<std::string>{"display_vehicle"}:tankOnly?std::vector<std::string>{"tracked"}:std::vector<std::string>{"tracked","ship","aircraft"};
    for(const auto& subject:subjects) {
        const auto directory=QString::fromStdString(subject);
        require(QDir().mkpath(output+"/"+directory),"could not create clip subdirectory");
        view.removeFeature(EngagementVisualFeature::kId);
        view.removeFeature(PlatformMotionFeature::kId);
        json params={{"subject",subject}};
        if(subject=="tracked")params["tank_assets"]=assets.string();
        else if(subject=="ship")params["ship_assets"]=std::filesystem::absolute(shipAssetPath.toStdString()).string();
        view.addFeature(PlatformMotionFeature::kId,params);
        view.addFeature(EngagementVisualFeature::kId);
        auto* actor=view.featureAs<PlatformMotionFeature>(PlatformMotionFeature::kId);
        auto* fx=view.featureAs<EngagementVisualFeature>(EngagementVisualFeature::kId);
        require(actor&&fx&&fx->particleMode(),"platform animation or exhaust graph unavailable");
        double recoilPeak=0.,coverPeak=0.,firstAngle=0.,lastAngle=0.,stopTravel=0.;
        double yawMin=0.,yawMax=0.,elevationMax=0.;
        size_t smokePeak=0;
        bool initiallyMounted=false,detached=false,closedAfter=false;
        for(int i=0;i<frames;++i) {
            const double t=double(i)/fps;
            D p;
            double travel=0.;
            if(subject=="tracked") {
                travel=4.1*ease(t/2.56);
                p=land+D(0,0,travel);
                p.y=earth->surfaceSnapshot()->sampleLocal(p.x,p.z).height+.044;
                pose(p+D(9.2,2.9,6.1),p+D(0,1.24,1.0),42.);
            } else if(subject=="ship") {
                p={1.0*t,.12*std::sin(t*1.6),3.2*t};
                const double close=ease(t/.88);
                pose(p+D(91.-47.*close,46.-17.*close,-124.+20.*close),p+D(0,11.+6.*close,-39.-16.*close),42.);
            } else if(subject=="display_vehicle") {
                p=land;
                p.y+=.044;
                pose(p+D(9.4,4.9,6.8),p+D(0,2.7,0),43.);
            } else {
                p={-8.+1.8*t,2450.+.8*t,-32.-9.*t};
                const double wide=ease((t-1.20)/1.35);
                if(i==72)pose(p+D(11.,5.,18.),p+D(0.,0.,2.),39.);
                else pose(p+D(-15.-19.*wide,2.+2.2*wide,8.+7.2*wide),p+D(-1.,-.36,-2.-9.*wide),42.);
            }
            actor->setMotion(vsg::translate(p),travel,subject=="aircraft"? .78+.17*ease(t):0.);
            render(t);
            const auto& state=actor->state();
            if(i==0){firstAngle=state.antennaAngle;initiallyMounted=state.propVisible;}
            if(i==frames-2)stopTravel=state.trackDistance;
            if(i==frames-1){lastAngle=state.antennaAngle;closedAfter=state.hatch<.001;}
            yawMin=std::min(yawMin,state.turretYaw);
            yawMax=std::max(yawMax,state.turretYaw);
            elevationMax=std::max(elevationMax,state.barrelElevation);
            recoilPeak=std::max(recoilPeak,state.recoil);coverPeak=std::max(coverPeak,state.hatch);
            smokePeak=std::max(smokePeak,fx->smokeHistoryCount());detached|=state.detached;
            shoot(directory+QString("/frame-%1.png").arg(i,4,10,QLatin1Char('0')));
            if(i==12&&subject=="tracked")shoot("00-tracked-rolling.png");
            if(i==0&&subject=="aircraft")shoot("05-aircraft-mounted.png");
            if(i==29&&subject=="tracked")shoot("01-tracked-flash.png");
            if(i==36&&subject=="tracked")shoot("02-tracked-recoil.png");
            if(i==40&&displayVehicle)shoot("12-display-vehicle.png");
            if(i==22&&subject=="tracked")shoot("09-turret-right.png");
            if(i==61&&subject=="tracked")shoot("10-turret-left.png");
            if(i==25&&subject=="ship")shoot("03-ship-covers.png");
            if(i==44&&subject=="ship")shoot("04-ship-deck-effect.png");
            if(i==46&&subject=="aircraft")shoot("06-aircraft-separation.png");
            if(i==72&&subject=="aircraft") {
                require(fx->visibleJetFlames()==1,"F-16C model exhaust did not reach the renderer");
                shoot("11-f16c-engine.png");
            }
        }
        const auto state=actor->state();
        if(subject=="tracked")require(state.wheelAngle>1.&&state.trackDistance==stopTravel&&recoilPeak>.25&&state.recoil==0.,"tracked vehicle wheel, recoil or stop validation failed");
        if(subject=="tracked") {
            require(state.animatedWheels==18,"M1A2 source wheels were not mounted");
            require(yawMin<-.25&&yawMax>.45&&elevationMax>.09,"M1A2 articulated rig did not apply both yaw directions and elevation");
            const D p=land+D(0,0,state.trackDistance);
            const auto centre=D(p.x,earth->surfaceSnapshot()->sampleLocal(p.x,p.z).height+.60,p.z-1.1);
            pose(centre+D(4.7,.3,1.6),centre,37.);render(double(frames-1)/fps);shoot("08-m1a2-wheels.png");
            json parts;std::ifstream(assets/"m1a2-rig.json")>>parts;
            report["mesh_source"]=parts.at("source");
            if(tankOnly) {
                const auto manifestPath=QDir(output).absoluteFilePath("m1a2-parts-test.json").toStdString();
                const auto asset=[&](const char* key){return (assets/parts.at(key).get<std::string>()).string();};
                const json manifest={
                    {"version",1},{"body",asset("hull")},
                    {"joints",json::array({
                        {{"name","turret"},{"pivot_m",parts.at("turret_pivot_m")},{"mesh",asset("turret")}},
                        {{"name","elevation"},{"parent","turret"},{"pivot_m",parts.at("barrel_pivot_m")}},
                        {{"name","recoil"},{"parent","elevation"},{"pivot_m",{0,0,0}},{"mesh",asset("barrel")}}
                    })},
                    {"attachments",json::array({
                        {{"name","muzzle"},{"joint","recoil"},{"position_m",parts.at("muzzle_m")}}
                    })}
                };
                std::ofstream(manifestPath)<<manifest.dump(2)<<'\n';
                view.removeFeature(PlatformMotionFeature::kId);
                view.addFeature(PlatformMotionFeature::kId,{{"model_id","m1a2"},{"parts_manifest",manifestPath}});
                auto* dataDriven=view.featureAs<PlatformMotionFeature>(PlatformMotionFeature::kId);
                require(dataDriven,"data-driven rig could not mount");
                dataDriven->setMotion(vsg::translate(p),0.);
                render(.92);
                require(dataDriven->state().turretYaw>.48&&dataDriven->state().barrelElevation>.08,
                    "data-driven turret and elevation binding");
                report["parts_manifest_verified"]=true;
            }
        }
        if(subject=="ship") {
            require(std::abs(lastAngle-firstAngle)>.5&&coverPeak>.98&&closedAfter&&detached,"ship antenna, cover or separation cue failed");
            const auto target=actor->antennaCentre();
            pose(target+D(17.,7.,16.),target,37.);render(double(frames-1)/fps);shoot("07-ship-antenna.png");
            require(QDir().mkpath(output+"/antenna"),"could not create antenna detail clip directory");
            for(int i=0;i<48;++i) {
                const double t=double(frames+i)/fps;
                actor->setMotion(vsg::translate(t,.12*std::sin(t*1.6),3.2*t),0.);
                const auto aerial=actor->antennaCentre();
                pose(aerial+D(17.,7.,16.),aerial,37.);render(t);
                shoot(QString("antenna/frame-%1.png").arg(i,4,10,QLatin1Char('0')));
            }
        }
        if(subject=="aircraft")
            require(initiallyMounted&&detached&&!state.propVisible,"aircraft mounted or detached visual state missing");
        if(displayVehicle)
            require(!initiallyMounted&&detached&&!state.propVisible,"display vehicle prop did not follow its presentation cue");
        require(smokePeak>1,"visual cue did not create persistent smoke puffs");
        report["clips"][subject]={{"wheel_angle_rad",state.wheelAngle},{"cover_fraction_peak",coverPeak},
            {"animated_wheels",state.animatedWheels},{"antenna_angle_rad",lastAngle},{"recoil_visual_peak",recoilPeak},
            {"turret_yaw_min_rad",yawMin},{"turret_yaw_max_rad",yawMax},{"barrel_elevation_peak_rad",elevationMax},
            {"detached_prop_visible",detached},{"smoke_history_peak",smokePeak}};
    }
    std::ofstream(QDir(output).absoluteFilePath("platform-motion.json").toStdString())<<report.dump(2)<<'\n';
    view.hide();
}


void runEarthFormalPlatformCapture(const QString& worker,const QString& topic,const QString& output,bool brokerOnly) {
    using namespace vsgraysim;
    using namespace gui::display;
    require(brokerOnly||QApplication::platformName()=="xcb","formal platform capture requires native XCB/Vulkan");
    const auto out=QDir(output).absolutePath();
    require(QDir().mkpath(out+"/worker"),"could not make formal platform capture directory");
    QProcess process;
    process.setStandardErrorFile(out+"/worker.log");
    process.setStandardOutputFile(out+"/worker-stdout.log");
    constexpr auto generation="earth_platform_acceptance";
    process.start(QFileInfo(worker).absoluteFilePath(),{"--engine-worker",QFileInfo(topic).absoluteFilePath(),out+"/worker",generation});
    require(process.waitForStarted(5000),"could not start the formal simulation worker");
    auto& net=BrokerNet::instance();
    struct Stop {
        QProcess& process; BrokerNet& net;
        ~Stop(){net.stop();process.closeWriteChannel();if(!process.waitForFinished(2500)){process.kill();process.waitForFinished(2500);}}
    } stop{process,net};
    net.startDisplayRole();
    json workerStatus=json::object();
    const auto refreshWorker=[&] {
        std::ifstream status((out+"/worker/status.json").toStdString());
        if(status)status>>workerStatus;
        if(workerStatus.contains("error"))throw std::runtime_error(workerStatus.at("error").get<std::string>());
    };
    auto wait=[&](const auto& predicate,const char* failure,int ms=35000) {
        QElapsedTimer duration;duration.start();
        while(duration.elapsed()<ms) {
            require(process.state()==QProcess::Running,"formal simulation worker exited");
            refreshWorker();
            if(predicate())return;
            pump(35);
        }
        throw std::runtime_error(failure);
    };
    wait([&]{return workerStatus.value("initialized",false);},"formal simulation worker not initialized",100000);
    require(net.pullSnapshot(2000)&&net.linkAttach(0,false),"could not link the formal world lease");
    if(brokerOnly&&workerStatus.at("earth").at("voxel_scene").at("scene_presentation").contains("acoustic_readings")){
        constexpr uint64_t targetTick=100;
        process.write(QByteArray::fromStdString(json{{"generation",generation},{"seq",1},
            {"command","play_to:"+std::to_string(targetTick)}}.dump()+"\n"));
        wait([&]{return workerStatus.at("tick")==targetTick&&!workerStatus.at("running").get<bool>();},
            "acoustic probe worker did not reach the target tick");
        require(net.pullSnapshot(2000),"acoustic measurements unavailable at broker");
        const auto meta=net.lastSimMeta();
        const auto earthContext=json::parse(meta.earth_json);
        auto scene=earthContext.at("voxel_scene");
        VoxelSceneReadView view;
        const auto snapshot=view.update(scene);
        const auto readings=snapshot->scenePresentation.at("acoustic_readings");
        require(snapshot->tick==targetTick&&readings.size()==2&&readings.at(0).at("tick")==targetTick&&
            readings.at(0).at("spl_db").is_number(),"authoritative acoustic measure missed the committed scene tick");
        require(readings==workerStatus.at("earth").at("voxel_scene").at("scene_presentation").at("acoustic_readings"),
            "read-only receiver changed the source's fixed-probe readings");
        EarthPanel panel;
        require(panel.updateContext(earthContext,meta.sim_time,meta.online,meta.running),"earth panel rejected acoustic metadata");
        auto* probe=panel.findChild<QComboBox*>("earth_acoustic_probe");
        auto* meter=panel.findChild<QLabel*>("earth_acoustic_meter");
        require(probe&&probe->count()==3&&meter,"formal display did not offer acoustic probes");
        probe->setCurrentIndex(1);
        require(meter->text().contains(QStringLiteral("dB SPL"))&&meter->toolTip().contains(QStringLiteral("Pa")),
            "acoustic readings were not shown in the existing display while audio was off");
        std::ofstream((out+"/acoustic-probes.json").toStdString())<<json{{"sim_seconds",meta.sim_time},{"probes",readings},
            {"source_tick_p95_ms",workerStatus.value("worker_tick_p95_ms",0.)}}.dump(2)<<'\n';
        std::cout<<"PASS: official worker, world lease and fixed-probe acoustic readings in EarthPanel\n";
        return;
    }
    if(brokerOnly&&workerStatus.at("platforms").size()==3&&
        workerStatus.at("platforms").at(2).value("model_id",std::string())=="swot"){
        const auto targetTick=uint64_t(65);
        process.write(QByteArray::fromStdString(json{{"generation",generation},{"seq",1},
            {"command","play_to:"+std::to_string(targetTick)}}.dump()+"\n"));
        wait([&]{return workerStatus.at("tick")==targetTick&&!workerStatus.at("running").get<bool>();},
            "formal Falcon 9 replay did not stop on the authored checkpoint",25000);
        require(net.pullSnapshot(2000),"could not read mission snapshots from the formal broker");
        const auto meta=net.lastSimMeta();
        const auto context=json::parse(meta.earth_json);
        vsgraysim::VoxelSceneReadView receiver;
        const auto readBack=receiver.update(context.at("voxel_scene"));
        const auto& booster=readBack->objects.at("falcon-booster");
        require(readBack->tick==targetTick&&booster.pose(readBack->tick,readBack->fixedStep)[3][1]>24.&&
            readBack->objects.at("swot-panel-right").poseTrack,
            "formal world publication lost the authored voxel mission pose or track");
        std::ofstream((out+"/rocket-timeline.json").toStdString())<<json{
            {"tick",targetTick},{"seconds",readBack->timeSeconds()},{"platforms",json::parse(meta.platforms_json)}}.dump(2)<<'\n';
        std::cout<<"PASS: formal Falcon 9 / SWOT replay, world lease and published voxel poses\n";
        return;
    }
    if(brokerOnly&&workerStatus.at("platforms").size()==2&&
        workerStatus.at("platforms").at(0).value("model_id",std::string())=="skywalker-x8"){
        const auto initial=workerStatus.at("tick").get<uint64_t>();
        process.write(QByteArray::fromStdString(json{{"generation",generation},{"seq",1},{"command","step"}}.dump()+"\n"));
        wait([&]{return workerStatus.value("ack",0u)==1;},"civil world step not acknowledged");
        require(workerStatus.at("tick")==initial+1&&!workerStatus.at("running").get<bool>()&&
            workerStatus.at("process_pool").at("gpu_capacity")==0,"civil world step or task classification is incorrect");
        require(net.pullSnapshot(2000),"could not read civil telemetry from the broker");
        const auto meta=net.lastSimMeta();
        const auto rows=json::parse(meta.platforms_json);
        require(rows.size()==2&&rows.at(0).at("model_id")=="skywalker-x8"&&
            rows.at(0).at("navigation").at("tick")==initial+1&&rows.at(1).at("rotors").size()==4&&
            rows.at(1).at("navigation").at("tick")==initial+1&&rows.at(1).at("model_id")=="3dr-iris",
            "civil broker lost platform, rotor or navigation state");
        EarthPanel panel;
        panel.updateContext(json::parse(meta.earth_json),meta.sim_time,meta.online,meta.running);
        panel.updateTelemetry(rows,801);
        const auto* label=panel.findChild<QLabel*>("earth_platform_position");
        require(label&&label->text().contains(QStringLiteral("场景定位"))&&
            label->text().contains(QStringLiteral("椭球高")),"navigation truth did not enter the existing Qt earth panel");
        std::ofstream((out+"/civil-timeline.json").toStdString())<<rows.dump(2)<<'\n';
        std::cout<<"PASS: formal civil world and snapshot navigation in existing Qt display\n";
        return;
    }
    if(brokerOnly&&workerStatus.at("source").value("visual_only",false)){
        const auto initial=workerStatus.at("tick").get<uint64_t>();
        const auto initialPacket=workerStatus.at("earth").at("voxel_scene").at("equipment_visuals");
        require(initialPacket.at("items").size()==4&&workerStatus.at("equipment").empty()&&
            workerStatus.at("process_pool").at("gpu_capacity")==0,"visual topic unexpectedly started measurements");
        process.write(QByteArray::fromStdString(json{{"generation",generation},{"seq",1},{"command","step"}}.dump()+"\n"));
        wait([&]{return workerStatus.value("ack",0u)==1;},"visual world step not acknowledged");
        require(workerStatus.at("tick")==initial+1&&!workerStatus.at("running").get<bool>(),
            "visual world single step did not end paused on the next tick");
        require(net.pullSnapshot(2000),"could not read the formal visual-world snapshot");
        const auto current=json::parse(net.lastSimMeta().earth_json).at("voxel_scene");
        const auto& visuals=current.at("equipment_visuals");
        const auto& devices=current.at("equipment_states");
        require(visuals.at("tick")==initial+1&&devices.at("tick")==initial+1&&
            visuals.at("state_revision")==current.at("revision")&&
            visuals.at("items").at(0).at("mount_id")=="mast_upper",
            "broker did not pair the ship beam and named mount with the committed scene");
        const auto angleBefore=initialPacket.at("items").at(0).at("preview_angle_deg").get<double>();
        const auto angleAfter=visuals.at("items").at(0).at("preview_angle_deg").get<double>();
        require(std::abs(angleAfter-angleBefore-1.8)<1e-8,"visual radar phase did not follow the formal scene step");
        pump(250);refreshWorker();
        require(workerStatus.at("tick")==initial+1,"paused formal ship world kept advancing");
        std::ofstream((out+"/ship-model-timeline.json").toStdString())<<json{{"initial_tick",initial},{"tick",initial+1},
            {"azimuth_before_deg",angleBefore},{"azimuth_after_deg",angleAfter},
            {"model_id",workerStatus.at("platforms").at(0).at("model_id")},{"equipment",devices}}.dump(2)<<'\n';
        std::cout<<"PASS: formal visual-only ship world, named mount, leased single step and paused radar phase\n";
        return;
    }
    auto poseData=[&]{return workerStatus.at("platforms").at(0).at("presentation");};
    auto rigidPose=[&]{return workerStatus.at("earth").at("voxel_scene").at("objects").at("m1a2").at("pose");};
    if(brokerOnly) {
        wait([&]{return net.lastSimMeta().online&&net.lastSimMeta().flight_control_available;},
            "formal worker did not advertise the display-pose command");
        EarthPanel control;
        const auto refreshControl=[&]{
            const auto meta=net.lastSimMeta();
            control.updateContext(json::parse(meta.earth_json),meta.sim_time,meta.online,meta.running);
            control.updateTelemetry(json::parse(meta.platforms_json),101);
        };
        auto* button=control.findChild<QPushButton*>("m1a2_display_apply");
        auto* yawInput=control.findChild<QDoubleSpinBox*>("m1a2_display_yaw");
        auto* pitchInput=control.findChild<QDoubleSpinBox*>("m1a2_display_pitch");
        auto* feedback=control.findChild<QLabel*>("m1a2_display_result");
        require(button&&yawInput&&pitchInput&&feedback,"formal operator widgets missing");
        wait([&]{refreshControl();return button->isEnabled();},"operator pose did not become available");
        const auto base=rigidPose();
        const auto save=[&](const char* key){
            auto context=workerStatus.at("earth");
            auto& descriptor=context.at("voxel_scene");
            const auto geometry=std::filesystem::path(out.toStdString())/"frozen-geometry.json";
            if(!std::filesystem::exists(geometry))
                std::filesystem::copy_file(descriptor.at("checkpoint_file").get<std::string>(),geometry);
            descriptor["checkpoint_file"]=geometry.string();
            std::ofstream((out+"/"+key+".json").toStdString())<<context.dump(2)<<'\n';
        };
        save("01-before");
        const auto commandPose=[&](double yaw,double pitch,uint64_t revision,const char* key) {
            yawInput->setValue(yaw);pitchInput->setValue(pitch);button->click();
            wait([&] {
                refreshControl();
                return poseData().at("revision")==revision&&feedback->text().contains(QStringLiteral("仿真已确认"));
            },"operator pose request or published revision timed out",12000);
            require(rigidPose()==base&&workerStatus.at("tick")==0&&poseData().at("yaw_deg")==yaw&&
                poseData().at("pitch_deg")==pitch,"pose publication disagrees or rigid body changed during pause");
            save(key);
            return poseData();
        };
        const auto first=commandPose(28.,9.,1,"02-right");
        const auto second=commandPose(-24.,-6.,2,"03-left");
        const auto step=json{{"generation",generation},{"seq",1},{"command","step"}}.dump()+"\n";
        require(process.write(step.data(),step.size())>=0,"could not request a formal single step");
        wait([&]{return workerStatus.at("tick")==1&&poseData().at("travel_m").get<double>()>.01;},
            "single step did not publish track travel",10000);
        save("04-stepped");
        const auto path=(out+"/broker-acceptance.json").toStdString();
        std::ofstream(path)<<json{{"first",first},{"second",second},{"step",poseData()},
            {"paused_rigid_pose_unchanged",true},{"operator_feedback",feedback->text().toStdString()},
            {"backend_pid",workerStatus.at("pid")}}.dump(2)<<'\n';
        return;
    }
    EarthPanel display;
    display.resize(1120,730);display.show();
    const auto update=[&] {
        const auto meta=net.lastSimMeta();
        if(!meta.online)return;
        display.updateContext(json::parse(meta.earth_json),meta.sim_time,true,meta.running);
        display.updateTelemetry(json::parse(meta.platforms_json),101);
    };
    bool logged=false;
    auto sampled=[&]()->const PlatformMotionFeature::PoseState* {
        update();
        auto* view=display.viewport();
        if(!view||!view->isReady())return nullptr;
        view->setAutoFrame(false);
        view->setIdleSuspend(false);
        view->markActivity();
        view->renderFrame();
        auto* platform=view->featureAs<ScenePlatformMotion>(ScenePlatformMotion::kId);
        auto* voxel=view->featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);
        if(!logged&&voxel&&voxel->snapshot()) {
            logged=true;
            std::cerr<<"Formal display got voxel tick "<<voxel->snapshot()->tick
                <<" metadata rows "<<voxel->presentation().size()<<" render feature "<<bool(platform)<<"\n";
        }
        return platform?platform->state("m1a2"):nullptr;
    };
    wait([&]{const auto* state=sampled();return state&&state->animatedWheels==18;},
         "formal native viewport did not bind the M1A2 rig",60000);
    auto* button=display.findChild<QPushButton*>("m1a2_display_apply");
    auto* yaw=display.findChild<QDoubleSpinBox*>("m1a2_display_yaw");
    auto* pitch=display.findChild<QDoubleSpinBox*>("m1a2_display_pitch");
    auto* label=display.findChild<QLabel*>("m1a2_display_result");
    require(button&&yaw&&pitch&&label&&button->isEnabled(),"operator pose controls did not become available");
    auto shot=[&](const char* name){
        auto image=display.viewport()->grabFrame();
        require(!image.isNull()&&image.save(out+"/"+name),"could not capture the actual Vulkan viewport");
    };
    const auto originalPose=rigidPose();
    shot("01-before.png");
    auto setPose=[&](double y,double p,unsigned revision,const char* frame) {
        yaw->setValue(y);pitch->setValue(p);button->click();
        wait([&] {
            const auto* sampledState=sampled();
            return sampledState&&std::abs(sampledState->turretYaw-vsg::radians(y))<1e-8&&
                std::abs(sampledState->barrelElevation-vsg::radians(p))<1e-8&&
                poseData().at("revision")==revision&&label->text().contains(QStringLiteral("仿真已确认"));
        },"viewport angle, transport acknowledgement or command revision did not match");
        require(workerStatus.at("tick")==0&&!workerStatus.value("running",true)&&rigidPose()==originalPose,
            "visual pose adjustment changed the authoritative rigid body or clock");
        shot(frame);
        return poseData();
    };
    const auto first=setPose(28.,9.,1,"02-right.png");
    const auto second=setPose(-24.,-6.,2,"03-left.png");
    const auto step=json{{"generation",generation},{"seq",1},{"command","step"}}.dump()+"\n";
    require(process.write(step.data(),step.size())>=0,"could not request a formal single step");
    double wheelAngle=0.;
    wait([&]{
        const auto* state=sampled();
        if(!state||workerStatus.at("tick")!=1||state->trackDistance<=0.||!state->wheelAngle)return false;
        wheelAngle=state->wheelAngle;
        return std::abs(state->trackDistance-poseData().at("travel_m").get<double>())<1e-9;
    },"single step distance did not reach the tracked rig");
    shot("04-stepped.png");
    std::ofstream((out+"/acceptance.json").toStdString())<<json{{"before_pose_unchanged",true},
        {"first",first},{"second",second},{"step",poseData()},{"wheel_angle_rad",wheelAngle},
        {"scene",QFileInfo(topic).absoluteFilePath().toStdString()},{"worker_pid",workerStatus.at("pid")},
        {"utc",workerStatus.at("earth").at("utc_origin")}}.dump(2)<<'\n';
    display.hide();
    pump(10);
}


void runEarthPublishedMotionCapture(const QString& input,const QString& output) {
    using namespace vsgraysim;
    require(QApplication::platformName()=="xcb","published motion capture needs XCB/Vulkan");
    require(QDir().mkpath(output),"could not create published motion capture directory");
    const std::array<const char*,4> keys{"01-before","02-right","03-left","04-stepped"};
    std::array<json,4> declarations;
    std::array<earth::VoxelSceneSnapshotPtr,4> snapshots;
    VoxelSceneReadView reader;
    for(size_t i=0;i<keys.size();++i) {
        std::ifstream in((QDir(input).filePath(keys[i])+".json").toStdString());
        in>>declarations[i];
        snapshots[i]=reader.update(declarations[i].at("voxel_scene"));
    }
    const auto& setup=declarations.front();
    const auto& anchor=setup.at("geo_anchor");
    auto config=ViewportConfig::fromPreset("earth_display");
    config.remove("rocky_ocean");
    config.add(EarthView::kId,{{"map_json",setup.at("earth").at("map")},{"lat",anchor.at("lat")},
        {"lon",anchor.at("lon")},{"alt",anchor.at("alt")},{"scene_context",true}});
    config.add(VoxelSceneFeature::kId,{{"external_only",true},{"camera_local",setup.at("voxel_camera")}});
    config.add(ScenePlatformMotion::kId);
    config.add(SkyFeature::kId,setup.at("utc_origin"));
    Viewport3DView view;
    view.resize(1120,630);
    QObject::connect(&view,&Viewport3DView::vsgReady,&view,[&]{view.setAutoFrame(false);});
    view.applyConfig(config);
    view.show();
    QElapsedTimer timer;timer.start();
    while(!view.isReady()&&timer.elapsed()<50000)pump(15);
    require(view.isReady(),"published motion viewport failed to start");
    auto* voxels=view.featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);
    auto* actors=view.featureAs<ScenePlatformMotion>(ScenePlatformMotion::kId);
    require(voxels&&actors,"published motion view features missing");
    json report=json::array();
    for(size_t i=0;i<keys.size();++i){
        const auto& presentation=declarations[i].at("voxel_scene").at("presentation");
        voxels->bindSnapshot(snapshots[i],presentation);
        view.context().setSimulationTime(snapshots[i]->timeSeconds());
        view.markActivity();view.renderFrame();pump(30);view.renderFrame();
        const auto* frame=actors->state("m1a2");
        const auto& data=presentation.at(0).at("presentation");
        require(frame&&frame->animatedWheels==18&&
            std::abs(frame->trackDistance-data.at("travel_m").get<double>())<1e-9&&
            std::abs(frame->turretYaw-vsg::radians(data.at("yaw_deg").get<double>()))<1e-9&&
            std::abs(frame->barrelElevation-vsg::radians(data.at("pitch_deg").get<double>()))<1e-9,
            "native pose does not match the published model state");
        require(voxels->display().placements.at("m1a2")->matrix==
            snapshots[i]->objects.at("m1a2").pose(snapshots[i]->tick,snapshots[i]->fixedStep),
            "display changed the recorded voxel body pose");
        const auto image=view.grabFrame();
        require(!image.isNull()&&image.save(QDir(output).filePath(keys[i])+".png"),"cannot save actual native frame");
        report.push_back({{"name",keys[i]},{"published",data},{"rendered_wheel_angle_rad",frame->wheelAngle},
            {"utc",snapshots[i]->utcOrigin}});
    }
    std::ofstream(QDir(output).filePath("view-acceptance.json").toStdString())<<report.dump(2)<<'\n';
    view.hide();
}
