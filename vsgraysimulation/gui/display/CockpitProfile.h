#pragma once
#include <QRectF>
#include <QString>
#include <QStringList>
#include <nlohmann/json.hpp>
#include <vector>

namespace vsgraysim::gui::display {
struct CockpitInstrument {
    QString id, kind, label, binding, unit,initialPage;
    QRectF rect;
    double minimum=0, maximum=100, scale=1;
    QRectF photoRect; // Fixed artwork coordinates; Qt may reflow rect.
};
struct CockpitHotspot {
    QString id,label,action,target,value;
    QRectF rect;
};

// Display configuration only. No airframe dynamics or voxel parameters are
// inferred from the choice of instrument layout.
struct CockpitProfile {
    QString id, name, category, description;
    QString photoBackground, photoGrips, photoMount;
    nlohmann::json photoPresentation() const;
    QString modelPath,modelUpAxis="z",modelKind="aircraft",modelLabel;
    double modelMaximumDimension=20,chaseDistance=65,modelHeading=0,chaseAzimuth=30;
    std::vector<CockpitInstrument> instruments;
    std::vector<CockpitHotspot> hotspots;
    QRectF stick, throttle,photoStick,photoThrottle;
    double maximumAxisDemand=.85;
    double stickTravelX=.06,stickTravelY=.04,throttleTravel=.26;
    nlohmann::json joystickMapping=nlohmann::json::object();
    int engineCount=1;
    bool hudAvailable=true;
    bool automaticLayout=false;
    static CockpitProfile load(const QString& path);
    static CockpitProfile parse(const nlohmann::json&,const QString& directory);
    static QStringList builtInProfiles();
};
}
