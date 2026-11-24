#include "TexturedCockpit.h"
#include "EquipmentSession.h"
#include "SensorPanels.h"
#include <QResizeEvent>
#include <QFocusEvent>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

namespace vsgraysim::gui::display {
namespace {
constexpr double pi=3.14159265358979323846;
void writing(QPainter& p,const QRectF& r,const QString& s,double size,QColor color=QColor("#a8ef9a"),Qt::Alignment alignment=Qt::AlignCenter) {
    QFont f("DejaVu Sans Mono");f.setPixelSize(std::max(6,int(size)));f.setWeight(QFont::Medium);p.setFont(f);p.setPen(color);p.drawText(r,alignment,s);
}
QString reading(std::optional<double> v,int places=0){return v?QString::number(*v,'f',places):QString("---");}
double heading(double value){const double h=std::fmod(value,360.);return h<0?h+360:h;}
}
TexturedCockpit::TexturedCockpit(QWidget* parent):QWidget(parent) {
    setObjectName("textured_cockpit");setMinimumSize(640,320);setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);
}
bool TexturedCockpit::loadProfile(const QString& path,QString& error) {
    try {
        auto p=CockpitProfile::load(path);QImage background(p.backgroundPath),grips(p.gripsPath);
        if(background.isNull()||grips.isNull())throw std::runtime_error("cockpit texture is missing or cannot be decoded");
        cancelInteraction();delete payload_;payload_=nullptr;payloadType_=-1;profile_=std::move(p);background_=std::move(background);grips_=std::move(grips);pages_.clear();
        for(const auto& i:profile_.instruments)if(i.kind=="mfd")pages_[i.id]=i.initialPage.isEmpty()?(pages_.isEmpty()?"FLT":"CTRL"):i.initialPage;setPayloadSession(nullptr);update();error.clear();return true;
    }catch(const std::exception& e){error=QString::fromUtf8(e.what());return false;}
}
void TexturedCockpit::layoutPayload(){if(!payload_)return;for(const auto& i:profile_.instruments)if(i.kind=="payload"){payload_->setGeometry(panelRect(i.rect).toAlignedRect());payload_->show();payload_->raise();return;}}
void TexturedCockpit::resizeEvent(QResizeEvent* e){QWidget::resizeEvent(e);layoutPayload();}
void TexturedCockpit::setPayloadSession(EquipmentSession* session){
    const bool configured=std::any_of(profile_.instruments.begin(),profile_.instruments.end(),[](const auto& i){return i.kind=="payload";});
    if(!configured)return;
    if(session&&session->sensorType()!=SensorType::VIS&&session->sensorType()!=SensorType::IR)session=nullptr;
    const auto type=session?session->sensorType():SensorType::VIS;
    if(!payload_||(session&&payloadType_!=int(type))){delete payload_;payload_=new ImageDisplayPanel(type,this,true);payload_->setObjectName("cockpit_payload_image");payloadType_=int(type);}
    payload_->tick(session);layoutPayload();
}
std::optional<double> TexturedCockpit::measurement(const nlohmann::json& data,const QString& binding) {
    try {
        if(binding=="ground_speed_kt") {
            const auto x=measurement(data,"velocity.0"),z=measurement(data,"velocity.2");
            if(x&&z)return std::hypot(*x,*z)*1.943844;
            return {};
        }
        const auto* at=&data;
        for(const auto& key:binding.split('.')) {
            if(at->is_object()){auto i=at->find(key.toStdString());if(i==at->end())return {};at=&*i;}
            else if(at->is_array()){bool ok=false;const int index=key.toInt(&ok);if(!ok||index<0||index>=int(at->size()))return {};at=&(*at)[index];}
            else return {};
        }
        if(at->is_number()){double v=at->get<double>();if(std::isfinite(v))return v;}
    }catch(const std::exception&){}
    return {};
}
void TexturedCockpit::setTelemetry(const nlohmann::json& state,bool fresh){if(state_!=state||fresh_!=fresh){state_=state.is_object()?state:nlohmann::json::object();fresh_=fresh;update();}}
void TexturedCockpit::setControlState(bool enabled,double throttle,double elevator,double aileron,double rudder) {
    if(!enabled&&enabled_)cancelInteraction();enabled_=enabled;
    if(!interacting()){throttle_=std::clamp(throttle,0.,1.);elevator_=std::clamp(elevator,-1.,1.);aileron_=std::clamp(aileron,-1.,1.);rudder_=std::clamp(rudder,-1.,1.);}
    update();
}
void TexturedCockpit::setPanelLighting(double value){lighting_=std::clamp(value,.2,1.);update();}
QRectF TexturedCockpit::imageRect()const {
    if(background_.isNull())return rect();const double s=std::min(double(width())/background_.width(),double(height())/background_.height());
    const QSizeF size(background_.width()*s,background_.height()*s);return QRectF((width()-size.width())*.5,(height()-size.height())*.5,size.width(),size.height());
}
QRectF TexturedCockpit::panelRect(const QRectF& r)const{const auto target=imageRect();return {target.x()+r.x()*target.width(),target.y()+r.y()*target.height(),r.width()*target.width(),r.height()*target.height()};}
QPointF TexturedCockpit::normalized(const QPointF& p)const{const auto target=imageRect();return {(p.x()-target.x())/target.width(),(p.y()-target.y())/target.height()};}
QPoint TexturedCockpit::controlPosition(const QString& id)const{for(const auto& h:profile_.hotspots)if(h.id==id)return panelRect(h.rect).center().toPoint();return panelRect(id=="stick"?profile_.stick:profile_.throttle).center().toPoint();}
void TexturedCockpit::drawInstrument(QPainter& p,const CockpitInstrument& i) {
    const auto r=panelRect(i.rect);const double w=r.width(),h=r.height(),font=std::min(w,h);
    const QColor green=fresh_?QColor("#b1f69a"):QColor("#e4b264");
    if(i.kind=="mfd"||i.kind=="ded") {
        p.fillRect(r,QColor("#060b07"));p.save();p.setClipRect(r.adjusted(2,2,-2,-2));
        const QString page=pages_.value(i.id,"FLT");
        if(i.kind=="ded") {
            const double size=std::min(h*.22,w*.07);writing(p,r.adjusted(4,0,-4,-h*.55),i.label+" / DATA",size,green);
            writing(p,r.adjusted(4,h*.45,-4,0),"SIM "+reading(measurement(state_,"sim_time"),1)+"   GRID "+reading(measurement(state_,"yaw")),size,green);p.restore();return;
        }
        writing(p,QRectF(r.x(),r.y()+h*.02,w,h*.13),page+"    "+(fresh_?"DATA":"HOLD"),font*.07,green);
        if(page=="FLT"||page=="NAV") {
            const auto centre=r.center()+QPointF(0,-h*.04);const double yaw=heading(measurement(state_,"yaw").value_or(0));p.save();p.translate(centre);p.rotate(-yaw);p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor("#518b52"),1));
            for(double f:{.16,.25})p.drawEllipse(QPointF(0,0),w*f,h*f);
            for(int d=0;d<360;d+=30){p.save();p.rotate(d);p.drawLine(QPointF(0,-h*.22),QPointF(0,-h*.27));writing(p,QRectF(-w*.075,-h*.33,w*.15,h*.08),QString::number(d/10),font*.055,green);p.restore();}p.restore();
            p.setPen(QPen(green,1.5));const auto c=centre;p.drawLine(c+QPointF(0,-h*.1),c+QPointF(-w*.06,h*.05));p.drawLine(c+QPointF(0,-h*.1),c+QPointF(w*.06,h*.05));
            writing(p,QRectF(r.x(),r.y()+h*.81,w,h*.08),"GS "+reading(measurement(state_,"ground_speed_kt"))+" KT",font*.065,green);
            writing(p,QRectF(r.x(),r.y()+h*.9,w,h*.08),"LOCAL "+reading(measurement(state_,"position.1"))+" M",font*.055,green);
        }else if(page=="CTRL"||page=="SYS") {
            writing(p,QRectF(r.x(),r.y()+h*.19,w,h*.1),"AXIS    DEM / ACT",font*.065,green);
            int row=0;for(const auto* key:{"throttle","elevator","aileron","rudder"}) {
                auto demand=measurement(state_,QString("flight_control.command.")+key),actual=measurement(state_,QString("flight_control.actual.")+key);
                if(demand)*demand*=100;if(actual)*actual*=100;
                writing(p,QRectF(r.x()+w*.02,r.y()+h*(.33+row++*.12),w*.96,h*.09),QString(key).left(3).toUpper()+" "+reading(demand)+" / "+reading(actual),font*.058,green);
            }
            writing(p,QRectF(r.x(),r.y()+h*.88,w,h*.08),"NORMALIZED %",font*.05,green);
        }else {
            writing(p,QRectF(r.x(),r.y()+h*.22,w,h*.1),QString("ENGINES %1").arg(profile_.engineCount),font*.07,green);bool any=false;
            for(int engine=0;engine<profile_.engineCount;++engine){const auto binding=profile_.engineCount==1?QString("engine.rpm_pct"):QString("engines.%1.rpm_pct").arg(engine);const auto rpm=measurement(state_,binding);any|=rpm.has_value();writing(p,QRectF(r.x(),r.y()+h*(.37+engine*.1),w,h*.09),QString("%1 RPM ").arg(engine+1)+reading(rpm),font*.07,green);}
            writing(p,QRectF(r.x(),r.y()+h*.79,w,h*.08),"FUEL "+reading(measurement(state_,"engine.fuel_kg")),font*.06,green);
            if(!any)writing(p,QRectF(r.x(),r.y()+h*.9,w,h*.08),"NO ENGINE DATA",font*.05,QColor("#dba26b"));
        }
        p.restore();return;
    }
    p.save();p.setRenderHint(QPainter::Antialiasing);p.setClipRect(r);p.translate(r.center());const double radius=font*.47;
    QPainterPath aperture;if(i.kind=="pfd")aperture.addRect(QRectF(-w/2,-h/2,w,h));else aperture.addEllipse(QPointF(0,0),radius,radius);p.setClipPath(aperture,Qt::IntersectClip);
    p.fillRect(QRectF(-w/2,-h/2,w,h),QColor("#101411"));
    if(i.kind=="attitude"||i.kind=="pfd") {
        const auto pitch=measurement(state_,"pitch"),roll=measurement(state_,"roll");
        if(pitch&&roll){p.save();p.rotate(-*roll);p.translate(0,std::clamp(*pitch,-90.,90.)*radius/40.);p.fillRect(QRectF(-w,-h*2,w*2,h*2),QColor("#39616a"));p.fillRect(QRectF(-w,0,w*2,h*2),QColor("#775536"));p.setPen(QPen(QColor("#e8dfc7"),1.2));p.drawLine(QPointF(-w,0),QPointF(w,0));for(int d=-40;d<=40;d+=10)if(d){const double y=-d*radius/40.;p.drawLine(QPointF(-radius*.32,y),QPointF(radius*.32,y));}p.restore();
            p.setPen(QPen(QColor("#e6ba5c"),2));p.drawLine(QPointF(-radius*.72,0),QPointF(-radius*.16,0));p.drawLine(QPointF(radius*.16,0),QPointF(radius*.72,0));p.drawEllipse(QPointF(0,0),radius*.07,radius*.07);
        }else writing(p,QRectF(-w/2,-h/2,w,h),"OFF",font*.18,QColor("#ddac6b"));
        if(i.kind=="pfd"){p.fillRect(QRectF(-w/2,-h/2,w*.22,h),QColor(8,13,15,220));p.fillRect(QRectF(w*.28,-h/2,w*.22,h),QColor(8,13,15,220));auto ias=measurement(state_,"air_data.ias_mps"),altitude=measurement(state_,"air_data.pressure_altitude_m");if(ias)*ias*=1.943844;if(altitude)*altitude*=3.28084;writing(p,QRectF(-w/2,-h*.1,w*.22,h*.2),reading(ias),font*.10);writing(p,QRectF(w*.28,-h*.1,w*.22,h*.2),reading(altitude),font*.10);writing(p,QRectF(-w/2,-h*.3,w*.22,h*.1),"IAS KT",font*.055);writing(p,QRectF(w*.28,-h*.3,w*.22,h*.1),"ALT FT",font*.055);writing(p,QRectF(-w/2,h*.35,w,h*.1),"GS "+reading(measurement(state_,"ground_speed_kt"))+" KT  /  LOCAL "+reading(measurement(state_,"position.1"))+" M",font*.055);}
    }else if(i.kind=="compass") {
        const auto angle=measurement(state_,i.binding);if(angle){p.save();p.rotate(-heading(*angle));p.setPen(QPen(QColor("#e6dfc8"),1));for(int d=0;d<360;d+=10){p.save();p.rotate(d);p.drawLine(QPointF(0,-radius*.79),QPointF(0,-radius*.94));if(d%30==0)writing(p,QRectF(-radius*.2,-radius*.75,radius*.4,radius*.21),QString::number(d/10),font*.09,QColor("#e6dfc8"));p.restore();}p.restore();p.setPen(QPen(QColor("#eccf80"),2));p.drawLine(QPointF(0,-radius*.95),QPointF(0,-radius*.6));}else writing(p,QRectF(-w/2,-h/2,w,h),"OFF",font*.18,QColor("#ddac6b"));
    }else {
        const auto raw=measurement(state_,i.binding);p.setPen(QPen(QColor("#e6dfc8"),1));
        const bool thousands=std::max(std::abs(i.minimum),std::abs(i.maximum))>=1000;
        for(int tick=0;tick<=40;++tick){const double angle=-135+tick*6.75;p.save();p.rotate(angle);p.drawLine(QPointF(0,-radius*(tick%5==0?.72:.83)),QPointF(0,-radius*.94));p.restore();
            if(tick%10==0){const auto c=QPointF(std::sin(angle*pi/180)*radius*.6,-std::cos(angle*pi/180)*radius*.6);const double value=(i.minimum+(i.maximum-i.minimum)*tick/40.)/(thousands?1000.:1.);writing(p,QRectF(c.x()-radius*.25,c.y()-radius*.12,radius*.5,radius*.24),QString::number(value,'g',3),font*.085,QColor("#e6dfc8"));}}
        writing(p,QRectF(-w/2,radius*.14,w,radius*.2),i.label,font*.09,QColor("#d7d5be"));writing(p,QRectF(-w/2,radius*.36,w,radius*.2),(thousands?"x1000 ":"")+i.unit,font*.075,QColor("#a2a693"));
        if(raw){p.save();p.rotate(-135+270*std::clamp((*raw*i.scale-i.minimum)/(i.maximum-i.minimum),0.,1.));p.setPen(QPen(QColor("#f2e2bc"),std::max(1.,radius*.045)));p.drawLine(QPointF(0,radius*.17),QPointF(0,-radius*.76));p.restore();p.setBrush(QColor("#bab4a2"));p.setPen(Qt::NoPen);p.drawEllipse(QPointF(0,0),radius*.09,radius*.09);}
        else writing(p,QRectF(-w/2,-radius*.23,w,radius*.24),"INOP",font*.135,QColor("#e0a864"));
    }
    QRadialGradient glass(-radius*.4,-radius*.6,radius*1.7);glass.setColorAt(0,QColor(235,244,236,34));glass.setColorAt(.55,QColor(255,255,255,0));glass.setColorAt(1,QColor(0,0,0,65));p.fillPath(aperture,glass);p.restore();
}
void TexturedCockpit::paintEvent(QPaintEvent*) {
    QPainter p(this);p.fillRect(rect(),QColor("#080b0c"));p.setRenderHint(QPainter::SmoothPixmapTransform);
    if(background_.isNull()){writing(p,rect(),tr("正在加载机型座舱贴图"),18,QColor("#d1b17c"));return;}
    p.drawImage(imageRect(),background_);for(const auto& i:profile_.instruments)drawInstrument(p,i);
    const QRectF leftSource(0,0,grips_.width()/2.,grips_.height()),rightSource(grips_.width()/2.,0,grips_.width()/2.,grips_.height());
    auto throttle=panelRect(profile_.throttle);throttle.translate(0,(.5-throttle_)*throttle.height()*profile_.throttleTravel);p.drawImage(throttle,grips_,leftSource);
    auto stick=panelRect(profile_.stick);stick.translate(aileron_*stick.width()*profile_.stickTravelX,elevator_*stick.height()*profile_.stickTravelY);p.drawImage(stick,grips_,rightSource);
    if(lighting_<1)p.fillRect(imageRect(),QColor(0,0,0,int((1-lighting_)*155)));
    if(!fresh_)writing(p,QRectF(imageRect().x()+10,imageRect().bottom()-30,230,25),state_.empty()?"NO TELEMETRY":"DATA HOLD",13,QColor("#e5b065"),Qt::AlignLeft|Qt::AlignVCenter);
}
void TexturedCockpit::mousePressEvent(QMouseEvent* e) {
    if(e->button()!=Qt::LeftButton)return QWidget::mousePressEvent(e);
    const auto at=normalized(e->position());const bool stick=profile_.stick.contains(at),throttle=profile_.throttle.contains(at);
    if(stick||throttle){if(!enabled_){if(statusChanged)statusChanged(tr("请先接管飞机并选择手动操纵"));return;}if(interactionStarted)interactionStarted();drag_=stick?"stick":"throttle";press_=at;setFocus(Qt::MouseFocusReason);grabMouse();dragTo(at);return;}
    for(const auto& h:profile_.hotspots)if(h.rect.contains(at)){if(h.action=="mfd_page")pages_[h.target]=h.value;else setPanelLighting(lighting_<.99?1.:.45);update();if(statusChanged)statusChanged(h.label+tr("（本地显示）"));return;}
    if(statusChanged)statusChanged(tr("该机械面板热区尚未接入系统模型；不发送虚构开关状态"));
}
void TexturedCockpit::dragTo(const QPointF& at) {
    if(!enabled_||drag_.isEmpty())return;
    if(drag_=="stick"){aileron_=std::clamp((at.x()-press_.x())/(profile_.stick.width()*.36),-1.,1.)*profile_.maximumAxisDemand;elevator_=std::clamp((at.y()-press_.y())/(profile_.stick.height()*.36),-1.,1.)*profile_.maximumAxisDemand;}
    else throttle_=std::clamp((profile_.throttle.bottom()-at.y())/profile_.throttle.height(),0.,1.);
    if(demandChanged)demandChanged(throttle_,elevator_,aileron_,rudder_);update();
}
void TexturedCockpit::mouseMoveEvent(QMouseEvent* e) {
    const auto at=normalized(e->position());if(interacting()){dragTo(at);return;}
    const bool hot=profile_.stick.contains(at)||profile_.throttle.contains(at);setCursor(hot&&enabled_?Qt::OpenHandCursor:Qt::ArrowCursor);
    if(hot)setToolTip(enabled_?(profile_.stick.contains(at)?tr("右侧力感式操作杆：拖动输入俯仰/滚转，松手回中"):tr("左侧油门：向前拖动增大需求，松手保持")):tr("先在控制栏接管飞机"));
    else {for(const auto& h:profile_.hotspots)if(h.rect.contains(at)){setCursor(Qt::PointingHandCursor);setToolTip(h.label+tr("；仅切换显示，不改变飞行状态"));return;}setToolTip(tr("仪表读取平台真值；使用 MFD 下沿实体按键切换页面"));}
}
void TexturedCockpit::cancelInteraction() {
    const bool stick=drag_=="stick";if(drag_.isEmpty())return;drag_.clear();releaseMouse();
    if(stick){elevator_=aileron_=0;if(enabled_&&demandChanged)demandChanged(throttle_,0,0,rudder_);}update();
}
void TexturedCockpit::mouseReleaseEvent(QMouseEvent* e){if(e->button()==Qt::LeftButton&&interacting()){cancelInteraction();return;}QWidget::mouseReleaseEvent(e);}
void TexturedCockpit::focusOutEvent(QFocusEvent* e){cancelInteraction();QWidget::focusOutEvent(e);}
void TexturedCockpit::hideEvent(QHideEvent* e){cancelInteraction();QWidget::hideEvent(e);}
}
