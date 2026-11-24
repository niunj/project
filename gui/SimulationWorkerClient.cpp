#include "SimulationWorkerClient.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QUuid>
#include <fstream>

namespace vsgraysim::gui {
SimulationWorkerClient::SimulationWorkerClient(QObject* parent):QObject(parent) {
    process_.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    // Consume incidental library stdout continuously; control/status uses atomic
    // bounded files, so blocked log pipes cannot stall the simulation owner.
    connect(&process_,&QProcess::readyReadStandardOutput,this,[this]{process_.readAllStandardOutput();});
    connect(&process_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit){
        poll();if(closing_)return;
        const bool retry=!recovery_.isEmpty()&&QFileInfo::exists(recovery_)&&crashes_<2;
        status_["initialized"]=false;status_["running"]=false;status_["state"]="fault";
        status_["error"]="后台仿真进程退出 (code="+std::to_string(code)+", crash="+std::to_string(exit==QProcess::CrashExit)+")";
        status_["recovery_pending"]=retry;if(changed)changed(status_);
        if(retry){++crashes_;QTimer::singleShot(300,this,[this]{if(!closing_)launch(true);});}
    });
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){
        if(e!=QProcess::FailedToStart)return;status_["state"]="fault";status_["error"]=process_.errorString().toStdString();if(changed)changed(status_);
    });
}
SimulationWorkerClient::~SimulationWorkerClient(){shutdown();}
void SimulationWorkerClient::restart(){crashes_=0;closing_=false;if(process_.state()!=QProcess::NotRunning)process_.kill();else launch(!recovery_.isEmpty());}
void SimulationWorkerClient::shutdown(){
    closing_=true;
    if(process_.state()!=QProcess::NotRunning){command("shutdown");process_.closeWriteChannel();if(!process_.waitForFinished(1500)){process_.kill();process_.waitForFinished(1500);}}
}
void SimulationWorkerClient::start(const QString& project,bool recover){
    if(process_.state()!=QProcess::NotRunning)throw std::runtime_error("后台进程尚未退出，请先停止后再初始化");
    project_=QFileInfo(project).absoluteFilePath();if(!recover){recovery_.clear();crashes_=0;}
    closing_=false;launch(recover);
}
void SimulationWorkerClient::launch(bool recover){
    if(!cache_.isValid())throw std::runtime_error("cannot create worker private cache");
    generation_=QUuid::createUuid().toString(QUuid::WithoutBraces);directory_=cache_.path()+"/"+generation_;QDir().mkdir(directory_);
    QStringList args{"--engine-worker",project_,directory_,generation_};if(recover&&!recovery_.isEmpty())args<<recovery_;
    seq_=0;stamp_=-1;changedAt_=std::chrono::steady_clock::now();
    status_={{"generation",generation_.toStdString()},{"project",project_.toStdString()},{"state","loading"},{"stage",recover?"checkpoint_recovery":"project"},{"initialized",false},{"running",false},{"voxel_source",true},{"restart_count",crashes_},{"recovered",recover}};
    process_.start(QCoreApplication::applicationFilePath(),args);if(changed)changed(status_);
}
bool SimulationWorkerClient::command(const std::string& op){
    if(op!="shutdown"&&status_.contains("pending_seq"))return false;
    if(process_.state()!=QProcess::Running||process_.bytesToWrite()>65536)return false;
    const auto line=nlohmann::json{{"generation",generation_.toStdString()},{"seq",++seq_},{"command",op}}.dump()+"\n";
    if(process_.write(line.data(),qint64(line.size()))<0)return false;
    commandAt_=std::chrono::steady_clock::now();status_["pending_command"]=op;status_["pending_seq"]=seq_;if(changed)changed(status_);return true;
}
void SimulationWorkerClient::poll(){
    const auto file=directory_+"/status.json";QFileInfo info(file);
    if(info.exists()&&info.size()<=1024*1024) {
        // 100 ms publications may share an mtime on coarse filesystems. Always
        // read this small descriptor; geometry remains outside the control plane.
        try {
            std::ifstream in(file.toStdString());auto next=nlohmann::json::parse(in);
            if(next.at("generation")==generation_.toStdString() && next.value("uptime_ms",-1.)!=status_.value("uptime_ms",-2.)) {
                const auto pending=status_.value("pending_seq",uint64_t(0));
                if(pending>next.value("ack",uint64_t(0))){next["pending_seq"]=pending;next["pending_command"]=status_.value("pending_command",std::string());}
                next["restart_count"]=crashes_;next["recovered"]=status_.value("recovered",false);next["project"]=project_.toStdString();
                status_=std::move(next);changedAt_=std::chrono::steady_clock::now();
                if(status_.contains("recovery_checkpoint"))recovery_=QString::fromStdString(status_.at("recovery_checkpoint").get<std::string>());
                if(changed)changed(status_);
            }
        } catch(const std::exception&) {/* An atomic rename race is retried at the next UI tick. */}
    }
    const double stale=std::chrono::duration<double>(std::chrono::steady_clock::now()-changedAt_).count();
    // Loading has its own stage and duration; a long first import is not a
    // crashed process. A running source that stops publishing is visibly stale.
    if(stale>10&&status_.value("initialized",false)&&!status_.value("unresponsive",false)){
        status_["unresponsive"]=true;status_["error"]="后台响应超时；当前画面为最后确认状态，可重启后台恢复检查点";if(changed)changed(status_);
    }
}
}
