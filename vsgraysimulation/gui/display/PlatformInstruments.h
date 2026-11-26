#pragma once
#include "CockpitProfile.h"
#include "base/PlatformPresentation.h"
#include <QWidget>
#include <QStringList>
#include <QMap>
#include <functional>
#include <optional>
class QPainter;

namespace vsgraysim::gui::display {
class EquipmentSession;class ImageDisplayPanel;
class PlatformInstruments final : public QWidget {
public:
    explicit PlatformInstruments(QWidget* parent=nullptr);
    bool loadProfile(const QString&,QString& error);
    const CockpitProfile& profile()const{return profile_;}
    void setTelemetry(const nlohmann::json&,bool fresh);
    void setControlState(bool enabled,double throttle,double elevator,double aileron,double rudder);
    void cancelInteraction();
    void setPayloadSession(EquipmentSession*);
    void setPanelLighting(double value);
    bool interacting()const{return !drag_.isEmpty();}
    QPoint controlPosition(const QString& id)const;
    static std::optional<double> measurement(const nlohmann::json&,const QString& binding);
    std::function<void(double,double,double,double)> demandChanged;
    std::function<void()> interactionStarted;
    std::function<void(QString)> statusChanged;
protected:
    void paintEvent(QPaintEvent*)override;
    void resizeEvent(QResizeEvent*)override;
    void mousePressEvent(QMouseEvent*)override;
    void mouseMoveEvent(QMouseEvent*)override;
    void mouseReleaseEvent(QMouseEvent*)override;
    void focusOutEvent(QFocusEvent*)override;
    void hideEvent(QHideEvent*)override;
private:
    void layoutPayload();
    ImageDisplayPanel* payload_=nullptr;
    int payloadType_=-1;
    QRectF imageRect()const;
    QRectF panelRect(const QRectF&)const;
    QPointF normalized(const QPointF&)const;
    void dragTo(const QPointF&);
    void drawInstrument(QPainter&,const CockpitInstrument&);
    void drawTelemetry(QPainter&);
    void drawControls(QPainter&);
    CockpitProfile profile_;
    nlohmann::json state_=nlohmann::json::object();
    PlatformDomain domain_=PlatformDomain::Air;
    bool fresh_=false,enabled_=false;
    double throttle_=0,elevator_=0,aileron_=0,rudder_=0,lighting_=1;
    QPointF press_;
    QString drag_;
    QMap<QString,QString> pages_;
};
}
