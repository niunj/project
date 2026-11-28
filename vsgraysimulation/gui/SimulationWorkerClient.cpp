#include "SimulationWorkerClient.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QNetworkRequest>
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
void SimulationWorkerClient::restart(){if(!serviceRun_.isEmpty()){command("recover");return;}crashes_=0;closing_=false;if(process_.state()!=QProcess::NotRunning)process_.kill();else launch(!recovery_.isEmpty());}
void SimulationWorkerClient::shutdown(){
    if(closing_)return;
    closing_=true;
    if(!serviceClient_.isEmpty()) {
        request("DELETE", "/sessions/" + serviceClient_, nullptr, [](auto, bool){});
        return;
    }
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
    if(!serviceRun_.isEmpty()){
        if(serviceSubmitting_ || !serviceJob_.isEmpty() || serviceGeneration_.isEmpty())return false;
        Json body={{"command",op}};
        if(op=="step")body["expected_tick"]=std::to_string(status_.value("tick",uint64_t(0)));
        sendServiceCommand(runPath("/commands"), "run_control", 0, std::move(body));
        return true;
    }
    if(op!="shutdown"&&status_.contains("pending_seq"))return false;
    if(process_.state()!=QProcess::Running||process_.bytesToWrite()>65536)return false;
    const auto line=nlohmann::json{{"generation",generation_.toStdString()},{"seq",++seq_},{"command",op}}.dump()+"\n";
    if(process_.write(line.data(),qint64(line.size()))<0)return false;
    commandAt_=std::chrono::steady_clock::now();status_["pending_command"]=op;status_["pending_seq"]=seq_;if(changed)changed(status_);return true;
}
void SimulationWorkerClient::poll(){
    if(!serviceRun_.isEmpty()){pollService();return;}
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

void SimulationWorkerClient::connectService(const QUrl& endpoint, const QString& run) {
    network_.setProxy(QNetworkProxy::NoProxy);
    serviceUrl_ = endpoint;
    serviceRun_ = run;
    status_ = {{"state", "connecting"}, {"initialized", false}, {"voxel_source", true}};
    if (changed) changed(status_);
    pollService();
}

QString SimulationWorkerClient::runPath(const QString& tail) const {
    return "/runs/" + serviceRun_ + tail;
}

void SimulationWorkerClient::request(const QByteArray& method, const QString& path, Json body,
                                     Reply done, const QString& lease, const QByteArray& throughTick) {
    auto url = serviceUrl_;
    url.setPath("/api/v1" + path);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(3500);
    request.setRawHeader("X-Client-Id", serviceClient_.toUtf8());
    if (!lease.isEmpty()) request.setRawHeader("X-Lease-Id", lease.toUtf8());
    if (!throughTick.isEmpty()) request.setRawHeader("X-Through-Tick", throughTick);
    if (method == "POST") {
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Idempotency-Key", QUuid::createUuid().toByteArray(QUuid::WithoutBraces));
    }
    auto* reply = network_.sendCustomRequest(request, method, body.is_null() ? QByteArray{} : QByteArray::fromStdString(body.dump()));
    connect(reply, &QNetworkReply::finished, this, [this, reply, done = std::move(done)] {
        auto bytes = reply->readAll();
        auto result = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto networkError = reply->errorString();
        reply->deleteLater();
        if (closing_) return;
        if (status == 401) {
            serviceClient_.clear();
            serviceLeases_.clear();
            serviceJob_.clear();
        }
        if (status == 204) result = Json::object();
        if (!result.is_object()) result = {{"message", networkError.toStdString()}};
        done(std::move(result), status >= 200 && status < 300);
    });
}

void SimulationWorkerClient::acquire(const QString& kind, uint32_t target, std::function<void(QString)> done) {
    const auto identity = std::make_pair(kind, target);
    if (const auto it = serviceLeases_.find(identity); it != serviceLeases_.end()) {
        done(it->second);
        return;
    }
    const auto generation = serviceGeneration_;
    Json data={{"kind",kind.toStdString()},{"expected_generation_id",generation.toStdString()}};
    if(kind=="equipment_data")data["equipment_id"]=target;
    if(kind=="platform_control")data["platform_id"]=target;
    request("POST", runPath("/leases"), std::move(data), [this, generation, identity, done = std::move(done)](Json result, bool ok) {
        QString id;
        if (ok && generation == serviceGeneration_) {
            id = QString::fromStdString(result.at("lease_id").get<std::string>());
            serviceLeases_[identity] = id;
        } else if (identity.first != "world" && serviceNotice) {
            serviceNotice(QString::fromStdString(result.value("message", "租约申请失败")));
        }
        done(id);
    });
}

void SimulationWorkerClient::sendServiceCommand(const QString& path, const QString& kind, uint32_t target, Json body) {
    if (serviceSubmitting_ || !serviceJob_.isEmpty() || serviceGeneration_.isEmpty()) return;
    serviceSubmitting_ = true;
    acquire(kind, target, [this, path, body = std::move(body)](QString lease) mutable {
        if (lease.isEmpty()) { serviceSubmitting_ = false; return; }
        body.update({{"control_lease_id",lease.toStdString()},{"expected_generation_id",serviceGeneration_.toStdString()}});
        request("POST", path, std::move(body), [this](Json reply, bool ok) {
            serviceSubmitting_ = false;
            if (ok) serviceJob_ = QString::fromStdString(reply.at("job_id").get<std::string>());
            if (serviceNotice) serviceNotice(ok ? tr("命令已提交，等待仿真确认") :
                QString::fromStdString(reply.value("message", "命令失败")));
        });
    });
}

void SimulationWorkerClient::setCourse(uint32_t platform, double heading, double speed) {
    sendServiceCommand(runPath("/platforms/" + QString::number(platform) + "/commands"), "platform_control", platform,
        {{"command_id","set_prescribed_course"},{"arguments",{{"heading_deg",heading},{"speed_mps",speed}}}});
}

void SimulationWorkerClient::selectEquipment(uint32_t id) {
    if (selectedEquipment_ == id) return;
    const auto old = serviceLeases_.find({"equipment_data", selectedEquipment_});
    if (old != serviceLeases_.end()) {
        request("DELETE", "/leases/" + old->second, nullptr, [](auto, bool){});
        serviceLeases_.erase(old);
    }
    selectedEquipment_ = id;
}

void SimulationWorkerClient::pollService() {
    using namespace std::chrono_literals;
    const auto now = std::chrono::steady_clock::now();
    if (closing_ || serviceReading_ || now - servicePolled_ < 350ms) return;
    servicePolled_ = now;
    if (serviceClient_.isEmpty()) {
        serviceReading_ = true;
        request("POST", "/sessions", {{"client_kind","qt"}}, [this](Json reply, bool ok) {
            serviceReading_ = false;
            if (ok) serviceClient_ = QString::fromStdString(reply.at("client_id").get<std::string>());
            else if (serviceNotice) serviceNotice(QString::fromStdString(reply.value("message", "连接失败")));
        });
        return;
    }
    if (now - serviceRenewed_ > 4s) {
        serviceRenewed_ = now;
        for (const auto& [key, lease] : serviceLeases_) {
            request("POST", "/leases/" + lease + "/renew", Json::object(), [this, key, lease](Json, bool ok) {
                const auto it = serviceLeases_.find(key);
                if (!ok && it != serviceLeases_.end() && it->second == lease) serviceLeases_.erase(it);
            });
        }
    }

    serviceReading_ = true;
    request("GET", runPath("/native-preview"), nullptr, [this](Json reply, bool ok) {
        serviceReading_ = false;
        if (!ok) {
            status_ = {{"state","fault"},{"error",reply.value("message", "连接中断")},{"initialized",false}};
            if (changed) changed(status_);
            return;
        }
        const auto generation = QString::fromStdString(reply.at("generation_id").get<std::string>());
        if (generation != serviceGeneration_) {
            serviceGeneration_ = generation;
            serviceLeases_.clear();
        }
        status_ = std::move(reply);
        if (changed) changed(status_);
        if (!status_.value("initialized", false)) return;
        acquire("world", 0, [](QString){});
        if (!selectedEquipment_ || productReading_) return;
        productReading_ = true;
        const auto id = selectedEquipment_;
        acquire("equipment_data", id, [this, id, generation](QString lease) {
            if (lease.isEmpty() || id != selectedEquipment_ || generation != serviceGeneration_) {
                productReading_ = false;
                if (!lease.isEmpty() && id != selectedEquipment_) {
                    request("DELETE", "/leases/" + lease, nullptr, [](auto, bool){});
                    serviceLeases_.erase({"equipment_data", id});
                }
                return;
            }
            const auto tick = QByteArray::number(qulonglong(status_.value("tick", uint64_t(0))));
            request("GET", runPath("/equipment/" + QString::number(id) + "/preview"), nullptr,
                [this, id, generation](Json product, bool available) {
                    productReading_ = false;
                    if (available && id == selectedEquipment_ && generation == serviceGeneration_ &&
                        product.value("generation_id", "") == generation.toStdString() && productChanged) productChanged(product);
                }, lease, tick);
        });
    });
    if (!serviceJob_.isEmpty()) {
        const auto job = serviceJob_;
        request("GET", "/jobs/" + job, nullptr, [this, job](Json reply, bool ok) {
            if (job != serviceJob_ || (ok && reply.value("state", "") == std::string("pending"))) return;
            serviceJob_.clear();
            if (serviceNotice) serviceNotice(ok && reply.value("state","") == std::string("succeeded") ? tr("仿真已确认命令") :
                QString::fromStdString(reply.value("error", reply.value("message", "命令查询失败"))));
        });
    }
}

}
