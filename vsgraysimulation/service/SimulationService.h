#pragma once

#include <QHttpServer>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTimer>

#include <nlohmann/json.hpp>
#include <chrono>
#include <deque>
#include <map>
#include <memory>

namespace vsgraysim::service {
class WebWorkspace;
class ModelLibrary;
class SimulationService final : public QObject {
public:
    SimulationService(QString root, QString web);
    ~SimulationService() override;
    quint16 listen(quint16 port);

private:
    using Json = nlohmann::json;
    using Clock = std::chrono::steady_clock;
    struct Run;
    struct FrameWait;
    struct SurfaceWait;
    struct Lease {
        std::string owner, run, generation, kind;
        uint32_t target = 0;
        Clock::time_point expiry;
    };
    struct Submission {
        std::string path;
        QByteArray body;
        Json response;
    };
    struct Session {
        Clock::time_point expiry;
        std::map<std::string, Submission> submissions;
        std::string author;
    };

    QHttpServerResponse handle(const QHttpServerRequest&);
    QHttpServerResponse dispatch(const QHttpServerRequest&);
    QHttpServerResponse runRequest(const QHttpServerRequest&, Run&, const QStringList&);
    void launch(Run&, const QString& checkpoint = {});
    void poll();
    void refresh(Run&);
    void flushFrameWaiters();
    void flushSurfaceWaiters();
    QHttpServerResponse surface(const Run&, const QByteArray& after) const;
    void publishDemand(Run&);
    void dispatchNext(Run&);
    Json queue(Run&, const std::string& operation, Json command, const std::string& lease);
    std::string newJob(const Run&, const std::string& kind);
    Json createLease(Run&, const std::string& owner, const Json&);
    const Lease& requireLease(const Run&, const std::string& owner,
                              const std::string& id, const std::string& kind, uint32_t target) const;
    Json runStatus(const Run&, bool native = false) const;

    QString root_, web_;
    QTemporaryDir cache_{QStringLiteral("/tmp/vsgsvc-XXXXXX")};
    std::unique_ptr<ModelLibrary> models_;
    std::unique_ptr<WebWorkspace> workspace_;
    QHttpServer http_;
    QTcpServer socket_;
    QTimer timer_;
    Json catalog_;
    std::map<std::string, std::unique_ptr<Run>> runs_;
    std::map<std::string, Json> jobs_;
    std::map<std::string, Session> sessions_;
    std::map<std::string, std::unique_ptr<FrameWait>> frameWaiters_;
    std::map<std::string, std::unique_ptr<SurfaceWait>> surfaceWaiters_;
    std::map<std::string, Lease> leases_;
    uint64_t nextRun_ = 0;
};
}
