#pragma once
#include <QWidget>
#include <QElapsedTimer>
#include <nlohmann/json.hpp>
#include "vsgraysim/broker/FlightControl.h"
#include <optional>
#include <string>
class QComboBox; class QDoubleSpinBox; class QGroupBox; class QLabel; class QPushButton; class QTimer;
namespace vsgraysim::gui::display {
class AttitudeView;
class FlightPanel : public QWidget {
public:
    explicit FlightPanel(QWidget* parent=nullptr,bool compact=false);
    ~FlightPanel() override;
    void updateTelemetry(const nlohmann::json& platforms,bool online,bool running,uint32_t preferredPlatform=0);
    void updateControlContext(const std::string& session_id,bool available);
    void releaseControl(const QString& reason={});
    bool controlActive() const {return active_;}
    std::string pendingRequestId() const {return request_id_;}
    nlohmann::json selectedTelemetry()const{return selectedState();}
    bool telemetryFresh()const{return fresh();}
    bool manualControlActive()const;
    bool toggleCockpitHold();
    bool toggleAircraftSystem(const std::string& channel);
    bool applyManualInputs(double throttle,double elevator,double aileron,double rudder);
    void useCockpitToolbar();
    QString commandStatusText()const;
protected:
    void hideEvent(QHideEvent*) override;
private:
    nlohmann::json selectedState() const;
    bool fresh() const;
    bool controllable() const;
    void updateControlUi();
    void readSetpoints();
    void takeControl();
    void sendInputs();
    void submitSnapshot(const FlightControlCommand&);
    void controlTick();
    void relinquishLocal();
    QComboBox *platforms_,*mode_;
    AttitudeView* attitude_;
    QGroupBox *controls_,*manual_,*hold_;
    QDoubleSpinBox *throttle_,*elevator_,*aileron_,*rudder_,*speed_,*altitude_,*heading_;
    QPushButton *take_,*release_,*apply_,*neutral_;
    QLabel *actual_,*ack_,*model_;
    QTimer* control_timer_;
    nlohmann::json samples_=nlohmann::json::array();
    bool online_=false,running_=false,available_=false,active_=false,last_release_=false,last_route_=false,observed_own_=false;
    uint32_t controlled_id_=0;
    std::string last_payload_,session_id_,client_id_,request_id_,result_key_;
    QString release_reason_;
    QElapsedTimer sample_age_,request_age_,last_sent_;
    std::optional<FlightControlCommand> submitted_;
    struct SystemRequest {std::string id, channel;bool enabled;uint32_t platform;};
    std::optional<SystemRequest> system_request_;
    QElapsedTimer system_age_;
    QString system_status_;
};
}
