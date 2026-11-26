#pragma once
#include "base/PlatformPresentation.h"
#include <QWidget>
#include <QSet>
#include <QPointer>
#include <nlohmann/json.hpp>
#include <array>
#include <optional>
#include <functional>
#include <vector>
class QToolButton;class QSlider;class QComboBox;class QLabel;class QTimer;
namespace vsgraysim::viewport3d {class Viewport3DView;}
namespace vsgraysim::gui::display {
class EquipmentSession;
class EarthPanel;class FlightPanel;class PlatformInstruments;class JoystickInput;
class AircraftCockpitPanel final : public QWidget {
public:
    explicit AircraftCockpitPanel(QWidget* parent=nullptr);
    ~AircraftCockpitPanel()override;
    void updateState(const nlohmann::json& earth,const nlohmann::json& platforms,double time,bool online,bool running,const std::string& session,bool controlAvailable,uint32_t preferred=0);
    FlightPanel* controls()const{return controls_;}
    EarthPanel* earthDisplay()const{return earth_;}
    PlatformInstruments* instrumentPanel()const{return instruments_;}
    bool loadCockpitProfile(const QString& path,QString& error);
    QString cockpitProfilePath()const{return profilePath_;}
    void releaseControl(const QString& reason);
    void useSharedSceneView(bool);
    void setSharedSceneTarget(EarthPanel*,viewport3d::Viewport3DView*);
    bool usesSharedSceneView()const{return sharedScene_;}
    void updatePayloads(const std::vector<std::pair<EquipmentSession*,uint32_t>>&);
    std::function<void()> payloadConnectionRequested;
    std::function<void(uint32_t)> payloadPowerRequested;
protected:
    bool eventFilter(QObject*,QEvent*)override;
    void hideEvent(QHideEvent*)override;
    void showEvent(QShowEvent*)override;
private:
    void inputTick();
    void neutralize();
    void viewChanged();
    void updateScenePresentation();
    QString cameraMode()const;
    void synchronizeInputs();
    void stopJoystick();
    void setCabinCursor(QWidget*,bool);
    EarthPanel* earth_;FlightPanel* controls_;PlatformInstruments* instruments_;
    QToolButton *keyboard_,*hud_;
    QSlider* fov_;QComboBox *mode_,*camera_;QLabel *status_,*commandStatus_,*fovCaption_,*profileTitle_;QTimer* inputTimer_;
    QWidget* payloadBar_=nullptr;QComboBox* payloadSources_=nullptr;QLabel* payloadStatus_=nullptr;QToolButton* payloadPower_=nullptr;
    uint32_t payloadPlatform_=0;std::string payloadSession_;bool payloadOnline_=false;
    QString profilePath_;bool sharedScene_=false;
    QPointer<EarthPanel> sharedEarth_;
    QPointer<QWidget> cabinCursor_;
    QPointer<viewport3d::Viewport3DView> sharedViewport_;
    QLabel* sharedViewStatus_=nullptr;
    nlohmann::json viewSettings_=nlohmann::json::object();
    PlatformDomain domain_=PlatformDomain::Air;
    JoystickInput* joystick_=nullptr;
    QToolButton* joystickButton_=nullptr;
    QComboBox* joystickDevices_=nullptr;
    std::optional<std::array<double,4>> pendingJoystick_;
    QSet<int> keys_;
    bool looking_=false;
    QPoint lastMouse_;
    double lookYaw_=0,lookPitch_=0,throttle_=0;
};
}
