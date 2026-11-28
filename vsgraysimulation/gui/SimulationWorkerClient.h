#pragma once
#include <QObject>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QUrl>
#include <map>
#include <QTemporaryDir>
#include <nlohmann/json.hpp>
#include <functional>
#include <chrono>

namespace vsgraysim::gui {
// GUI supervisor only: no scene, solver or sensor GPU session lives here.
class SimulationWorkerClient final : public QObject {
public:
    explicit SimulationWorkerClient(QObject* parent=nullptr);
    ~SimulationWorkerClient() override;
    void start(const QString& project,bool recover=false);
    void connectService(const QUrl&, const QString& run);
    bool command(const std::string&);
    void setCourse(uint32_t platform, double heading, double speed);
    void selectEquipment(uint32_t);
    std::function<void(const nlohmann::json&)> productChanged;
    std::function<void(const QString&)> serviceNotice;
    void poll();
    void restart();
    void shutdown();
    const nlohmann::json& status() const {return status_;}
    bool active() const {return !project_.isEmpty() || !serviceRun_.isEmpty();}
    std::function<void(const nlohmann::json&)> changed;
private:
    using Json = nlohmann::json;
    using Reply = std::function<void(Json, bool)>;
    void launch(bool recover);
    void pollService();
    void request(const QByteArray& method, const QString& path, Json body, Reply, const QString& lease = {}, const QByteArray& throughTick = {});
    void acquire(const QString& kind, uint32_t target, std::function<void(QString)>);
    void sendServiceCommand(const QString& path, const QString& kind, uint32_t target, Json body);
    QString runPath(const QString& tail = {}) const;
    QNetworkAccessManager network_;
    QUrl serviceUrl_;
    QString serviceRun_, serviceClient_, serviceGeneration_, serviceJob_;
    std::map<std::pair<QString, uint32_t>, QString> serviceLeases_;
    std::chrono::steady_clock::time_point servicePolled_{}, serviceRenewed_{};
    bool serviceReading_ = false, serviceSubmitting_ = false, productReading_ = false;
    uint32_t selectedEquipment_ = 0;
    QProcess process_;
    QTemporaryDir cache_;
    QString project_,directory_,generation_,recovery_;
    nlohmann::json status_=nlohmann::json::object();
    uint64_t seq_=0;
    int crashes_=0;
    bool closing_=false;
    std::chrono::steady_clock::time_point changedAt_,commandAt_;
    qint64 stamp_=-1;
};
}
