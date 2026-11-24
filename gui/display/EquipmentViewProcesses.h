#pragma once
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <vsgraysim/broker/EquipmentInfo.h>
#include <nlohmann/json.hpp>
#include <map>
#include <memory>
namespace vsgraysim::gui::display {
class EquipmentViewProcesses final : public QObject {
public:
    explicit EquipmentViewProcesses(QObject* parent=nullptr):QObject(parent){}
    ~EquipmentViewProcesses() override;
    bool open(const EquipmentInfo&,const std::string& scene);
    bool openConsole(const QString& mode,uint32_t platform,const std::vector<EquipmentInfo>&,const std::string& scene);
    void closeConsoles();
    void close(uint32_t id,bool force=false);
    void closeAll();
    void poll();
    nlohmann::json status() const;
    nlohmann::json consoleStatus() const;
private:
    struct View {std::unique_ptr<QProcess> process;QString path;std::string scene;EquipmentInfo equipment;nlohmann::json report=nlohmann::json::object();};
    QTemporaryDir directory_;
    std::map<uint32_t,View> views_;
    std::map<QString,std::unique_ptr<QProcess>> consoles_;
};
}
