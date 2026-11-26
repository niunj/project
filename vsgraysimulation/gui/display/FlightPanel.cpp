#include "FlightPanel.h"
#include "base/WidgetFactory.h"
#include "base/PlatformPresentation.h"
#include "vsgraysim/broker/BrokerNet.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <map>

namespace vsgraysim::gui::display {
namespace {
std::map<std::string,QPointer<FlightPanel>> localOwners;
std::string ownerKey(const std::string& session,uint32_t id){return session+":"+std::to_string(id);}
double finiteNumber(const nlohmann::json& j,const char* key,double fallback=0) {
    const auto it=j.find(key);return it!=j.end()&&it->is_number()&&std::isfinite(it->get<double>())?it->get<double>():fallback;
}
std::string text(const nlohmann::json& j,const char* key,const std::string& fallback={}) {
    const auto it=j.find(key);return it!=j.end()&&it->is_string()?it->get<std::string>():fallback;
}
bool flag(const nlohmann::json& j,const char* key,bool fallback=false) {
    const auto it=j.find(key);return it!=j.end()&&it->is_boolean()?it->get<bool>():fallback;
}
}
class AttitudeView : public QWidget {
public:
    explicit AttitudeView(QWidget* parent):QWidget(parent){setMinimumHeight(240);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);}
    nlohmann::json state;
    bool online=false,running=false,valid=false;
protected:
    void paintEvent(QPaintEvent*)override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#101923"));
        if(!valid) {
            p.setPen(QColor("#9caec1"));p.drawText(rect().adjusted(12,12,-12,-12),Qt::AlignCenter|Qt::TextWordWrap,
                online?tr("等待新鲜平台遥测\n在仿真主控中加载并初始化场景"):tr("平台数据链未连接\n连接仿真服务后显示真实姿态与速度"));return;
        }
        const QRectF horizon(10,8,width()-20,std::max(100,height()-126));
        p.save();p.setClipRect(horizon);p.translate(horizon.center());p.rotate(-finiteNumber(state,"roll"));
        p.translate(0,std::clamp(finiteNumber(state,"pitch"),-80.,80.)*horizon.height()/70.);
        const double extent=2*std::max(width(),height());
        p.fillRect(QRectF(-extent,-extent,2*extent,extent),QColor("#214d73"));
        p.fillRect(QRectF(-extent,0,2*extent,extent),QColor("#67503b"));
        p.setPen(QPen(QColor("#c9d9e9"),2));p.drawLine(QPointF(-extent,0),QPointF(extent,0));
        for(int angle=-60;angle<=60;angle+=10)if(angle){const double y=-angle*horizon.height()/70.;p.drawLine(QPointF(-35,y),QPointF(35,y));p.drawText(QPointF(43,y+4),QString::number(angle));}
        p.restore();p.setPen(QPen(QColor("#efcb75"),3));const auto c=horizon.center();
        p.drawLine(c+QPointF(-55,0),c+QPointF(-12,0));p.drawLine(c+QPointF(12,0),c+QPointF(55,0));p.drawEllipse(c,4,4);
        p.setPen(QColor("#dfe9f2"));
        const QString data=tr("航向 %1°   速度 %2 m/s\n局部高度 %3 m   升降 %4 m/s\n俯仰 %5°   滚转 %6°")
            .arg(finiteNumber(state,"yaw"),0,'f',1).arg(finiteNumber(state,"speed"),0,'f',1)
            .arg(state["position"][1].get<double>(),0,'f',1).arg(state["velocity"][1].get<double>(),0,'f',1)
            .arg(finiteNumber(state,"pitch"),0,'f',1).arg(finiteNumber(state,"roll"),0,'f',1);
        p.drawText(QRectF(14,horizon.bottom()+8,width()-28,78),Qt::AlignLeft|Qt::AlignVCenter,data);
        p.setPen(QColor("#8ba2b9"));p.drawText(QRectF(14,height()-28,width()-28,24),
            tr("%1 · Y-up · t=%2 s").arg(running?tr("实时遥测"):tr("已暂停 / 最后状态")).arg(finiteNumber(state,"sim_time"),0,'f',3));
    }
};

FlightPanel::FlightPanel(QWidget* parent,bool compact):QWidget(parent) {
    setMinimumSize(300,330);
    client_id_=BrokerNet::instance().flightClientId()+"_"+QUuid::createUuid().toString(QUuid::Id128).left(12).toStdString();
    auto* root=new QVBoxLayout(this);root->setContentsMargins(8,6,8,6);root->setSpacing(6);
    platforms_=ui::WidgetFactory::createComboBox(this,{});platforms_->setObjectName("flight_platform");platforms_->setAccessibleName(tr("遥测平台"));root->addWidget(platforms_);
    auto* scroll=new QScrollArea(this);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);root->addWidget(scroll,1);
    auto* body=new QWidget(scroll);auto* bodyLayout=new QVBoxLayout(body);bodyLayout->setContentsMargins(0,0,0,0);bodyLayout->setSpacing(6);scroll->setWidget(body);
    attitude_=new AttitudeView(body);bodyLayout->addWidget(attitude_,1);attitude_->setVisible(!compact);
    controls_=new QGroupBox(tr("飞行控制 · 设定值"),body);controls_->setObjectName("flight_controls");auto* grid=new QGridLayout(controls_);bodyLayout->addWidget(controls_);
    mode_=ui::WidgetFactory::createComboBox(controls_,{});mode_->setObjectName("flight_mode");mode_->addItem(tr("手动操纵"),"manual");mode_->addItem(tr("高度 / 航向保持"),"altitude_hold");mode_->addItem(tr("预设航迹"),"route");grid->addWidget(mode_,0,0,1,2);
    take_=ui::WidgetFactory::createButton(controls_,tr("接管控制"),true);take_->setObjectName("flight_take_control");grid->addWidget(take_,1,0);
    release_=ui::WidgetFactory::createButton(controls_,tr("释放控制"));release_->setObjectName("flight_release_control");grid->addWidget(release_,1,1);
    manual_=new QGroupBox(tr("归一化操纵量"),controls_);auto* manualGrid=new QGridLayout(manual_);grid->addWidget(manual_,2,0,1,2);
    auto spin=[&](const char* name,double low,double high,double value,const QString& label,int row,int col){
        auto* field=ui::WidgetFactory::createDoubleSpinBox(manual_,low,high,value,1,1,tr(" %"));field->setObjectName(name);field->setAccessibleName(label);
        manualGrid->addWidget(new QLabel(label,manual_),row,col);manualGrid->addWidget(field,row,col+1);return field;
    };
    throttle_=spin("flight_throttle",0,100,50,tr("油门"),0,0);elevator_=spin("flight_elevator",-100,100,0,tr("升降舵"),compact?1:0,compact?0:2);
    aileron_=spin("flight_aileron",-100,100,0,tr("副翼"),compact?2:1,0);rudder_=spin("flight_rudder",-100,100,0,tr("方向舵"),compact?3:1,compact?0:2);
    elevator_->setToolTip(tr("正值抬头；数值为归一化需求，不是舵偏角。"));aileron_->setToolTip(tr("正值向右滚转。"));rudder_->setToolTip(tr("正值向右偏航。"));
    hold_=new QGroupBox(tr("保持目标 · Y-up 局部坐标"),controls_);auto* form=new QFormLayout(hold_);grid->addWidget(hold_,3,0,1,2);
    speed_=ui::WidgetFactory::createDoubleSpinBox(hold_,0,2000,100,1,1,tr(" m/s"));speed_->setObjectName("flight_target_speed");form->addRow(tr("速度"),speed_);
    altitude_=ui::WidgetFactory::createDoubleSpinBox(hold_,-1000,100000,1000,1,10,tr(" m"));altitude_->setObjectName("flight_target_altitude");form->addRow(tr("局部高度"),altitude_);
    heading_=ui::WidgetFactory::createDoubleSpinBox(hold_,0,359.9,0,1,1,tr(" °"));heading_->setWrapping(true);heading_->setObjectName("flight_target_heading");form->addRow(tr("航向"),heading_);
    apply_=ui::WidgetFactory::createButton(controls_,compact?tr("发送设定"):tr("发送设定 (Ctrl+Enter)"));apply_->setObjectName("flight_apply");grid->addWidget(apply_,4,0);
    apply_->setToolTip(tr("编辑值和模式不会随续租自动执行；按此按钮提交整组设定。"));
    neutral_=ui::WidgetFactory::createButton(controls_,tr("舵面归中 (Ctrl+0)"));neutral_->setObjectName("flight_neutral");grid->addWidget(neutral_,4,1);
    actual_=new QLabel(controls_);actual_->setObjectName("flight_actual");actual_->setWordWrap(true);actual_->setTextInteractionFlags(Qt::TextSelectableByMouse);grid->addWidget(actual_,5,0,1,2);
    ack_=new QLabel(tr("只读遥测 · 接管后才会上送操纵量"),controls_);ack_->setObjectName("flight_command_status");ack_->setWordWrap(true);ack_->setTextInteractionFlags(Qt::TextSelectableByMouse);grid->addWidget(ack_,6,0,1,2);
    model_=new QLabel(controls_);model_->setWordWrap(true);grid->addWidget(model_,7,0,1,2);
    connect(platforms_,&QComboBox::currentIndexChanged,this,[this]{releaseControl(tr("已切换平台"));last_payload_.clear();sample_age_.invalidate();updateControlUi();});
    connect(take_,&QPushButton::clicked,this,&FlightPanel::takeControl);
    connect(release_,&QPushButton::clicked,this,[this]{releaseControl(tr("操作员释放"));});
    connect(apply_,&QPushButton::clicked,this,[this]{if(active_)sendInputs();});
    connect(neutral_,&QPushButton::clicked,this,[this]{if(!active_)return;elevator_->setValue(0);aileron_->setValue(0);rudder_->setValue(0);sendInputs();});
    connect(mode_,&QComboBox::currentIndexChanged,this,[this]{updateControlUi();});
    for(const auto& pair:{std::pair{QString("Ctrl+Return"),apply_},std::pair{QString("Ctrl+0"),neutral_},std::pair{QString("Escape"),release_}}){auto* shortcut=new QShortcut(QKeySequence(pair.first),this);shortcut->setContext(Qt::WidgetWithChildrenShortcut);connect(shortcut,&QShortcut::activated,pair.second,&QPushButton::click);}
    control_timer_=new QTimer(this);control_timer_->setInterval(100);connect(control_timer_,&QTimer::timeout,this,&FlightPanel::controlTick);control_timer_->start();
    updateControlUi();
}
FlightPanel::~FlightPanel(){control_timer_->stop();releaseControl(tr("操作面板已关闭"));}
QString FlightPanel::commandStatusText()const {
    return system_status_.isEmpty()?ack_->text():system_status_+tr("  |  ")+ack_->text();
}
void FlightPanel::useCockpitToolbar() {
    auto* root=qobject_cast<QVBoxLayout*>(layout());
    // Retain one controller/lease. Reparent its live controls into a toolbar;
    // hidden numeric editors still supply the exact same command snapshots.
    root->removeWidget(platforms_);
    if(auto* scroll=findChild<QScrollArea*>())scroll->hide();
    auto* row=new QHBoxLayout;row->setObjectName("cockpit_control_row");row->setSpacing(8);row->addWidget(platforms_,1);
    row->addWidget(mode_);row->addWidget(take_);row->addWidget(release_);row->addWidget(apply_);
    apply_->setText(tr("应用模式"));mode_->setMaximumWidth(190);platforms_->setMinimumWidth(160);
    root->insertLayout(0,row);setMinimumSize(300,0);setMaximumHeight(58);
}
bool FlightPanel::manualControlActive()const{return active_&&controllable()&&mode_->currentData().toString()=="manual"&&isVisible();}
bool FlightPanel::toggleAircraftSystem(const std::string& channel) {
    if(!active_||!isVisible()||!controllable()||!submitted_||system_request_)return false;
    const auto state=selectedState();
    const auto& flight=state.at("flight_control");
    if(text(flight,"owner",text(flight,"owner_client_id"))!=client_id_||text(flight,"mode")!=submitted_->mode)return false;
    const auto system=flight.find("systems");
    if(system==flight.end()||!system->is_object()||!flag(*system,"available")||!system->contains(channel)||!system->at(channel).is_boolean())
        return false;
    const bool enabled=!system->at(channel).get<bool>();
    auto command=*submitted_; // Preserve flight inputs. The backend handles the system separately.
    command.aircraft_system=AircraftSystemDemand{channel,enabled};
    system_request_=SystemRequest{BrokerNet::instance().submitFlightControl(command),channel,enabled,controlled_id_};
    system_age_.restart();system_status_=tr("开关命令已提交 · 等待仿真回报");
    return true;
}
bool FlightPanel::toggleCockpitHold() {
    if(!active_||!isVisible()||!controllable())return false;
    const auto state=selectedState();
    const auto& flight=state.at("flight_control");
    // Wait for the server to publish the last mode. Never toggle against a
    // pending command or a lease still owned by another control station.
    if(text(flight,"owner",text(flight,"owner_client_id"))!=client_id_||!submitted_||text(flight,"mode")!=submitted_->mode)
        return false;
    if(submitted_->mode!="manual"&&submitted_->mode!="altitude_hold")return false;
    const bool enable=submitted_->mode=="manual";
    const auto actual=flight.value("actual",nlohmann::json::object());
    // Enter hold from measured state rather than an old draft. Neutralize
    // axes when returning to manual and preserve the reported thrust.
    const double speed=finiteNumber(state,"speed"),altitude=state.at("position").at(1).get<double>();
    if(enable&&(speed<speed_->minimum()||speed>speed_->maximum()||altitude<altitude_->minimum()||altitude>altitude_->maximum()))return false;
    speed_->setValue(speed);altitude_->setValue(altitude);
    heading_->setValue(std::fmod(std::fmod(finiteNumber(state,"yaw"),360.)+360.,360.));
    throttle_->setValue(std::clamp(finiteNumber(actual,"throttle",submitted_->throttle),0.,1.)*100.);
    elevator_->setValue(0);aileron_->setValue(0);rudder_->setValue(0);
    mode_->setCurrentIndex(mode_->findData(enable?"altitude_hold":"manual"));
    sendInputs();updateControlUi();
    return true;
}
bool FlightPanel::applyManualInputs(double throttle,double elevator,double aileron,double rudder) {
    if(!manualControlActive()||!std::isfinite(throttle)||!std::isfinite(elevator)||!std::isfinite(aileron)||!std::isfinite(rudder)||throttle<0||throttle>1||std::abs(elevator)>1||std::abs(aileron)>1||std::abs(rudder)>1)return false;
    throttle_->setValue(throttle*100);elevator_->setValue(elevator*100);aileron_->setValue(aileron*100);rudder_->setValue(rudder*100);sendInputs();return true;
}
void FlightPanel::hideEvent(QHideEvent* event){releaseControl(tr("操作面板已隐藏"));QWidget::hideEvent(event);}
nlohmann::json FlightPanel::selectedState()const {
    const auto id=platforms_->currentData().toUInt();for(const auto& sample:samples_)if(sample["platform_id"].get<uint32_t>()==id)return sample;return nlohmann::json::object();
}
bool FlightPanel::fresh()const{return online_&&!selectedState().empty()&&(!running_||(sample_age_.isValid()&&sample_age_.elapsed()<3000));}
bool FlightPanel::controllable()const {
    const auto state=selectedState();const auto it=state.find("flight_control");
    return available_&&!session_id_.empty()&&platforms_->currentData().toUInt()!=0&&fresh()&&
        platformDomain(state)==PlatformDomain::Air&&it!=state.end()&&it->is_object()&&flag(*it,"available");
}
void FlightPanel::updateControlContext(const std::string& session,bool available) {
    if(session!=session_id_){system_request_.reset();system_status_.clear();releaseControl(tr("仿真会话已变更"));request_id_.clear();result_key_.clear();last_payload_.clear();sample_age_.invalidate();session_id_=session;ack_->setText(tr("仿真会话已变更 · 请重新接管控制"));}
    available_=available;if(!available_&&active_)releaseControl(tr("仿真控制服务不可用"));updateControlUi();
}
void FlightPanel::updateTelemetry(const nlohmann::json& samples,bool online,bool running,uint32_t preferred) {
    online_=online;running_=running;samples_=nlohmann::json::array();
    if(samples.is_array())for(const auto& item:samples){
        if(!item.is_object()||!item.contains("platform_id")||!item["platform_id"].is_number_integer()||item["platform_id"].get<double>()<0||item["platform_id"].get<double>()>UINT32_MAX||!item.contains("name")||!item["name"].is_string())continue;
        bool valid=true;for(const auto* key:{"roll","pitch","yaw","speed","sim_time"})valid=valid&&item.contains(key)&&item[key].is_number()&&std::isfinite(item[key].get<double>());
        for(const auto* key:{"position","velocity"}){if(!item.contains(key)||!item[key].is_array()||item[key].size()!=3){valid=false;continue;}for(const auto& value:item[key])valid=valid&&value.is_number()&&std::isfinite(value.get<double>());}
        if(valid)samples_.push_back(item);
    }
    const auto current=platforms_->currentData().toUInt();bool listChanged=platforms_->count()!=int(samples_.size());
    for(int i=0;!listChanged&&i<platforms_->count();++i)
        listChanged=platforms_->itemData(i).toUInt()!=samples_[i]["platform_id"].get<uint32_t>()||platforms_->itemText(i)!=QString::fromStdString(samples_[i]["name"].get<std::string>());
    if(listChanged){QSignalBlocker block(platforms_);platforms_->clear();
        for(const auto& item:samples_)platforms_->addItem(QString::fromStdString(item["name"].get<std::string>()),item["platform_id"].get<uint32_t>());
        int index=platforms_->findData(current?current:preferred);if(index<0&&preferred)index=platforms_->findData(preferred);if(index>=0)platforms_->setCurrentIndex(index);
    }
    // A changing lease countdown is not a new physical sample. Freshness must
    // follow the authoritative sample clock, including while control renews.
    const auto state=selectedState();const auto payload=state.empty()?std::string():nlohmann::json{{"platform_id",state["platform_id"]},{"sim_time",state["sim_time"]}}.dump();if(payload!=last_payload_||!sample_age_.isValid()){last_payload_=payload;sample_age_.restart();}
    if(active_&&controlled_id_!=platforms_->currentData().toUInt())releaseControl(tr("受控平台已离线或切换"));
    if(system_request_&&(!online_||system_request_->platform!=platforms_->currentData().toUInt())) {
        system_request_.reset();system_status_.clear();
    }
    if(active_&&!controllable())releaseControl(tr("遥测过期或控制能力不可用"));
    if(active_) {
        const auto fc=state["flight_control"];const auto owner=text(fc,"owner",text(fc,"owner_client_id"));
        if(owner==client_id_)observed_own_=true;
        const bool routeHandoff=submitted_&&submitted_->mode=="route"&&text(fc,"mode")=="route"&&owner.empty()&&!flag(fc,"failsafe");
        // Initial telemetry may predate the acquisition request. Only treat an
        // empty/expired lease as loss after this panel's ownership was observed.
        if((!owner.empty()&&owner!=client_id_)||(!routeHandoff&&observed_own_&&(owner.empty()||flag(fc,"failsafe")||
           (fc.contains("lease_remaining_ms")&&finiteNumber(fc,"lease_remaining_ms")<=0))))
            releaseControl(tr("仿真端已解除控制权，请重新接管"));
    }
    updateControlUi();
}
void FlightPanel::readSetpoints() {
    const auto state=selectedState();const auto fc=state.value("flight_control",nlohmann::json::object());
    const auto inputs=fc.contains("actual")&&fc["actual"].is_object()?fc["actual"]:fc;
    const auto command=fc.contains("command")&&fc["command"].is_object()?fc["command"]:nlohmann::json::object();
    const auto limits=fc.contains("limits")&&fc["limits"].is_object()?fc["limits"]:nlohmann::json::object();
    const auto range=[&](QDoubleSpinBox* field,const char* lowKey,const char* highKey,double low,double high) {
        const double minimum=std::max(low,finiteNumber(limits,lowKey,low)),maximum=std::min(high,finiteNumber(limits,highKey,high));
        if(minimum<=maximum)field->setRange(minimum,maximum);
        else field->setRange(low,high);
    };
    // Limits and seed values are sampled only during explicit acquisition.
    // Periodic telemetry must not clamp or overwrite an operator's draft.
    range(speed_,"min_speed","max_speed",0,2000);
    range(altitude_,"min_altitude","max_altitude",-1000,100000);
    const QSignalBlocker modeBlock(mode_);const auto mode=text(fc,"mode","manual");const int index=mode_->findData(QString::fromStdString(mode));mode_->setCurrentIndex(index>=0?index:0);
    throttle_->setValue(finiteNumber(inputs,"throttle",finiteNumber(command,"throttle",.5))*100);
    elevator_->setValue(finiteNumber(inputs,"elevator",finiteNumber(command,"elevator"))*100);
    aileron_->setValue(finiteNumber(inputs,"aileron",finiteNumber(command,"aileron"))*100);
    rudder_->setValue(finiteNumber(inputs,"rudder",finiteNumber(command,"rudder"))*100);
    // A newly acquired hold starts at current truth, never a zero/default target
    // left in another controller's old command snapshot.
    speed_->setValue(finiteNumber(state,"speed"));altitude_->setValue(state["position"][1].get<double>());heading_->setValue(finiteNumber(state,"yaw"));
}
void FlightPanel::takeControl() {
    if(active_||!controllable()||!isVisible())return;
    const auto id=platforms_->currentData().toUInt();auto& owner=localOwners[ownerKey(session_id_,id)];
    if(owner&&owner!=this){ack_->setText(tr("同一平台已由本进程另一操作面板接管"));return;}
    readSetpoints();if(mode_->currentData().toString()=="route")mode_->setCurrentIndex(0);
    controlled_id_=id;active_=true;observed_own_=false;owner=this;release_reason_.clear();sendInputs();updateControlUi();
}
void FlightPanel::relinquishLocal(){
    const auto key=ownerKey(session_id_,controlled_id_);auto it=localOwners.find(key);if(it!=localOwners.end()&&it->second==this)localOwners.erase(it);active_=false;observed_own_=false;submitted_.reset();
}
void FlightPanel::sendInputs() {
    if(!active_||!controllable())return;
    for(auto* input:{throttle_,elevator_,aileron_,rudder_,speed_,altitude_,heading_})input->interpretText();
    FlightControlCommand c;c.client_id=client_id_;c.session_id=session_id_;c.platform_id=controlled_id_;c.mode=mode_->currentData().toString().toStdString();
    c.throttle=throttle_->value()/100;c.elevator=elevator_->value()/100;c.aileron=aileron_->value()/100;c.rudder=rudder_->value()/100;c.target_speed=speed_->value();c.target_altitude=altitude_->value();c.target_heading=heading_->value();
    submitted_=c;submitSnapshot(c);
}
void FlightPanel::submitSnapshot(const FlightControlCommand& c) {
    request_id_=BrokerNet::instance().submitFlightControl(c);last_release_=false;last_route_=c.mode=="route";result_key_.clear();request_age_.restart();last_sent_.restart();ack_->setText(tr("命令已提交 · 等待仿真端执行回报"));
}
void FlightPanel::releaseControl(const QString& reason) {
    if(!active_)return;
    FlightControlCommand c;c.client_id=client_id_;c.session_id=session_id_;c.platform_id=controlled_id_;c.release=true;
    request_id_=BrokerNet::instance().submitFlightControl(c);last_release_=true;last_route_=false;release_reason_=reason;result_key_.clear();request_age_.restart();relinquishLocal();
    ack_->setText(reason+tr(" · 等待释放回报；失联后由仿真端租约保护"));updateControlUi();
}
void FlightPanel::controlTick() {
    if(active_&&(!controllable()||!isVisible()))releaseControl(tr("控制已解除：面板不可见或链路过期"));
    if(system_request_) {
        const auto request=*system_request_;FlightControlResult result;
        if(BrokerNet::instance().flightControlResult(request.id,&result)&&result.client_id==client_id_&&
           result.session_id==session_id_&&result.platform_id==request.platform&&result.stage!="queued") {
            if(result.stage!="applied") {
                system_status_=tr("开关未执行：%1").arg(QString::fromStdString(result.code));system_request_.reset();
            } else {
                const auto selected=selectedState();
                const auto fc=selected.find("flight_control");
                const auto systems=fc!=selected.end()&&fc->is_object()?fc->value("systems",nlohmann::json::object()):nlohmann::json::object();
                const auto value=systems.find(request.channel);
                if(systems.value("revision",uint64_t(0))>=result.revision&&value!=systems.end()&&value->is_boolean()&&
                   value->get<bool>()==request.enabled) {
                    system_status_=tr("仿真已确认：%1 %2").arg(request.channel=="gear_down"?tr("起落架手柄"):request.channel=="nav_lights"?tr("航行灯"):tr("着陆灯"),
                        request.channel=="gear_down"?(request.enabled?tr("放下"):tr("收上")):(request.enabled?tr("开"):tr("关")));
                    system_request_.reset();
                }
            }
        }
        if(system_request_&&system_age_.elapsed()>2600) {
            system_status_=tr("未收到开关状态回报 · 请看仿真端仪表");system_request_.reset();
        }
    }
    if(!request_id_.empty()) {
        FlightControlResult result;
        if(BrokerNet::instance().flightControlResult(request_id_,&result)) {
            if(result.client_id!=client_id_||result.session_id!=session_id_||result.platform_id!=controlled_id_){if(active_)releaseControl(tr("命令回报身份不匹配，结果未知"));return;}
            const auto key=result.request_id+result.stage+result.code+std::to_string(result.revision)+(running_?"running":"paused");
            if(key!=result_key_) {
                result_key_=key;const auto detail=QString::fromStdString(result.message.empty()?result.code:result.message);
                if(result.stage=="applied") {
                    if(last_route_&&active_)relinquishLocal();
                    ack_->setText(last_release_?release_reason_+tr(" · 仿真端已释放控制"):
                        (last_route_?tr("已切回预设航迹，自主执行 · 修订 %1 · t=%2 s%3"):tr("已装订 · 修订 %1 · t=%2 s%3"))
                            .arg(result.revision).arg(result.sim_time,0,'f',3).arg(running_?QString():tr(" · 仿真暂停，待推进")));
                }
                else if(result.stage=="queued")ack_->setText(last_release_
                    ? release_reason_+tr(" · 释放请求已接收，等待执行")
                    : tr("命令已接收 · 等待仿真线程执行"));
                else {
                    const auto reason=tr("%1：%2").arg(result.stage=="unknown"?tr("执行结果未知"):result.stage=="expired"?tr("控制租约已过期"):tr("命令未执行"),detail);
                    if(active_)releaseControl(reason);else ack_->setText(release_reason_.isEmpty()?reason:release_reason_+" · "+reason);
                }
            }
            // Renew the last committed snapshot. Draft edits/mode changes only
            // become executable after the operator explicitly presses Apply.
            if(active_&&result.stage=="applied"&&last_sent_.elapsed()>=500&&submitted_)submitSnapshot(*submitted_);
            if(active_&&result.stage=="queued"&&request_age_.elapsed()>1800)releaseControl(tr("执行回报超时，结果未知"));
        } else if(active_&&request_age_.isValid()&&request_age_.elapsed()>1800)releaseControl(tr("未收到执行回报，结果未知"));
    }
    updateControlUi();
}
void FlightPanel::updateControlUi() {
    const auto state=selectedState();const auto fc=state.contains("flight_control")&&state["flight_control"].is_object()?state["flight_control"]:nlohmann::json::object();
    attitude_->state=state;attitude_->online=online_;attitude_->running=running_;attitude_->valid=fresh();attitude_->update();
    const bool capability=flag(fc,"available");controls_->setVisible(!fc.empty());
    const auto owner=text(fc,"owner",text(fc,"owner_client_id"));const auto local=localOwners.find(ownerKey(session_id_,platforms_->currentData().toUInt()));const bool otherLocal=local!=localOwners.end()&&local->second&&local->second!=this;
    take_->setEnabled(controllable()&&!active_&&!otherLocal&&(owner.empty()||owner==client_id_));release_->setEnabled(active_);
    mode_->setEnabled(active_);manual_->setVisible(mode_->currentData().toString()=="manual");hold_->setVisible(mode_->currentData().toString()=="altitude_hold");
    manual_->setEnabled(active_);hold_->setEnabled(active_);apply_->setEnabled(active_);neutral_->setEnabled(active_&&mode_->currentData().toString()=="manual");
    if(auto* items=qobject_cast<QStandardItemModel*>(mode_->model()))items->item(2)->setEnabled(flag(fc,"has_route"));
    const auto actual=fc.contains("actual")&&fc["actual"].is_object()?fc["actual"]:fc;
    const QString ownership=owner.empty()?tr("无操作台持有"):owner==client_id_?tr("本操作台"):tr("其他操作台");
    bool actualValid=true;for(const char* key:{"throttle","elevator","aileron","rudder"})actualValid=actualValid&&actual.contains(key)&&actual[key].is_number()&&std::isfinite(actual[key].get<double>());
    const auto serverMode=text(fc,"mode");
    const QString modeLabel=serverMode=="manual"?tr("手动"):serverMode=="altitude_hold"?tr("高度/航向保持"):serverMode=="route"?tr("航迹"):serverMode=="stabilized"?tr("当前状态保持"):tr("未同步");
    actual_->setText((actualValid?tr("实际：油门 %1% · 升降舵 %2% · 副翼 %3% · 方向舵 %4%")
        .arg(finiteNumber(actual,"throttle")*100,0,'f',1).arg(finiteNumber(actual,"elevator")*100,0,'f',1).arg(finiteNumber(actual,"aileron")*100,0,'f',1).arg(finiteNumber(actual,"rudder")*100,0,'f',1):tr("执行器状态尚未同步"))+
        tr("\n模式 %1 · 控制权 %2%3").arg(modeLabel,ownership,flag(fc,"failsafe")?tr(" · 租约保护生效"):QString()));
    model_->setText(capability?tr("简化飞行控制律 · 舵量为归一化需求，执行器状态以遥测为准"):tr("当前平台不提供飞行控制能力：%1").arg(QString::fromStdString(text(fc,"unavailable_reason",text(fc,"reason","unsupported")))));
    if(!active_&&request_id_.empty()&&!fc.empty())ack_->setText(!controllable()?tr("控制不可用：请检查服务、平台能力与遥测时效"):otherLocal||!owner.empty()?tr("其他操作台持有控制权；释放后可接管"):tr("只读遥测 · 点击接管后开始操纵；隐藏面板会释放控制"));
}
}
