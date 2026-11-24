#include "display/EarthPanel.h"
#include "display/ControlWorkbench.h"
#include "display/WorkspaceProfile.h"
#include "base/ThemeManager.h"
#include "vsgraysim/broker/BrokerNet.h"
#include "vsgraysim/broker/SimDataBroker.h"
#include <QApplication>
#include <QAbstractItemModel>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace vsgraysim;
using namespace vsgraysim::gui::display;
using json=nlohmann::json;
namespace {
void require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
void pump(int ms=30){QElapsedTimer timer;timer.start();do{QApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}while(timer.elapsed()<ms);}
json fixture(){return {{"geo_anchor",{{"lat",25.078},{"lon",121.772},{"alt",0.0},{"heading_deg",12.0}}},
    {"earth",{{"map","/opt/vsgraysim/earth/mission.map.json"}}},{"terrain_ref","/opt/vsgraysim/earth/mission.map.json"},
    {"simulation",{{"epoch_utc","2026-11-06T02:15:00Z"}}},{"atmosphere",{{"cloud_cover",.7},{"rain",.4}}},
    {"ocean",{{"sea_level",1.3}}},{"source_scenario","/opt/vsgraysim/scenes/mission.json"},{"surface_revision",uint64_t(1234)}};}
json platforms(){return json::array({{{"platform_id",77},{"name","真实平台 A"},{"position",{100,1250.5,-25}}},
    {{"platform_id",88},{"name","真实平台 B"},{"position",{-30,750,-10}}}});}
void metadataContracts(){
    auto& net=BrokerNet::instance();BrokerNet::Config config;config.enabled=false;
    net.stop();net.startSimRole(config);SimDataBroker::instance().serverOnline(true);
    net.setSimMeta("earth_fixture",12.5,40,true);net.setEarthContext(fixture().dump());
    require(json::parse(net.lastSimMeta().earth_json)==fixture(),"Earth context was lost in authoritative simulation metadata");
    for(const auto& invalid:{std::string("[]"),std::string("{\"earth\":[]}"),std::string("{\"atmosphere\":1}"),std::string("{\"source_scenario\":true}"),std::string(1024*1024+1,' ')}){
        bool rejected=false;try{net.setEarthContext(invalid);}catch(const std::exception&){rejected=true;}
        require(rejected&&json::parse(net.lastSimMeta().earth_json)==fixture(),"invalid Earth metadata partially replaced live context");
    }
    net.setSimMeta("next_scene",0,0,false);require(net.lastSimMeta().earth_json=="{}","scene change retained prior Earth context");
    net.setEarthContext(fixture().dump());net.setSimMeta("next_scene",5,0,false);net.setSimMeta("next_scene",0,0,false);
    require(net.lastSimMeta().earth_json=="{}","time reset retained prior Earth context");
    net.setEarthContext(fixture().dump());net.clearLinks();require(net.lastSimMeta().earth_json=="{}","scene clearing retained prior Earth context");
    net.setEarthContext(fixture().dump());
    std::cout<<"PASS: bounded transactional Earth metadata, scenario changes and resets\n";
}
void panelContracts(const QString& capture){
    EarthPanel panel;panel.resize(700,450);panel.show();pump();
    require(!panel.renderingAvailable()&&panel.viewport()==nullptr,"offscreen Earth panel attempted to create a Vulkan window");
    require(panel.updateContext(fixture(),12.5,true,true),"valid Earth context rejected");
    const auto revision=panel.contextRevision();
    require(panel.contextJson()==fixture(),"Earth panel altered the authoritative dataset declaration");
    auto telemetry=platforms();telemetry.push_back(telemetry[0]);
    telemetry.push_back({{"platform_id",0},{"name","bad"},{"position",{0,0,0}}});
    telemetry.push_back({{"platform_id",1.5},{"name","fractional"},{"position",{0,0,0}}});
    telemetry.push_back({{"platform_id",5},{"name","bad coordinate"},{"position",{0,"invalid",0}}});
    telemetry.push_back({{"platform_id",6},{"name","nonfinite"},{"position",{0,std::numeric_limits<double>::infinity(),0}}});
    panel.updateTelemetry(telemetry,88);
    auto* choice=panel.findChild<QComboBox*>("earth_platform");
    require(panel.visiblePlatformCount()==2&&choice&&choice->currentData().toUInt()==88,"platform selection or malformed telemetry filtering failed");
    require(panel.findChild<QLabel*>("earth_platform_position")->text().contains("750.0"),"Earth panel did not show the selected physical position");
    int listMutations=0;
    QObject::connect(choice->model(),&QAbstractItemModel::rowsRemoved,&panel,[&]{++listMutations;});
    QObject::connect(choice->model(),&QAbstractItemModel::rowsInserted,&panel,[&]{++listMutations;});
    auto moved=platforms();moved[1]["position"][1]=760.0;panel.updateTelemetry(moved,77);
    require(listMutations==0&&choice->currentData().toUInt()==88,"pose-only telemetry rebuilt the open platform selector or lost operator selection");
    require(panel.findChild<QLabel*>("earth_unavailable")->text().contains(QStringLiteral("离屏")),"offscreen rendering unavailability was hidden");
    require(panel.updateContext(fixture(),13.0,true,false)&&panel.contextRevision()==revision,"clock-only update reloaded unchanged Earth configuration");
    require(panel.findChild<QLabel*>("earth_context_status")->text().contains(QStringLiteral("已暂停")),"pause state is not synchronized with simulation metadata");
    auto invalid=fixture();invalid["geo_anchor"]["lat"]=95;
    require(!panel.updateContext(invalid,14,true,true)&&panel.contextJson()==fixture(),"invalid WGS84 context partially committed");
    require(panel.updateContext(fixture(),13,true,false),"valid context could not recover after malformed update");
    auto changed=fixture();changed["atmosphere"]["rain"]=.8;panel.updateContext(changed,13,true,false);
    require(panel.contextRevision()==revision+1,"weather context change was not tracked");
    require(panel.grab().save(capture),"Earth panel screenshot failed");
    panel.hide();pump();require(panel.viewport()==nullptr,"hidden offscreen panel allocated a renderer");
    panel.updateContext(json::object(),0,false,false);require(panel.visiblePlatformCount()==0&&panel.contextJson().empty(),"cleared Earth context retained stale platform state");
    std::cout<<"PASS: shared Earth panel metadata, physical telemetry, pause, deduplication and offscreen lifecycle\n";
}
void workspaceContracts(){
    for(const auto& id:WorkspaceProfile::presetIds()){
        const auto profile=WorkspaceProfile::preset(id);bool earth=false;for(const auto& spec:profile.panels)earth|=spec.kind=="earth";
        require(earth,"built-in workstation omitted the shared Earth situation panel");
    }
    WorkspaceProfile profile;profile.id="custom_earth";profile.title=QStringLiteral("地球定制工作台");
    profile.panels={{"earth_main","earth",QStringLiteral("地球主屏"),0,"right"},{"earth_follow","earth",QStringLiteral("跟随副屏"),88,"bottom"}};
    WorkspaceProfile parsed;QString error;require(WorkspaceProfile::parse(profile.serialize(),parsed,error)&&parsed.panels.size()==2,"custom Earth panel profile roundtrip failed");
    ControlWorkbench workbench("mission",{},nullptr,false);workbench.applyWorkspace(profile);workbench.resize(1000,700);workbench.show();pump(600);
    int count=0;for(auto* dock:workbench.findChildren<QDockWidget*>())if(auto* earth=dynamic_cast<EarthPanel*>(dock->widget())){
        ++count;require(earth->contextJson()==fixture(),"workbench did not route authoritative Earth context to a custom panel");
        require(earth->viewport()==nullptr,"custom offscreen workbench created a Vulkan renderer");
    }
    require(count==2,"custom workbench failed to assemble two independent shared Earth panels");
    std::cout<<"PASS: Earth panel factory, presets and custom workbench metadata routing\n";
}
}
int main(int argc,char** argv){
    qputenv("QT_QPA_PLATFORM","offscreen");QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());QSettings::setPath(QSettings::NativeFormat,QSettings::UserScope,settings.path());
    ui::ThemeManager::applyDarkTheme(&app);
    try{require(settings.isValid(),"cannot isolate Earth display settings");metadataContracts();panelContracts(QDir::tempPath()+"/vsgraysim_earth_display.png");workspaceContracts();BrokerNet::instance().stop();SimDataBroker::instance().serverOnline(false);return 0;}
    catch(const std::exception& e){BrokerNet::instance().stop();SimDataBroker::instance().serverOnline(false);std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
