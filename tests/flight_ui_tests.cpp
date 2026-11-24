#include "display/FlightPanel.h"
#include "base/ThemeManager.h"
#include "vsgraysim/broker/BrokerNet.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>

using namespace vsgraysim;
using namespace vsgraysim::gui::display;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(int ms=120){QElapsedTimer elapsed;elapsed.start();do{QApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}while(elapsed.elapsed()<ms);}
template<class T>T* widget(FlightPanel& panel,const char* id){auto* value=panel.findChild<T*>(id);require(value,"cockpit widget missing");return value;}
nlohmann::json telemetry(uint32_t id=77){
    return {{"platform_id",id},{"name","飞控闭环验证平台"},{"roll",15},{"pitch",8},{"yaw",73},{"speed",120},
        {"position",{100,1200,70}},{"velocity",{30,5,0}},{"sim_time",2.5},
        {"flight_control",{{"available",true},{"mode","manual"},{"owner",""},{"has_route",false},
            {"actual",{{"throttle",.6},{"elevator",.05},{"aileron",.1},{"rudder",0}}},
            {"command",{{"throttle",.6},{"elevator",.05},{"aileron",.1},{"rudder",0},{"target_speed",120},{"target_altitude",1200},{"target_heading",73}}}}}};
}
FlightControlCommand nextCommand(BrokerNet& net){FlightControlCommand c;require(net.pollFlightControl(&c),"UI action did not enqueue a flight command");return c;}
void applied(BrokerNet& net,const FlightControlCommand& c){auto r=flightResult(c,"applied","applied");r.revision=17;r.sim_time=2.5;net.completeFlightControl(r);}
void cockpitContract(){
    auto& net=BrokerNet::instance();net.stop();BrokerNet::Config config;config.enabled=false;net.startSimRole(config);
    SimDataBroker::instance().serverOnline(true);net.setSimMeta("cockpit_fixture",2.5,20,false);net.setFlightControlAvailable(true);
    const auto session=net.flightSessionId();require(!session.empty(),"simulation session unavailable");
    auto samples=nlohmann::json::array({telemetry()});
    FlightPanel panel;panel.resize(560,860);panel.show();pump(20);
    panel.updateTelemetry(samples,true,false,77);require(!widget<QPushButton>(panel,"flight_take_control")->isEnabled(),"cockpit armed without control service context");
    panel.updateControlContext(session,true);require(widget<QPushButton>(panel,"flight_take_control")->isEnabled(),"capable fresh platform could not be explicitly acquired");
    QElapsedTimer submission;submission.start();widget<QPushButton>(panel,"flight_take_control")->click();require(submission.elapsed()<100,"cockpit command blocked the UI thread");
    require(panel.controlActive(),"explicit takeover did not arm the panel");auto first=nextCommand(net);
    require(first.platform_id==77 && first.session_id==session && first.mode=="manual" && first.throttle==.6 && !first.release,"takeover did not preserve the acknowledged setpoint snapshot");
    require(widget<QLabel>(panel,"flight_command_status")->text().contains(QStringLiteral("等待")),"submission was falsely marked executed");
    applied(net,first);pump();require(widget<QLabel>(panel,"flight_command_status")->text().contains(QStringLiteral("待推进")),"paused controller installation was mislabeled as physical advancement");
    FlightPanel second;second.resize(560,860);second.show();second.updateControlContext(session,true);second.updateTelemetry(samples,true,false,77);pump(20);
    require(!widget<QPushButton>(second,"flight_take_control")->isEnabled(),"another panel could contend for the same local control lease");
    auto* throttle=widget<QDoubleSpinBox>(panel,"flight_throttle");throttle->setValue(75);
    pump(520);auto renewal=nextCommand(net);require(renewal.throttle==.6 && renewal.mode=="manual","lease renewal executed an uncommitted cockpit edit");applied(net,renewal);pump(20);
    widget<QDoubleSpinBox>(panel,"flight_elevator")->setValue(25);widget<QDoubleSpinBox>(panel,"flight_aileron")->setValue(-30);
    widget<QPushButton>(panel,"flight_apply")->click();auto manual=nextCommand(net);
    require(manual.throttle==.75 && manual.elevator==.25 && manual.aileron==-.3,"manual controls did not reach the command snapshot");
    applied(net,manual);pump();
    auto changed=samples;changed[0]["flight_control"]["command"]["throttle"]=.1;panel.updateTelemetry(changed,true,false,77);
    require(throttle->value()==75,"heartbeat overwrote the operator's pending setpoint");
    require(widget<QLabel>(panel,"flight_actual")->text().contains(QStringLiteral("60.0")),"actual actuator telemetry was replaced with demanded values");
    auto* mode=widget<QComboBox>(panel,"flight_mode");mode->setCurrentIndex(mode->findData("altitude_hold"));
    widget<QDoubleSpinBox>(panel,"flight_target_speed")->setValue(140);widget<QDoubleSpinBox>(panel,"flight_target_altitude")->setValue(1300);widget<QDoubleSpinBox>(panel,"flight_target_heading")->setValue(91);
    widget<QPushButton>(panel,"flight_apply")->click();auto hold=nextCommand(net);
    require(hold.mode=="altitude_hold" && hold.target_speed==140 && hold.target_altitude==1300 && hold.target_heading==91,"hold targets lost before worker delivery");
    applied(net,hold);pump();
    require(QDir().mkpath("/tmp/vsgraysim_workspace_ui"),"cannot create UI artifact directory");require(panel.grab().save("/tmp/vsgraysim_workspace_ui/flight_control_closed_loop.png"),"cannot capture real cockpit controls");
    widget<QPushButton>(panel,"flight_apply")->click();auto rejected=nextCommand(net);net.completeFlightControl(flightResult(rejected,"rejected","outside_envelope","target exceeds airframe envelope"));pump(250);
    require(!panel.controlActive(),"rejected command left cockpit controls armed");require(widget<QLabel>(panel,"flight_command_status")->text().contains("target exceeds airframe envelope"),"execution rejection reason was hidden from the operator");
    auto release=nextCommand(net);require(release.release && release.client_id==first.client_id,"rejection did not release the original controller lease");applied(net,release);pump();
    panel.updateTelemetry(samples,true,false,77);widget<QPushButton>(panel,"flight_take_control")->click();auto acquire=nextCommand(net);applied(net,acquire);pump();
    panel.hide();require(!panel.controlActive(),"hidden panel retained active control");require(nextCommand(net).release,"hiding cockpit did not request release");
    panel.show();panel.updateTelemetry(samples,true,false,77);pump(20);widget<QPushButton>(panel,"flight_take_control")->click();auto beforeReset=nextCommand(net);applied(net,beforeReset);
    panel.updateControlContext("replacement_session",true);require(!panel.controlActive(),"session replacement retained old control state");pump();
    require(!panel.controlActive(),"old flight demand replayed into replacement session");
    require(nextCommand(net).release,"session replacement did not release the old controller");
    FlightPanel leasePanel;leasePanel.resize(560,860);leasePanel.show();leasePanel.updateControlContext(session,true);leasePanel.updateTelemetry(samples,true,false,77);
    widget<QPushButton>(leasePanel,"flight_take_control")->click();auto leased=nextCommand(net);applied(net,leased);
    auto owned=samples;owned[0]["flight_control"]["owner"]=leased.client_id;owned[0]["flight_control"]["lease_remaining_ms"]=1500;leasePanel.updateTelemetry(owned,true,false,77);
    require(leasePanel.controlActive(),"acknowledged lease was lost before expiry");
    auto expired=samples;expired[0]["flight_control"]["failsafe"]=true;expired[0]["flight_control"]["lease_remaining_ms"]=0;leasePanel.updateTelemetry(expired,true,false,77);
    require(!leasePanel.controlActive()&&nextCommand(net).release,"expired server lease silently re-acquired itself");leasePanel.hide();
    FlightPanel routePanel;routePanel.resize(560,860);routePanel.show();routePanel.updateControlContext(session,true);
    auto routed=samples;routed[0]["flight_control"]["has_route"]=true;routePanel.updateTelemetry(routed,true,false,77);
    widget<QPushButton>(routePanel,"flight_take_control")->click();auto routeAcquire=nextCommand(net);applied(net,routeAcquire);
    auto routeOwned=routed;routeOwned[0]["flight_control"]["owner"]=routeAcquire.client_id;routeOwned[0]["flight_control"]["lease_remaining_ms"]=1500;routePanel.updateTelemetry(routeOwned,true,false,77);
    auto* routeMode=widget<QComboBox>(routePanel,"flight_mode");routeMode->setCurrentIndex(routeMode->findData("route"));widget<QPushButton>(routePanel,"flight_apply")->click();auto route=nextCommand(net);require(route.mode=="route","route mode was not submitted");applied(net,route);
    routed[0]["flight_control"]["mode"]="route";routePanel.updateTelemetry(routed,true,false,77);pump();
    require(!routePanel.controlActive()&&widget<QLabel>(routePanel,"flight_command_status")->text().contains(QStringLiteral("自主执行")),"route acknowledgment did not transfer control to autonomous execution");
    routePanel.hide();pump(520);FlightControlCommand unwanted;require(!net.pollFlightControl(&unwanted),"route handoff or hidden panel cancelled the autonomous route");
    FlightPanel initialPanel;initialPanel.resize(560,860);initialPanel.show();initialPanel.updateControlContext(session,true);
    auto initial=samples;initial[0]["flight_control"]["actual"]=nlohmann::json::object();initial[0]["flight_control"]["command"]["throttle"]=.23;
    initial[0]["flight_control"]["limits"]={{"min_speed",60},{"max_speed",170},{"min_altitude",200},{"max_altitude",3500}};initialPanel.updateTelemetry(initial,true,false,77);
    widget<QPushButton>(initialPanel,"flight_take_control")->click();auto initialControl=nextCommand(net);
    require(initialControl.throttle==.23,"initial acquisition imposed 50 percent throttle instead of the server demand");
    auto* limitedSpeed=widget<QDoubleSpinBox>(initialPanel,"flight_target_speed");auto* limitedAltitude=widget<QDoubleSpinBox>(initialPanel,"flight_target_altitude");
    require(limitedSpeed->minimum()==60&&limitedSpeed->maximum()==170&&limitedAltitude->minimum()==200&&limitedAltitude->maximum()==3500,"airframe limits were not applied on acquisition");
    limitedSpeed->setValue(135);auto revisedLimits=initial;revisedLimits[0]["flight_control"]["limits"]["max_speed"]=100;initialPanel.updateTelemetry(revisedLimits,true,false,77);
    require(limitedSpeed->value()==135&&limitedSpeed->maximum()==170,"periodic telemetry changed an uncommitted draft or its input limits");
    initialPanel.hide();require(nextCommand(net).release,"initial cockpit did not release on hide");
    FlightPanel legacy;auto readOnly=samples;readOnly[0].erase("flight_control");legacy.updateTelemetry(readOnly,true,true,77);require(widget<QGroupBox>(legacy,"flight_controls")->isHidden(),"legacy telemetry invented unsupported flight controls");
    second.hide();panel.hide();net.stop();SimDataBroker::instance().serverOnline(false);
    std::cout<<"PASS: cockpit explicit takeover, nonblocking delivery, target/actual separation, hold targets, rejection, leases, paused state and session isolation\n";
}
}
int main(int argc,char** argv){
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))qputenv("QT_QPA_PLATFORM","offscreen");QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    QTemporaryDir settings;QSettings::setPath(QSettings::NativeFormat,QSettings::UserScope,settings.path());QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    vsgraysim::ui::ThemeManager::applyDarkTheme(&app);
    try{cockpitContract();return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
