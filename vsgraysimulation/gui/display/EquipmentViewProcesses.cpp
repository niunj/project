#include "EquipmentViewProcesses.h"
#include <vsgraysim/model_packages/SceneModelRegistry.h>
#include <QCoreApplication>
#include <QFileInfo>
#include <QUuid>
#include <QFile>
#include <fstream>
namespace vsgraysim::gui::display {
EquipmentViewProcesses::~EquipmentViewProcesses(){closeAll();for(auto& [key,p]:consoles_)if(p->state()!=QProcess::NotRunning&&!p->waitForFinished(800)){p->kill();p->waitForFinished(800);}for(auto& [id,v]:views_)if(v.process->state()!=QProcess::NotRunning&&!v.process->waitForFinished(800)){v.process->kill();v.process->waitForFinished(800);}}
bool EquipmentViewProcesses::open(const EquipmentInfo& equipment,const std::string& scene){
    if(!directory_.isValid()||scene.empty())return false;
    auto it=views_.find(equipment.equipment_id);if(it!=views_.end()&&it->second.process->state()!=QProcess::NotRunning){it->second.process->write("raise\n");return true;}
    if(it==views_.end()&&views_.size()>=24)return false;
    auto& view=views_[equipment.equipment_id];if(!view.path.isEmpty())QFile::remove(view.path);
    view.process=std::make_unique<QProcess>();view.equipment=equipment;view.scene=scene;view.report=nlohmann::json::object();
    view.path=directory_.path()+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".json";
    QString mode=equipment.type==SensorType::RADAR?"radar":equipment.type==SensorType::IR?"infrared":equipment.type==SensorType::SAR?"sar":equipment.type==SensorType::SONAR?"sonar":"optical";
    if(const auto* model=model_packages::equipmentModel(equipment.model_id))
        mode=QString::fromLatin1(model->console.data(),qsizetype(model->console.size()));
    view.process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    auto* process=view.process.get();connect(process,&QProcess::readyReadStandardOutput,this,[process]{process->readAllStandardOutput();});
    process->start(QCoreApplication::applicationFilePath(),{"--equipment-view="+QString::number(equipment.equipment_id),"--workmode="+mode,"--view-state="+view.path,"--view-scene="+QString::fromStdString(scene),"--view-managed=1"});return true;
}
void EquipmentViewProcesses::close(uint32_t id,bool force){auto it=views_.find(id);if(it==views_.end())return;if(force)it->second.process->kill();else {it->second.process->write("quit\n");it->second.process->closeWriteChannel();}}
bool EquipmentViewProcesses::openConsole(const QString& mode,uint32_t platform,const std::vector<EquipmentInfo>& equipment,const std::string& scene){
    if(!QStringList{"aircraft","helicopter","airliner","uav"}.contains(mode)||scene.empty())return false;const auto key=mode+"_"+QString::number(platform);auto& p=consoles_[key];if(p&&p->state()!=QProcess::NotRunning){p->write("raise\n");return true;}if(consoles_.size()>16)return false;
    p=std::make_unique<QProcess>();p->setProcessChannelMode(QProcess::ForwardedErrorChannel);auto* process=p.get();connect(process,&QProcess::readyReadStandardOutput,this,[process]{process->readAllStandardOutput();});QStringList ids;for(const auto& e:equipment)if(!platform||e.platform_id==platform)ids<<QString::number(e.equipment_id);
    process->start(QCoreApplication::applicationFilePath(),{"--console-view="+mode,"--workmode="+mode,"--equipment="+ids.join(","),"--preferred-platform="+QString::number(platform),"--view-scene="+QString::fromStdString(scene),"--view-managed=1"});return true;
}
void EquipmentViewProcesses::closeConsoles(){for(auto& [key,p]:consoles_)if(p->state()!=QProcess::NotRunning){p->write("quit\n");p->closeWriteChannel();}}
void EquipmentViewProcesses::closeAll(){closeConsoles();for(const auto& [id,v]:views_)close(id);}
void EquipmentViewProcesses::poll(){for(auto& [id,v]:views_){QFileInfo file(v.path);if(file.exists()&&file.size()<512*1024)try{std::ifstream input(v.path.toStdString());auto report=nlohmann::json::parse(input);if(report.value("equipment_view",uint32_t(0))==id&&report.value("expected_scene",std::string())==v.scene)v.report=std::move(report);}catch(const std::exception&){} }}
nlohmann::json EquipmentViewProcesses::status() const {auto out=nlohmann::json::array();for(const auto& [id,v]:views_){auto row=v.report;row["id"]=id;row["pid"]=v.process->processId();row["process_state"]=int(v.process->state());row["exit_code"]=v.process->exitCode();row["state_file"]=v.path.toStdString();out.push_back(row);}return out;}
nlohmann::json EquipmentViewProcesses::consoleStatus() const {auto out=nlohmann::json::array();for(const auto& [key,p]:consoles_)out.push_back({{"key",key.toStdString()},{"pid",p->processId()},{"process_state",int(p->state())},{"exit_code",p->exitCode()}});return out;}
}
