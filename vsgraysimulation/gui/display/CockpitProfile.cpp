#include "CockpitProfile.h"
#include "JoystickInput.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <cmath>
#include <stdexcept>

namespace vsgraysim::gui::display {
namespace {
QRectF rectangle(const nlohmann::json& j) {
    if(!j.is_array()||j.size()!=4)throw std::invalid_argument("cockpit rectangle requires x/y/width/height");
    for(const auto& v:j)if(!v.is_number()||!std::isfinite(v.get<double>()))throw std::invalid_argument("invalid cockpit rectangle");
    QRectF r(j[0].get<double>(),j[1].get<double>(),j[2].get<double>(),j[3].get<double>());
    if(r.x()<0||r.y()<0||r.width()<=0||r.height()<=0||r.right()>1||r.bottom()>1)throw std::invalid_argument("cockpit rectangle lies outside panel");
    return r;
}
QString text(const nlohmann::json& j,const char* key) {
    const auto s=j.at(key).get<std::string>();if(s.empty()||s.size()>200)throw std::invalid_argument("invalid cockpit text");
    return QString::fromStdString(s);
}
}
CockpitProfile CockpitProfile::load(const QString& path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>256*1024)throw std::runtime_error("cannot read cockpit profile");
    const auto data=file.readAll();return parse(nlohmann::json::parse(data.constData(),data.constData()+data.size()),QFileInfo(path).absolutePath());
}
CockpitProfile CockpitProfile::parse(const nlohmann::json& j,const QString& directory) {
    if(j.at("schema")!="vsgraysim.cockpit"||j.at("version")!=1)throw std::invalid_argument("unsupported cockpit profile");
    CockpitProfile p;p.id=text(j,"id");p.name=text(j,"name");
    p.category=QString::fromStdString(j.value("category",std::string("fighter")));p.description=QString::fromStdString(j.value("description",std::string()));
    p.hudAvailable=j.value("hud_available",true);
    const auto layout=j.value("layout",std::string("fixed"));
    if(layout!="fixed"&&layout!="vector-flow")throw std::invalid_argument("unsupported cockpit instrument layout");
    p.automaticLayout=layout=="vector-flow";
    if (j.contains("photo_panel")) {
        const auto& panel=j.at("photo_panel");
        const auto root=QDir(directory);
        p.photoBackground=root.absoluteFilePath(text(panel,"background"));
        const auto mount=panel.value("mount",std::string{});
        if(!mount.empty()&&(mount!="fighter"||p.category!="fighter"))
            throw std::invalid_argument("unsupported cockpit panel mount");
        p.photoMount=QString::fromStdString(mount);
        if(panel.contains("grips"))p.photoGrips=root.absoluteFilePath(text(panel,"grips"));
    }
    const auto model=j.value("external_model",nlohmann::json::object());const auto uri=model.value("uri",std::string());if(!uri.empty())p.modelPath=QDir(directory).absoluteFilePath(QString::fromStdString(uri));
    p.modelUpAxis=QString::fromStdString(model.value("up_axis",std::string("z")));p.modelKind=QString::fromStdString(model.value("kind",std::string("aircraft")));p.modelLabel=QString::fromStdString(model.value("label",std::string()));
    p.modelMaximumDimension=model.value("maximum_dimension_m",20.);p.chaseDistance=model.value("chase_distance_m",65.);p.modelHeading=model.value("heading_deg",0.);
    if(!std::isfinite(p.modelHeading))throw std::invalid_argument("invalid exterior model heading");
    p.chaseAzimuth=model.value("view_azimuth_deg",30.);if(!std::isfinite(p.chaseAzimuth)||std::abs(p.chaseAzimuth)>180)throw std::invalid_argument("invalid exterior camera azimuth");
    if(!std::isfinite(p.modelMaximumDimension)||!std::isfinite(p.chaseDistance)||p.modelMaximumDimension<=0||p.chaseDistance<10||p.chaseDistance>1000||!QStringList{"y","z"}.contains(p.modelUpAxis))throw std::invalid_argument("invalid exterior model presentation");
    p.photoStick=p.stick=rectangle(j.at("controls").at("stick"));p.photoThrottle=p.throttle=rectangle(j.at("controls").at("throttle"));
    p.maximumAxisDemand=j.at("controls").value("maximum_axis_demand",.85);p.engineCount=j.value("engine_count",1);
    p.stickTravelX=j.at("controls").value("stick_travel_x",.06);p.stickTravelY=j.at("controls").value("stick_travel_y",.04);p.throttleTravel=j.at("controls").value("throttle_travel",.26);
    for(double travel:{p.stickTravelX,p.stickTravelY,p.throttleTravel})if(!std::isfinite(travel)||travel<0||travel>.5)throw std::invalid_argument("invalid mechanical control travel");
    p.joystickMapping=j.value("joystick",nlohmann::json::object());JoystickInput::validateMapping(p.joystickMapping);
    if(!std::isfinite(p.maximumAxisDemand)||p.maximumAxisDemand<=0||p.maximumAxisDemand>1||p.engineCount<1||p.engineCount>4)throw std::invalid_argument("invalid cockpit airframe presentation parameters");
    const auto& items=j.at("instruments");if(!items.is_array()||items.size()>64)throw std::invalid_argument("invalid cockpit instrument list");
    QSet<QString> ids;
    for(const auto& entry:items) {
        CockpitInstrument i;i.id=text(entry,"id");i.kind=text(entry,"kind");i.label=text(entry,"label");i.photoRect=i.rect=rectangle(entry.at("rect"));
        if(ids.contains(i.id)||!QStringList{"mfd","ded","attitude","pfd","compass","dial","payload"}.contains(i.kind))throw std::invalid_argument("duplicate or unsupported cockpit instrument");
        if(i.kind=="payload")for(const auto& other:p.instruments)if(other.kind=="payload")throw std::invalid_argument("only one payload aperture is supported");
        ids.insert(i.id);i.binding=QString::fromStdString(entry.value("binding",std::string()));i.unit=QString::fromStdString(entry.value("unit",std::string()));
        i.initialPage=QString::fromStdString(entry.value("initial_page",std::string()));if(!i.initialPage.isEmpty()&&!QStringList{"FLT","CTRL","ENG","NAV","SYS"}.contains(i.initialPage))throw std::invalid_argument("invalid initial MFD page");
        i.minimum=entry.value("min",0.);i.maximum=entry.value("max",100.);i.scale=entry.value("scale",1.);
        if(!std::isfinite(i.minimum)||!std::isfinite(i.maximum)||!std::isfinite(i.scale)||i.maximum<=i.minimum||i.scale==0)throw std::invalid_argument("invalid cockpit gauge scale");
        if(i.kind=="dial"&&i.binding.isEmpty())throw std::invalid_argument("unbound cockpit dial");
        p.instruments.push_back(std::move(i));
    }
    const auto buttons=j.value("hotspots",nlohmann::json::array());if(!buttons.is_array()||buttons.size()>128)throw std::invalid_argument("invalid cockpit hotspots");
    for(const auto& button:buttons){CockpitHotspot h;h.id=text(button,"id");h.label=text(button,"label");h.action=text(button,"action");h.rect=rectangle(button.at("rect"));
        if(ids.contains(h.id)||!QStringList{"mfd_page","panel_light"}.contains(h.action))throw std::invalid_argument("duplicate or unsupported cockpit hotspot");ids.insert(h.id);
        if(h.action=="mfd_page"){h.target=text(button,"target");h.value=text(button,"value");bool found=false;for(const auto& i:p.instruments)found|=i.id==h.target&&i.kind=="mfd";
            if(!found||!QStringList{"FLT","CTRL","ENG","NAV","SYS"}.contains(h.value))throw std::invalid_argument("unbound MFD page button");}
        p.hotspots.push_back(std::move(h));
    }
    return p;
}
nlohmann::json CockpitProfile::photoPresentation() const {
    if(photoBackground.isEmpty())return nullptr;
    const auto rect=[](const QRectF& r){return nlohmann::json::array({r.x(),r.y(),r.width(),r.height()});};
    auto items=nlohmann::json::array();
    bool firstMfd=true;
    for(const auto& i:instruments) {
        auto page=i.initialPage;
        if(i.kind=="mfd") {if(page.isEmpty())page=firstMfd?"FLT":"CTRL";firstMfd=false;}
        items.push_back({{"id",i.id.toStdString()},{"kind",i.kind.toStdString()},{"label",i.label.toStdString()},
            {"binding",i.binding.toStdString()},{"unit",i.unit.toStdString()},{"page",page.toStdString()},
            {"rect",rect(i.photoRect)},{"min",i.minimum},{"max",i.maximum},{"scale",i.scale}});
    }
    return {{"background",photoBackground.toStdString()},{"grips",photoGrips.toStdString()},{"mount",photoMount.toStdString()},{"instruments",std::move(items)},
        {"engine_count",engineCount},{"stick",rect(photoStick)},{"throttle",rect(photoThrottle)},
        {"stick_travel_x",stickTravelX},{"stick_travel_y",stickTravelY},{"throttle_travel",throttleTravel}};
}
QStringList CockpitProfile::builtInProfiles(){QStringList paths;QDir directory("config/cockpits");for(const auto& name:directory.entryList({"*.json"},QDir::Files,QDir::Name)){const auto path=directory.filePath(name);try{load(path);paths.push_back(path);}catch(const std::exception&){}}return paths;}
}
