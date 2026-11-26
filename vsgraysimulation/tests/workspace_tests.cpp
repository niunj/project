#include "display/WorkspaceProfile.h"
#include "display/ControlWorkbench.h"
#include "display/FlightPanel.h"
#include "display/ConsoleSelector.h"
#include "display/AircraftCockpitPanel.h"
#include "display/PlatformInstruments.h"
#include <QComboBox>
#include "display/SensorPanels.h"
#include "base/ThemeManager.h"
#include "base/ProductWorkflow.h"
#include "vsgraysim/broker/EquipmentInfo.h"
#include "vsgraysim/broker/SimDataBroker.h"

#include <QApplication>
#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTreeWidget>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace vsgraysim;
using namespace vsgraysim::gui::display;
namespace {
void require(bool condition,const char* reason) {
    if(!condition) throw std::runtime_error(reason);
}
void pump(int milliseconds=80) {
    QElapsedTimer timer; timer.start();
    do { QApplication::processEvents(QEventLoop::AllEvents,10); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete); QThread::msleep(1); }
    while(timer.elapsed()<milliseconds);
}
void capture(QWidget& widget,const QString& path) {
    widget.show(); pump(30);
    const auto pixmap=widget.grab();
    require(!pixmap.isNull() && pixmap.width()>100 && pixmap.height()>100,"Qt workspace capture is empty");
    require(pixmap.save(path),"cannot save Qt workspace capture");
    std::cout<<"UI capture: "<<path.toStdString()<<'\n';
}
void consoleSelectorContract(const QString& captures){
    WorkbenchSelector selector;selector.show();pump(30);for(const QString id:{"mission","aircraft","radar","infrared","optical"})require(selector.findChild<QPushButton*>("workbench_"+id)&&selector.findChild<QPushButton*>("workbench_"+id)->height()>=120,"large workbench selector omitted or collapsed a console");capture(selector,captures+"/console_selector.png");selector.findChild<QPushButton*>("workbench_infrared")->click();require(selector.result()==QDialog::Accepted&&selector.selectedMode()=="infrared","console selection opened wrong workbench");
}
void profileContracts(const QString& directory) {
    QString error;
    for(const auto& id:WorkspaceProfile::presetIds()) {
        const auto preset=WorkspaceProfile::preset(id);
        WorkspaceProfile copy;
        require(WorkspaceProfile::parse(preset.serialize(),copy,error),"built-in profile failed its own schema");
        require(copy.id==id && copy.title==preset.title && copy.panels.size()==preset.panels.size(),"profile roundtrip changed its identity");
        for(std::size_t i=0;i<copy.panels.size();++i) {
            require(copy.panels[i].id==preset.panels[i].id && copy.panels[i].kind==preset.panels[i].kind &&
                    copy.panels[i].area==preset.panels[i].area,"profile roundtrip changed its panel contract");
        }
        const auto file=directory+"/"+id+".workspace.json";
        require(copy.save(file,error) && WorkspaceProfile::load(file,copy,error),"atomic workspace file save/load failed");
    }
    auto custom=WorkspaceProfile::preset("uav");
    custom.id="operator_alpha"; custom.title=QStringLiteral("验证任务台");
    custom.panels.push_back({"thermal_right","ir",QStringLiteral("右侧红外"),82,"right"});
    custom.dock_state=QByteArray("\0\1\2\3\4",5);
    WorkspaceProfile copy;
    require(WorkspaceProfile::parse(custom.serialize(),copy,error) && copy.dock_state==custom.dock_state &&
            copy.panels.back().equipment_id==82,"custom layout or equipment assignment was lost");
    auto json=QJsonDocument::fromJson(custom.serialize()).object();
    auto reject=[&](QJsonObject invalid,const char* reason) {
        WorkspaceProfile result=copy;
        require(!WorkspaceProfile::parse(QJsonDocument(invalid).toJson(),result,error) && !error.isEmpty(),reason);
        require(result.serialize()==copy.serialize(),"failed profile parse partially overwrote live layout");
    };
    auto invalid=json; auto panels=invalid["panels"].toArray(); panels.append(panels[0]); invalid["panels"]=panels;
    reject(invalid,"duplicate stable panel IDs accepted");
    invalid=json; panels=invalid["panels"].toArray(); auto panel=panels[0].toObject(); panel["equipment_id"]=1.25; panels[0]=panel; invalid["panels"]=panels;
    reject(invalid,"fractional equipment ID accepted");
    invalid=json; panels=invalid["panels"].toArray(); panel=panels[0].toObject(); panel["kind"]="not_installed"; panels[0]=panel; invalid["panels"]=panels;
    reject(invalid,"unknown panel factory accepted");
    invalid=json; invalid["dock_state"]="%%%invalid_base64"; reject(invalid,"corrupt dock state accepted");
    invalid=json; invalid["dock_state"]=QJsonObject{}; reject(invalid,"non-string dock state silently discarded");
    invalid=json; invalid["version"]=999; reject(invalid,"unsupported workspace schema accepted");
    std::cout<<"PASS: workspace presets, custom layout, atomic files and transactional schema rejection\n";
}
void workflowAssets(const QString& root) {
    const QString scene=root+QStringLiteral("/带空格的场景目录");
    require(QDir().mkpath(scene+"/assets"),"cannot create scene-relative fixture");
    const QString file=scene+"/assets/model.obj";
    {QFile f(file);require(f.open(QIODevice::WriteOnly),"asset fixture write");f.write("v 0 0 0\n");}
    require(vsgraysim::ui::ProductWorkflow::resolveSceneAsset("assets/model.obj",scene+"/mission.json",root)==QFileInfo(file).canonicalFilePath(),"scenario-relative asset lost across product handoff");
    require(vsgraysim::ui::ProductWorkflow::resolveSceneAsset("missing.obj",scene+"/mission.json",root).isEmpty(),"missing asset silently resolved");
}
QAction* action(QWidget& widget,const QString& title) {
    for(auto* candidate:widget.findChildren<QAction*>()) if(candidate->text()==title) return candidate;
    throw std::runtime_error("common workspace action missing: "+title.toStdString());
}
void commonShell(const QString& captures) {
    vsgraysim::ui::BaseWindow window(QStringLiteral("公共界面验证"));
    window.setWorkspacePersistenceEnabled(false);
    window.buildBaseStatusBar(); window.buildBaseMenuBar(); window.buildBaseToolBar();
    window.createDock(QStringLiteral("场景结构"),Qt::LeftDockWidgetArea,160,"test.scene");
    window.createDock(QStringLiteral("参数面板"),Qt::RightDockWidgetArea,160,"test.properties");
    window.resize(960,650); window.show(); pump(); window.finalizeWorkspace(true);
    const auto docks=window.findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly);
    require(docks.size()==2,"common dock builder lost ownership");
    docks[0]->hide(); window.resetWorkspaceLayout(); pump();
    require(!docks[0]->isHidden(),"restore-default layout failed to reopen a closed panel");
    auto* lock=action(window,QStringLiteral("锁定面板布局")); lock->setChecked(true);
    for(auto* dock:docks) require(!dock->features().testFlag(QDockWidget::DockWidgetMovable) &&
        !dock->features().testFlag(QDockWidget::DockWidgetFloatable),"layout lock did not prevent moving/floating panels");
    lock->setChecked(false);
    auto* focus=action(window,QStringLiteral("专注工作区")); focus->setChecked(true);
    for(auto* dock:docks) require(dock->isHidden(),"focus workspace left a visible dock");
    focus->setChecked(false); pump();
    for(auto* dock:docks) require(!dock->isHidden(),"focus workspace forgot the previous visible panel set");
    vsgraysim::ui::ThemeManager::setCompactDensity(qApp,true);
    require(vsgraysim::ui::ThemeManager::compactDensity(),"shared compact density was not persisted");
    vsgraysim::ui::ThemeManager::setCompactDensity(qApp,false);
    capture(window,captures+"/common_shell.png"); window.close();
    std::cout<<"PASS: common dock visibility, layout lock, focus mode and shared appearance settings\n";
}
void publishFixture() {
    auto& broker=SimDataBroker::instance();
    RadarFrame radar; radar.sensor_id=81; radar.frame_id=0; radar.sim_time=.25;
    radar.resize(64,32); radar.prf=800; radar.sample_rate_hz=2e6; radar.carrier_hz=10e9;
    radar.stage=2; radar.coherent_pulses=32;
    radar.rd_map[16*64+20]={20,5}; require(broker.publishRadar(radar),"UI radar fixture publication failed");
    IrFrame infrared; infrared.sensor_id=82; infrared.frame_id=0; infrared.sim_time=.25; infrared.stage=0;
    infrared.resize(24,16);
    for(std::size_t i=0;i<infrared.radiance.size();++i) infrared.radiance[i]=1.5f+float(i)/infrared.radiance.size();
    require(broker.publishIr(infrared),"UI radiance fixture publication failed");
    VisFrame visible; visible.sensor_id=83; visible.frame_id=0; visible.sim_time=.25; visible.stage=4;
    visible.resize(24,16);
    for(std::size_t i=0;i<visible.intensity.size();++i) visible.intensity[i]=float(i%24)/23;
    require(broker.publishVis(visible),"UI electronic image fixture publication failed");
    SarFrame sar; sar.sensor_id=84; sar.frame_id=0; sar.sim_time=.25; sar.stage=0;
    sar.resize(32,8); sar.sample_rate_hz=2e6; sar.prf=800; sar.echo_data[20]={2,1};
    require(broker.publishSar(sar),"UI raw SAR fixture publication failed");
}
void assembledWorkspaces(const QString& captures) {
    auto& registry=EquipmentRegistry::instance(); auto& broker=SimDataBroker::instance();
    registry.clear(); broker.serverOnline(true);
    const std::vector<std::pair<uint32_t,SensorType>> ids{{81,SensorType::RADAR},{82,SensorType::IR},{83,SensorType::VIS},{84,SensorType::SAR}};
    for(const auto& [id,type]:ids) {
        EquipmentInfo info; info.equipment_id=id; info.type=type;
        info.name="验证数据 #"+std::to_string(id); info.platform_id=901; info.platform_name="验证平台"; info.frame_rate=20;
        registry.registerEquipment(info);
    }
    {
        ControlWorkbench workbench("mission",{},nullptr,false);
        workbench.resize(1360,860); workbench.show(); pump();
        for(auto* button:workbench.findChildren<QPushButton*>()) if(button->text()==QStringLiteral("发现装备")) button->click();
        auto* tree=workbench.findChild<QTreeWidget*>("equipment_tree"); require(tree,"equipment discovery tree missing");
        for(int t=0;t<tree->topLevelItemCount();++t) for(int i=0;i<tree->topLevelItem(t)->childCount();++i)
            tree->topLevelItem(t)->child(i)->setCheckState(0,Qt::Checked);
        auto* bind=workbench.findChild<QPushButton*>("bind_equipment"); require(bind,"bind equipment control missing"); bind->click();
        for(const auto& [id,type]:ids) require(broker.isSubscribed(id),"workbench did not bind a discovered equipment channel");
        publishFixture(); pump(600);
        for(const auto& profileId:QStringList{"radar","aircraft","uav"}) {
            QList<QPointer<QDockWidget>> previous;
            for(auto* dock:workbench.findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly))
                if(dock->objectName().startsWith("panel_")) previous.push_back(dock);
            const auto preset=WorkspaceProfile::preset(profileId); workbench.applyWorkspace(preset); pump(600);
            for(auto old:previous) require(old.isNull(),"switching template retained an old dock/panel tree");
            for(const auto& [id,type]:ids) require(broker.isSubscribed(id),"switching template silently detached a live session");
            int panels=0;
            for(auto* dock:workbench.findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly))
                if(dock->objectName().startsWith("panel_")) ++panels;
            require(panels==int(preset.panels.size()),"template panel count grew after switching");
            require(workbench.workspaceProfile().id==profileId,"workbench did not adopt template identity");
            capture(workbench,captures+"/workspace_"+profileId+".png");
        }
        for(const QString mode:{"radar","infrared","optical","aircraft","mission"}){
            workbench.selectConsole(mode);pump(120);require(workbench.workspaceProfile().id==mode,"large workbench console navigation failed");
            for(const auto& [id,type]:ids)require(broker.isSubscribed(id),"console navigation discarded a connected device");
            for(const auto& panel:workbench.workspaceProfile().panels)if(panel.kind=="ir"||panel.kind=="vis"||panel.kind=="radar")require(workbench.findChild<QComboBox*>("source_"+panel.id),"sensor console omitted independent source selector");
        }
        workbench.selectConsole("aircraft");QString profileError;require(workbench.setCockpitProfile("config/cockpits/ah64d.json",profileError),"selected cockpit profile failed to load");workbench.selectConsole("radar");workbench.selectConsole("aircraft");require(workbench.workspaceProfile().cockpit_profile.endsWith("ah64d.json"),"console switching forgot selected aircraft instruments");
        auto customized=WorkspaceProfile::preset("uav"); customized.id="test_custom"; customized.title=QStringLiteral("定制双光工作台");
        customized.panels.push_back({"ir_secondary","ir",QStringLiteral("红外副屏"),82,"top"});
        workbench.applyWorkspace(customized); pump(600);
        require(workbench.dockWidgetArea(workbench.findChild<QDockWidget*>("panel_ir_secondary"))==Qt::TopDockWidgetArea,
            "custom top panel was silently moved into the right dock group");
        const QString file=captures+"/roundtrip.workspace.json"; QString error;
        require(workbench.exportWorkspace(file,error),"workbench export failed");
        const auto exported=workbench.workspaceProfile();
        workbench.applyWorkspace(WorkspaceProfile::preset("radar"));
        require(workbench.importWorkspace(file,error),"workbench import failed");
        pump(600); const auto imported=workbench.workspaceProfile();
        require(imported.id==exported.id && imported.panels.size()==exported.panels.size() &&
            imported.panels.back().equipment_id==82,"import changed custom panel bindings");
        require(!imported.dock_state.isEmpty(),"workbench export omitted dock positions");
        capture(workbench,captures+"/workspace_custom.png");
        for(auto* table:workbench.findChildren<QTableWidget*>()) if(table->columnCount()==6)
            require(table->rowCount()==int(ids.size()),"template switch discarded the live session table");
    }
    pump(30);
    for(const auto& [id,type]:ids) require(!broker.isSubscribed(id),"destroyed workbench retained an equipment subscription");
    registry.clear(); broker.serverOnline(false);
    std::cout<<"PASS: real Broker fixtures, three templates, retained sessions, dock destruction and custom layout import/export\n";
}
void flightTelemetry(const QString& captures) {
    FlightPanel panel; panel.resize(560,450);
    nlohmann::json valid=nlohmann::json::array({{{"platform_id",901},{"name","验证飞行平台"},{"roll",15},{"pitch",8},{"yaw",73},
        {"speed",42},{"position",{100,1200,70}},{"velocity",{30,5,0}},{"sim_time",.25}}});
    panel.updateTelemetry(valid,true,true,901); capture(panel,captures+"/flight_telemetry_fixture.png");
    auto malformed=valid; malformed[0]["velocity"][1]="invalid"; malformed[0]["sim_time"]="invalid";
    panel.updateTelemetry(malformed,true,true,901); panel.grab();
    panel.updateTelemetry(nlohmann::json::array({17,{{"platform_id","wrong"},{"name",23}}}),true,true,901); panel.grab();
    panel.updateTelemetry(valid,false,false,901); capture(panel,captures+"/flight_offline.png");
    std::cout<<"PASS: truthful flight telemetry, offline state and malformed payload rejection\n";
}
}
int main(int argc,char** argv) {
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM","offscreen");
    QApplication application(argc,argv);
    QTemporaryDir configuration; require(configuration.isValid(),"cannot isolate GUI settings");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,configuration.path());
    QSettings::setPath(QSettings::NativeFormat,QSettings::UserScope,configuration.path());
    application.setOrganizationName("vsgraysim"); application.setApplicationName("workspace_contract_tests");
    application.setQuitOnLastWindowClosed(false);
    vsgraysim::ui::ThemeManager::applyDarkTheme(&application);
    QString captures=QDir::tempPath()+"/vsgraysim_workspace_ui";
    const auto args=application.arguments(); const int at=args.indexOf("--artifacts");
    if(at>=0 && at+1<args.size()) captures=QDir(args[at+1]).absolutePath();
    try {
        require(QDir().mkpath(captures),"cannot create screenshot directory");
        consoleSelectorContract(captures);profileContracts(configuration.path()); workflowAssets(configuration.path()); commonShell(captures);
        assembledWorkspaces(captures); flightTelemetry(captures);
        std::cout<<"PASS: workspace UI integration and offscreen Qt artifacts\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n'; return 1; }
}
