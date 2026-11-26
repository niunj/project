#include "PlatformInstruments.h"
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
#include <array>
#include <cmath>

namespace vsgraysim::gui::display {
namespace {
constexpr double pi=3.14159265358979323846;
void writing(QPainter& p,const QRectF& r,const QString& s,double size,QColor color=QColor("#a8ef9a"),Qt::Alignment alignment=Qt::AlignCenter) {
    QFont f("DejaVu Sans Mono");f.setPixelSize(std::max(6,int(size)));f.setWeight(QFont::Medium);p.setFont(f);p.setPen(color);p.drawText(r,alignment,s);
}
QString reading(std::optional<double> v,int places=0){return v?QString::number(*v,'f',places):QString("---");}
double heading(double value){const double h=std::fmod(value,360.);return h<0?h+360:h;}

void arrangeInstruments(CockpitProfile& profile) {
    std::vector<CockpitInstrument*> displays, gauges;
    for (auto& instrument : profile.instruments) {
        if (instrument.kind=="mfd" || instrument.kind=="pfd" || instrument.kind=="payload") displays.push_back(&instrument);
        else gauges.push_back(&instrument);
    }
    auto arrange = [](const auto& instruments, double top, double height) {
        const double pitch=.95/std::max<size_t>(1,instruments.size());
        for(size_t n=0;n<instruments.size();++n) instruments[n]->rect={.025+n*pitch,top,pitch-.013,height};
    };
    const double displayHeight=gauges.empty()?.504:.312;
    arrange(displays,.215,displayHeight);
    arrange(gauges,.646,.175);
    QMap<QString,int> buttons;
    for(auto& button:profile.hotspots) {
        if(button.action=="panel_light") {button.rect={.401,.881,.198,.078};continue;}
        const auto display=std::find_if(displays.begin(),displays.end(),[&](const auto* instrument){return instrument->id==button.target;});
        if(display==displays.end())continue;
        const auto screen=(*display)->rect;
        const auto count=std::count_if(profile.hotspots.begin(),profile.hotspots.end(),[&](const auto& candidate){return candidate.target==button.target;});
        const double pitch=screen.width()/std::max<decltype(count)>(1,count);
        button.rect={screen.x()+buttons[button.target]++*pitch,screen.bottom()+.009,pitch-.006,.045};
    }
    const auto stickLeft=profile.stick.center().x()<profile.throttle.center().x();
    profile.stick={stickLeft?.035:.76,.848,.205,.143};
    profile.throttle={stickLeft?.76:.035,.848,.205,.143};
}
}
PlatformInstruments::PlatformInstruments(QWidget* parent):QWidget(parent) {
    setObjectName("platform_instruments");setMinimumSize(620,330);setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);
}
bool PlatformInstruments::loadProfile(const QString& path,QString& error) {
    try {
        auto p=CockpitProfile::load(path);
        if(p.automaticLayout)arrangeInstruments(p);
        cancelInteraction();delete payload_;payload_=nullptr;payloadType_=-1;profile_=std::move(p);pages_.clear();
        for(const auto& i:profile_.instruments)if(i.kind=="mfd")pages_[i.id]=i.initialPage.isEmpty()?(pages_.isEmpty()?"FLT":"CTRL"):i.initialPage;setPayloadSession(nullptr);update();error.clear();return true;
    }catch(const std::exception& e){error=QString::fromUtf8(e.what());return false;}
}
void PlatformInstruments::layoutPayload(){if(!payload_)return;for(const auto& i:profile_.instruments)if(i.kind=="payload"){payload_->setGeometry(panelRect(i.rect).toAlignedRect());payload_->show();payload_->raise();return;}}
void PlatformInstruments::resizeEvent(QResizeEvent* e){QWidget::resizeEvent(e);layoutPayload();}
void PlatformInstruments::setPayloadSession(EquipmentSession* session){
    if(domain_!=PlatformDomain::Air){if(payload_)payload_->hide();return;}
    const bool configured=std::any_of(profile_.instruments.begin(),profile_.instruments.end(),[](const auto& i){return i.kind=="payload";});
    if(!configured)return;
    if(session&&session->sensorType()!=SensorType::VIS&&session->sensorType()!=SensorType::IR)session=nullptr;
    const auto type=session?session->sensorType():SensorType::VIS;
    if(!payload_||(session&&payloadType_!=int(type))){delete payload_;payload_=new ImageDisplayPanel(type,this,true);payload_->setObjectName("cockpit_payload_image");payloadType_=int(type);}
    payload_->tick(session);layoutPayload();
}
std::optional<double> PlatformInstruments::measurement(const nlohmann::json& data,const QString& binding) {
    try {
        if(binding=="ground_speed_kt") {
            const auto x=measurement(data,"velocity.0"),z=measurement(data,"velocity.2");
            if(x&&z)return std::hypot(*x,*z)*1.943844;
            return {};
        }
        if(binding=="ground_speed_kmh") {
            if(const auto speed=measurement(data,"ground_speed_kt"))return *speed*(3.6/1.943844);
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
void PlatformInstruments::setTelemetry(const nlohmann::json& state,bool fresh){if(state_!=state||fresh_!=fresh){state_=state.is_object()?state:nlohmann::json::object();fresh_=fresh;if(!state_.empty())domain_=platformDomain(state_);if(payload_&&domain_!=PlatformDomain::Air)payload_->hide();update();}}
void PlatformInstruments::setControlState(bool enabled,double throttle,double elevator,double aileron,double rudder) {
    bool changed=enabled_!=enabled;
    if(!enabled&&enabled_)cancelInteraction();enabled_=enabled;
    if(!interacting()){
        const auto next=std::array{std::clamp(throttle,0.,1.),std::clamp(elevator,-1.,1.),std::clamp(aileron,-1.,1.),std::clamp(rudder,-1.,1.)};
        changed|=next!=std::array{throttle_,elevator_,aileron_,rudder_};
        throttle_=next[0];elevator_=next[1];aileron_=next[2];rudder_=next[3];
    }
    if(changed)update();
}
void PlatformInstruments::setPanelLighting(double value){lighting_=std::clamp(value,.2,1.);update();}
QRectF PlatformInstruments::imageRect()const {
    const double s=std::min(double(width())/1152.,double(height())/650.);
    const QSizeF size(1152*s,650*s);return QRectF((width()-size.width())*.5,(height()-size.height())*.5,size.width(),size.height());
}
QRectF PlatformInstruments::panelRect(const QRectF& r)const{const auto target=imageRect();return {target.x()+r.x()*target.width(),target.y()+r.y()*target.height(),r.width()*target.width(),r.height()*target.height()};}
QPointF PlatformInstruments::normalized(const QPointF& p)const{const auto target=imageRect();return {(p.x()-target.x())/target.width(),(p.y()-target.y())/target.height()};}
QPoint PlatformInstruments::controlPosition(const QString& id)const{for(const auto& h:profile_.hotspots)if(h.id==id)return panelRect(h.rect).center().toPoint();return panelRect(id=="stick"?profile_.stick:profile_.throttle).center().toPoint();}
void PlatformInstruments::drawInstrument(QPainter& p,const CockpitInstrument& i) {
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
            for(double f:{.16,.25})p.drawEllipse(QPointF(0,0),font*f,font*f);
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
void PlatformInstruments::drawTelemetry(QPainter& p) {
    struct Reading { QString label, value, unit; };
    const bool ground=domain_==PlatformDomain::Ground;
    const bool air=domain_==PlatformDomain::Air;
    const auto headingDeg=measurement(state_,"yaw");
    const std::array readings{
        Reading{air?tr("地速"):tr("对地速度"),reading(measurement(state_,ground?"ground_speed_kmh":"ground_speed_kt"),1),ground?"km/h":"kt"},
        Reading{tr("网格航向"),headingDeg?QString::number(int(std::lround(heading(*headingDeg)))%360).rightJustified(3,'0'):"---","°"},
        Reading{air?tr("局部高度"):tr("俯仰"),reading(measurement(state_,air?"position.1":"pitch"),1),air?"m":"°"},
        Reading{air?tr("垂直速度"):tr("横滚"),reading(measurement(state_,air?"velocity.1":"roll"),1),air?"m/s":"°"}
    };
    const auto accent=fresh_?QColor("#b4ebd6"):QColor("#dfbb80");
    const auto scale=imageRect().height()/650.;
    for(size_t k=0;k<readings.size();++k) {
        const auto r=panelRect({.025+k*.24,.029,.228,.123});
        p.setPen(QColor("#334754"));p.setBrush(QColor("#111d27"));p.drawRoundedRect(r,7,7);
        writing(p,r.adjusted(13*scale,4*scale,-8*scale,-r.height()*.57),readings[k].label,16*scale,QColor("#97b0bf"),Qt::AlignLeft|Qt::AlignVCenter);
        writing(p,r.adjusted(13*scale,r.height()*.39,-8*scale,-4*scale),readings[k].value+"  "+readings[k].unit,29*scale,accent,Qt::AlignLeft|Qt::AlignVCenter);
    }
}
void PlatformInstruments::drawControls(QPainter& p) {
    const auto stick=panelRect(profile_.stick), throttle=panelRect(profile_.throttle);
    const auto ink=enabled_?QColor("#65d3af"):QColor("#75919f");
    p.setPen(QColor("#344a56"));p.setBrush(QColor("#14222b"));
    p.drawRoundedRect(stick,7,7);p.drawRoundedRect(throttle,7,7);
    const auto pad=stick.adjusted(stick.width()*.57,stick.height()*.12,-stick.width()*.08,-stick.height()*.12);
    const auto c=pad.center();
    p.setPen(QPen(QColor("#405969"),1));p.setBrush(QColor("#0a141a"));p.drawRoundedRect(pad,4,4);
    p.drawLine(QPointF(pad.left(),c.y()),QPointF(pad.right(),c.y()));p.drawLine(QPointF(c.x(),pad.top()),QPointF(c.x(),pad.bottom()));
    p.setPen(QPen(ink,2));p.setBrush(ink);
    const auto travelX=std::min(.43,.35*profile_.stickTravelX/.06);
    const auto travelY=std::min(.43,.35*profile_.stickTravelY/.04);
    const QPointF knob=c+QPointF(aileron_*pad.width()*travelX,elevator_*pad.height()*travelY);
    p.drawLine(c,knob);p.drawEllipse(knob,pad.height()*.11,pad.height()*.11);
    const auto rail=throttle.adjusted(throttle.width()*.74,throttle.height()*.12,-throttle.width()*.13,-throttle.height()*.12);
    p.setPen(QColor("#405969"));p.setBrush(QColor("#0a141a"));p.drawRoundedRect(rail,3,3);
    p.setPen(Qt::NoPen);p.setBrush(ink);
    const double marker=rail.bottom()-throttle_*rail.height();
    p.drawRoundedRect(QRectF(rail.left()-3,marker-3,rail.width()+6,6),2,2);
    const double font=std::min(stick.height(),throttle.height())*.17;
    const auto stickName=profile_.category=="helicopter"?tr("周期变距杆"):profile_.category=="uav"?tr("手柄操纵轴"):tr("操纵杆");
    const auto throttleName=profile_.category=="helicopter"?tr("总距需求"):tr("油门需求");
    writing(p,stick.adjusted(5,0,-stick.width()*.49,0),stickName,font,ink);
    writing(p,throttle.adjusted(5,0,-throttle.width()*.35,0),throttleName+"\n"+QString::number(throttle_*100,'f',0)+" %",font,ink);
}
void PlatformInstruments::paintEvent(QPaintEvent*) {
    QPainter p(this);p.fillRect(rect(),QColor("#0c141c"));p.setRenderHint(QPainter::Antialiasing);
    if(profile_.id.isEmpty()){writing(p,rect(),tr("请选择仪表配置"),18,QColor("#d1b17c"));return;}
    const auto board=imageRect();p.fillRect(board,QColor("#182630"));
    if(profile_.automaticLayout||domain_!=PlatformDomain::Air)drawTelemetry(p);
    const auto frame=[&](const CockpitInstrument& i){
        const auto r=panelRect(i.rect);
        p.setPen(QColor("#3b505d"));p.setBrush(QColor("#101b24"));p.drawRoundedRect(r.adjusted(-3,-3,3,3),6,6);
        if(i.kind!="payload")drawInstrument(p,i);
        const auto title=QRectF(r.x(),r.y()-board.height()*.043,r.width(),board.height()*.035);
        writing(p,title,i.label,board.height()*.021,QColor("#afc8d5"));
    };
    if(domain_==PlatformDomain::Air) {
        for(const auto& instrument:profile_.instruments)frame(instrument);
        for(const auto& button:profile_.hotspots) {
            const auto r=panelRect(button.rect);
            const bool active=button.action=="mfd_page"&&pages_.value(button.target)==button.value;
            p.setPen(active?QColor("#619588"):QColor("#344a56"));p.setBrush(active?QColor("#213d3c"):QColor("#101e28"));p.drawRoundedRect(r,4,4);
            writing(p,r,button.action=="mfd_page"?button.value:tr("仪表照明"),board.height()*.018,QColor("#c2d5db"));
        }
        drawControls(p);
    } else {
        const bool sea=domain_==PlatformDomain::Sea;
        frame({"speed","dial",sea?tr("航速"):tr("车速"),sea?"ground_speed_kt":"ground_speed_kmh",sea?"kt":"km/h",{}, {.10,.28,.235,.52},0,sea?50.:100.,1});
        frame({"heading","compass",tr("网格航向"),"yaw",{}, {}, {.383,.28,.235,.52}});
        frame({"attitude","attitude",sea?tr("舰体姿态"):tr("车体姿态"),{}, {}, {}, {.665,.28,.235,.52}});
    }
    if(lighting_<1)p.fillRect(imageRect(),QColor(0,0,0,int((1-lighting_)*155)));
    if(!fresh_)writing(p,panelRect(domain_==PlatformDomain::Air?QRectF(.29,.845,.42,.031):QRectF(.28,.87,.44,.06)),state_.empty()?tr("等待平台遥测"):tr("保留最后一帧遥测"),board.height()*.020,QColor("#e5b065"));
}
void PlatformInstruments::mousePressEvent(QMouseEvent* e) {
    if(e->button()!=Qt::LeftButton||domain_!=PlatformDomain::Air)return QWidget::mousePressEvent(e);
    const auto at=normalized(e->position());const bool stick=profile_.stick.contains(at),throttle=profile_.throttle.contains(at);
    if(stick||throttle){if(!enabled_){if(statusChanged)statusChanged(tr("请先接管飞机并选择手动操纵"));return;}if(interactionStarted)interactionStarted();drag_=stick?"stick":"throttle";press_=at;setFocus(Qt::MouseFocusReason);grabMouse();dragTo(at);return;}
    for(const auto& h:profile_.hotspots)if(h.rect.contains(at)){if(h.action=="mfd_page")pages_[h.target]=h.value;else setPanelLighting(lighting_<.99?1.:.45);update();if(statusChanged)statusChanged(h.label+tr("（本地显示）"));return;}
    if(statusChanged)statusChanged(tr("该机械面板热区尚未接入系统模型；不发送虚构开关状态"));
}
void PlatformInstruments::dragTo(const QPointF& at) {
    if(!enabled_||drag_.isEmpty())return;
    if(drag_=="stick"){aileron_=std::clamp((at.x()-press_.x())/(profile_.stick.width()*.36),-1.,1.)*profile_.maximumAxisDemand;elevator_=std::clamp((at.y()-press_.y())/(profile_.stick.height()*.36),-1.,1.)*profile_.maximumAxisDemand;}
    else throttle_=std::clamp((profile_.throttle.bottom()-at.y())/profile_.throttle.height(),0.,1.);
    if(demandChanged)demandChanged(throttle_,elevator_,aileron_,rudder_);update();
}
void PlatformInstruments::mouseMoveEvent(QMouseEvent* e) {
    if(domain_!=PlatformDomain::Air)return QWidget::mouseMoveEvent(e);
    const auto at=normalized(e->position());if(interacting()){dragTo(at);return;}
    const bool hot=profile_.stick.contains(at)||profile_.throttle.contains(at);setCursor(hot&&enabled_?Qt::OpenHandCursor:Qt::ArrowCursor);
    if(hot)setToolTip(enabled_?(profile_.stick.contains(at)?tr("拖动输入俯仰和滚转，松手回中"):tr("向前拖动增大油门需求，松手保持")):tr("先在控制栏接管平台"));
    else {for(const auto& h:profile_.hotspots)if(h.rect.contains(at)){setCursor(Qt::PointingHandCursor);setToolTip(h.label+tr("；仅切换显示，不改变飞行状态"));return;}setToolTip(tr("仪表读取平台真值；使用 MFD 下沿实体按键切换页面"));}
}
void PlatformInstruments::cancelInteraction() {
    const bool stick=drag_=="stick";if(drag_.isEmpty())return;drag_.clear();releaseMouse();
    if(stick){elevator_=aileron_=0;if(enabled_&&demandChanged)demandChanged(throttle_,0,0,rudder_);}update();
}
void PlatformInstruments::mouseReleaseEvent(QMouseEvent* e){if(e->button()==Qt::LeftButton&&interacting()){cancelInteraction();return;}QWidget::mouseReleaseEvent(e);}
void PlatformInstruments::focusOutEvent(QFocusEvent* e){cancelInteraction();QWidget::focusOutEvent(e);}
void PlatformInstruments::hideEvent(QHideEvent* e){cancelInteraction();QWidget::hideEvent(e);}
}
