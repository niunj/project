#include "display/AircraftCockpitPanel.h"
#include "cockpit_exterior_capture.h"
#include "display/ControlWorkbench.h"
#include "display/EarthPanel.h"
#include "display/FlightPanel.h"
#include "display/PlatformInstruments.h"
#include "display/ConsoleSelector.h"
#include "display/JoystickInput.h"
#include "display/SensorPanels.h"
#include "ScrollableImageView.h"
#include <vsgraysim/broker/EquipmentInfo.h>
#include "viewport3d/AircraftExterior.h"
#include <vsgXchange/all.h>
#include "display/WorkspaceProfile.h"
#include "viewport3d/AircraftCockpitFeature.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/EarthView.h"
#include "viewport3d/EarthViewFeature.h"
#include "viewport3d/VsgQtWidget.h"
#include "base/ThemeManager.h"
#include "PlatformManager.h"
#include <simcore/Orientation.h>
#include <vsgraysim/broker/BrokerNet.h>
#include <vsgraysim/layer0_phys/PhysicsEngine.h>
#include <vsg/all.h>
#include <QApplication>
#include <QAbstractItemModel>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace vsgraysim;
using namespace vsgraysim::gui::display;
using vsgraysim::viewport3d::AircraftCockpitFeature;
using json=nlohmann::json;
namespace {
void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
void pump(int ms=30){QElapsedTimer t;t.start();do{QApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}while(t.elapsed()<ms);}
template<class T>T* widget(QWidget& parent,const char* name){auto* value=parent.findChild<T*>(name);require(value,"missing cockpit widget");return value;}
json telemetry(){return {{"platform_id",77},{"name","Aircraft"},{"position",{100,1400,-80}},{"velocity",{0,5,-120}},
    {"yaw",0},{"pitch",0},{"roll",0},{"speed",120.104},{"sim_time",2.5},
    {"flight_control",{{"available",true},{"mode","manual"},{"owner",""},{"has_route",false},
        {"actual",{{"throttle",.6},{"elevator",0},{"aileron",0},{"rudder",0}}},
        {"command",{{"throttle",.6},{"elevator",0},{"aileron",0},{"rudder",0},{"target_speed",120},{"target_altitude",1400},{"target_heading",0}}}}}};}
void initializeBroker(){auto& net=BrokerNet::instance();net.stop();BrokerNet::Config config;config.enabled=false;net.startSimRole(config);SimDataBroker::instance().serverOnline(true);net.setSimMeta("cockpit_acceptance",2.5,20,false);net.setFlightControlAvailable(true);}
FlightControlCommand command(){FlightControlCommand c;require(BrokerNet::instance().pollFlightControl(&c),"cockpit action did not reach flight command service");return c;}
void acknowledge(const FlightControlCommand& c){BrokerNet::instance().completeFlightControl(flightResult(c,"applied","applied"));}
void drain(){FlightControlCommand c;while(BrokerNet::instance().pollFlightControl(&c))acknowledge(c);}
void key(QWidget& panel,int k,bool down){QKeyEvent event(down?QEvent::KeyPress:QEvent::KeyRelease,k,Qt::NoModifier);QApplication::sendEvent(&panel,&event);}
void mouse(QWidget& panel,QEvent::Type type,const QPoint& point,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent event(type,QPointF(point),QPointF(panel.mapToGlobal(point)),button,buttons,Qt::NoModifier);QApplication::sendEvent(&panel,&event);}
void instrumentContract(){
    const auto profile=CockpitProfile::load("config/cockpits/f16c.json");require(profile.engineCount==1&&profile.stick.center().x()>.5&&profile.throttle.center().x()<.5,"F-16 panel did not use a single engine/right sidestick/left throttle layout");
    std::ifstream source("config/cockpits/f16c.json");json definition;source>>definition;auto alternate=definition;alternate["id"]="other_airframe";alternate["engine_count"]=2;alternate["controls"]["maximum_axis_demand"]=.5;alternate["instruments"][3]["max"]=450;
    const auto other=CockpitProfile::parse(alternate,QDir::currentPath()+"/config/cockpits");require(other.engineCount==2&&other.maximumAxisDemand==.5&&other.instruments[3].maximum==450,"airframe presentation parameters were hardcoded");
    auto invalid=definition;invalid["instruments"][3]["max"]=-1;bool rejected=false;try{CockpitProfile::parse(invalid,QDir::currentPath());}catch(const std::exception&){rejected=true;}require(rejected,"invalid gauge range was accepted");
    auto sample=telemetry();require(!PlatformInstruments::measurement(sample,"air_data.ias_mps"),"panel invented indicated airspeed from ground speed");require(!PlatformInstruments::measurement(sample,"engine.rpm_pct"),"panel invented engine instrumentation");
    sample["air_data"]["ias_mps"]=89.;require(PlatformInstruments::measurement(sample,"air_data.ias_mps")==89.,"configured instrument binding did not read its actual field");
    initializeBroker();auto& net=BrokerNet::instance();AircraftCockpitPanel panel;widget<QComboBox>(panel,"cockpit_view_mode")->setCurrentIndex(0);panel.resize(1400,950);panel.show();pump(20);auto samples=json::array({telemetry()});
    auto update=[&]{panel.updateState(json::object(),samples,2.5,true,false,net.flightSessionId(),true,77);};update();auto* canvas=panel.instrumentPanel();require(canvas->profile().id=="f16c","cockpit did not load the selected instrument profile");
    QString error;require(!canvas->loadProfile("/no/such/cockpit.json",error)&&canvas->profile().id=="f16c","failed profile load destroyed the current panel");
    const auto beforePage=canvas->grab().toImage();auto osb=canvas->controlPosition("right_eng");mouse(*canvas,QEvent::MouseButtonPress,osb,Qt::LeftButton,Qt::LeftButton);mouse(*canvas,QEvent::MouseButtonRelease,osb,Qt::LeftButton,Qt::NoButton);require(canvas->grab().toImage()!=beforePage,"configured physical OSB button did not change its MFD page");FlightControlCommand localOnly;require(!net.pollFlightControl(&localOnly),"local avionics page change sent a flight command");
    osb=canvas->controlPosition("right_ctrl");mouse(*canvas,QEvent::MouseButtonPress,osb,Qt::LeftButton,Qt::LeftButton);mouse(*canvas,QEvent::MouseButtonRelease,osb,Qt::LeftButton,Qt::NoButton);
    auto at=canvas->controlPosition("stick");mouse(*canvas,QEvent::MouseButtonPress,at,Qt::LeftButton,Qt::LeftButton);require(!canvas->interacting(),"unauthorized joystick acquired control");drain();
    widget<QPushButton>(panel,"flight_take_control")->click();acknowledge(command());pump(30);update();
    mouse(*canvas,QEvent::MouseButtonPress,at,Qt::LeftButton,Qt::LeftButton);acknowledge(command());const auto moved=at+QPoint(35,35);mouse(*canvas,QEvent::MouseMove,moved,Qt::NoButton,Qt::LeftButton);auto demand=command();require(demand.elevator>0&&demand.aileron>0&&demand.elevator<=profile.maximumAxisDemand&&demand.aileron<=profile.maximumAxisDemand,"Qt sidestick did not submit bounded pitch/roll demands");acknowledge(demand);
    mouse(*canvas,QEvent::MouseButtonRelease,moved,Qt::LeftButton,Qt::NoButton);auto centre=command();require(centre.elevator==0&&centre.aileron==0&&!canvas->interacting(),"physical joystick release did not spring to centre");acknowledge(centre);
    at=canvas->controlPosition("throttle");mouse(*canvas,QEvent::MouseButtonPress,at,Qt::LeftButton,Qt::LeftButton);acknowledge(command());mouse(*canvas,QEvent::MouseMove,at-QPoint(0,45),Qt::NoButton,Qt::LeftButton);auto throttle=command();require(throttle.throttle>.5&&throttle.throttle<=1,"Qt throttle travel did not map to normalized demand");acknowledge(throttle);mouse(*canvas,QEvent::MouseButtonRelease,at,Qt::LeftButton,Qt::NoButton);
    require(panel.controls()->selectedTelemetry()["flight_control"]["actual"]["throttle"]==.6,"throttle graphic replaced measured actuator feedback");
    require(QDir().mkpath("build/f16c-panel"),"cannot create instrument panel artifacts");require(panel.grab().save("build/f16c-panel/f16c-workbench.png"),"cannot capture Qt control panel");require(canvas->grab().save("build/f16c-panel/f16c-instruments.png"),"cannot capture instrument composition");
    panel.hide();drain();std::cout<<"PASS: F-16 instruments, configurable airframes/gauges, honest missing channels, pointer HOTAS, spring return and throttle latch\n";
}
void fleetContract(){
    const auto profiles=CockpitProfile::builtInProfiles();require(profiles.size()==4,"startup selector did not discover four complete consoles");require(QDir().mkpath("build/fleet-cockpit-ui"),"cannot create fleet UI artifacts");
    ConsoleSelector selector(nullptr,"mq9");selector.show();pump(20);const auto cards=selector.findChildren<QPushButton*>(QRegularExpression("console_.*"));require(cards.size()==4,"startup cards omitted an airframe");for(auto* card:cards)require(card->height()>=240&&card->width()>=300,"global button styling collapsed the console preview cards");require(selector.grab().save("build/fleet-cockpit-ui/selector.png"),"cannot capture startup selector");widget<QPushButton>(selector,"console_ah64d")->click();require(selector.result()==QDialog::Accepted&&selector.selectedProfile().endsWith("ah64d.json"),"startup selection opened the wrong console");
    auto options=vsg::Options::create();options->add(vsgXchange::all::create());auto& net=BrokerNet::instance();
    for(const auto& path:profiles){const auto profile=CockpitProfile::load(path);AircraftCockpitPanel panel;widget<QComboBox>(panel,"cockpit_view_mode")->setCurrentIndex(0);QString error;require(panel.loadCockpitProfile(path,error),"airframe console load failed");panel.resize(1440,980);panel.show();panel.updateState(json::object(),json::array({telemetry()}),2.5,true,false,net.flightSessionId(),true,77);pump(20);
        require(panel.instrumentPanel()->profile().id==profile.id,"runtime model selection retained the prior panel");require(panel.grab().save("build/fleet-cockpit-ui/"+profile.id+"-panel.png"),"cannot capture fleet panel");
        bool representative=false;const auto model=vsgraysim::viewport3d::loadAircraftExterior(profile.modelPath.toStdString(),profile.modelUpAxis.toStdString(),profile.modelKind.toStdString(),profile.modelMaximumDimension,options,representative);require(bool(model)&&representative==(profile.id=="a320"),"existing aircraft model did not load or airliner placeholder was concealed");
        vsg::ComputeBounds bounds;model->accept(bounds);require(bounds.bounds.valid(),"exterior model has no display bounds");const auto extent=bounds.bounds.max-bounds.bounds.min;require(std::abs(std::max({extent.x,extent.y,extent.z})-profile.modelMaximumDimension)<.01,"per-airframe exterior dimensions were ignored");
        const auto chase=AircraftCockpitFeature::chaseCamera({},telemetry(),profile.chaseDistance);require(std::abs(vsg::length(chase.eye-chase.center)-profile.chaseDistance)<1e-7,"exterior follow distance did not use airframe configuration");panel.hide();
    }
    JoystickInput device;device.configure(CockpitProfile::load("config/cockpits/mq9.json").joystickMapping);int changes=0,releases=0;device.axesChanged=[&](const auto&){++changes;};device.releasePressed=[&]{++releases;};require(!device.demands(),"joystick invented unreceived axis positions");
    device.consumePacket(0x82,0,-32768);device.consumePacket(0x82,1,16384);device.consumePacket(0x82,2,-32768);require(changes==0,"partial joystick initialization sent a flight command");device.consumePacket(0x82,3,100);const auto axes=device.demands();require(axes&&(*axes)[0]==1&&(*axes)[1]>.45&&(*axes)[1]<.5&&(*axes)[2]==-1&&(*axes)[3]==0,"USB packet mapping, throttle direction or deadzone incorrect");
    device.consumePacket(0x81,7,1);require(releases==0,"initial button state caused a release action");device.consumePacket(1,7,1);require(releases==1,"explicit joystick release button was ignored");device.consumePacket(2,99,32767);require(device.demands()==axes,"out-of-range USB axis corrupted the state");
    bool rejected=false;try{device.configure({{"roll_axis",0},{"pitch_axis",0}});}catch(const std::exception&){rejected=true;}require(rejected&&device.demands()==axes,"invalid device mapping partially replaced live axes");device.closeDevice();require(!device.connected()&&!device.demands(),"device close retained active hardware demand");
    std::cout<<"PASS: four model consoles/startup selection, real exterior meshes, explicit airliner illustration, chase cameras and USB decoder/calibration\n";
}
void cameraContract(){
    auto sample=telemetry();simcore::geo::GeoAnchor anchor;anchor.heading_deg=37;const auto inverse=vsg::inverse(simcore::geo::makeEnuToWorldMatrix(anchor));
    auto camera=AircraftCockpitFeature::pilotCamera(anchor,sample);auto eye=inverse*camera.eye;
    require(vsg::length(eye-vsg::dvec3(100,1401.2,-80))<1e-7,"pilot camera did not preserve the physical ENU position");
    auto direction=[&](const vsg::LookAt& c){const auto d=inverse*vsg::dvec4(vsg::normalize(c.center-c.eye),0);return vsg::dvec3(d.x,d.y,d.z);};
    require(vsg::length(direction(camera)-vsg::dvec3(0,0,-1))<1e-8,"north-looking camera ignored grid heading");
    sample["yaw"]=90;camera=AircraftCockpitFeature::pilotCamera(anchor,sample);require(vsg::length(direction(camera)-vsg::dvec3(1,0,0))<1e-8,"east heading was inverted");
    sample["pitch"]=30;camera=AircraftCockpitFeature::pilotCamera(anchor,sample);require(std::abs(direction(camera).y-.5)<1e-8,"positive pitch did not point the camera upward");
    sample["roll"]=25;const auto before=sample;camera=AircraftCockpitFeature::pilotCamera(anchor,sample,25,-15);require(sample==before&&std::abs(vsg::dot(vsg::normalize(camera.center-camera.eye),camera.up))<1e-8,"head look modified truth or broke camera orthogonality");
    auto invalid=sample;invalid["velocity"][0]=std::numeric_limits<double>::infinity();require(!AircraftCockpitFeature::validTelemetry(invalid),"nonfinite cockpit telemetry accepted");
    bool rejected=false;try{AircraftCockpitFeature::pilotCamera(anchor,invalid);}catch(const std::exception&){rejected=true;}require(rejected,"invalid pose produced an operational pilot camera");
    auto vertical=sample;vertical["velocity"][1]=100;vertical["speed"]=156;
    require(PlatformInstruments::measurement(sample,"ground_speed_kt")==PlatformInstruments::measurement(vertical,"ground_speed_kt"),"GS was total speed instead of horizontal ground speed");
    std::cout<<"PASS: ENU camera, heading/pitch/roll, independent head look and horizontal GS\n";
}
void presentationContract(){
    initializeBroker();auto& net=BrokerNet::instance();AircraftCockpitPanel panel;panel.resize(1320,910);panel.show();pump(20);
    auto* mode=widget<QComboBox>(panel,"cockpit_view_mode");auto* canvas=panel.instrumentPanel();
    require(mode->currentData()=="panel"&&canvas->isVisible()&&!panel.earthDisplay()->isVisible(),"Qt mode must open without the 3D viewport");
    auto sample=telemetry();
    auto state=[&](bool online=true){panel.updateState(json::object(),json::array({sample}),2.5,online,false,net.flightSessionId(),true,77);pump(12);};
    state();const auto original=canvas->grab().toImage();sample["pitch"]=19.;sample["flight_control"]=nullptr;state();
    require(original!=canvas->grab().toImage(),"Qt attitude did not redraw after telemetry changed");
    const auto live=canvas->grab().toImage();state(false);
    require(live!=canvas->grab().toImage(),"held telemetry lacks a visible state change");
    sample["category"]="NavalVessel";state();const auto ship=canvas->grab().toImage();
    require(!widget<QPushButton>(panel,"flight_take_control")->isEnabled(),"ship used an aircraft control lease");
    sample["category"]="GroundVehicle";state();require(ship!=canvas->grab().toImage(),"naval and land speed instruments used the same presentation");
    mode->setCurrentIndex(mode->findData("hud"));require(!canvas->isVisible()&&panel.earthDisplay()->isVisible(),"HUD mode did not switch presentation");
    mode->setCurrentIndex(mode->findData("panel"));require(canvas->isVisible()&&!panel.earthDisplay()->isVisible(),"switch back did not restore Qt instruments");
    panel.hide();drain();
}
void operatorContract(){
    initializeBroker();auto& net=BrokerNet::instance();AircraftCockpitPanel panel;panel.resize(1180,800);panel.show();pump();
    auto samples=json::array({telemetry()});auto update=[&](bool online=true){panel.updateState(json::object(),samples,2.5,online,false,net.flightSessionId(),true,77);};update();
    auto* keyboard=widget<QToolButton>(panel,"cockpit_keyboard");require(!keyboard->isEnabled()&&!panel.controls()->controlActive(),"cockpit implicitly took control");
    int listChanges=0;auto* platforms=widget<QComboBox>(panel,"flight_platform");QObject::connect(platforms->model(),&QAbstractItemModel::rowsRemoved,&panel,[&]{++listChanges;});QObject::connect(platforms->model(),&QAbstractItemModel::rowsInserted,&panel,[&]{++listChanges;});update();update();require(listChanges==0,"cockpit heartbeats rebuilt the operator's platform selector");
    widget<QPushButton>(panel,"flight_take_control")->click();auto acquire=command();require(!acquire.release&&acquire.throttle==.6,"takeover failed to preserve authoritative throttle");acknowledge(acquire);pump();update();
    keyboard->click();require(keyboard->isChecked(),"manual controller could not enable keyboard");
    key(panel,Qt::Key_W,true);auto climb=command();require(climb.elevator==.6&&climb.throttle==.6,"pitch key did not use the existing normalized control channel");acknowledge(climb);
    key(panel,Qt::Key_D,true);auto bank=command();require(bank.aileron==.6&&bank.elevator==.6,"combined axes were lost");acknowledge(bank);
    key(panel,Qt::Key_W,false);auto stopPitch=command();require(stopPitch.elevator==0&&stopPitch.aileron==.6,"key release left the pitch axis stuck");acknowledge(stopPitch);
    require(panel.controls()->selectedTelemetry()["flight_control"]["actual"]["aileron"]==0,"keyboard demand was presented as measured actuator feedback");
    auto* views=widget<QComboBox>(panel,"cockpit_view_mode");views->setCurrentIndex(views->findData("hud"));
    const auto switched=command();require(!switched.release&&switched.aileron==0&&switched.elevator==0&&panel.controls()->controlActive(),"switching presentation lost ownership or left a held input");acknowledge(switched);
    views->setCurrentIndex(views->findData("panel"));
    QEvent deactivated(QEvent::WindowDeactivate);QApplication::sendEvent(&panel,&deactivated);require(command().release&&!panel.controls()->controlActive()&&!keyboard->isChecked(),"focus loss retained manual control");drain();
    update();widget<QPushButton>(panel,"flight_take_control")->click();acknowledge(command());pump();update();keyboard->click();key(panel,Qt::Key_A,true);acknowledge(command());
    update(false);require(!panel.controls()->controlActive()&&!keyboard->isChecked()&&command().release,"offline telemetry retained keyboard authority");drain();
    update();widget<QPushButton>(panel,"flight_take_control")->click();acknowledge(command());pump();update();
    auto* mode=widget<QComboBox>(panel,"flight_mode");mode->setCurrentIndex(mode->findData("altitude_hold"));widget<QPushButton>(panel,"flight_apply")->click();auto hold=command();require(hold.mode=="altitude_hold","hold command lost");acknowledge(hold);pump();update();require(!keyboard->isEnabled(),"keyboard could override an altitude-hold controller");
    panel.hide();require(command().release,"hidden cockpit retained control lease");drain();
    panel.show();pump(20);panel.updateState(json::object(),samples,2.5,true,true,net.flightSessionId(),true,77);widget<QPushButton>(panel,"flight_take_control")->click();acknowledge(command());
    for(int i=0;i<33;++i){samples[0]["flight_control"]["lease_remaining_ms"]=1500-i;panel.updateState(json::object(),samples,2.5,true,true,net.flightSessionId(),true,77);pump(100);drain();}
    require(!panel.controls()->telemetryFresh()&&!panel.controls()->controlActive(),"stale running telemetry retained cockpit control");panel.hide();drain();
    std::cout<<"PASS: explicit ownership, command/actual separation, combined keys, release, offline and hold-mode isolation\n";
}
void uavPayloadContract(){
    auto& broker=SimDataBroker::instance();auto& registry=EquipmentRegistry::instance();broker.serverOnline(true);
    for(auto [id,type,platform]:std::vector<std::tuple<uint32_t,SensorType,uint32_t>>{{7101,SensorType::VIS,77},{7102,SensorType::IR,77},{7103,SensorType::VIS,88}}){EquipmentInfo info;info.equipment_id=id;info.type=type;info.platform_id=platform;info.platform_name="MQ-9 payload fixture";info.name="UAV acceptance";info.powered=true;registry.registerEquipment(info);}
    {
        EquipmentChannelSpec eoSpec;eoSpec.sensor_type=SensorType::VIS;EquipmentSession eo(eoSpec),foreign(eoSpec);EquipmentChannelSpec irSpec;irSpec.sensor_type=SensorType::IR;EquipmentSession ir(irSpec);
        require(eo.bind(7101,"EO")&&ir.bind(7102,"IR")&&foreign.bind(7103,"Other aircraft"),"UAV payload subscriptions failed");
        VisFrame visible;visible.sensor_id=7101;visible.resize(2,2);visible.intensity={.125f,.25f,.5f,1};visible.stage=4;visible.surface_revision=42;require(broker.publishVis(visible),"UAV EO publication failed");
        IrFrame thermal;thermal.sensor_id=7102;thermal.resize(2,2);thermal.radiance={12,20,30,40};thermal.stage=0;thermal.surface_revision=42;require(broker.publishIr(thermal),"UAV IR publication failed");eo.tick(42);ir.tick(42);foreign.tick(42);
        AircraftCockpitPanel panel;QString error;require(panel.loadCockpitProfile("config/cockpits/mq9.json",error),"MQ-9 profile failed");widget<QComboBox>(panel,"cockpit_view_mode")->setCurrentIndex(0);panel.resize(1400,1000);panel.show();pump();
        const auto update=[&](uint32_t platform=77,bool online=true){auto sample=telemetry();sample["platform_id"]=platform;panel.updateState(json::object(),json::array({sample}),2.5,online,false,BrokerNet::instance().flightSessionId(),true,platform);};update();
        const std::vector<std::pair<EquipmentSession*,uint32_t>> sources{{&eo,77},{&ir,77},{&foreign,88}};panel.updatePayloads(sources);
        auto awaitPixels=[&](const QString& value){for(int n=0;n<100;++n){panel.updatePayloads(sources);pump(10);auto* image=panel.instrumentPanel()->findChild<gui::ScrollableImageView*>();if(image&&image->pixelText(0,0).contains(value))return;}throw std::runtime_error("background cockpit payload did not arrive");};awaitPixels("0.125");
        auto* source=widget<QComboBox>(panel,"cockpit_payload_source");require(source->count()==2,"UAV selected payloads belonging to a different aircraft");
        auto pixels=[&]{return panel.instrumentPanel()->findChild<gui::ScrollableImageView*>();};require(pixels()&&pixels()->pixelText(0,0).contains("0.125")&&pixels()->pixelText(0,0).contains(QString::fromUtf8("归一化 DN")),"UAV EO frame zero or physical units lost");
        require(eo.frames()==1&&eo.lastFrameId()==0,"UAV first frame identity changed");source->setCurrentIndex(1);panel.updatePayloads(sources);awaitPixels("12 W/");require(pixels()->pixelText(0,0).contains("12 W/"),"EO/IR switching reused pixels at the same frame counter");
        ir.tick(43);panel.updatePayloads(sources);require(pixels()->pixelText(0,0).isEmpty(),"UAV showed a payload from a different terrain revision");ir.tick(42);panel.updatePayloads(sources);awaitPixels("12 W/");require(pixels()->pixelText(0,0).contains("12 W/"),"matching UAV surface did not resume imagery");
        update(88);require(pixels()->pixelText(0,0).isEmpty(),"platform switch retained prior aircraft image");panel.updatePayloads(sources);require(source->count()==1&&source->currentData().toUInt()==7103,"UAV platform filter did not change sources");
        update();panel.updatePayloads(sources);update(77,false);panel.updatePayloads(sources);require(pixels()->pixelText(0,0).isEmpty()&&!widget<QToolButton>(panel,"cockpit_payload_power")->isEnabled(),"offline UAV retained imagery or live power controls");update();panel.updatePayloads(sources);
        QDir().mkpath("build/uav-cockpit-ui");require(panel.grab().save("build/uav-cockpit-ui/mq9-payload-console.png"),"cannot capture MQ-9 payload composition");panel.updatePayloads({});require(pixels()->pixelText(0,0).isEmpty(),"equipment disconnect retained UAV pixels");panel.hide();drain();
    }
    {
        auto& net=BrokerNet::instance();net.setPlatformTelemetry(json::array({telemetry()}).dump());ControlWorkbench workbench("uav",{"7101","7102","7103"},nullptr,false);workbench.resize(1450,1000);workbench.show();pump(100);AircraftCockpitPanel* panel=nullptr;for(auto* dock:workbench.findChildren<QDockWidget*>())if(auto* value=dynamic_cast<AircraftCockpitPanel*>(dock->widget()))panel=value;require(panel&&panel->instrumentPanel()->profile().id=="mq9","UAV workspace did not open MQ-9 console");
        widget<QToolButton>(*panel,"cockpit_payload_connect")->click();pump(700);require(widget<QComboBox>(*panel,"cockpit_payload_source")->count()==2,"actual workbench did not route platform-filtered UAV sessions");auto* power=widget<QToolButton>(*panel,"cockpit_payload_power");require(power->isEnabled(),"connected UAV payload power control disabled");const auto id=widget<QComboBox>(*panel,"cockpit_payload_source")->currentData().toUInt();power->click();pump(650);require(!broker.isEquipmentPowered(id),"UAV power click did not reach existing backend executor");power->click();pump(650);require(broker.isEquipmentPowered(id),"UAV power acknowledgement did not allow restart");workbench.hide();drain();
    }
    for(uint32_t id:{7101u,7102u,7103u})registry.unregisterEquipment(id);
    std::cout<<"PASS: MQ-9 EO/IR physical values, frame zero, binding identity, platform isolation, surface revision, disconnect and real workbench power execution\n";
}
void workspaceContract(){
    const auto profile=WorkspaceProfile::preset("aircraft");bool found=false;for(const auto& p:profile.panels)found|=p.kind=="cockpit";require(found,"aircraft preset did not expose the cockpit");
    WorkspaceProfile parsed;QString error;require(WorkspaceProfile::parse(profile.serialize(),parsed,error),"cockpit workspace cannot roundtrip");
    auto& net=BrokerNet::instance();net.setPlatformTelemetry(json::array({telemetry()}).dump());
    ControlWorkbench workbench("aircraft",{},nullptr,false);workbench.show();pump(180);AircraftCockpitPanel* panel=nullptr;
    for(auto* dock:workbench.findChildren<QDockWidget*>())if(auto* value=dynamic_cast<AircraftCockpitPanel*>(dock->widget()))panel=value;
    require(panel&&panel->controls()->selectedTelemetry()==telemetry(),"display workbench failed to route authoritative pose into cockpit");
    require(!panel->earthDisplay()->viewport(),"offscreen workstation created a Vulkan window");workbench.hide();
    std::cout<<"PASS: aircraft workspace assembly and live broker metadata routing\n";
}
void nativeCapture(const QString& output,const QString& map){
    require(QApplication::platformName()=="xcb","native panel capture requires X11");require(QDir().mkpath(output),"cannot create panel artifact directory");
    initializeBroker();auto& net=BrokerNet::instance();PhysicsEngine physics;physics.setGravity({0,-9.81,0});platform::PlatformManager manager;require(manager.initialize({}),"platform initialization");manager.setPhysicsEngine(&physics);
    platform::MotionState initial;initial.position={0,1100,0};initial.velocity={0,0,-120};const auto id=manager.createPlatform("F-16C panel / simulation aircraft",platform::PlatformCategory::AIRCRAFT,{},initial);manager.onStart();double time=0;
    const json context={{"geo_anchor",{{"lat",25.078},{"lon",121.772},{"alt",364.}}},{"earth",{{"map",std::filesystem::absolute(map.toStdString()).string()}}},{"simulation",{{"epoch_utc","2026-11-18T04:00:00Z"}}}};
    auto publish=[&]{auto samples=manager.getAllPlatformsSnapshot();for(auto& item:samples)item["sim_time"]=time;net.setSimMeta("cockpit_acceptance",time,20,false);net.setEarthContext(context.dump());net.setPlatformTelemetry(samples.dump());};publish();
    ControlWorkbench workbench("aircraft",{},nullptr,false);workbench.resize(1520,1050);AircraftCockpitPanel* panel=nullptr;
    for(auto* dock:workbench.findChildren<QDockWidget*>())if(auto* value=dynamic_cast<AircraftCockpitPanel*>(dock->widget()))panel=value;
    require(panel,"native console missing");widget<QComboBox>(*panel,"cockpit_view_mode")->setCurrentIndex(0);workbench.show();pump(180);
    require(panel&&panel->instrumentPanel()->profile().id=="f16c","product did not assemble the F-16 Qt panel");
    for(auto* timer:workbench.findChildren<QTimer*>())if(timer->interval()==50)timer->stop();
    auto refresh=[&]{const auto meta=net.lastSimMeta();panel->updateState(json::parse(meta.earth_json),json::parse(meta.platforms_json),time,true,false,meta.session_id,true,id);};refresh();
    auto* canvas=panel->instrumentPanel();require(!panel->earthDisplay()->isVisible()&&!panel->earthDisplay()->viewport(),"panel-only mode opened a hidden Vulkan globe");
    auto install=[&]{FlightControlCommand c;while(net.pollFlightControl(&c)){auto result=manager.applyFlightControl(c,time);require(result.stage=="applied","real flight model rejected panel HOTAS demand");net.completeFlightControl(result);}};
    widget<QPushButton>(*panel,"flight_take_control")->click();install();pump(30);publish();refresh();
    const auto before=manager.getPlatformSnapshot(id);const auto stick=canvas->controlPosition("stick");
    mouse(*canvas,QEvent::MouseButtonPress,stick,Qt::LeftButton,Qt::LeftButton);mouse(*canvas,QEvent::MouseMove,stick+QPoint(35,35),Qt::NoButton,Qt::LeftButton);install();
    for(int i=0;i<20;++i){TimeTick tick;tick.dt=.05;tick.sim_time=time+.05;manager.onPreTick(tick);physics.step(.05);manager.onPostTick(tick);time=tick.sim_time;install();}
    mouse(*canvas,QEvent::MouseButtonRelease,stick+QPoint(35,35),Qt::LeftButton,Qt::NoButton);install();publish();refresh();pump(50);install();
    const auto after=manager.getPlatformSnapshot(id);require(std::abs(after["pitch"].get<double>()-before["pitch"].get<double>())>.1&&std::abs(after["roll"].get<double>()-before["roll"].get<double>())>.1,"pointer-operated sidestick did not change authoritative flight attitude");
    require(workbench.grab().save(output+"/f16c-workbench.png"),"native workbench capture failed");require(canvas->grab().save(output+"/f16c-instruments.png"),"native instrument composition capture failed");
    const auto held=manager.getPlatformSnapshot(id);canvas->repaint();pump(30);canvas->repaint();const auto redrawn=manager.getPlatformSnapshot(id);for(const auto* field:{"position","velocity","pitch","roll","yaw"})require(redrawn[field]==held[field],"panel paint integrated a second flight model");
    json report={{"workspace","actual ControlWorkbench aircraft"},{"profile",canvas->profile().id.toStdString()},{"capture","native Qt dynamic instruments"},{"before",before},{"after",after},{"mouse_HOTAS_to_Bullet_flight",true},{"spring_return",after["flight_control"]["command"]["elevator"]==0&&after["flight_control"]["command"]["aileron"]==0},{"paused_paint_preserved_physics",true},{"missing_IAS_not_fabricated",!PlatformInstruments::measurement(panel->controls()->selectedTelemetry(),"air_data.ias_mps").has_value()},{"voxel_architecture_changed",false}};
    std::ofstream(output.toStdString()+"/f16c-acceptance.json")<<report.dump(2)<<'\n';workbench.hide();install();manager.shutdown();std::cout<<"PASS: actual native F-16 Qt panel, pointer HOTAS to real flight/Bullet, spring return, honest instrumentation and paused paint\n";
}

}
int main(int argc,char** argv){
    const bool native=argc==4&&(std::string(argv[1])=="--native-capture"||std::string(argv[1])=="--external-capture");if(!native)qputenv("QT_QPA_PLATFORM","offscreen");QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());QSettings::setPath(QSettings::NativeFormat,QSettings::UserScope,settings.path());ui::ThemeManager::applyDarkTheme(&app);
    try{if(native){if(std::string(argv[1])=="--external-capture")nativeCockpitExteriors(argv[2],argv[3]);else nativeCapture(argv[2],argv[3]);}else{require(argc==1,"usage: sim_aircraft_cockpit_tests [--native-capture|--external-capture OUT MAP]");cameraContract();operatorContract();instrumentContract();presentationContract();fleetContract();uavPayloadContract();workspaceContract();}BrokerNet::instance().stop();SimDataBroker::instance().serverOnline(false);return 0;}
    catch(const std::exception& e){BrokerNet::instance().stop();SimDataBroker::instance().serverOnline(false);std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
