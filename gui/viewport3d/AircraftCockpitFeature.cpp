#include "AircraftCockpitFeature.h"
#include "ViewportContext.h"
#include "EarthView.h"
#include "FeatureRegistry.h"
#include "VsgQtWidget.h"
#include "AircraftExterior.h"
#include <simcore/Orientation.h>
#include <vsg/all.h>
#include <QPainter>
#include <QLinearGradient>
#include <QFont>
#include <cstring>
#include <cmath>

namespace vsgraysim::viewport3d {
namespace {
using json=nlohmann::json;
double value(const json& j,const char* name,double fallback=0){const auto i=j.find(name);return i!=j.end()&&i->is_number()&&std::isfinite(i->get<double>())?i->get<double>():fallback;}
QString fixed(double v,int places=0){return QString::number(v,'f',places);}
double groundSpeed(const json& s){return std::hypot(s["velocity"][0].get<double>(),s["velocity"][2].get<double>());}
void label(QPainter& p,const QRectF& rect,const QString& text,int size,QColor color=QColor("#a8ecc0")) {
    p.setPen(color);QFont font("DejaVu Sans Mono");font.setPixelSize(size);p.setFont(font);p.drawText(rect,Qt::AlignCenter,text);
}
vsg::dquat attitude(const json& s){return simcore::headingRotation(value(s,"yaw"),value(s,"pitch"),value(s,"roll"));}
void upload(vsg::ubvec4Array2D& data,const QImage& image){const auto rgba=image.convertToFormat(QImage::Format_RGBA8888);for(int y=0;y<rgba.height();++y)std::memcpy(data.dataPointer(y*rgba.width()),rgba.constScanLine(y),size_t(rgba.width())*4);data.dirty();}
}
bool AircraftCockpitFeature::validTelemetry(const json& s) {
    if(!s.is_object())return false;
    for(const auto* k:{"yaw","pitch","roll","speed","sim_time"})if(!s.contains(k)||!s[k].is_number()||!std::isfinite(s[k].get<double>()))return false;
    for(const auto* k:{"position","velocity"}){if(!s.contains(k)||!s[k].is_array()||s[k].size()!=3)return false;for(const auto& v:s[k])if(!v.is_number()||!std::isfinite(v.get<double>()))return false;}
    return true;
}
vsg::LookAt AircraftCockpitFeature::pilotCamera(const simcore::geo::GeoAnchor& anchor,const json& s,double yaw,double pitch) {
    if(!validTelemetry(s)||!std::isfinite(yaw)||!std::isfinite(pitch))throw std::invalid_argument("invalid cockpit pose");
    const auto& p=s.at("position");const auto body=vsg::translate(p[0].get<double>(),p[1].get<double>(),p[2].get<double>())*vsg::rotate(attitude(s));
    const auto world=simcore::geo::makeEnuToWorldMatrix(anchor)*body;
    const auto look=vsg::rotate(simcore::headingRotation(yaw,pitch,0));
    const auto f=world*(look*vsg::dvec4(0,0,-1,0)),u=world*(look*vsg::dvec4(0,1,0,0));
    const auto eye=world*vsg::dvec3(0,1.2,0);return {eye,eye+vsg::dvec3(f.x,f.y,f.z)*1000,vsg::dvec3(u.x,u.y,u.z)};
}
vsg::LookAt AircraftCockpitFeature::chaseCamera(const simcore::geo::GeoAnchor& anchor,const json& s,double distance,double yaw,double pitch){
    if(!validTelemetry(s)||!std::isfinite(distance)||distance<10||distance>1000||!std::isfinite(yaw)||!std::isfinite(pitch))throw std::invalid_argument("invalid exterior camera pose");
    const auto world=simcore::geo::makeEnuToWorldMatrix(anchor);const auto& p=s.at("position");const vsg::dvec3 centre(p[0].get<double>(),p[1].get<double>(),p[2].get<double>());
    const double elevation=vsg::radians(std::clamp(15.+pitch,-60.,75.));const auto orbit=vsg::rotate(simcore::headingRotation(value(s,"yaw")+yaw,0,0));
    const auto eye=centre+orbit*vsg::dvec3(0,std::sin(elevation)*distance,std::cos(elevation)*distance);
    const auto up=world*vsg::dvec4(0,1,0,0);return {world*eye,world*centre,vsg::dvec3(up.x,up.y,up.z)};
}
QImage AircraftCockpitFeature::instruments(const json& s,bool fresh) {
    QImage image(1536,512,QImage::Format_RGBA8888);image.fill(QColor("#151a1c"));QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient metal(0,0,0,512);metal.setColorAt(0,QColor("#38403f"));metal.setColorAt(1,QColor("#171d1e"));p.fillRect(image.rect(),metal);
    const bool valid=validTelemetry(s);const auto entry=s.is_object()?s.value("flight_control",json::object()):json::object();const auto fc=entry.is_object()?entry:json::object();const auto actual=fc.value("actual",json::object());
    const QColor green=fresh?QColor("#9ee6ad"):QColor("#d6b86d");
    auto mfd=[&](QRect r,const QString& title){p.setPen(QPen(QColor("#65716b"),4));p.setBrush(QColor("#080e0d"));p.drawRoundedRect(r,12,12);label(p,r.adjusted(16,12,-16,-r.height()+52),title,26,green);
        for(int i=0;i<6;++i){p.setBrush(QColor("#3d4541"));p.setPen(QColor("#151a18"));p.drawRoundedRect(QRect(r.x()-27,r.y()+25+i*63,20,28),3,3);p.drawRoundedRect(QRect(r.right()+7,r.y()+25+i*63,20,28),3,3);}};
    const QRect nav(55,52,450,405),sys(1031,52,450,405);mfd(nav,"NAV / INERTIAL");mfd(sys,"SYS / FLIGHT CONTROL");
    if(valid) {
        p.save();p.setClipRect(nav.adjusted(12,56,-12,-32));p.translate(nav.center()+QPoint(0,20));p.rotate(-value(s,"yaw"));p.setPen(QPen(QColor("#284f3c"),2));p.setBrush(Qt::NoBrush);
        for(int r:{60,120,180})p.drawEllipse(QPoint(0,0),r,r);
        for(int i=0;i<360;i+=30){p.save();p.rotate(i);p.drawLine(0,-160,0,-180);label(p,QRectF(-30,-202,60,30),QString::number(i/10),22);p.restore();}p.restore();
        p.setPen(QPen(green,4));const auto c=nav.center()+QPoint(0,20);p.drawLine(c+QPoint(0,-28),c+QPoint(-17,22));p.drawLine(c+QPoint(0,-28),c+QPoint(17,22));p.drawLine(c+QPoint(-17,22),c+QPoint(17,22));
        label(p,QRect(80,390,400,40),"GRID "+fixed(value(s,"yaw"))+"   GS "+fixed(groundSpeed(s)*1.943844)+" KT",24,green);
        int row=0;auto line=[&](QString name,QString val){label(p,QRect(1050,114+row++*45,410,42),name+"   "+val,26,green);};
        auto actuator=[&](const char* name){return actual.is_object()&&actual.contains(name)&&actual[name].is_number()?fixed(actual[name].get<double>()*100,1)+" %":QString("--");};
        line("THR",actuator("throttle"));line("ELEV",actuator("elevator"));line("AIL",actuator("aileron"));line("RUD",actuator("rudder"));
        line("MODE",fc.contains("mode")&&fc["mode"].is_string()?QString::fromStdString(fc["mode"].get<std::string>()).toUpper():QString("UNAVAILABLE"));
        line("VERT",fixed(s["velocity"][1].get<double>(),1)+" M/S");
    }else{label(p,nav,"NO TELEMETRY",34,QColor("#dca967"));label(p,sys,"WAITING FOR DATA",30,QColor("#dca967"));}
    QRectF standby(593,76,350,230);p.save();p.setClipRect(standby);p.translate(standby.center());if(valid){p.rotate(-value(s,"roll"));p.translate(0,value(s,"pitch")*3.2);}
    p.fillRect(QRectF(-400,-500,800,500),QColor("#285b75"));p.fillRect(QRectF(-400,0,800,500),QColor("#69523b"));p.setPen(QPen(QColor("#e4e2c7"),3));p.drawLine(-400,0,400,0);p.restore();
    p.setPen(QPen(QColor("#d7bf7a"),4));p.drawLine(700,190,752,190);p.drawLine(784,190,836,190);p.drawEllipse(QPoint(768,190),8,8);
    label(p,QRect(570,16,400,44),fresh?"FLIGHT / LIVE":"FLIGHT / DATA HOLD",26,green);
    if(!valid)label(p,standby,"NO ATTITUDE DATA",26,QColor("#dca967"));
    label(p,QRect(550,330,440,50),valid?"GS "+fixed(groundSpeed(s)*1.943844)+" KT":"GS -- KT",36,green);
    label(p,QRect(550,380,440,44),valid?"H LOCAL "+fixed(s["position"][1].get<double>())+" M":"H LOCAL -- M",28,green);
    label(p,QRect(540,443,460,40),"GROUND SPEED / LOCAL HEIGHT",18,QColor("#8e9a90"));
    for(int x:{20,525,1011,1516})for(int y:{20,490}){p.setBrush(QColor("#606560"));p.setPen(QColor("#080c0a"));p.drawEllipse(QPoint(x,y),6,6);p.drawLine(x-4,y,x+4,y);}
    return image;
}
QImage AircraftCockpitFeature::hud(const json& s,bool fresh) {
    QImage image(1024,768,QImage::Format_RGBA8888);image.fill(Qt::transparent);if(!validTelemetry(s))return image;
    QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);const QColor color=fresh?QColor(131,255,151,230):QColor(255,193,92,230);p.setPen(QPen(color,2.5));
    const double heading=value(s,"yaw"),pitch=value(s,"pitch"),roll=value(s,"roll");
    for(int d=-30;d<=30;d+=5){const double x=512+d*12;int h=int(std::round(heading/5)*5+d+360)%360;p.drawLine(QPointF(x,100),QPointF(x,115));if(h%10==0)label(p,QRectF(x-28,63,56,32),fixed(h/10),24,color);}
    p.drawLine(512,120,500,139);p.drawLine(512,120,524,139);
    p.save();p.setClipRect(QRect(260,180,504,410));p.translate(512,384);p.rotate(-roll);
    for(int angle=-40;angle<=40;angle+=5){const double delta=(pitch-angle)*3.141592653589793/180;if(std::abs(delta)>.55)continue;const double y=std::tan(delta)*(3./1.35*768);
        const int w=angle?95:175;p.drawLine(QPointF(-w,y),QPointF(-25,y));p.drawLine(QPointF(25,y),QPointF(w,y));if(angle)label(p,QRectF(w+8,y-16,55,30),fixed(angle),24,color);}
    p.restore();p.drawEllipse(QPointF(512,384),13,13);p.drawLine(470,384,499,384);p.drawLine(525,384,554,384);p.drawLine(512,371,512,350);
    label(p,QRect(65,325,175,60),fixed(groundSpeed(s)*1.943844),44,color);label(p,QRect(65,388,175,35),"GS / KT",22,color);
    label(p,QRect(790,325,200,60),fixed(s["position"][1].get<double>()),42,color);label(p,QRect(790,388,200,35),"H LOCAL / M",21,color);
    label(p,QRect(315,663,394,45),fresh?"FLIGHT TELEMETRY":"LAST STATE / LINK STALE",24,color);return image;
}
void AircraftCockpitFeature::onAttach(ViewportContext& ctx) {
    root_=vsg::Switch::create();body_=vsg::MatrixTransform::create();root_->addChild(~vsg::Mask(1),body_);ctx.worldRoot()->addChild(root_);
    interior_=vsg::Switch::create();auto geometry=vsg::Group::create();interior_->addChild(true,geometry);body_->addChild(interior_);
    exterior_=vsg::Switch::create();exteriorGeometry_=vsg::Group::create();exterior_->addChild(false,exteriorGeometry_);body_->addChild(exterior_);
    auto builder=vsg::Builder::create();vsg::StateInfo metal;metal.lighting=true;
    auto box=[&](vsg::vec3 p,vsg::vec3 size,vsg::vec4 color){vsg::GeometryInfo g;g.position=p;g.dx={size.x,0,0};g.dy={0,size.y,0};g.dz={0,0,size.z};g.color=color;geometry->addChild(builder->createBox(g,metal));};
    const vsg::vec4 dark(.035,.045,.043,1),edge(.08,.095,.087,1);
    box({0,.44f,-1.72f},{3.05f,.99f,.18f},edge);box({0,.99f,-1.77f},{3.15f,.12f,.55f},dark);
    for(float sign:{-1.f,1.f}){box({sign*1.3f,.2f,-.2f},{.34f,.38f,2.4f},dark);box({sign*1.20f,1.53f,-1.9f},{.08f,1.18f,.10f},edge);box({sign*.55f,2.10f,-1.9f},{1.14f,.075f,.10f},edge);}
    instrumentImage_=vsg::ubvec4Array2D::create(1536,512);hudImage_=vsg::ubvec4Array2D::create(1024,768);
    for(auto image:{instrumentImage_,hudImage_}){image->properties.format=VK_FORMAT_R8G8B8A8_SRGB;image->properties.dataVariance=vsg::DYNAMIC_DATA;}
    auto quad=[&](vsg::ref_ptr<vsg::Data> image,vsg::vec3 centre,vsg::vec3 width,vsg::vec3 height,bool alpha){vsg::StateInfo state;state.lighting=false;state.blending=alpha;state.two_sided=true;state.image=image;vsg::GeometryInfo g;g.position=centre;g.dx=width;g.dy=height;g.dz={0,0,0};
        if(!alpha)return builder->createQuad(g,state);
        auto transparent=vsg::Builder::create();transparent->shaderSet=vsg::createFlatShadedShaderSet();
        auto blend=vsg::ColorBlendState::create();blend->configureAttachments(true);auto depth=vsg::DepthStencilState::create();depth->depthWriteEnable=VK_FALSE;
        transparent->shaderSet->defaultGraphicsPipelineStates={blend,depth};
        // Discard the unlit glass's fully transparent pixels as well: they must
        // never obscure the outside world or write cockpit depth.
        for(auto& stage:transparent->shaderSet->stages)if(stage->stage==VK_SHADER_STAGE_FRAGMENT_BIT){auto& source=stage->module->source;const auto at=source.find("outColor = diffuseColor;");if(at!=std::string::npos){source.insert(at,"if (diffuseColor.a < 0.01) discard;\n    ");stage->module->code.clear();}}
        return transparent->createQuad(g,state);};
    geometry->addChild(quad(instrumentImage_,{0,.48f,-1.615f},{2.9f,0,0},{0,.96f,0},false));
    hud_=vsg::Switch::create();hud_->addChild(true,quad(hudImage_,{0,1.2f,-3},{2.4f,0,0},{0,1.35f,0},true));body_->addChild(hud_);
    dirty_=true;onFrame(ctx,{});ctx.requestCompile();
}
void AircraftCockpitFeature::onDetach(ViewportContext& ctx){if(ctx.widget())ctx.widget()->setNearClipOverride(0);ctx.removeFromWorld(root_);root_=nullptr;body_=nullptr;hud_=nullptr;interior_=nullptr;exterior_=nullptr;exteriorGeometry_=nullptr;exteriorReady_=false;modelDirty_=true;instrumentImage_=nullptr;hudImage_=nullptr;}
void AircraftCockpitFeature::configure(const json& j){const auto mode=j.value("view_mode",mode_);if(mode!="pilot"&&mode!="overview"&&mode!="external")throw std::invalid_argument("invalid cockpit camera mode");
    sceneExterior_=j.value("scene_exterior",sceneExterior_);
    const double yaw=j.value("look_yaw_deg",lookYaw_),pitch=j.value("look_pitch_deg",lookPitch_),fov=j.value("fov_deg",fov_);const bool hud=j.value("hud",hudEnabled_);
    if(!std::isfinite(yaw)||!std::isfinite(pitch)||!std::isfinite(fov)||std::abs(yaw)>(mode=="external"?180.:120.)||std::abs(pitch)>75||fov<40||fov>90)throw std::invalid_argument("invalid cockpit view angles/FOV");
    const double distance=j.value("chase_distance",chaseDistance_),dimension=j.value("model_max_dimension",modelDimension_),heading=j.value("model_heading_deg",modelHeading_);const auto path=j.value("model_uri",modelPath_),up=j.value("model_up_axis",modelUpAxis_),kind=j.value("model_kind",modelKind_);
    if(!std::isfinite(distance)||distance<10||distance>1000||!std::isfinite(dimension)||dimension<=0||(up!="y"&&up!="z"))throw std::invalid_argument("invalid exterior view parameters");
    if(!std::isfinite(heading))throw std::invalid_argument("invalid exterior heading");modelDirty_|=path!=modelPath_||up!=modelUpAxis_||kind!=modelKind_||dimension!=modelDimension_||heading!=modelHeading_;modelPath_=path;modelUpAxis_=up;modelKind_=kind;modelDimension_=dimension;modelHeading_=heading;chaseDistance_=distance;
    const bool interior=j.value("interior",interiorEnabled_);if(interior!=interiorEnabled_)dirty_=true;mode_=mode;lookYaw_=yaw;lookPitch_=pitch;fov_=fov;hudEnabled_=hud;interiorEnabled_=interior;}
void AircraftCockpitFeature::setAircraft(const json& j,bool fresh){const auto next=validTelemetry(j)?j:json::object();if(next!=aircraft_||fresh_!=fresh){aircraft_=next;fresh_=fresh;dirty_=true;}}
void AircraftCockpitFeature::synchronizeCamera(ViewportContext& ctx){const bool follow=enabled_&&mode_!="overview"&&validTelemetry(aircraft_);if(ctx.widget())ctx.widget()->setNearClipOverride(follow&&mode_=="pilot"?.04:0);if(!follow)return;auto* earth=ctx.featureAs<EarthView>(EarthView::kId);auto* cam=ctx.camera();if(!earth||!cam)return;
    const auto look=mode_=="external"?chaseCamera(earth->anchor(),aircraft_,chaseDistance_,lookYaw_,lookPitch_):pilotCamera(earth->anchor(),aircraft_,lookYaw_,lookPitch_);if(auto target=cam->viewMatrix.cast<vsg::LookAt>()){target->eye=look.eye;target->center=look.center;target->up=look.up;}
    if(auto perspective=cam->projectionMatrix.cast<vsg::Perspective>()){perspective->fieldOfViewY=fov_;perspective->nearDistance=.04;perspective->farDistance=5e7;}
}
void AircraftCockpitFeature::onFrame(ViewportContext& ctx,const FrameTick&) {
    if(!body_)return;const bool valid=validTelemetry(aircraft_);root_->children.front().mask=enabled_&&valid&&mode_!="overview"?(mode_=="external"?~vsg::Mask(0):~vsg::Mask(1)):vsg::Mask(0);hud_->setAllChildren(hudEnabled_&&mode_=="pilot");interior_->setAllChildren(interiorEnabled_&&mode_=="pilot");exterior_->setAllChildren(mode_=="external"&&!sceneExterior_);
    if(modelDirty_&&mode_=="external"&&!sceneExterior_){auto model=loadAircraftExterior(modelPath_,modelUpAxis_,modelKind_,modelDimension_,ctx.options(),representative_,modelHeading_);if(!exteriorGeometry_->children.empty()&&ctx.viewer())ctx.viewer()->deviceWaitIdle();exteriorGeometry_->children.clear();if(model)exteriorGeometry_->addChild(model);exteriorReady_=bool(model);modelDirty_=false;ctx.requestCompile();}
    if(valid){if(auto* earth=ctx.featureAs<EarthView>(EarthView::kId)){const auto& p=aircraft_["position"];body_->matrix=simcore::geo::makeEnuToWorldMatrix(earth->anchor())*vsg::translate(p[0].get<double>(),p[1].get<double>(),p[2].get<double>())*vsg::rotate(attitude(aircraft_));}}
    if(dirty_){if(interiorEnabled_)upload(*instrumentImage_,instruments(aircraft_,fresh_));upload(*hudImage_,hud(aircraft_,fresh_));dirty_=false;}synchronizeCamera(ctx);
}
VSGVP_REGISTER_FEATURE(AircraftCockpitFeature);
}
