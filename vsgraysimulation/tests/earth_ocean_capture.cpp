#include "earth_ocean_capture.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/ViewportConfig.h"
#include "viewport3d/EarthViewFeature.h"
#include "viewport3d/SensorFovFeature.h"
#include "viewport3d/VoxelSceneFeature.h"
#include "viewport3d/EnvFeatures.h"
#include "viewport3d/VsgQtWidget.h"
#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include <vsgraysim/broker/SimDataBroker.h>
#include <vsgraysim/earth/ScenePresentation.h>
#include <vsgraysim/earth/VoxelOcean.h>
#pragma push_macro("slots")
#undef slots
#include <vsgraysim/earth/VoxelEquipmentRuntime.h>
#pragma pop_macro("slots")
#include <simcore/Orientation.h>
#include <vsgraysim/earth/VoxelOceanOptics.h>
#include <simcore/GeoFrame.h>
#include <vsg/all.h>
#include <QApplication>
#include <QMainWindow>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QThread>
#include <QShortcut>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include "earth_naval_pass.h"
#include "earth_topic_pass.h"
#include "earth_ocean_weather_pass.h"
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void pump(int ms){QElapsedTimer t;t.start();while(t.elapsed()<ms){QApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}}
}
void runEarthOceanCapture(const QString& output,bool interactive){
 using namespace vsgraysim;using namespace vsgraysim::earth;using namespace vsgraysim::viewport3d;using json=nlohmann::json;
 check(QApplication::platformName()=="xcb","ocean acceptance needs a native Vulkan surface");const auto directory=std::filesystem::absolute(output.isEmpty()?"build/voxel-ocean/interactive":output.toStdString());std::filesystem::create_directories(directory);
 const bool coastal=std::getenv("VSGRAYSIM_OCEAN_ISOLATED")==nullptr;const char* customScene=std::getenv("VSGRAYSIM_OCEAN_SCENE");const char* topicPath=std::getenv("VSGRAYSIM_SCENE_TOPIC");
 const bool equipmentCheck=std::getenv("VSGRAYSIM_EQUIPMENT_VISUAL_CHECK")!=nullptr;
 const bool civilCheck=std::getenv("VSGRAYSIM_CIVIL_MODEL_CHECK")!=nullptr;
 const bool missionCheck=std::getenv("VSGRAYSIM_FALCON_SWOT_CHECK")!=nullptr;
 const bool audioCheck=std::getenv("VSGRAYSIM_SCENE_AUDIO_CHECK")!=nullptr;
 const bool oceanAudioCheck=std::getenv("VSGRAYSIM_OCEAN_ENV_AUDIO_CHECK")!=nullptr;
 const bool surveyCheck=std::getenv("VSGRAYSIM_UNDERWATER_SURVEY_CHECK")!=nullptr;
 const bool acousticsCheck=std::getenv("VSGRAYSIM_ACOUSTIC_FLYOVER_CHECK")!=nullptr;
 const bool mountsCheck=std::getenv("VSGRAYSIM_NAMED_SOUND_MOUNTS_CHECK")!=nullptr;
 if(mountsCheck)check(topicPath,"named sound mounts require the acceptance topic");
 if(equipmentCheck&&!topicPath)topicPath="config/topics/taipei-ship-visual-preview.vtopic.json";
 if(civilCheck&&!topicPath)topicPath="config/topics/taipei-civil-aviation.vtopic.json";
 if(missionCheck&&!topicPath)topicPath="config/topics/taipei-falcon9-swot.vtopic.json";
 if(audioCheck&&!topicPath)topicPath="config/topics/taipei-falcon9-swot.vtopic.json";
 if(surveyCheck&&!topicPath)topicPath="config/topics/taipei-underwater-survey.vtopic.json";
 if(acousticsCheck&&!topicPath)topicPath="config/topics/taipei-acoustic-flyover.vtopic.json";
 auto config=topicPath?ViewportConfig::fromTopicFile(topicPath):ViewportConfig::fromPreset("earth_display");
 if(!topicPath&&(interactive||oceanAudioCheck))config.add("scene_audio");
 std::string scenePath=customScene?customScene:coastal?"config/earth/taipei-coast-ocean.vscene.json":"config/earth/taipei-ocean.vscene.json";if(topicPath)for(const auto& entry:config.entries())if(entry.id==VoxelSceneFeature::kId)scenePath=entry.params.at("scene_json");
 json declaration;std::ifstream(scenePath)>>declaration;const auto& geo=declaration.at("geo_anchor");simcore::geo::GeoAnchor anchor;anchor.lat_deg=geo.at("lat");anchor.lon_deg=geo.at("lon");anchor.alt_m=geo.at("alt");anchor.heading_deg=geo.value("heading_deg",0.);const auto chart=simcore::geo::makeEnuToWorldMatrix(anchor);

 QMainWindow window;window.setFixedSize(960,540);window.setWindowTitle(QStringLiteral("体素海洋 — 1 水面 / 2 水线 / 3 水下 / 4 斯涅尔窗口 / 5 地球海岸 / 空格 暂停"));auto* view=new Viewport3DView(&window);window.setCentralWidget(view);bool ready=false;QString error;
 QObject::connect(view,&Viewport3DView::vsgReady,&window,[&]{ready=true;view->setAutoFrame(false);});QObject::connect(view->widget(),&gui::VsgQtWidget::initializationFailed,&window,[&](const QString& s){error=s;});
 if(!topicPath){config.remove(OceanFeature::kId);if(!std::getenv("VSGRAYSIM_OCEAN_WEATHER_PASS"))config.remove(CloudFeature::kId);else config.add(CloudFeature::kId,{{"cover",.65},{"volume",true},{"cumulus_count",36},{"cumulus_altitude",1200},{"stratus_altitude",1800},{"wind_speed",6},{"wind_az",70}});}if(!topicPath)config.add(EarthView::kId,{{"map_json",std::filesystem::absolute("config/earth/default.map.json").string()},{"lat",anchor.lat_deg},{"lon",anchor.lon_deg},{"alt",anchor.alt_m},{"view_height",400},{"cloud_objects",true},{"scene_context",coastal}});config.add(VoxelSceneFeature::kId,{{"scene_json",scenePath},{"background_loading",bool(topicPath)},{"paused",true},{"advance_from_frames",false},{"frame_scene",false}});view->applyConfig(config);window.show();QElapsedTimer deadline;deadline.start();while(!ready&&error.isEmpty()&&deadline.elapsed()<45000)pump(10);check(ready&&error.isEmpty(),"ocean viewport failed to initialise");
 auto* feature=view->featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);size_t preparationFrames=0;QElapsedTimer preparing;preparing.start();while(!feature->runtime()&&feature->lastError().empty()&&preparing.elapsed()<600000){const auto before=view->context().viewer()->getFrameStamp()->frameCount;view->markActivity();view->renderFrame();if(view->context().viewer()->getFrameStamp()->frameCount!=before)++preparationFrames;pump(5);}view->setProperty("topicPreparationMs",double(preparing.elapsed()));view->setProperty("topicPreparationFrames",qulonglong(preparationFrames));if(!feature->runtime())throw std::runtime_error("topic preparation failed: "+feature->lastError());auto& scene=*feature->runtime();const auto initial=scene.snapshot();const bool naval=initial->objects.count("vessel")&&(initial->objects.at("vessel").localBounds().max.z-initial->objects.at("vessel").localBounds().min.z)>80.;view->setAutoFrame(false);view->setIdleSuspend(false);
 auto look=view->context().camera()->viewMatrix.cast<vsg::LookAt>();auto perspective=view->context().camera()->projectionMatrix.cast<vsg::Perspective>();perspective->fieldOfViewY=53.;
 auto camera=[&](vsg::dvec3 eye,vsg::dvec3 target){look->eye=chart*eye;look->center=chart*target;const auto up=chart*vsg::dvec4(0,1,0,0);look->up={up.x,up.y,up.z};};if(naval)camera({160,58,200},{0,10,0});else if(initial->objects.count("vessel"))camera({22,7,24},{0,.3,0});else camera({6500,2200,12500},{1000,30,5000});
 double lastNativeRenderMs=0;auto render=[&](){view->setAutoFrame(false);view->markActivity();view->context().setSimulationTime(scene.snapshot()->timeSeconds());const auto first=view->context().viewer()->getFrameStamp()->frameCount;QElapsedTimer timer;timer.start();QElapsedTimer limit;limit.start();while(limit.elapsed()<(first<=1?120000:(scene.snapshot()->tick==0?30000:3000))){QElapsedTimer native;native.start();view->renderFrame();view->context().viewer()->deviceWaitIdle();const double nativeMs=native.nsecsElapsed()/1e6;const double ms=timer.nsecsElapsed()/1e6;check(feature->lastError().empty(),"voxel ocean feature paused after an exception");if(view->context().viewer()->getFrameStamp()->frameCount!=first){lastNativeRenderMs=nativeMs;return ms;}QThread::msleep(1);}throw std::runtime_error("ocean native render deadline");};
 auto capture=[&](const std::string& name){render();QImage image;QElapsedTimer t;t.start();while(image.isNull()&&t.elapsed()<8000){pump(10);QThread::msleep(40);view->markActivity();image=view->grabFrame();}if(image.isNull())throw std::runtime_error("ocean native framebuffer unavailable: "+view->widget()->property("frameCaptureStatus").toString().toStdString());check(image.save(QString::fromStdString((directory/(name+".png")).string())),"ocean screenshot failed");};
 if(interactive){
  bool play=true;QElapsedTimer clock;clock.start();double previous=0,targetSeconds=scene.snapshot()->timeSeconds();QTimer timer;
  auto pose=[&](int i){if(naval&&i<5){if(i==1)camera({160,58,200},{0,10,0});if(i==2)camera({40,.3,70},{0,2,20});if(i==3)camera({35,-4,35},{0,-4,0});if(i==4)camera({35,-3,35},{35,7,34.9});return;}if(i==1)camera({22,7,24},{0,.3,0});if(i==2)camera({10,.15,14},{0,-.3,0});if(i==3)camera({8,-5,10},{-5,-5,-3});if(i==4)camera({10,-3,10},{10,7,9.9});if(i==5)camera({6500,2200,12500},{1000,30,5000});};
  for(int i=1;i<=5;++i){auto* shortcut=new QShortcut(QKeySequence(QString::number(i)),&window);QObject::connect(shortcut,&QShortcut::activated,&window,[&,i]{pose(i);});}auto* pause=new QShortcut(QKeySequence(Qt::Key_Space),&window);QObject::connect(pause,&QShortcut::activated,&window,[&]{play=!play;});
  int seaState=1,timeOfDay=0;const double heights[3]={.15,.65,2.1},winds[3]={2.,6.,14.};
  auto oceanEdit=[&](json ocean){scene.setPaused(true);scene.edit(json::array({{{"kind","ocean"},{"ocean",std::move(ocean)}}}),scene.snapshot()->revision);};
  auto* sea=new QShortcut(QKeySequence("6"),&window);QObject::connect(sea,&QShortcut::activated,&window,[&]{seaState=(seaState+1)%3;auto o=scene.snapshot()->ocean->encode();o["wave_height_m"]=heights[seaState];o["wave_length_m"]=seaState==2?42:24;o["wind_speed_mps"]=winds[seaState];o.erase("harmonics");oceanEdit(o);});
  auto* fog=new QShortcut(QKeySequence("7"),&window);QObject::connect(fog,&QShortcut::activated,&window,[&]{auto o=scene.snapshot()->ocean->encode();o["sea_fog"]={{"visibility_m",scene.snapshot()->ocean->fogVisibility>0?0:1600},{"height_m",45}};oceanEdit(o);});
  auto* utc=new QShortcut(QKeySequence("8"),&window);QObject::connect(utc,&QShortcut::activated,&window,[&]{timeOfDay=(timeOfDay+1)%3;if(auto* sky=view->featureAs<SkyFeature>(SkyFeature::kId))sky->configure({{"year",2026},{"month",9},{"day",26},{"hours_utc",timeOfDay==0?4.:timeOfDay==1?9.5:16.}});});
  int detailIndex=0;auto* details=new QShortcut(QKeySequence("9"),&window);QObject::connect(details,&QShortcut::activated,&window,[&]{if(!naval)return;const vsg::dvec3 eyes[4]={{32,17,-87},{27,25,-12},{24,41,-12},{28,23,75}},targets[4]={{0,7,-60},{0,20,-12},{0,34,-12},{0,10,52}};const auto state=scene.snapshot();const auto matrix=state->objects.at("vessel").pose(state->tick,state->fixedStep);camera(matrix*eyes[detailIndex],matrix*targets[detailIndex]);detailIndex=(detailIndex+1)%4;});
  auto* widget=view->widget();
  widget->setProperty("sceneAudioRequested",true);
  auto* audio=new QShortcut(QKeySequence("M"),&window);
  QObject::connect(audio,&QShortcut::activated,&window,[widget]{
   widget->setProperty("sceneAudioRequested",!widget->property("sceneAudioRequested").toBool());
  });
  window.setWindowTitle(QStringLiteral("海岸海洋 — 1–5 机位 / 6 海况 / 7 海雾 / 8 日夜 / 9 舰体细节 / M 音效 / 空格 暂停"));
  QObject::connect(&timer,&QTimer::timeout,&window,[&]{const double now=clock.elapsed()*.001,dt=now-previous;previous=now;scene.setPaused(!play);if(play){targetSeconds+=dt;scene.advanceTo(targetSeconds,8);}render();});timer.start(20);QApplication::exec();return;
 }
 if(oceanAudioCheck){
  auto* widget=view->widget();
  widget->setProperty("sceneAudioRequested",true);
  scene.setPaused(false);
  const auto listen=[&](const char* medium,vsg::dvec3 eye){
   camera(eye,{0,eye.y,-10});
   QElapsedTimer clock;clock.start();
   const double start=scene.snapshot()->timeSeconds();
   while(clock.elapsed()<4500){
    scene.advanceTo(start+.5+clock.elapsed()*.001,16);
    render();pump(24);
    const auto value=json::parse(widget->property("sceneAcousticsReading").toString().toStdString(),nullptr,false);
    if(!value.is_object()||value.value("medium",std::string())!=medium||!value.at("spl_db").is_number())continue;
    const bool haveSea=std::any_of(value.at("paths").begin(),value.at("paths").end(),[](const auto& entry){
     return entry.at("id").template get<std::string>().rfind("sea:",0)==0;
    });
    if(haveSea&&widget->property("sceneAudioOutputActive").toBool())return value;
   }
   throw std::runtime_error(std::string("native ocean audio did not reach the ")+medium+" listener");
  };
  const auto air=listen("air",{9.,1.6,-10.});
  capture("audio-air");
  const auto water=listen("water",{9.,-4.8,-10.});
  capture("audio-water");
  scene.setPaused(true);
  for(int i=0;i<6;++i){render();pump(25);}
  check(!widget->property("sceneAudioOutputActive").toBool(),"ocean PCM output did not mute on pause");
  std::ofstream(directory/"native-ocean-audio.json")<<json{{"air",air},{"water",water},{"pause_muted",true}}.dump(2)<<'\n';
  std::cout<<"PASS: native ocean stereo, listener media and pause\n";
  window.hide();pump(20);return;
 }
 if(mountsCheck){
  auto* widget=view->widget();
  widget->setProperty("sceneAudioRequested",true);
  widget->setProperty("sceneAcousticProbe",QStringLiteral("mount-check"));
  camera({-25.,20.,-27.},{2.,25.,-1.});
  const auto project=VoxelSceneProject::open(topicPath);
  VoxelEquipmentRuntime sensors(scene,project.equipmentConfig(),view->context().window()->getDevice());
  auto& broker=SimDataBroker::instance();
  check(broker.subscribe(887),"could not subscribe actual optics fixture");
  const vsg::dvec3 listener={4.,9.,-12.};
  const auto vec=[](const json& value){const auto p=value.get<std::array<double,3>>();return vsg::dvec3{p[0],p[1],p[2]};};
  const auto coords=[](const vsg::dvec3& p){return json::array({p.x,p.y,p.z});};
  const std::array<std::string,4> names{"platform:880/engine","platform:883/main_rotor","platform:883/tail_rotor","equipment:887/fan"};
  std::vector<std::array<vsg::dvec3,4>> recorded;
  json paths=json::object(),impact,contactOnset,heardImpact;
  std::map<uint64_t,std::vector<vsg::dvec3>> contacts;
  std::map<std::string,double> impulses;
  std::array<unsigned,4> verified{};
  std::array<double,4> errors{};
  bool off=false,fanSilent=false,device=false;double last=-1.;unsigned readingCount=0;
  scene.setPaused(false);
  QElapsedTimer clock;clock.start();
  while(scene.snapshot()->timeSeconds()<3.30&&clock.elapsed()<45000){
   const auto target=std::min(3.30,clock.elapsed()*.001);
   while(scene.snapshot()->timeSeconds()+1e-8<target){
    const auto state=scene.snapshot();
    const auto pose=[&](const char* name){return state->objects.at(name).pose(state->tick,state->fixedStep);};
    if(recorded.size()==state->tick)recorded.push_back({pose("flyover")*vsg::dvec3{0.,0.,.6},
        pose("main-rotor")*vsg::dvec3{},pose("tail-rotor")*vsg::dvec3{},pose("body")*vsg::dvec3{.72,-.14,1.975}});
    scene.advanceTo(state->timeSeconds()+state->fixedStep,1);
    const auto current=scene.snapshot();
    for(const auto& c:current->contacts){
     if(contactOnset.is_null()&&(c.a=="crate"||c.b=="crate")){
      const auto equip=voxelEquipmentStateTelemetry(sensors.config(),*current);
      const auto emission=scenePresentation(sensors.config(),*current,equip,state.get());
      contactOnset={{"tick",current->tick},{"objects",{c.a,c.b}},{"normal_on_b",coords(c.normalOnB)},
        {"point_a_m",coords(c.pointA)},{"point_b_m",coords(c.pointB)},
        {"before_velocity_mps",coords(state->objects.at("crate").velocity)},
        {"after_velocity_mps",coords(current->objects.at("crate").velocity)},{"events",emission.at("events")}};
     }
     if(c.a=="crate"){
      impulses[c.b]=std::max(impulses[c.b],c.impulseNs);
      if(c.b=="contact-pad"||c.b=="taipei-terrain")contacts[current->tick].push_back(c.pointB);
     }else if(c.b=="crate"){
      impulses[c.a]=std::max(impulses[c.a],c.impulseNs);
      if(c.a=="contact-pad"||c.a=="taipei-terrain")contacts[current->tick].push_back(c.pointA);
     }
    }
    if(!off&&current->timeSeconds()>=2.25){
     ControlCommand command;command.type=ControlCommand::Type::PowerOff;command.equipment_id=887;command.equipment_type=SensorType::VIS;
     broker.sendControl(command);off=true;
    }
    sensors.advanceMeasurements();
   }
   render();pump(10);
   device=device||widget->property("sceneAudioOutputActive").toBool();
   const auto row=json::parse(widget->property("sceneAcousticsReading").toString().toStdString(),nullptr,false);
   if(!row.is_object()||row.at("sample_s").get<double>()<=last||!row.at("spl_db").is_number())continue;
   last=row.at("sample_s");++readingCount;
   const auto spl=row.at("spl_db").get<double>();
   check(std::abs(20e-6*std::pow(10.,spl/20.)-row.at("pressure_rms_pa").get<double>())<1e-8,"meter SPL did not match its received pressure");
   bool haveFan=false;
   for(const auto& path:row.at("paths")){
    if(path.at("reflected").get<bool>())continue;
    const auto key=path.at("id").get<std::string>();
    const auto origin=vec(path.at("source_position_m"));
    check(std::abs(vsg::length(origin-listener)-path.at("path_length_m").get<double>())<1e-5,"measured path did not start at the reported origin");
    const auto t=path.at("emission_s").get<double>();
    const auto part=std::find(names.begin(),names.end(),key);
    if(part!=names.end()){
     const auto index=std::size_t(part-names.begin());
     const auto frame=t/initial->fixedStep;
     const auto tick=static_cast<size_t>(frame);
     if(tick+1>=recorded.size())continue;
     const auto fraction=frame-tick;
     const auto reflectedPose=recorded[tick][index]*(1.-fraction)+recorded[tick+1][index]*fraction;
     const auto distance=vsg::length(origin-reflectedPose);
     check(distance<.08,"sound propagation did not use the model mount at emission time");
     errors[index]=std::max(errors[index],distance);++verified[index];paths[key]=path;
     if(index==3)haveFan=true;
    }else if(key.rfind("impact:",0)==0){
     heardImpact=path;
     const auto tick=uint64_t(std::llround(t/initial->fixedStep));
     const auto it=contacts.find(tick);
     if(it==contacts.end())continue;
     for(const auto& point:it->second)if(vsg::length(origin-point)<1e-7){
      impact=path;impact["bullet_contact_m"]=coords(point);impact["contact_tick"]=tick;break;
     }
    }
   }
   if(last>=2.65&&!haveFan)fanSilent=true;
  }
  const auto stats=sensors.statistics();
  auto report=json{{"verified_path_counts",verified},{"max_mount_position_error_m",errors},{"path_order",names},
    {"paths",paths},{"impact",impact},{"working_vis_frames",broker.channelStats(887).published_frames},
    {"rejected_sensor_windows",stats.rejectedWindows},{"reading_count",readingCount},{"collision_ticks",contacts.size()},
    {"crate_contact_max_impulse_ns",impulses},{"contact_onset",contactOnset},{"last_impact_path",heardImpact},
    {"crate_position_final_m",coords(scene.snapshot()->objects.at("crate").pose(scene.snapshot()->tick,scene.snapshot()->fixedStep)*vsg::dvec3{})},
    {"fan_silent_after_power_off",fanSilent},{"playback_active",device}};
  std::ofstream(directory/"named-mounts-validation.json")<<report.dump(2)<<'\n';
  check(scene.snapshot()->timeSeconds()>=3.30&&readingCount>20&&device,"native named-mount scene did not produce audio");
  check(std::all_of(verified.begin(),verified.end(),[](auto n){return n>5;})&&fanSilent&&off,"named sources or fan power transition missing");
  check(!impact.is_null()&&broker.channelStats(887).published_frames>0,"actual bullet collision or GPU equipment frames were not captured");
  scene.setPaused(true);
  for(int i=0;i<4;++i){render();pump(25);}
  check(!widget->property("sceneAudioOutputActive").toBool(),"named acoustics did not mute during pause");
  camera({-23.,24.,-26.},{2.,22.,-3.});capture("named-mount-airframes");
  camera({2.,24.,-23.},{10.,15.,-9.});capture("actual-contact-scene");
  report["pause_muted"]=true;
  std::ofstream(directory/"named-mounts-validation.json")<<report.dump(2)<<'\n';
  window.hide();pump(20);return;
 }
 if(acousticsCheck) {
  const QString clear=QStringLiteral("无遮拦"),screened=QStringLiteral("屏障后");
  auto* widget=view->widget();
  widget->setProperty("sceneAudioRequested",true);
  widget->setProperty("sceneAcousticProbe",clear);
  camera({-62.,8.,-10.},{-5.,30.,12.});
  scene.setPaused(false);
  const auto reading=[&](){return json::parse(widget->property("sceneAcousticsReading").toString().toStdString(),nullptr,false);};
  json series=json::array();double last=-1.;bool device=false,approach=false,recede=false;
  double closest=0.;QElapsedTimer wall;wall.start();
  while(scene.snapshot()->timeSeconds()<12.20&&wall.elapsed()<45000){
   const double target=std::min(12.20,wall.elapsed()*.001);
   scene.advanceTo(target,24);
   const double time=scene.snapshot()->timeSeconds();
   if(time>=5.96&&time<6.15&&closest==0.){
    camera({0.,9.,-23.},{0.,70.,20.});
    capture("x8-flyover-ground");closest=time;
   }
   render();pump(12);
   device=device||widget->property("sceneAudioOutputActive").toBool();
   auto r=reading();
   if(!r.is_object()||r.at("sample_s").get<double>()<=last)continue;
   last=r.at("sample_s");
   for(const auto& path:r.at("paths"))if(path.at("id")=="acoustic:x8-engine"){
    const double factor=path.at("doppler");approach|=factor>1.025;recede|=factor<.975;
   }
   series.push_back(std::move(r));
  }
  check(scene.snapshot()->timeSeconds()>=12.18&&series.size()>40&&approach&&recede,"flight acoustics did not resolve both sides of flyover");
  check(device,"native flyover did not start PCM output");
  scene.setPaused(true);
  const auto settled=[&](const QString& probe){
   widget->setProperty("sceneAcousticProbe",probe);
   QElapsedTimer wait;wait.start();
   while(wait.elapsed()<3000){
    render();pump(25);const auto r=reading();
    if(r.is_object()&&r.value("probe",std::string())==probe.toStdString()&&r.at("sample_s")==scene.snapshot()->timeSeconds())return r;
   }
   throw std::runtime_error("native acoustic probe did not settle");
  };
  const auto clean=settled(clear),behind=settled(screened);
  const auto speaker=[](const json& r){for(const auto& path:r.at("paths"))if(path.at("id")=="acoustic:reference-speaker")return path;throw std::runtime_error("no speaker path");};
  const auto direct=speaker(clean),occluded=speaker(behind);
  check(!direct.at("screened").get<bool>()&&occluded.at("screened").get<bool>()&&
    direct.at("spl_db").get<double>()>occluded.at("spl_db").get<double>()+8.,"real scene barrier did not reduce speaker level");
  check(!widget->property("sceneAudioOutputActive").toBool(),"pause did not stop physical scene playback");
  camera({-55.,24.,-64.},{20.,5.,10.});capture("fixed-source-and-barrier");
  std::ofstream(directory/"acoustics-validation.json")<<json{{"clear",clean},{"screened",behind},
   {"speaker_clear",direct},{"speaker_screened",occluded},{"playback_active",device},{"pause_muted",true},
   {"flyover_capture_s",closest},{"flyover",series}}.dump(2)<<'\n';
  window.hide();pump(20);return;
 }
 if(surveyCheck) {
  scene.setPaused(false);
  check(!scene.advanceTo(18.,1000).pendingSteps,"underwater scene did not reach sampling tick");
  scene.setPaused(true);
  const auto state=scene.snapshot();
  const auto target=state->objects.at("alvin").pose(state->tick,state->fixedStep)*vsg::dvec3{};
  camera(target+vsg::dvec3{-7.2,1.6,2.7},target);
  capture("alvin-underwater");
  std::ofstream(directory/"underwater.json")<<json{{"tick",state->tick},{"seconds",state->timeSeconds()},
      {"object","alvin"},{"camera_offset_m",{-7.2,1.6,2.7}},{"occupied_voxels",state->objects.at("alvin").volume->occupiedCount()},
      {"depth_reference","synthetic demonstration seabed"}}.dump(2)<<'\n';
  window.hide();pump(20);return;
 }
 if(audioCheck) {
  view->widget()->setProperty("sceneAudioRequested",true);
  camera({90,54,93},{0,52,0});
  scene.setPaused(false);
  check(!scene.advanceTo(3.25,100).pendingSteps,"audio capture did not reach liftoff");
  QElapsedTimer wait;wait.start();
  while(!view->widget()->property("sceneAudioOutputActive").toBool()&&wait.elapsed()<4200){
   scene.advanceTo(3.25+wait.elapsed()*.001,16);
   render();pump(25);
  }
  check(view->widget()->property("sceneAudioOutputActive").toBool(),"native earth viewport did not open audio output");
  capture("liftoff-audio-active");
  scene.setPaused(true);
  wait.restart();
  while(view->widget()->property("sceneAudioOutputActive").toBool()&&wait.elapsed()<1600){
   render();pump(25);
  }
  check(!view->widget()->property("sceneAudioOutputActive").toBool(),"native scene audio did not mute on pause");
  std::ofstream(directory/"scene-audio-native.json")<<json{{"tick",scene.snapshot()->tick},
      {"seconds",scene.snapshot()->timeSeconds()},{"device_active_in_play",true},{"device_active_in_pause",false}}.dump(2)<<'\n';
  std::cout<<"PASS: native Taipei scene drove Qt audio output and muted on pause\n";
  window.hide();pump(20);return;
 }
 if(missionCheck) {
  const auto matrix=[&](const char* id){
   const auto state=scene.snapshot();
   return state->objects.at(id).pose(state->tick,state->fixedStep);
  };
  const auto position=[&](const char* id){return matrix(id)*vsg::dvec3{};};
  const auto spacing=[&](const char* a,const char* b){return vsg::length(position(a)-position(b));};
  const double stackGap=spacing("falcon-booster","falcon-upper");
  const double releaseGap=spacing("falcon-upper","swot-spacecraft");
  const auto advance=[&](double seconds){
   scene.setPaused(false);
   const auto result=scene.advanceTo(seconds,1024);
   scene.setPaused(true);
   check(!result.pendingSteps,"mission playback lost fixed steps");
  };
  json cases=json::array();
  const auto shot=[&](const char* name,vsg::dvec3 point,vsg::dvec3 offset){
   camera(point+offset,point);
   for(int i=0;i<3;++i)render();
   capture(name);
   const auto state=scene.snapshot();
   json geometry=json::object();
   for(const auto& [id,object]:state->objects){
    check(object.volume==initial->objects.at(id).volume&&object.volume->occupiedCount()>0&&
       feature->display().placements.at(id)->matrix==object.pose(state->tick,state->fixedStep),
       "mission component geometry or viewport diverged from the voxel snapshot");
    const auto p=object.pose(state->tick,state->fixedStep)*vsg::dvec3{};
    geometry[id]={{"position_m",{p.x,p.y,p.z}},{"occupied_voxels",object.volume->occupiedCount()}};
   }
   cases.push_back({{"file",name},{"tick",state->tick},{"seconds",state->timeSeconds()},{"objects",std::move(geometry)}});
  };
  shot("01-stacked-on-pad",{0,41,0},{91,13,93});
  camera({91,54,93},{0,41,0});
  for(int i=1;i<=25;++i){advance(i*.13);render();}
  check(position("falcon-booster").y>initial->objects.at("falcon-booster").basePose[3][1]+15.,
      "stack did not lift off");
  shot("02-liftoff",position("falcon-booster")+vsg::dvec3(0,29,0),{91,13,93});
  advance(7.55);
  check(spacing("falcon-booster","falcon-upper")>stackGap+35.,"first stage did not detach");
  const auto boosterHeight=position("falcon-booster").y;
  shot("03-booster-separation",(position("falcon-booster")+position("falcon-upper"))*.5+vsg::dvec3(0,20,0),{98,33,170});
  advance(10.65);
  check(spacing("fairing-port","fairing-starboard")>26.,"fairing halves did not clear the payload");
  shot("04-fairing-separation",position("falcon-upper")+vsg::dvec3(0,8,0),{38,11,86});
  advance(15.0);
  check(spacing("falcon-upper","swot-spacecraft")>releaseGap+20.,"upper stage and payload did not separate");
  shot("05-upper-stage-and-payload",(position("falcon-upper")+position("swot-spacecraft"))*.5,{53,21,93});
  advance(18.35);
  check(position("falcon-booster").y<boosterHeight,"released first stage did not descend");
  const auto partlyOpen=matrix("swot-panel-right");
  shot("06-solar-array-deploying",position("swot-spacecraft"),{21,16,22});
  advance(22.);
  check(matrix("swot-panel-right")!=partlyOpen&&spacing("falcon-upper","swot-spacecraft")>160.,
      "final payload scene did not advance through deployment");
  shot("07-swot-deployed",position("swot-spacecraft"),{21,16,22});
  const auto state=scene.snapshot();
  const auto retained=scene.sample(state->timeSeconds()-.5*state->fixedStep);
  check(retained->timeSeconds()<state->timeSeconds()&&retained->objects.at("swot-spacecraft").basePose!=matrix("swot-spacecraft"),
      "fractional sensor sampling did not follow the authored track");
  const auto saved=(directory/"mission-checkpoint.json").string();
  scene.saveCheckpoint(saved);
  auto restored=VoxelScene::open(saved);
  check(restored->snapshot()->tick==state->tick&&restored->snapshot()->objects.at("swot-panel-right").basePose==
       state->objects.at("swot-panel-right").basePose,"checkpoint did not restore the deployed voxel assembly");
  check(scene.advanceTo(state->timeSeconds()+1,10).steps==0&&scene.snapshot()==state,"mission track advanced during pause");
  std::ofstream(directory/"mission-timeline.json")<<json{{"executed_as","authored visualization"},
      {"scene",scenePath},{"utc",initial->utcOrigin},{"cases",std::move(cases)}}.dump(2)<<'\n';
  std::cout<<"PASS: Falcon 9 / SWOT voxel mission, separation, deployment, pause and checkpoint\n";
  window.hide();pump(20);return;
 }
 if(civilCheck) {
  const auto configuration=VoxelSceneProject::open(topicPath).equipmentConfig();
  const auto pose=[&](std::string_view id){
   const auto state=scene.snapshot();
   return state->objects.at(std::string(id)).pose(state->tick,state->fixedStep)*vsg::dvec3{};
  };
  const auto captureDetail=[&](const char* id,const char* label,const vsg::dvec3& offset){
   const auto point=pose(id);
   camera(point+offset,point);
   render();
   capture(label);
  };
  camera({8.,1804.,9.},{1.8,1800.,1.});
  for(int i=0;i<3;++i)render();
  capture("01-civil-fleet");
  captureDetail("survey-wing","02-skywalker-x8",{3.0,1.65,-2.8});
  captureDetail("inspection-quad","03-3dr-iris",{1.15,.85,1.2});
  const auto first=voxelEquipmentPlatformTelemetry(configuration,initial);
  const auto firstWing=pose("survey-wing");
  const auto firstQuad=pose("inspection-quad");
  scene.setPaused(false);
  const auto advanced=scene.advanceTo(initial->timeSeconds()+25*initial->fixedStep,25);
  scene.setPaused(true);
  check(!advanced.pendingSteps&&scene.snapshot()->tick==initial->tick+25,"civil platform scene lost fixed steps");
  const auto state=scene.snapshot();
  auto rows=voxelEquipmentPlatformTelemetry(configuration,state);
  check(rows.size()==2&&rows.at(0).at("model_id")=="skywalker-x8"&&rows.at(1).at("model_id")=="3dr-iris",
      "civil model IDs failed to publish");
  check(rows.at(0).at("navigation").at("tick")==state->tick&&rows.at(1).at("navigation").at("tick")==state->tick&&
      rows.at(1).at("rotors").size()==4&&rows.at(1).at("air_motion").at("near_stationary")==true,
      "rotor assembly or navigation sample time is incorrect");
  const double seconds=state->timeSeconds()-initial->timeSeconds();
  check(vsg::length(pose("survey-wing")-firstWing-vsg::dvec3(0,0,-3.*seconds))<1e-8&&
      pose("inspection-quad")==firstQuad,
      "civil platform poses differ from the authored scene motion");
  render();
  for(const auto& [id,object]:state->objects)
   check(feature->display().placements.at(id)->matrix==object.pose(state->tick,state->fixedStep),
      "civil model display differs from the frozen world pose");
  check(first.at(1).at("rotors").at("quad-prop-1")!=rows.at(1).at("rotors").at("quad-prop-1"),
      "quadrotor animation did not advance");
  camera({8.,1804.,9.},{1.8,1800.,1.});
  capture("05-civil-fleet-stepped");
  for(int i=0;i<3;++i)render();
  check(rows==voxelEquipmentPlatformTelemetry(configuration,scene.snapshot()),"civil telemetry drifted during pause");
  std::ofstream(directory/"civil-aviation.json")<<json{{"scene",scenePath},{"utc",initial->utcOrigin},
      {"initial",first},{"stepped",std::move(rows)}}.dump(2)<<'\n';
  std::cout<<"PASS: native Taipei civil models, scheduled rotor pose, navigation tick and paused scene\n";
  window.hide();pump(20);return;
 }
 if(equipmentCheck) {
  auto* equipmentView=view->featureAs<SensorFovFeature>(SensorFovFeature::kId);
  check(equipmentView&&initial->objects.count("vessel"),"equipment overlay capture requires a vessel and the sensor viewport feature");
  const auto equipment=VoxelSceneProject::open(topicPath).equipmentConfig();
  check(equipment.equipment.size()==4,"equipment capture needs the four authored preview models");
  const uint32_t radarId=equipment.equipment.front().sensor.id;
  const vsg::dvec3 eye(37.,29.,40.),target(0.,2.,0.);
  camera(eye,target);
  equipmentView->setEnabled(false);
  for(int i=0;i<3;++i)render();
  capture("01-overlay-off");
  equipmentView->setEnabled(true);
  const auto inspect=[&](const VoxelSceneSnapshot& state){
   const auto pose=state.objects.at("vessel").pose(state.tick,state.fixedStep);
   check(feature->display().placements.at("vessel")->matrix==pose,"ship visual differs from authoritative pose");
   for(const auto& spec:equipment.equipment){
    const auto shown=equipmentView->displayedEquipmentPose(spec.sensor.id);
    check(shown.has_value()&&shown->simulationSeconds==state.timeSeconds(),"equipment pose did not use snapshot time");
    check(vsg::length(shown->origin-pose*spec.aperture.position)<1e-8,"equipment moved off its ship mount");
    const double expected=spec.visual->previewPeriodSeconds==0.?0.:
        std::remainder(spec.visual->phaseDegrees+state.timeSeconds()*360./spec.visual->previewPeriodSeconds,360.);
    check(std::abs(shown->azimuthDegrees-expected)<1e-9,"model scan phase differs from simulation time");
    const auto base=spec.aperture.forward;
    const auto right=vsg::normalize(vsg::cross(spec.aperture.up,base));
    const double radians=vsg::radians(expected);
    const auto world=pose*vsg::dvec4(base*std::cos(radians)+right*std::sin(radians),0.);
    check(vsg::length(shown->direction-vsg::dvec3(world.x,world.y,world.z))<1e-8,"beam direction differs from equipment pose");
   }
  };
  render();
  inspect(*initial);
  capture("02-overlay-start");
  const auto measured=[&]{
   std::vector<double> sample;
   for(int i=0;i<3;++i)render();
   for(int i=0;i<21;++i){render();sample.push_back(lastNativeRenderMs);}
   std::sort(sample.begin(),sample.end());
   return json{{"median",sample[10]},{"p95",sample[19]}};
  };
  const auto enabledRender=measured();
  equipmentView->setEnabled(false);
  const auto baselineRender=measured();
  check(!equipmentView->displayedEquipmentPose(radarId).has_value(),"disabled equipment overlay remained visible");
  equipmentView->setEnabled(true);
  for(int i=0;i<3;++i)render();
  inspect(*initial);
  const auto step=[&](double seconds,uint64_t count){
   scene.setPaused(false);
   const auto result=scene.advanceTo(seconds,int(count));
   scene.setPaused(true);
   check(!result.pendingSteps&&scene.snapshot()->tick==initial->tick+count,"equipment test lost simulation steps");
   render();inspect(*scene.snapshot());
  };
  step(initial->timeSeconds()+initial->fixedStep,1);
  const auto oneStep=*equipmentView->displayedEquipmentPose(radarId);
  for(int i=0;i<4;++i){pump(15);render();inspect(*scene.snapshot());}
  check(equipmentView->displayedEquipmentPose(radarId)->azimuthDegrees==oneStep.azimuthDegrees,
        "paused radar changed phase as wall time advanced");
  step(initial->timeSeconds()+25*initial->fixedStep,25);
  const auto later=*equipmentView->displayedEquipmentPose(radarId);
  capture("03-overlay-after-25-steps");
  check(equipmentView->equipmentGeometryBuilds()==4,"phase or movement rebuilt static equipment geometry");
  equipmentView->configure(json::object());
  auto stale=voxelEquipmentVisualTelemetry(equipment,*initial);
  feature->setEquipmentVisuals(std::move(stale));
  render();
  check(!equipmentView->displayedEquipmentPose(radarId).has_value(),"stale equipment data remained attached to a newer snapshot");
  std::ofstream(directory/"equipment-visual.json")<<json{
      {"scene",scenePath},{"utc",initial->utcOrigin},{"position_local_m",{eye.x,eye.y,eye.z}},
      {"target_local_m",{target.x,target.y,target.z}},{"model_count",4},
      {"radar_1_initial_deg",-15.},{"radar_1_after_one_step_deg",oneStep.azimuthDegrees},
      {"radar_1_after_25_steps_deg",later.azimuthDegrees},{"seconds",later.simulationSeconds},
      {"geometry_builds",equipmentView->equipmentGeometryBuilds()},
      {"native_render_ms_disabled",baselineRender},{"native_render_ms_enabled",enabledRender}}.dump(2)<<'\n';
  std::cout<<"PASS: native Taipei equipment mounts, four models, paused phase, exact single step and stale snapshot rejection\n";
  window.hide();pump(20);return;
 }
 if(std::getenv("VSGRAYSIM_SURFACE_MOTION_CHECK")) {
  check(initial->objects.count("vessel")!=0,"motion telemetry test requires a vessel");
  VoxelEquipmentConfig telemetry;telemetry.platforms.push_back({200,"vessel","船状态验收","ship",0.,{}});
  std::vector<double> renderMs;json outputFrames=json::array();
  capture("ship-initial");
  for(int i=1;i<=14;++i){
   scene.setPaused(false);scene.advanceTo(initial->timeSeconds()+i*initial->fixedStep);scene.setPaused(true);
   render();renderMs.push_back(lastNativeRenderMs);
   const auto state=scene.snapshot();const auto& boat=state->objects.at("vessel");
   const auto pose=boat.pose(state->tick,state->fixedStep);
   check(feature->display().placements.at("vessel")->matrix==pose,"ship view did not use the authoritative rigid pose");
   const auto packets=voxelEquipmentPlatformTelemetry(telemetry,state);check(packets.size()==1,"ship telemetry missing");
   const auto& packet=packets.front();
   const auto& motion=packet.at("surface_motion");
   check(std::abs(motion.at("speed_mps").get<double>()-std::hypot(boat.velocity.x,boat.velocity.z))<1e-10&&
      std::abs(motion.at("vertical_position_m").get<double>()-pose[3][1])<1e-9,"ship speed or height differs from the body state");
   const auto viewRotation=vsg::rotate(simcore::headingRotation(packet.at("yaw").get<double>(),
       motion.at("pitch_deg").get<double>(),motion.at("roll_deg").get<double>()));
   for(int c=0;c<3;++c)for(int r=0;r<3;++r)check(std::abs(pose[c][r]-viewRotation[c][r])<1e-7,"ship attitude telemetry/pose mismatch");
   outputFrames.push_back({{"tick",state->tick},{"state",motion}});
  }
  capture("ship-updated");std::sort(renderMs.begin(),renderMs.end());
  std::ofstream(directory/"ship-motion.json")<<json{{"scene",scenePath},{"utc",initial->utcOrigin},
      {"source","Taipei Earth viewport"},{"frames",outputFrames},{"render_p95_ms",renderMs[size_t(.95*(renderMs.size()-1))]}}.dump(2)<<'\n';
  window.hide();pump(20);return;
 }
 if(std::getenv("VSGRAYSIM_OCEAN_WEATHER_PASS")){runOceanWeatherPass(scene,*feature,*view,directory,scenePath,camera,render,[&]{return lastNativeRenderMs;},capture);window.hide();pump(20);return;}
 if(topicPath){runEarthTopicPass(scene,*feature,*view,directory,topicPath,camera,render,[&]{return lastNativeRenderMs;},capture);window.hide();pump(20);return;}
 if(naval){runNavalOceanPass(scene,*feature,*view,directory,scenePath,camera,render,[&]{return lastNativeRenderMs;},capture);window.hide();pump(20);return;}
 if(!initial->objects.count("vessel")){
  json r={{"scene",scenePath},{"utc",initial->utcOrigin},{"data_root","/home/mte/EarthData/Taipei80km"},{"bathymetry","not provided; water lower extent is computational"},{"views",json::array()}};
  auto shot=[&](std::string name,vsg::dvec3 eye,vsg::dvec3 target){camera(eye,target);for(int i=0;i<3;++i)render();capture(name);std::vector<double> ms;for(int i=0;i<15;++i){render();ms.push_back(lastNativeRenderMs);}std::sort(ms.begin(),ms.end());r["views"].push_back({{"name",name},{"eye_local_m",{eye.x,eye.y,eye.z}},{"target_local_m",{target.x,target.y,target.z}},{"render_median_ms",ms[ms.size()/2]}});};
  shot("taipei-80km-voxel-ocean",{20000,6000,-20000},{0,400,-3000});for(const auto& [id,o]:scene.snapshot()->objects)if(o.earth){scene.edit(json::array({{{"kind","terrain_mode"},{"object",id},{"mode","hd"}}}),scene.snapshot()->revision);break;}shot("taipei-80km-hd-ocean",{20000,6000,-20000},{0,400,-3000});shot("taipei-80km-coast",{15000,1200,-16000},{3000,50,-8000});std::ofstream(directory/"earth-ocean-acceptance.json")<<r.dump(2)<<'\n';std::cout<<"PASS: native 80km Taipei HD/voxel terrain with mean-sea integration\n";window.hide();pump(20);return;
 }
 if(!interactive){const auto& vessel=initial->objects.at("vessel");const auto c=vessel.localCentreOfMass();scene.schedule({{"kind","impulse"},{"object","vessel"},{"tick",550},{"space","object"},{"point_m",{c.x,c.y,c.z}},{"impulse_ns",{0,0,-vessel.massKg()*1.5}}});}
 capture("initial-surface");scene.saveCheckpoint((directory/"initial-checkpoint.json").string());scene.setPaused(false);scene.advanceTo(3.,200);scene.setPaused(true);
 json report={{"timing_scope","render_wall_ms includes frame-gate wait; render_submit_wait_ms measures native update/submit plus deviceWaitIdle only; screenshot IO excluded; per-row advance_simulation_ms is authoritative"},{"reference_hydro",bool(std::getenv("VSGRAYSIM_OCEAN_REFERENCE_HYDRO"))},{"scene",scenePath},{"data_root","/home/mte/EarthData/Taipei80km"},{"assumed_bathymetry",true},{"frames",json::array()}};std::vector<double> advances,renders,complete;double minHeave=1e9,maxHeave=-1e9,maxRoll=0,rangeError=0;const auto initialMass=initial->objects.at("vessel").massKg();
 for(int frame=0;frame<80;++frame){QElapsedTimer time;time.start();scene.setPaused(false);auto advance=scene.advanceTo(3.02+frame*.1,16);scene.setPaused(true);check(!advance.pendingSteps,"ocean capture lost simulation time");const double ms=time.nsecsElapsed()/1e6;
  const double u=double(frame%20)/19.;auto mix=[](vsg::dvec3 a,vsg::dvec3 b,double u){return a*(1-u)+b*u;};
  if(frame<20)camera(mix({22,7,24},{12,2.2,18},u),{0,.2,0});else if(frame<40)camera(mix({12,2.2,18},{9,-2,14},u),{-2,-2,-2});else if(frame<60)camera(mix({9,-2,14},{3,-5,11},u),{-5,-5,-3});else camera(mix({3,-5,11},{1,-9,13},u),{-1,-12,0});
  const double renderMs=render(),nativeRenderMs=lastNativeRenderMs;advances.push_back(ms);renders.push_back(renderMs);complete.push_back(ms+renderMs);const auto s=scene.snapshot();const auto& vessel=s->objects.at("vessel");const auto pose=vessel.pose(s->tick,s->fixedStep);const auto centre=pose*vessel.localCentreOfMass();minHeave=std::min(minHeave,centre.y);maxHeave=std::max(maxHeave,centre.y);maxRoll=std::max(maxRoll,std::acos(std::clamp(pose[1][1],-1.,1.))*180/vsg::PI);check(std::abs(vessel.massKg()-initialMass)<1e-6,"water coupling changed voxel mass");
  for(const auto& [id,o]:s->objects)check(feature->display().placements.at(id)->matrix==o.pose(s->tick,s->fixedStep),"water display and published physical poses differ");
  for(int z=-2;z<=2;++z)for(int x=-2;x<=2;++x){const vsg::dvec3 p(x*2.+18.,8.,z*2.+18.);double distance;check(s->ocean->intersectSurface(p,{0,-1,0},s->timeSeconds(),20,distance),"water surface ray missed");rangeError=std::max(rangeError,std::abs((8.-distance)-s->ocean->sample(p.x,p.z,s->timeSeconds()).height));}
  const auto underwater=traceVoxelOceanScene(*s,{20,-4,20},{0,-1,0},30);check(underwater.hit&&underwater.hit->object=="assumed-bed"&&underwater.waterLength>5,"underwater optical query lost the physical seabed");
  const auto floor=scene.rigidRaycast({20,0,20},{0,-1,0},30);check(floor&&floor->distance>10,"DSM sea surface became a solid water collider");
  capture(QString("route-%1").arg(frame,3,10,QChar('0')).toStdString());report["frames"].push_back({{"seconds",s->timeSeconds()},{"advance_simulation_ms",frame?100:20},{"buoyancy_wall_ms",advance.buoyancyMs},{"water_drag_wall_ms",advance.waterDragMs},{"water_total_wall_ms",advance.waterMs},{"advance_wall_ms",ms},{"render_submit_wait_ms",nativeRenderMs},{"render_wall_ms",renderMs},{"complete_wall_ms",ms+renderMs},{"heave_m",centre.y},{"speed_mps",vsg::length(vessel.velocity)}});
 }
 // An additional fixed camera watches a controlled propulsion impulse.
 // The preceding 80 frames retain the original route/camera/UTC comparison.
 const auto start=scene.snapshot();const auto& ship=start->objects.at("vessel");const auto centre=ship.pose(start->tick,start->fixedStep)*ship.localCentreOfMass();
 camera({centre.x+10,5,centre.z+14},{centre.x,.2,centre.z-3});
 report["wake_pass"]=json::array();std::vector<double> wakeAdvance,wakeRender,wakeComplete;
 for(int i=0;i<40;++i){QElapsedTimer clock;clock.start();scene.setPaused(false);const auto advance=scene.advanceTo(11.02+i*.1,16);scene.setPaused(true);check(!advance.pendingSteps,"wake pass lost simulation steps");const double ms=clock.nsecsElapsed()/1e6,renderMs=render();const auto state=scene.snapshot();for(const auto& [id,o]:state->objects)check(feature->display().placements.at(id)->matrix==o.pose(state->tick,state->fixedStep),"wake pass display/physics pose mismatch");wakeAdvance.push_back(ms);wakeRender.push_back(renderMs);wakeComplete.push_back(ms+renderMs);report["wake_pass"].push_back({{"seconds",state->timeSeconds()},{"advance_simulation_ms",100},{"buoyancy_wall_ms",advance.buoyancyMs},{"water_drag_wall_ms",advance.waterDragMs},{"water_total_wall_ms",advance.waterMs},{"advance_wall_ms",ms},{"render_submit_wait_ms",lastNativeRenderMs},{"render_wall_ms",renderMs},{"complete_wall_ms",ms+renderMs},{"wake_packets",state->ocean->wakes.size()}});capture(QString("wake-%1").arg(i,3,10,QChar('0')).toStdString());}
 // A real-time pacing test has no screenshot/encoding sleeps and never
 // drops physics ticks. All 60 frames must actually reach the native viewer.
 const double liveBase=scene.snapshot()->timeSeconds();QElapsedTimer liveClock;liveClock.start();report["live_frames"]=json::array();
 for(int i=1;i<=60;++i){const double target=liveBase+i/30.;while(liveClock.nsecsElapsed()/1e6<i*1000./30.)QThread::msleep(1);QElapsedTimer measured;measured.start();scene.setPaused(false);const auto advance=scene.advanceTo(target,16);scene.setPaused(true);check(!advance.pendingSteps,"paced ocean run dropped fixed steps");const double advanceMs=measured.nsecsElapsed()/1e6,renderMs=render();report["live_frames"].push_back({{"seconds",scene.snapshot()->timeSeconds()},{"advance_wall_ms",advanceMs},{"render_submit_wait_ms",lastNativeRenderMs},{"render_wall_ms",renderMs},{"buoyancy_wall_ms",advance.buoyancyMs},{"water_total_wall_ms",advance.waterMs},{"complete_wall_ms",advanceMs+renderMs}});}
 report["live_simulation_seconds"]=2.;report["live_wall_seconds"]=liveClock.nsecsElapsed()/1e9;report["live_realtime_ratio"]=2./report["live_wall_seconds"].get<double>();
 capture("wake-closeup");if(coastal){camera({6500,2200,12500},{1000,30,5000});capture("earth-coast-aerial");camera({5000,450,9500},{0,30,6500});capture("earth-coast-shore");}
 camera({20,6,22},{0,.2,0});capture("surface");camera({10,.15,14},{0,-.3,0});capture("waterline");camera({8,-5,10},{-5,-5,-3});capture("underwater");camera({10,-3,10},{10,7,9.9});capture("snell-window");camera({1,-9,13},{-1,-12,0});capture("caustics");
 if(auto* sky=view->featureAs<SkyFeature>(SkyFeature::kId)){sky->configure({{"year",2026},{"month",9},{"day",10},{"hours_utc",10.0}});camera({20,6,22},{0,.2,0});render();capture("dusk-surface");camera({8,-5,10},{-5,-5,-3});capture("dusk-underwater");}
 auto timing=[&](const char* key,std::vector<double> v){std::sort(v.begin(),v.end());report[std::string(key)+"_median_ms"]=v[v.size()/2];report[std::string(key)+"_p95_ms"]=v[size_t(.95*(v.size()-1))];};std::vector<double> buoyancy,water,wakeBuoyancy,wakeWater;for(const auto& f:report["frames"]){buoyancy.push_back(f["buoyancy_wall_ms"]);water.push_back(f["water_total_wall_ms"]);}for(const auto& f:report["wake_pass"]){wakeBuoyancy.push_back(f["buoyancy_wall_ms"]);wakeWater.push_back(f["water_total_wall_ms"]);}timing("buoyancy",buoyancy);timing("water",water);timing("wake_buoyancy",wakeBuoyancy);timing("wake_water",wakeWater);timing("advance",advances);timing("render",renders);timing("complete",complete);timing("wake_advance",wakeAdvance);timing("wake_render",wakeRender);timing("wake_complete",wakeComplete);report["propulsion_test"]="COM impulse at 11s: 1.5m/s equivalent along local -Z; not a calibrated engine/propeller";report["heave_range_m"]={minHeave,maxHeave};report["maximum_inclination_deg"]=maxRoll;report["maximum_surface_ray_error_m"]=rangeError;report["range_checks"]=2000;report["sensor_scope"]="medium-aware CPU geometry/refraction/range; not a calibrated GPU underwater radiometry product";scene.saveCheckpoint((directory/"final-checkpoint.json").string());
 // Independent visual cases use exactly the same frozen geometry, camera and
 // UTC in the legacy/current effects runs. No screenshot cost enters timings.
 report["effects_legacy"]=bool(std::getenv("VSGRAYSIM_OCEAN_LEGACY_EFFECTS"));report["weather_cases"]=json::array();const auto originalOcean=scene.snapshot()->ocean->encode();
 auto changeOcean=[&](json ocean){scene.setPaused(true);scene.edit(json::array({{{"kind","ocean"},{"ocean",std::move(ocean)}}}),scene.snapshot()->revision);};
 auto weatherCase=[&](std::string name,int day,double hour,double hs,double wind,double visibility,bool shore){auto o=originalOcean;o["wave_height_m"]=hs;o["wave_length_m"]=hs>1?42:24;o["wind_speed_mps"]=wind;o["sea_fog"]={{"visibility_m",visibility},{"height_m",45}};o.erase("harmonics");changeOcean(o);auto* sky=view->featureAs<SkyFeature>(SkyFeature::kId);if(sky)sky->configure({{"year",2026},{"month",9},{"day",day},{"hours_utc",hour}});camera(shore?vsg::dvec3(5000,90,9500):vsg::dvec3(20,3,22),shore?vsg::dvec3(0,20,6500):vsg::dvec3(0,0,0));for(int j=0;j<3;++j)render();
 if(!shore&&sky)if(auto* provider=sky->sunProvider()){provider->syncDateTime(2026,9,day,hour);rockyenv::EnvSunInfo source;float phase=0;const bool ok=hour>=12?provider->moonInfo(chart,source,phase):provider->sunInfo(chart,source);if(ok){vsg::dvec3 direction(source.dirENU.x,0,source.dirENU.z);if(vsg::length(direction)>.01){direction=vsg::normalize(direction);const double height=std::clamp(double(source.dirENU.y)/std::hypot(double(source.dirENU.x),double(source.dirENU.z))*35.,1.5,150.);camera(-direction*35.+vsg::dvec3(0,height,0),{0,0,0});}}}
 render();capture(name);std::vector<double> ms;for(int j=0;j<15;++j){render();ms.push_back(lastNativeRenderMs);}std::sort(ms.begin(),ms.end());report["weather_cases"].push_back({{"name",name},{"utc",{{"year",2026},{"month",9},{"day",day},{"hours",hour}}},{"camera_eye_local_m",{(vsg::inverse(chart)*look->eye).x,(vsg::inverse(chart)*look->eye).y,(vsg::inverse(chart)*look->eye).z}},{"hs_m",hs},{"wind_mps",wind},{"fog_visibility_m",visibility},{"render_median_ms",ms[ms.size()/2]}});
 };
 weatherCase("sun-glitter",10,8.,.65,6,0,false);weatherCase("sunset-glitter",10,9.5,.65,6,0,false);weatherCase("moon-glitter",26,12.,.65,6,0,false);weatherCase("calm-sea",10,4.,.15,2,0,false);weatherCase("rough-sea",10,4.,2.1,14,0,false);weatherCase("marine-fog",10,4.,.65,6,1600,false);if(coastal)weatherCase("coast-marine-fog",10,4.,.65,6,4000,true);
 changeOcean(originalOcean);if(auto* sky=view->featureAs<SkyFeature>(SkyFeature::kId))sky->configure({{"year",2026},{"month",9},{"day",10},{"hours_utc",4.}});
 // One destructible voxel body fractures while falling, then the fragments
 // enter the same canonical water field and settle/sink under existing forces.
 json drop={{"id","entry-target"},{"role","target"},{"position_m",{8,3,4}},{"velocity_mps",{.5,-3,0}},{"source",{{"type","box"},{"dimensions",{12,6,12}},{"cell_size_m",.1}}},{"material",{{"density_kg_m3",1400},{"color_rgba",{.7,.28,.08,1}}}},{"physics",{{"motion","dynamic"},{"fracture",true},{"support","none"}}}};
 scene.edit(json::array({{{"kind","add"},{"object",drop}}}),scene.snapshot()->revision);const uint64_t breakTick=scene.snapshot()->tick+1;scene.schedule({{"kind","fracture"},{"object","entry-target"},{"tick",breakTick},{"space","object"},{"point_m",{.6,.3,.6}},{"normal",{1,0,0}},{"radius_m",5},{"energy_j",1e7}});const double entryBase=scene.snapshot()->timeSeconds();camera({14,3,13},{8,0,4});report["entry_frames"]=json::array();size_t maxSpray=0;
 for(int j=1;j<=45;++j){QElapsedTimer timer;timer.start();scene.setPaused(false);const auto step=scene.advanceTo(entryBase+j*.04,8);scene.setPaused(true);check(!step.pendingSteps,"water-entry capture lost physics ticks");const double ms=timer.nsecsElapsed()/1e6,rm=render();const auto state=scene.snapshot();maxSpray=std::max(maxSpray,state->ocean->splashes.size());for(const auto& [id,o]:state->objects)check(feature->display().placements.at(id)->matrix==o.pose(state->tick,state->fixedStep),"entry fragment display/physics pose mismatch");report["entry_frames"].push_back({{"seconds",state->timeSeconds()},{"advance_simulation_ms",40},{"advance_wall_ms",ms},{"render_wall_ms",rm},{"complete_wall_ms",ms+rm},{"buoyancy_wall_ms",step.buoyancyMs},{"water_drag_wall_ms",step.waterDragMs},{"water_total_wall_ms",step.waterMs},{"splash_events",state->ocean->splashes.size()}});capture(QString("entry-%1").arg(j,3,10,QChar('0')).toStdString());}
 check(maxSpray>0,"falling fractured target produced no impact spray");report["maximum_entry_splash_events"]=maxSpray;report["spray_scope"]="deterministic visual ballistic droplets from swept body-bounds entry; no added liquid mass or rigid collision particles";scene.saveCheckpoint((directory/"entry-checkpoint.json").string());
 std::ofstream(directory/"ocean-acceptance.json")<<report.dump(2)<<'\n';check(maxHeave-minHeave>.03,"wind waves did not produce physical vessel heave");std::cout<<"PASS: native voxel ocean, physical heave/righting, wave ranges, seabed collision and underwater optical queries\n";window.hide();pump(20);
}
