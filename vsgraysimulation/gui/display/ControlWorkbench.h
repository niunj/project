#pragma once
#include "base/BaseWindow.h"
#include "WorkMode.h"
#include "EquipmentSession.h"
#include "SensorPanels.h"
#include "WorkspaceProfile.h"
#include <set>
#include <map>
#include <QElapsedTimer>
#include <nlohmann/json.hpp>
class QTreeWidget; class QTableWidget; class QComboBox; class QPlainTextEdit; class QDockWidget; class QTimer; class QLineEdit;
namespace vsgraysim::gui::display {
class FlightPanel;
class EarthPanel;
class AircraftCockpitPanel;
class EquipmentViewProcesses;
class ControlWorkbench : public ui::BaseWindow {
    Q_OBJECT
public:
    explicit ControlWorkbench(const QString& mode,const QStringList& forced={},QWidget* parent=nullptr,bool connectRemote=true,bool separateViews=false,bool equipmentView=false);
    ~ControlWorkbench() override;
    void applyWorkspace(const WorkspaceProfile&);
    void refreshEquipment(){buildSelectTree();}
    void selectConsole(const QString& id);
    void setLocalPlatformTelemetry(const nlohmann::json& value){local_platforms_=value;}
    void setLocalEarthMap(const std::string& path){local_earth_map_=path;}
    void setSharedSceneView(bool value){shared_scene_view_=value;}
    void setLocalSceneState(uint64_t revision,double time,bool running){if(!connect_remote_){display_surface_revision_=revision;local_sim_time_=time;local_running_=running;local_scene_state_=true;}}
    WorkspaceProfile workspaceProfile() const;
    nlohmann::json connectionStatus() const;
    bool setCockpitProfile(const QString& path,QString& error);
    bool importWorkspace(const QString& path,QString& error);
    bool exportWorkspace(const QString& path,QString& error) const;
    void openEquipmentView(uint32_t);
    void closeEquipmentView(uint32_t,bool force=false);
    void setExpectedScene(const std::string& value){expected_scene_=value;}
    void setPreferredPlatform(uint32_t value){preferred_platform_=value;}
    void openPlatformConsole(const QString&,uint32_t platform=0);
    void closePlatformConsoles();
private:
    void buildAssembleUi();
    void buildSelectTree();
    void onBindRequested();
    void rebuildForSelection(const std::vector<EquipmentInfo>&);
    void rebuildWorkspace();
    void onTick();
    void updateSessions();
    void editWorkspace();
    void disconnectEquipment();
    void logEvent(const QString&);
    EquipmentSession* sessionFor(const WorkspacePanel&) const;
    WorkspaceProfile layout_;
    std::map<QString,QString> console_profiles_;
    std::vector<EquipmentInfo> online_equip_,selected_equip_;
    std::vector<std::unique_ptr<EquipmentSession>> sessions_;
    std::set<uint32_t> linked_ids_;
    struct PanelBinding { WorkspacePanel spec; DisplayPanel* panel; };
    std::vector<PanelBinding> bindings_;
    std::vector<QDockWidget*> panel_docks_;
    struct FlightBinding { FlightPanel* panel; uint32_t equipment_id; };
    std::vector<FlightBinding> flight_panels_;
    struct EarthBinding { EarthPanel* panel; uint32_t equipment_id; };
    std::vector<EarthBinding> earth_panels_;
    struct CockpitBinding {AircraftCockpitPanel* panel;uint32_t equipment_id;};
    std::vector<CockpitBinding> cockpit_panels_;
    std::vector<QTableWidget*> link_tables_;
    std::vector<QPlainTextEdit*> event_views_;
    QStringList events_,forced_ids_;
    QTreeWidget* tree_select_=nullptr;
    QComboBox* preset_=nullptr;
    QLineEdit* filter_=nullptr;
    QLabel* state_=nullptr;
    QTimer* timer_=nullptr;
    bool connect_remote_=true,forced_bound_=false;
    int ticks_=0;
    uint64_t display_surface_revision_=0;
    QElapsedTimer refresh_elapsed_;
    QString last_scene_;
    std::string last_session_;
    double last_sim_time_=-1;
    nlohmann::json local_platforms_=nlohmann::json::array();
    std::string local_earth_map_;
    bool shared_scene_view_=false;
    bool separate_views_=false,equipment_view_=false;
    bool platform_console_=false,world_lease_=false;
    uint32_t preferred_platform_=0;
    std::string expected_scene_;
    std::unique_ptr<EquipmentViewProcesses> views_;
    double local_sim_time_=0;bool local_running_=false,local_scene_state_=false;
};
}
