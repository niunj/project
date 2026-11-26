#include <vsgraysim/earth/VoxelEquipmentRuntime.h>
#include "VoxelEquipmentWorkbench.h"
#include "ControlWorkbench.h"
#include "AircraftCockpitPanel.h"
#include "FlightPanel.h"
#include "PlatformInstruments.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/VoxelSceneFeature.h"
#include <vsg/all.h>
#include <QApplication>
#include <vsgraysim/broker/SimDataBroker.h>
#include <chrono>
#include <algorithm>
#include <set>
#include <QDockWidget>
#include <QDir>
#include <QLabel>
#include <QPainter>
#include "viewport3d/VsgQtWidget.h"
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QCheckBox>
#include <QTimer>
#include <QToolBar>
#include <fstream>
#include <iostream>
namespace vsgraysim::gui::display {
int runVoxelEquipmentWorkbench(const QString& path,const QString& captureDirectory,const QString& initialConsole,const QString& cockpitProfile) try {
    using namespace earth;using namespace viewport3d;using json=nlohmann::json;
    auto config=VoxelEquipmentConfig::open(path.toStdString());QStringList ids;for(const auto& s:config.equipment)ids.push_back(QString::number(s.sensor.id));
    ControlWorkbench window("mission",ids,nullptr,false);window.setSharedSceneView(true);window.setWindowTitle(QObject::tr("体素场景 · 光学 / 红外 / 雷达装备显控"));window.resize(1600,1000);
    auto vc=ViewportConfig::fromTopicFile(config.topicPath);
    for(const auto& entry:vc.entries()){
        if(entry.id=="rocky_map")window.setLocalEarthMap(entry.params.at("map_json").get<std::string>());
        if(entry.id=="rocky_cloud"&&entry.params.value("cover",.5)!=0)throw std::invalid_argument("voxel equipment workbench currently requires an explicit clear-sky topic (cloud cover 0)");
        if(entry.id=="rocky_sky"&&entry.params.value("time_scale",1.)!=1)throw std::invalid_argument("equipment sky and acquisition must share simulation UTC (time_scale 1)");
    }
    vc.add("rocky_sky",{{"visibility",config.visibilityM}});vc.add("rocky_cloud",{{"visibility",config.visibilityM}});
    auto* view=new Viewport3DView;window.setSharedViewport(view);
    for(const auto& entry:vc.entries())if(entry.id=="voxel_scene"){auto params=entry.params;params["background_loading"]=false;params["advance_from_frames"]=false;params["paused"]=false;vc.add(entry.id,params);break;}
    view->applyConfig(vc);auto* dock=new QDockWidget(QObject::tr("地球三维场景 · 与装备测量共用体素真值"),&window);dock->setObjectName("voxel_equipment_earth");dock->setWidget(view);window.addDockWidget(Qt::LeftDockWidgetArea,dock);dock->setMinimumSize(470,370);
    auto* tools=window.addToolBar(QObject::tr("体素仿真"));auto* play=tools->addAction(QObject::tr("暂停仿真"));auto* status=new QLabel(QObject::tr("正在载入台北新数据及装备…"));tools->addWidget(status);
    std::unique_ptr<VoxelEquipmentRuntime> runtime;VoxelScene* scene=nullptr;QTimer timer;bool stopped=false;uint64_t ticks=0;double renderingMs=0,advanceMs=0,sensorMs=0;uint64_t samples=0;int exitCode=0;
    QObject::connect(play,&QAction::triggered,&window,[&]{if(!scene)return;scene->setPaused(!scene->paused());play->setText(scene->paused()?QObject::tr("继续仿真"):QObject::tr("暂停仿真"));});
    QObject::connect(view,&Viewport3DView::vsgReady,&window,[&]{try{
        auto* feature=view->featureAs<VoxelSceneFeature>("voxel_scene");if(!feature||!feature->runtime())throw std::runtime_error("voxel scene was not loaded");scene=feature->runtime();
        runtime=std::make_unique<VoxelEquipmentRuntime>(*scene,config,view->context().window()->getDevice());window.setLocalPlatformTelemetry(runtime->platformTelemetry());
        WorkspaceProfile workspace;workspace.id="mission";workspace.title=QObject::tr("大显控 · 场景与多装备");
        for(const auto& s:config.equipment){const auto kind=s.equipment.type==SensorType::VIS?"vis":s.equipment.type==SensorType::RADAR?"radar":s.equipment.type==SensorType::SAR?"sar":"ir";workspace.panels.push_back({"equipment_"+QString::number(s.sensor.id),kind,QString::fromStdString(s.equipment.name)+" · "+QString::fromStdString(s.equipment.description),s.sensor.id,"right"});}
        workspace.panels.push_back({"cockpit","cockpit",QObject::tr("飞机操作台 · 场景遥测"),0,"right"});
        workspace.panels.push_back({"links","links",QObject::tr("独立装备通道与测量时刻"),0,"bottom"});window.applyWorkspace(workspace);window.refreshEquipment();if(initialConsole!="mission"&&!initialConsole.isEmpty())window.selectConsole(initialConsole=="aircraft_cockpit"?"aircraft":initialConsole=="uav_cockpit"?"uav":initialConsole);if(!cockpitProfile.isEmpty()){QString error;if(!window.setCockpitProfile(cockpitProfile,error))throw std::runtime_error(error.toStdString());}timer.start(20);
    }catch(const std::exception& e){stopped=true;exitCode=1;status->setText(QString::fromUtf8(e.what()));std::cerr<<"Voxel equipment: "<<e.what()<<std::endl;if(!captureDirectory.isEmpty())QApplication::exit(1);else QMessageBox::critical(&window,QObject::tr("装备初始化失败"),QString::fromUtf8(e.what()));}});
    QObject::connect(&timer,&QTimer::timeout,&window,[&]{if(!runtime||stopped)return;try{
        const auto start=std::chrono::steady_clock::now();if(!scene->paused())scene->advanceTo(scene->snapshot()->timeSeconds()+scene->snapshot()->fixedStep,1);const auto stepped=std::chrono::steady_clock::now();runtime->advanceMeasurements();window.setLocalPlatformTelemetry(runtime->platformTelemetry());const auto measured=std::chrono::steady_clock::now();window.setLocalSceneState(scene->snapshot()->contentRevision,scene->snapshot()->timeSeconds(),!scene->paused());view->context().setSimulationTime(scene->snapshot()->timeSeconds());view->renderFrame();if(!captureDirectory.isEmpty())view->viewer()->waitForFences(0,UINT64_MAX);const auto rendered=std::chrono::steady_clock::now();
        auto elapsed=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};++ticks;if(ticks>10){advanceMs+=elapsed(start,stepped);sensorMs+=elapsed(stepped,measured);renderingMs+=elapsed(measured,rendered);++samples;}
        status->setText(QObject::tr("t=%1 s  已发布 %2 帧  测量 %3 ms  跨版本拒绝 %4").arg(scene->snapshot()->timeSeconds(),0,'f',2).arg(runtime->statistics().published).arg(runtime->statistics().sensorMs,0,'f',1).arg(runtime->statistics().rejectedWindows));
        if(!captureDirectory.isEmpty()&&ticks==45){timer.stop();QDir().mkpath(captureDirectory);
            std::vector<double> steady;for(int frame=0;frame<45;++frame){const auto begin=std::chrono::steady_clock::now();auto* viewer=view->viewer();if(!viewer->advanceToNextFrame())throw std::runtime_error("Earth viewer stopped during stable-render benchmark");viewer->handleEvents();viewer->update();viewer->recordAndSubmit();viewer->present();viewer->waitForFences(0,UINT64_MAX);if(frame>=10)steady.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());}std::sort(steady.begin(),steady.end());
            const auto earthImage=view->grabFrame();if(earthImage.isNull())throw std::runtime_error("native Vulkan screenshot unavailable");earthImage.save(captureDirectory+"/earth.png");
            json channels=json::array();for(const auto& s:config.equipment){const auto statistics=SimDataBroker::instance().channelStats(s.sensor.id);json channel={{"id",s.sensor.id},{"name",s.equipment.name},{"frames",statistics.published_frames},{"period_seconds",s.sensor.period_seconds}};if(!statistics.published_frames)exitCode=2;
                const auto stem=captureDirectory+"/equipment-"+QString::number(s.sensor.id);std::ofstream payload((stem+".bin").toStdString(),std::ios::binary);
                if(s.equipment.type==SensorType::RADAR){RadarFrame f;if(SimDataBroker::instance().latestRadar(s.sensor.id,&f)){payload.write(reinterpret_cast<const char*>(f.rd_map.data()),std::streamsize(f.rd_map.size()*sizeof(f.rd_map[0])));channel.update({{"width",f.range_bins},{"height",f.doppler_bins},{"dtype","complex64 native little-endian"},{"stage",f.stage},{"sample_rate_hz",f.sample_rate_hz},{"prf_hz",f.prf},{"range_min_m",f.range_min},{"range_max_m",f.range_max},{"pulse_width_s",f.pulse_width},{"bandwidth_hz",f.bandwidth},{"carrier_hz",f.carrier_hz},{"sim_time",f.sim_time},{"surface_revision",f.surface_revision}});
                    if(f.receiver)channel.update({{"first_transmit_s",f.receiver->first_transmit_s},{"receiver_noise_iq_rms",f.receiver->noise_iq_rms}});
                    if(f.ground){
                        channel["ground_samples"]=f.ground->size();
                        if(!f.ground->empty()){
                            const auto [min,max]=std::minmax_element(f.ground->begin(),f.ground->end(),[](const auto& a,const auto& b){return a.slant_range_m<b.slant_range_m;});
                            channel["ground_slant_range_m"]={min->slant_range_m,max->slant_range_m};
                        }
                    }
                }}
                else if(s.equipment.type==SensorType::VIS){VisFrame f;if(SimDataBroker::instance().latestVis(s.sensor.id,&f)){payload.write(reinterpret_cast<const char*>(f.intensity.data()),std::streamsize(f.intensity.size()*sizeof(float)));channel.update({{"width",f.width},{"height",f.height},{"dtype","float32 native little-endian"},{"stage",f.stage},{"quantity",f.stage==0?"visible luminance-weighted radiance W/m2/sr":"normalized detector DN"},{"sim_time",f.sim_time},{"surface_revision",f.surface_revision}});}}
                else if(s.equipment.type==SensorType::SAR){SarFrame f;if(SimDataBroker::instance().latestSar(s.sensor.id,&f)){payload.write(reinterpret_cast<const char*>(f.echo_data.data()),std::streamsize(f.echo_data.size()*sizeof(f.echo_data[0])));channel.update({{"width",f.range_samples},{"height",f.azimuth_samples},{"dtype","complex64 native little-endian"},{"stage",f.stage},{"quantity","unfocused raw I/Q"},{"sample_rate_hz",f.sample_rate_hz},{"prf_hz",f.prf},{"range_min_m",f.range_min},{"sim_time",f.sim_time},{"surface_revision",f.surface_revision}});}}
                else {IrFrame f;if(SimDataBroker::instance().latestIr(s.sensor.id,&f)){payload.write(reinterpret_cast<const char*>(f.radiance.data()),std::streamsize(f.radiance.size()*sizeof(float)));channel.update({{"width",f.width},{"height",f.height},{"dtype","float32 native little-endian"},{"stage",f.stage},{"quantity",f.stage==0?"band-integrated radiance W/m2/sr":"normalized detector DN"},{"wavelength_min_um",f.wavelength_min},{"wavelength_max_um",f.wavelength_max},{"sim_time",f.sim_time},{"surface_revision",f.surface_revision}});}}
                channels.push_back(channel);std::ofstream meta((stem+".json").toStdString());meta<<channel.dump(2);
                if(auto* panel=window.findChild<QDockWidget*>("panel_equipment_"+QString::number(s.sensor.id))){
                    panel->raise();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);panel->grab().save(stem+".png");
                    if(auto* tabs=panel->findChild<QTabWidget*>("radarMapTabs")){
                        if(channel.value("ground_samples",size_t(0))>0){
                            auto* layer=panel->findChild<QCheckBox*>("radarPpiTerrainEnabled");
                            QEventLoop settle;QTimer::singleShot(150,&settle,&QEventLoop::quit);settle.exec(QEventLoop::ExcludeUserInputEvents);
                            panel->grab().save(stem+"-ground-on.png");
                            layer->setChecked(false);panel->grab().save(stem+"-ground-off.png");layer->setChecked(true);
                        }
                        const auto* terrain=tabs->findChild<QWidget*>("radarTerrainMap");
                        tabs->setCurrentIndex(1);
                        QEventLoop wait;
                        QTimer poll;
                        QObject::connect(&poll,&QTimer::timeout,&wait,[&]{if(terrain&&terrain->property("terrainReady").toBool())wait.quit();});
                        poll.start(50);
                        QTimer::singleShot(14000,&wait,&QEventLoop::quit);
                        wait.exec(QEventLoop::ExcludeUserInputEvents);
                        if(terrain&&terrain->property("terrainReady").toBool())panel->grab().save(stem+"-terrain.png");
                        else std::cerr<<"Radar terrain screenshot unavailable: "<<(terrain?terrain->property("terrainState").toString().toStdString():"no terrain view")<<'\n';
                        tabs->setCurrentIndex(0);
                        if(auto* zoom=panel->findChild<QCheckBox*>("radarNoiseZoom");zoom&&zoom->isVisible()){
                            zoom->setChecked(true);
                            QEventLoop refreshNoise;QTimer::singleShot(220,&refreshNoise,&QEventLoop::quit);
                            refreshNoise.exec(QEventLoop::ExcludeUserInputEvents);
                            panel->grab().save(stem+"-noise.png");zoom->setChecked(false);
                        }
                    }
                }
            }
            scene->setPaused(true);play->setText(QObject::tr("继续仿真"));window.setLocalSceneState(scene->snapshot()->contentRevision,scene->snapshot()->timeSeconds(),false);
            json consoleChecks=json::array();
            for(const QString mode:{"aircraft","radar","infrared","optical","mission"}){
                window.selectConsole(mode);QEventLoop refreshConsole;QTimer::singleShot(600,&refreshConsole,&QEventLoop::quit);refreshConsole.exec(QEventLoop::ExcludeUserInputEvents);
                for(const auto& spec:config.equipment)if(!SimDataBroker::instance().isSubscribed(spec.sensor.id))throw std::runtime_error("console switch lost equipment subscription");
                if(mode=="aircraft"&&!config.platforms.empty()){AircraftCockpitPanel* cockpit=nullptr;for(auto* child:window.findChildren<QWidget*>())if(auto* panel=dynamic_cast<AircraftCockpitPanel*>(child))cockpit=panel;if(!cockpit||cockpit->controls()->selectedTelemetry().empty()||!cockpit->usesSharedSceneView())throw std::runtime_error("aircraft console lost actual voxel telemetry/shared Earth view");}
                if(mode=="radar"||mode=="infrared"||mode=="optical"){
                    const auto kind=mode=="radar"?"radar":mode=="infrared"?"ir":"vis";const auto type=mode=="radar"?SensorType::RADAR:mode=="infrared"?SensorType::IR:SensorType::VIS;std::set<uint32_t> wanted,shown;
                    for(const auto& device:config.equipment)if(device.equipment.type==type||(mode=="radar"&&device.equipment.type==SensorType::SAR))wanted.insert(device.sensor.id);
                    for(const auto& panel:window.workspaceProfile().panels)if(panel.kind==kind||(mode=="radar"&&panel.kind=="sar"))shown.insert(panel.equipment_id);
                    if(!wanted.empty()&&wanted!=shown)throw std::runtime_error("console did not assemble a separate panel for each matching equipment");
                }
                const auto frame=view->grabFrame();if(frame.isNull())throw std::runtime_error("shared Vulkan view disappeared during console switch");auto screen=window.grab();{QPainter painter(&screen);const auto point=view->widget()->mapTo(&window,QPoint{});painter.drawImage(QRect(point,view->widget()->size()),frame);}screen.save(captureDirectory+"/console-"+mode+".png");
                consoleChecks.push_back({{"console",mode.toStdString()},{"retained_subscriptions",config.equipment.size()},{"shared_scene_revision",scene->snapshot()->contentRevision}});
            }
            QEventLoop refresh;QTimer::singleShot(550,&refresh,&QEventLoop::quit);refresh.exec(QEventLoop::ExcludeUserInputEvents);
            // QWidget backing-store grabs cannot read a native Vulkan child.
            // Place its actual fenced Vulkan readback at its unchanged widget location.
            const auto finalFrame=view->grabFrame();auto screenshot=window.grab();{QPainter painter(&screenshot);const auto pos=view->widget()->mapTo(&window,QPoint{});painter.drawImage(QRect(pos,view->widget()->size()),finalFrame);}screenshot.save(captureDirectory+"/workbench.png");
            std::ofstream f((captureDirectory+"/report.json").toStdString());f<<json({{"consoles",consoleChecks},{"platform_telemetry",runtime->platformTelemetry()},{"steady_render_median_ms",steady[steady.size()/2]},{"steady_render_p95_ms",steady[size_t(.95*(steady.size()-1))]},{"steady_render_samples",steady.size()},{"channels",channels},{"simulation_seconds",scene->snapshot()->timeSeconds()},{"content_revision",scene->snapshot()->contentRevision},{"render_submit_wait_ms",renderingMs/std::max(uint64_t(1),samples)},{"scene_advance_ms",advanceMs/std::max(uint64_t(1),samples)},{"sensor_generation_ms",sensorMs/std::max(uint64_t(1),samples)},{"complete_tick_ms",(renderingMs+advanceMs+sensorMs)/std::max(uint64_t(1),samples)},{"screenshot","Qt panels plus actual Vulkan child readback, no synthetic sensor images"},{"scope","clear-sky land scene; fixed configured apertures; radar relative scatter, not measured absolute RCS"}}).dump(2);f.close();QApplication::exit(exitCode);}

    }catch(const std::exception& e){timer.stop();stopped=true;exitCode=1;status->setText(QString::fromUtf8(e.what()));std::cerr<<"Voxel equipment: "<<e.what()<<std::endl;if(!captureDirectory.isEmpty())QApplication::exit(1);else QMessageBox::critical(&window,QObject::tr("装备仿真暂停"),QString::fromUtf8(e.what()));}});
    window.show();int rc=QApplication::exec();timer.stop();runtime.reset();return rc;
}catch(const std::exception& e){if(!captureDirectory.isEmpty()){std::cerr<<e.what()<<std::endl;return 1;}QMessageBox::critical(nullptr,QObject::tr("无法打开体素装备显控"),QString::fromUtf8(e.what()));return 1;}
}
