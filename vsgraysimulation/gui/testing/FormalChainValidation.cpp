#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include "FormalChainValidation.h"
#include "SceneDesignerWindow.h"
#include "SimulationControlWindow.h"
#include "SimulationWorkerClient.h"
#include "display/ControlWorkbench.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/VoxelSceneFeature.h"
#include "viewport3d/EarthView.h"
#include "viewport3d/EarthViewFeature.h"
#include "viewport3d/EnvFeatures.h"
#include <vsgraysim/broker/BrokerNet.h>
#include <vsgraysim/broker/SimDataBroker.h>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QSaveFile>
#include "viewport3d/VsgQtWidget.h"
#include <QElapsedTimer>
#include <QPainter>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QComboBox>
#include "viewport3d/PlatformMotionFeature.h"
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <fstream>
#include <cmath>
#include <unistd.h>
#include <algorithm>

namespace vsgraysim::gui {
namespace {
using json=nlohmann::json;
QString root(){return qEnvironmentVariable("VSGRAYSIM_FORMAL_CHAIN_DIR");}
void require(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
void write(const QString& name,const json& value){
    QDir().mkpath(root());
    const auto bytes=QByteArray::fromStdString(value.dump(2));
    std::string error;
    for(int attempt=0;attempt<3;++attempt) {
        QSaveFile f(root()+"/"+name);
        if(f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit())return;
        error=f.errorString().toStdString();
    }
    throw std::runtime_error("cannot write "+name.toStdString()+": "+error);
}
template<class T> std::string hash(const std::vector<T>& values){return QCryptographicHash::hash(QByteArray::fromRawData(reinterpret_cast<const char*>(values.data()),qsizetype(values.size()*sizeof(T))),QCryptographicHash::Sha256).toHex().toStdString();}
json terrainPageStats(QWidget& window) {
    for (auto* view : window.findChildren<viewport3d::Viewport3DView*>()) {
        if (!view->isVisible() || !view->isReady()) continue;
        auto* feature = view->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene");
        if (!feature) continue;
        auto pages = feature->terrainDisplayPageStats();
        if (!pages.empty()) return pages;
    }
    return json::object();
}
json platformMotion(viewport3d::Viewport3DView& view) {
    const auto* motion=view.featureAs<viewport3d::ScenePlatformMotion>(viewport3d::ScenePlatformMotion::kId);
    const auto* voxel=view.featureAs<viewport3d::VoxelSceneFeature>("voxel_scene");
    if(!motion||!voxel||!voxel->snapshot())return {};
    const auto* state=motion->state("m1a2");
    if(!state)return {};
    return {{"tick",voxel->snapshot()->tick},{"yaw_rad",state->turretYaw},{"pitch_rad",state->barrelElevation},
        {"travel_m",state->trackDistance},{"wheel_angle_rad",state->wheelAngle},{"wheels",state->animatedWheels},
        {"utc",voxel->snapshot()->utcOrigin}};
}
void capture(QWidget& window,const QString& name){
    auto picture=window.grab();bool native=false;
    for(auto* view:window.findChildren<viewport3d::Viewport3DView*>())if(view->isVisible()&&view->isReady()){
        require(view->widget()->width()<=view->width()+2&&view->widget()->height()<=view->height()+2,"native Vulkan child overflowed its dock viewport");
        const auto frame=view->grabFrame();require(!frame.isNull(),"native Vulkan readback failed");
        require(frame.save(root()+"/"+name+"-earth.png"),"cannot save native Vulkan screenshot");
        QPainter painter(&picture);painter.drawImage(QRect(view->widget()->mapTo(&window,QPoint{}),view->widget()->size()),frame);native=true;
    }
    require(native,"no visible ready Vulkan view in the actual application");require(picture.save(root()+"/"+name+".png"),"cannot save window screenshot");
}
void watch(QWidget& window,const QString& role,std::function<void()> action){
    if(root().isEmpty())return;auto* timer=new QTimer(&window);auto elapsed=std::make_shared<QElapsedTimer>();elapsed->start();
    QObject::connect(timer,&QTimer::timeout,&window,[timer,elapsed,role,action=std::move(action)]{
        try{require(elapsed->elapsed()<180000,"formal application validation timeout");action();}
        catch(const std::exception& error){timer->stop();write(role+"-error.json",{{"error",error.what()},{"pid",::getpid()}});QApplication::exit(2);}
    });timer->start(200);
}
json channels(){
    auto out=json::array();auto& broker=SimDataBroker::instance();
    for(const uint32_t id:{7101,7102,7103,7104}){
        const auto st=broker.channelStats(id);json item={{"id",id},{"received",st.published_frames},{"frame_id",st.last_frame_id}};
        if(id==7101){VisFrame f;if(broker.latestVis(id,&f)){double sum=0;for(float v:f.intensity){require(std::isfinite(v),"nonfinite VIS product");sum+=std::abs(v);}item["payload_sha256"]=hash(f.intensity);item.update({{"width",f.width},{"height",f.height},{"stage",f.stage},{"sim_time",f.sim_time},{"surface_revision",f.surface_revision},{"absolute_sum",sum}});}}
        else if(id==7104){RadarFrame f;if(broker.latestRadar(id,&f)){double sum=0;for(auto v:f.rd_map){require(std::isfinite(v.real())&&std::isfinite(v.imag()),"nonfinite RD product");sum+=std::norm(v);}item["payload_sha256"]=hash(f.rd_map);item.update({{"width",f.range_bins},{"height",f.doppler_bins},{"stage",f.stage},{"sim_time",f.sim_time},{"surface_revision",f.surface_revision},{"power_sum",sum},{"sample_rate_hz",f.sample_rate_hz},{"prf_hz",f.prf}});}}
        else {IrFrame f;if(broker.latestIr(id,&f)){double sum=0;for(float v:f.radiance){require(std::isfinite(v),"nonfinite IR product");sum+=std::abs(v);}item["payload_sha256"]=hash(f.radiance);item.update({{"width",f.width},{"height",f.height},{"stage",f.stage},{"sim_time",f.sim_time},{"surface_revision",f.surface_revision},{"absolute_sum",sum},{"wavelength_min_um",f.wavelength_min},{"wavelength_max_um",f.wavelength_max}});}}
        out.push_back(item);
    }return out;
}
QPushButton* powerButton(display::ControlWorkbench& window){
    for(auto* table:window.findChildren<QTableWidget*>())if(table->columnCount()==6)for(int r=0;r<table->rowCount();++r)
        if(auto* item=table->item(r,0);item&&item->text().contains(QStringLiteral("可见光相机")))return qobject_cast<QPushButton*>(table->cellWidget(r,5));
    return nullptr;
}
}
void installFormalValidation(SceneDesignerWindow& window){
    const auto destination=qEnvironmentVariable("VSGRAYSIM_FORMAL_SCENE_SAVE");if(root().isEmpty()||destination.isEmpty())return;
    const auto usdInput = qEnvironmentVariable("VSGRAYSIM_FORMAL_USD_EDITS");
    const auto usdOutput = qEnvironmentVariable("VSGRAYSIM_FORMAL_USD_EXPORT");
    watch(window,"designer",[&window,destination,usdInput,usdOutput,done=false,edited=false,
        usdEdited=false,settledAt=qint64(-1),settling=QElapsedTimer{}]()mutable{
        if(done)return;auto* view=window.findChild<viewport3d::Viewport3DView*>();if(!view||!view->isReady())return;
        auto* feature=view->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene");if(!feature||!feature->runtime())return;
        require(feature->lastError().empty(),"designer voxel preparation failed");
        const auto editFile=qEnvironmentVariable("VSGRAYSIM_FORMAL_AUTHORING_EDITS");
        if(!edited&&!editFile.isEmpty()){
            require(QFileInfo(editFile).size()<=16384,"authoring validation commands too large");json commands;std::ifstream(editFile.toStdString())>>commands;
            feature->runtime()->setPaused(true);feature->runtime()->edit(commands,feature->runtime()->snapshot()->revision);edited=true;view->markActivity();return;
        }
        if(settledAt<0){settling.start();settledAt=0;view->markActivity();return;}
        if(settling.elapsed()<3000){view->markActivity();return;}
        if (!usdInput.isEmpty() && !usdEdited) {
            const auto revision = feature->runtime()->snapshot()->revision;
            bool applied = false;
            require(QMetaObject::invokeMethod(&window, "applyUsdTargetEdits", Q_RETURN_ARG(bool, applied),
                Q_ARG(QString, usdInput)) && applied, "designer USD edit layer failed");
            require(feature->runtime()->snapshot()->revision == revision + 1, "USD edit layer did not commit one edit");
            usdEdited = true;
            settledAt = -1;
            return;
        }
        if (!usdOutput.isEmpty()) {
            bool exported = false;
            require(QMetaObject::invokeMethod(&window, "saveUsdTargetEdits", Q_RETURN_ARG(bool, exported),
                Q_ARG(QString, usdOutput)) && exported, "designer USD export failed");
        }
        auto* globe=view->featureAs<viewport3d::EarthViewFeature>("rocky_map");
        require(globe&&globe->weather(),"designer weather not ready after settling");
        require(std::abs(globe->weather()->seconds-feature->runtime()->snapshot()->timeSeconds())<1e-9,"paused designer cloud clock drifted from scene time");
        const auto expected=feature->runtime()->checkpoint();bool saved=false;
        done=true; // The publisher processes paint and timer events while waiting for its worker.
        require(QMetaObject::invokeMethod(&window,"saveScenarioFile",Q_RETURN_ARG(bool,saved),Q_ARG(QString,destination))&&saved,"designer save failed");
        auto project=VoxelSceneProject::open(destination.toStdString());require(project.createScene()->checkpoint()==expected,"designer dropped or altered authored voxel state");
        bool countConfigured=false;
        const auto configuredCount=qEnvironmentVariableIntValue("VSGRAYSIM_FORMAL_EQUIPMENT_COUNT",&countConfigured);
        const auto equipmentCount=project.equipmentConfig().equipment.size();
        require(equipmentCount==size_t(countConfigured?configuredCount:4),"designer dropped equipment configuration");
        capture(window,qEnvironmentVariable("VSGRAYSIM_FORMAL_DESIGNER_NAME","designer"));
        write(qEnvironmentVariable("VSGRAYSIM_FORMAL_DESIGNER_NAME","designer")+".json",{{"pid",::getpid()},{"saved",destination.toStdString()},{"checkpoint_sha256",QCryptographicHash::hash(QByteArray::fromStdString(expected.dump()),QCryptographicHash::Sha256).toHex().toStdString()},{"content_revision",expected.at("content_revision")},{"tick",expected.at("tick")},{"equipment_count",equipmentCount},{"authoring_commands_applied",edited},{"usd_edit_layer_applied",usdEdited},{"terrain_display_pages",feature->terrainDisplayPageStats()},{"sky_clock_seconds",view->featureAs<viewport3d::SkyFeature>("rocky_sky")->clockSeconds()},{"cloud_clock_seconds",globe->weather()->seconds}});
        QApplication::quit();
    });
}
void installFormalValidation(SimulationControlWindow& window){
    if(qEnvironmentVariableIsSet("VSGRAYSIM_WORKER_PROBE")) {
        watch(window,"simulation",[&window,last=uint64_t(0),previous=int64_t(0),gap=int64_t(0),serial=qulonglong(0),samples=std::vector<double>{},checkpointKey=std::string(),checkpointHash=std::string()]()mutable{
            const int64_t now=QDateTime::currentMSecsSinceEpoch();if(previous)gap=std::max(gap,now-previous);previous=now;
            auto status=window.runtimeStatus();status["ui_pid"]=::getpid();status["ui_probe_max_gap_ms"]=gap;
            if(!status.value("running",false)&&status.contains("recovery_checkpoint")){const auto path=status.at("recovery_checkpoint").get<std::string>();const auto key=path+":"+std::to_string(status.value("recovery_tick",uint64_t(0)))+":"+std::to_string(status.value("checkpoint_ms",0.));if(key!=checkpointKey){QFile checkpoint(QString::fromStdString(path));if(checkpoint.open(QIODevice::ReadOnly)){checkpointHash=QCryptographicHash::hash(checkpoint.readAll(),QCryptographicHash::Sha256).toHex().toStdString();checkpointKey=key;}}if(key==checkpointKey)status["checkpoint_sha256"]=checkpointHash;}
            auto* view=window.findChild<viewport3d::Viewport3DView*>();bool ready=false;
            if(view&&view->isReady()) {
                auto* voxel=view->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene");auto* earth=view->featureAs<viewport3d::EarthView>(viewport3d::EarthView::kId);
                ready=voxel&&voxel->snapshot()&&voxel->runtime()==nullptr&&earth&&earth->surfaceSnapshot();
                status["viewHasVoxel"]=bool(voxel&&voxel->snapshot());
                status["viewHasTerrain"]=bool(earth&&earth->surfaceSnapshot());
                if(voxel&&voxel->snapshot()) {
                    status["view_tick"]=voxel->snapshot()->tick;
                    status["platform_motion"]=platformMotion(*view);
                }
                auto* widget=view->widget();const auto current=widget->property("renderFrameSerial").toULongLong();
                if(ready&&current!=serial){serial=current;samples.push_back(widget->property("renderFrameMs").toDouble());if(samples.size()>600)samples.erase(samples.begin());}
                if(!samples.empty()){auto sorted=samples;std::sort(sorted.begin(),sorted.end());status["render"]={{"samples",sorted.size()},{"p50_ms",sorted[(sorted.size()-1)/2]},{"p95_ms",sorted[size_t((sorted.size()-1)*.95)]},{"fenced",widget->property("renderFrameFenced").toBool()},{"width",widget->width()},{"height",widget->height()}};}
            }
            if(view)status["voxel_read_error"]=view->property("publishedVoxelReadError").toString().toStdString();
            if (auto* product=window.findChild<QLabel*>("serviceProductValues")) {
                auto* image=window.findChild<QLabel*>("serviceProductImage");
                auto* notice=window.findChild<QLabel*>("serviceCommandNotice");
                status["service_console"]={{"equipment_id",product->property("equipmentId").toUInt()},
                    {"platform_id",product->property("platformId").toUInt()},
                    {"frame_id",product->property("frameId").toString().toStdString()},
                    {"values",product->text().toStdString()},{"image_visible",image&&!image->pixmap().isNull()},
                    {"notice",notice ? notice->text().toStdString() : std::string{}}};
            }
            status["view_ready"]=ready;status["ui_command_seq"]=last;write("probe-simulation.json",status);
            const auto file=root()+"/probe-command-simulation.json";if(!QFileInfo::exists(file))return;
            json command;try{std::ifstream(file.toStdString())>>command;}catch(...){return;}
            const auto seq=command.at("seq").get<uint64_t>();if(seq<=last)return;last=seq;
            const auto action=command.at("action").get<std::string>();
            if(action=="play"||action=="pause")QMetaObject::invokeMethod(&window,"onPlayPause");
            else if(action=="play_to_tick"){if(auto* supervisor=dynamic_cast<SimulationWorkerClient*>(window.findChild<QObject*>("simulation_engine_supervisor")))supervisor->command("play_to:"+std::to_string(command.at("tick").get<uint64_t>()));}
            else if(action=="step")QMetaObject::invokeMethod(&window,"onStepOnce");
            else if(action=="stop")QMetaObject::invokeMethod(&window,"onStop");
            else if(action=="restart")QMetaObject::invokeMethod(&window,"onRestartWorker");
            else if(action=="service_select_equipment") {
                auto* box=window.findChild<QComboBox*>("serviceEquipment");
                require(box,"service equipment selector missing");
                const auto index=box->findData(command.at("equipment_id").get<uint32_t>());
                require(index>=0,"equipment missing from service selector");
                box->setCurrentIndex(index);
            }
            else if(action=="service_course") {
                auto* box=window.findChild<QComboBox*>("servicePlatforms");
                auto* heading=window.findChild<QDoubleSpinBox*>("serviceHeading");
                auto* speed=window.findChild<QDoubleSpinBox*>("serviceSpeed");
                auto* submit=window.findChild<QPushButton*>("serviceSendCourse");
                require(box&&heading&&speed&&submit,"service course controls missing");
                const auto index=box->findData(command.at("platform_id").get<uint32_t>());
                require(index>=0,"platform missing from service selector");
                box->setCurrentIndex(index);
                heading->setValue(command.at("heading_deg").get<double>());
                speed->setValue(command.at("speed_mps").get<double>());
                require(submit->isEnabled(),"course controls disabled");
                submit->click();
            }
            else if(action=="capture")capture(window,QString::fromStdString(command.value("name",std::string("probe-simulation"))));
            else if(action=="reset_metrics"){samples.clear();gap=0;if(auto* supervisor=dynamic_cast<SimulationWorkerClient*>(window.findChild<QObject*>("simulation_engine_supervisor")))supervisor->command("reset_metrics");}
            else if(action=="quit")QApplication::quit();
        });return;
    }
    watch(window,"simulation",[&window,phase=0]()mutable{
        auto status=window.runtimeStatus();status["pid"]=::getpid();if(!status.value("initialized",false))return;
        require(status.value("voxel_source",false),"formal simulation did not select the voxel source");
        require(status.at("source").value("error",std::string()).empty(),"formal voxel source failed");
        auto* view=window.findChild<viewport3d::Viewport3DView*>();if(!view||!view->isReady())return;
        auto* feature=view->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene");if(!feature||!feature->snapshot())return;
        require(feature->runtime()==nullptr,"simulation viewport constructed a duplicate physical world");
        if(phase==0){
            require(status["equipment"].size()==4,"formal source did not register all equipment");
            for(const auto& e:status["equipment"])require(e["frames"]==0,"equipment generated data before display link");
            const auto context=status.value("worker_process",false)?status.at("earth"):json::parse(BrokerNet::instance().lastSimMeta().earth_json);
            if (context.value("terrain_display_paging",false) && feature->terrainDisplayPageStats().empty()) {
                view->markActivity();
                return;
            }
            json archive;std::ifstream(context.at("voxel_scene").at("checkpoint_file").get<std::string>())>>archive;
            const auto project=VoxelSceneProject::open(context.at("source_scenario"));const auto authored=project.createScene()->checkpoint();
            archive["paused"]=authored.at("paused");require(archive==authored,"formal simulation did not restore the saved canonical state");
            status["saved_checkpoint_restored"]=true;
            capture(window,"simulation-ready");status["terrain_display_pages"]=terrainPageStats(window);write("simulation-ready.json",status);phase=1;
        }
        if(phase==1){bool linked=true;for(const auto& e:status["equipment"])linked=linked&&e["subscribed"].get<bool>();if(linked){QMetaObject::invokeMethod(&window,"onPlayPause");phase=2;}}
        if(phase==2&&QFileInfo::exists(root()+"/display-controls.json")){
            QMetaObject::invokeMethod(&window,"onPlayPause");phase=20;return;
        }
        if(phase==20&&!status.value("running",true)&&!status.contains("pending_seq")){capture(window,"simulation-paused");auto paused=window.runtimeStatus();if(!paused.value("worker_process",false))paused["channels"]=channels();paused["ui_pid"]=::getpid();write("simulation-paused.json",paused);phase=3;}
        if(phase==3&&QFileInfo::exists(root()+"/display.json")){write("simulation.json",window.runtimeStatus());QApplication::quit();}
    });
}
void installFormalValidation(display::ControlWorkbench& window){
    if(qEnvironmentVariableIsSet("VSGRAYSIM_WORKER_PROBE")) {
        watch(window,"display",[&window,last=uint64_t(0),serial=qulonglong(0),render=std::vector<double>{}]()mutable{
            auto status=window.connectionStatus();const auto meta=BrokerNet::instance().lastSimMeta();status["online"]=BrokerNet::instance().simOnline();status["running"]=meta.running;status["scene_session_id"]=meta.scene_session_id;status["sim_time"]=meta.sim_time;status["channels"]=channels();status["ui_pid"]=::getpid();status["command_seq"]=last;
            for(auto* view:window.findChildren<viewport3d::Viewport3DView*>())if(view->isVisible()&&view->isReady()){auto* earth=view->featureAs<viewport3d::EarthView>(viewport3d::EarthView::kId);auto* voxel=view->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene");if(earth&&earth->surfaceSnapshot()&&voxel&&voxel->snapshot()){const auto current=view->widget()->property("renderFrameSerial").toULongLong();if(current!=serial){serial=current;render.push_back(view->widget()->property("renderFrameMs").toDouble());if(render.size()>600)render.erase(render.begin());}status["view_ready"]=true;const auto motion=platformMotion(*view);if(!motion.is_null())status["platform_motion"]=motion;}}
            if(!render.empty()){auto sorted=render;std::sort(sorted.begin(),sorted.end());status["render_3d"]={{"samples",sorted.size()},{"p50_ms",sorted[(sorted.size()-1)/2]},{"p95_ms",sorted[size_t((sorted.size()-1)*.95)]},{"fenced",true}};}
            for(auto* label:window.findChildren<QLabel*>("m1a2_display_result"))
                if(label->isVisible()){status["presentation_feedback"]=label->text().toStdString();break;}
            write("probe-display.json",status);
            const auto file=root()+"/probe-command-display.json";if(!QFileInfo::exists(file))return;
            json command;try{std::ifstream(file.toStdString())>>command;}catch(...){return;}const auto seq=command.at("seq").get<uint64_t>();if(seq<=last)return;last=seq;
            const auto action=command.at("action").get<std::string>();
            if(action=="bind"){
                // After a worker generation change the old discovery tree may
                // still be waiting for its periodic refresh. Use the original
                // discovery action before selecting the newly online equipment.
                for(auto* button:window.findChildren<QPushButton*>())if(button->text()==QStringLiteral("发现装备")){button->click();break;}
                auto* tree=window.findChild<QTreeWidget*>("equipment_tree");require(tree,"original equipment tree missing");std::set<uint32_t> found;
                const auto selected=command.value("equipment",std::set<uint32_t>{7101,7102,7103,7104});
                for(int p=0;p<tree->topLevelItemCount();++p)for(int n=0;n<tree->topLevelItem(p)->childCount();++n){auto* row=tree->topLevelItem(p)->child(n);const auto id=row->data(0,Qt::UserRole).toUInt();row->setCheckState(0,selected.count(id)?Qt::Checked:Qt::Unchecked);if(selected.count(id))found.insert(id);}
                require(found.size()==selected.size(),"online equipment not yet discovered");auto* button=window.findChild<QPushButton*>("bind_equipment");require(button,"original link button missing");button->click();window.selectConsole("mission");
            }else if(action=="close_view"||action=="kill_view")window.closeEquipmentView(command.at("equipment_id").get<uint32_t>(),action=="kill_view");
            else if(action=="open_console")window.openPlatformConsole(QString::fromStdString(command.value("mode",std::string("aircraft"))),command.value("platform_id",uint32_t(0)));
            else if(action=="close_consoles")window.closePlatformConsoles();
            else if(action=="open_view")window.openEquipmentView(command.at("equipment_id").get<uint32_t>());
            else if(action=="m1a2_pose") {
                const auto id=command.value("platform",uint32_t(101));
                for(auto* platforms:window.findChildren<QComboBox*>("earth_platform")) {
                    const auto i=platforms->findData(id);
                    if(i>=0&&platforms->isVisible())platforms->setCurrentIndex(i);
                }
                for(auto* apply:window.findChildren<QPushButton*>("m1a2_display_apply")) {
                    if(!apply->isVisible()||!apply->isEnabled())continue;
                    auto* yaw=apply->parentWidget()->findChild<QDoubleSpinBox*>("m1a2_display_yaw");
                    auto* pitch=apply->parentWidget()->findChild<QDoubleSpinBox*>("m1a2_display_pitch");
                    require(yaw&&pitch,"pose fields missing");
                    yaw->setValue(command.at("yaw").get<double>());pitch->setValue(command.at("pitch").get<double>());
                    apply->click();return;
                }
                throw std::runtime_error("M1A2 controls unavailable in visible Earth panels");
            }
            else if(action=="reset_metrics")render.clear();
            else if(action=="detach"){for(auto* button:window.findChildren<QPushButton*>())if(button->text()==QStringLiteral("断开全部装备")){button->click();break;}}
            else if(action=="capture")capture(window,QString::fromStdString(command.value("name",std::string("probe-display"))));
            else if(action=="quit")QApplication::quit();
        });return;
    }
    watch(window,"display",[&window,phase=0,power=uint64_t(0),other=uint64_t(0),settled=int64_t(0)]()mutable{
        auto status=window.connectionStatus();require(status.at("remote_source").get<bool>(),"display owns a local simulation instead of a remote source");
        auto& broker=SimDataBroker::instance();const auto meta=BrokerNet::instance().lastSimMeta();
        if(phase==0){
            if(!meta.online)return;auto* tree=window.findChild<QTreeWidget*>("equipment_tree");if(!tree)return;std::set<uint32_t> found;
            for(int p=0;p<tree->topLevelItemCount();++p)for(int n=0;n<tree->topLevelItem(p)->childCount();++n){auto* row=tree->topLevelItem(p)->child(n);const auto id=row->data(0,Qt::UserRole).toUInt();if(id>=7101&&id<=7104){row->setCheckState(0,Qt::Checked);found.insert(id);}}
            if(found.size()!=4)return;auto* button=window.findChild<QPushButton*>("bind_equipment");require(button,"existing link button unavailable");button->click();window.selectConsole("mission");phase=1;return;
        }
        if(status.at("sessions").size()!=4)return;
        if(phase==1){
            const auto earth = json::parse(meta.earth_json);
            const auto pages = terrainPageStats(window);
            if (earth.value("terrain_display_paging",false) && pages.empty()) {
                for (auto* view : window.findChildren<viewport3d::Viewport3DView*>()) view->markActivity();
                return;
            }
            for(const auto& s:status.at("sessions"))if(!s.at("connected").get<bool>()||!s.at("has_frame").get<bool>()||s.at("frames").get<uint64_t>()<3)return;
            const auto products=channels();for(const auto& p:products){require(p.at("surface_revision")==status.at("scene_revision"),"display accepted a mismatched voxel revision");if(p.at("id")==7104)require(p.at("power_sum").get<double>()>0,"empty remote radar return");else require(p.at("absolute_sum").get<double>()>0,"empty remote imaging product");}
            capture(window,"display-live");
            write("display-linked.json",{{"pid",::getpid()},{"state",status},{"channels",products},
                {"earth",earth},{"terrain_display_pages",terrainPageStats(window)}});
            auto* button=powerButton(window);require(button&&button->isEnabled(),"actual display power button unavailable");button->click();phase=2;return;
        }
        if(phase==2){
            bool off=false;for(const auto& s:status.at("sessions"))if(s["id"]==7101)off=!s["powered"].get<bool>()&&!s["power_pending"].get<bool>();if(!off)return;
            power=broker.channelStats(7101).published_frames;other=broker.channelStats(7102).published_frames;settled=QDateTime::currentMSecsSinceEpoch();phase=3;return;
        }
        if(phase==3){
            if(QDateTime::currentMSecsSinceEpoch()-settled<1000)return;
            require(broker.channelStats(7101).published_frames==power,"powered-off remote camera continued producing products");require(broker.channelStats(7102).published_frames>other,"powering off VIS blocked another device");
            auto* button=powerButton(window);require(button&&button->isEnabled(),"display cannot restore power");button->click();phase=4;return;
        }
        if(phase==4){
            if(broker.channelStats(7101).published_frames<=power)return;
            for(const auto& s:status.at("sessions"))if(s["id"]==7101&&(!s["powered"].get<bool>()||s["power_pending"].get<bool>()))return;
            write("display-controls.json",{{"power_off_stopped_only_selected_channel",true},{"power_on_resumed",true}});phase=5;return;
        }
        if(phase==5&&QFileInfo::exists(root()+"/simulation-paused.json")&&!meta.running){
            // A heartbeat callback and the GUI's 50ms consumer are separate
            // events. Wait for bounded UI convergence, not synchronous delivery.
            phase=6;settled=QDateTime::currentMSecsSinceEpoch();return;
        }
        if(phase==6&&!meta.running){
            if(QDateTime::currentMSecsSinceEpoch()-settled<750)return; // allow the throttled Earth status/next presentation to consume the pause
            bool ready=true;for(const auto& s:status.at("sessions"))ready=ready&&s["has_frame"].get<bool>()&&s["status"].get<std::string>().find("暂停")!=std::string::npos;
            if(!ready){require(QDateTime::currentMSecsSinceEpoch()-settled<5000,"pause discarded a product or was not reflected in the existing session");return;}
            json source;std::ifstream((root()+"/simulation-paused.json").toStdString())>>source;const auto received=channels();
            for(const auto& product:received){const auto& sent=source.at("channels");auto it=std::find_if(sent.begin(),sent.end(),[&](const auto& s){return s.at("id")==product.at("id");});require(it!=sent.end(),"source equipment identity missing");if(product.at("frame_id")!=it->at("frame_id"))return;require(product.at("payload_sha256")==it->at("payload_sha256"),"remote product payload differs from the source after pause");}
            capture(window,"display-paused");write("display.json",{{"pid",::getpid()},{"state",status},{"channels",channels()},{"simulation_paused",true},{"source_payloads_exactly_equal",true}});QApplication::quit();
        }
    });
}
}
