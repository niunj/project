#pragma once

#include <QByteArray>
#include <QFileSystemWatcher>
#include <QString>

#include <nlohmann/json.hpp>
#include <chrono>
#include <functional>
#include <map>
#include <memory>

namespace vsgraysim::service {
class ModelLibrary;
struct WorkspaceError { int status; const char* code; const char* message; };

class WebWorkspace {
public:
    using Json = nlohmann::json;
    struct Frame {
        QByteArray jpeg;
        Json stamp;
    };

    WebWorkspace(QString projectRoot, QString scratch, const ModelLibrary&);
    ~WebWorkspace();

    bool available() const;
    void setFrameNotification(std::function<void()> notify);
    Json layout(const QString& topic, const std::string& project, const std::string& revision) const;
    Json save(const QString& topic, const std::string& project, const std::string& revision, const Json&, const std::string& author);
    Json previewDeployment(Json layout, const Json& plan) const;
    Json deployment(const Json& layout) const;
    Json bindModel(const QString& topic, const std::string& project, const std::string& revision, const Json&, const std::string& author);
    Json open(const std::string& owner, const QString& topic, const QString& statusPath,
              const Json& camera, const Json& positions, const Json& model = nullptr,
              const Json& bindings = Json::object(), const Json& deployment = nullptr, const Json& sharedLayout = nullptr);
    void camera(const std::string& owner, const std::string& id, const Json&);
    Frame frame(const std::string& owner, const std::string& id, const QByteArray& after);
    void close(const std::string& owner);
    void sweep();

private:
    struct View;
    View& view(const std::string& owner, const std::string& id);
    void write(View&);
    void restart(View&);
    void updateFrameIntervals();
    QString layoutPath(const std::string& project, const std::string& revision) const;
    void saveLayout(const std::string& project, const std::string& revision, Json&);
    void publishLayout(const QString& topic, const Json&);
    QString root_, scratch_, renderer_, drafts_;
    const ModelLibrary& models_;
    QFileSystemWatcher viewWatcher_;
    std::map<std::string, std::unique_ptr<View>> views_;
};
}
