#pragma once
#include <QObject>
#include <QProcess>
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
    bool command(const std::string&);
    void poll();
    void restart();
    void shutdown();
    const nlohmann::json& status() const {return status_;}
    bool active() const {return !project_.isEmpty();}
    std::function<void(const nlohmann::json&)> changed;
private:
    void launch(bool recover);
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
