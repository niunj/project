#include "AircraftCockpitPanel.h"
#include "CockpitControlSignals.h"
#include "EarthPanel.h"
#include "FlightPanel.h"
#include "PlatformInstruments.h"
#include "ConsoleSelector.h"
#include "JoystickInput.h"
#include "EquipmentSession.h"
#include "viewport3d/AircraftCockpitFeature.h"
#include "viewport3d/EngagementVisualFeature.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/VsgQtWidget.h"
#include <QSignalBlocker>
#include <QFileDialog>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QShowEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QShortcut>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace vsgraysim::gui::display {
AircraftCockpitPanel::AircraftCockpitPanel(QWidget* parent):QWidget(parent) {
    setObjectName("aircraft_cockpit");setMinimumSize(760,480);setFocusPolicy(Qt::StrongFocus);
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(3);
    auto* bar=new QHBoxLayout;
    auto* title=new QLabel(tr("平台操作台"));profileTitle_=title;title->setTextFormat(Qt::PlainText);title->setStyleSheet("color:#bdc9c3;font-weight:600;padding:8px;");bar->addWidget(title);
    mode_=new QComboBox;mode_->setObjectName("cockpit_view_mode");mode_->addItem(tr("Qt 动态仪表"),"panel");mode_->addItem(tr("三维座舱 / HUD"),"hud");bar->addWidget(mode_);
    keyboard_=new QToolButton;keyboard_->setText(tr("键盘操纵"));keyboard_->setCheckable(true);keyboard_->setObjectName("cockpit_keyboard");keyboard_->setToolTip(tr("W/S 俯仰 · A/D 滚转 · Q/E 偏航 · Page Up/Down 油门 · Esc 释放控制"));keyboard_->setEnabled(false);bar->addWidget(keyboard_);bar->addStretch();layout->addLayout(bar);
    auto* cameraBar=new QWidget(this);auto* cameraButtons=new QHBoxLayout(cameraBar);cameraButtons->setContentsMargins(6,0,6,0);
    camera_=new QComboBox(cameraBar);camera_->setObjectName("cockpit_camera_mode");camera_->addItem(tr("座舱第一人称"),"pilot");camera_->addItem(tr("外部跟随"),"external");camera_->setToolTip(tr("右键拖动转头，左键操作座舱，F1 回正，滚轮调整视场"));cameraButtons->addWidget(camera_);
    auto* reset=new QToolButton;reset->setObjectName("cockpit_reset_view");reset->setText(tr("回正视线 F1"));cameraButtons->addWidget(reset);
    hud_=new QToolButton;hud_->setText("HUD");hud_->setCheckable(true);hud_->setChecked(true);hud_->setObjectName("cockpit_hud");cameraButtons->addWidget(hud_);
    fov_=new QSlider(Qt::Horizontal);fov_->setObjectName("cockpit_fov");fov_->setRange(40,90);fov_->setValue(55);fov_->setFixedWidth(100);fovCaption_=new QLabel(tr("视场"));cameraButtons->addWidget(fovCaption_);cameraButtons->addWidget(fov_);cameraButtons->addStretch();layout->addWidget(cameraBar);
    controls_=new FlightPanel(this,true);controls_->useCockpitToolbar();layout->addWidget(controls_);
    commandStatus_=new QLabel;commandStatus_->setObjectName("cockpit_command_status");commandStatus_->setStyleSheet("padding:3px 10px;color:#c0c6af;");commandStatus_->setTextFormat(Qt::PlainText);layout->addWidget(commandStatus_);
    payloadBar_=new QWidget(this);auto* payloadRow=new QHBoxLayout(payloadBar_);payloadRow->setContentsMargins(6,0,6,0);
    auto* payloadConnect=new QToolButton(payloadBar_);payloadConnect->setText(tr("连接载荷…"));payloadConnect->setObjectName("cockpit_payload_connect");payloadRow->addWidget(payloadConnect);
    payloadSources_=new QComboBox(payloadBar_);payloadSources_->setObjectName("cockpit_payload_source");payloadSources_->setMaximumWidth(240);payloadRow->addWidget(payloadSources_);
    payloadPower_=new QToolButton(payloadBar_);payloadPower_->setObjectName("cockpit_payload_power");payloadPower_->setText(tr("载荷电源"));payloadPower_->setEnabled(false);payloadRow->addWidget(payloadPower_);
    payloadStatus_=new QLabel(tr("连接当前飞机的 EO / IR 装备后显示实测图像"),payloadBar_);payloadStatus_->setObjectName("cockpit_payload_status");payloadStatus_->setTextFormat(Qt::PlainText);payloadStatus_->setWordWrap(true);payloadRow->addWidget(payloadStatus_,1);layout->addWidget(payloadBar_);payloadBar_->hide();
    connect(payloadConnect,&QToolButton::clicked,this,[this]{if(payloadConnectionRequested)payloadConnectionRequested();});
    connect(payloadPower_,&QToolButton::clicked,this,[this]{if(payloadPowerRequested)payloadPowerRequested(payloadSources_->currentData().toUInt());});
    connect(payloadSources_,&QComboBox::currentIndexChanged,this,[this]{instruments_->setPayloadSession(nullptr);payloadPower_->setEnabled(false);payloadStatus_->setText(tr("等待所选载荷的状态与测量"));});
    auto* split=new QSplitter(Qt::Vertical,this);earth_=new EarthPanel(split);earth_->setCockpitMode(true);
    instruments_=new PlatformInstruments(split);split->addWidget(earth_);split->addWidget(instruments_);split->setStretchFactor(0,1);split->setStretchFactor(1,3);split->setSizes({270,600});layout->addWidget(split,1);
    sharedViewStatus_=new QLabel(tr("三维 HUD 已切换到共享地球视口。键盘或手柄输入使用上方接管控制。"));
    sharedViewStatus_->setAlignment(Qt::AlignCenter);sharedViewStatus_->setWordWrap(true);layout->addWidget(sharedViewStatus_,1);
    auto* choose=new QToolButton;choose->setText(tr("选择型号"));choose->setObjectName("choose_console");bar->insertWidget(1,choose);
    auto* skin=new QToolButton;skin->setText(tr("导入机型配置"));bar->insertWidget(2,skin);
    QString error;if(!instruments_->loadProfile("config/cockpits/f16c.json",error))title->setText(tr("机型面板加载失败：%1").arg(error));else{title->setText(instruments_->profile().name);profilePath_="config/cockpits/f16c.json";}
    connect(choose,&QToolButton::clicked,this,[this]{ConsoleSelector selector(this,profilePath_);if(selector.exec()==QDialog::Accepted){QString error;if(!loadCockpitProfile(selector.selectedProfile(),error))status_->setText(error);}});
    connect(skin,&QToolButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,tr("加载机型面板配置"),"config/cockpits",tr("机型面板 (*.json)"));if(path.isEmpty())return;QString error;if(!loadCockpitProfile(path,error))status_->setText(error);});
    instruments_->interactionStarted=[this]{keyboard_->setChecked(false);stopJoystick();};
    instruments_->demandChanged=[this](double throttle,double elevator,double aileron,double rudder){controls_->applyManualInputs(throttle,elevator,aileron,rudder);};
    instruments_->statusChanged=[this](const QString& text){status_->setText(text);};
    status_=new QLabel(tr("等待仿真遥测与地球数据 · 不生成虚构仪表值"));status_->setObjectName("cockpit_status");status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);status_->setStyleSheet("padding:6px;color:#9caaa2;");layout->addWidget(status_);
    joystick_=new JoystickInput(this);joystickDevices_=new QComboBox(controls_);joystickDevices_->setObjectName("joystick_devices");joystickDevices_->setMaximumWidth(170);
    joystickButton_=new QToolButton(controls_);joystickButton_->setObjectName("joystick_control");joystickButton_->setCheckable(true);joystickButton_->setText(tr("连接手柄"));joystickButton_->setEnabled(false);
    auto* refreshDevices=new QToolButton(controls_);refreshDevices->setText(tr("刷新手柄"));auto refresh=[this]{const auto current=joystickDevices_->currentData().toString();QSignalBlocker block(joystickDevices_);joystickDevices_->clear();for(const auto& path:JoystickInput::devices())joystickDevices_->addItem(path,path);if(joystickDevices_->count()==0)joystickDevices_->addItem(tr("未发现手柄"),QString());int index=joystickDevices_->findData(current);if(index>=0)joystickDevices_->setCurrentIndex(index);};refresh();connect(refreshDevices,&QToolButton::clicked,this,refresh);
    if(auto* row=controls_->findChild<QHBoxLayout*>("cockpit_control_row")){row->addWidget(joystickDevices_);row->addWidget(joystickButton_);row->addWidget(refreshDevices);}
    connect(joystickDevices_,&QComboBox::currentIndexChanged,this,[this]{if(joystick_->connected())releaseControl(tr("切换手柄设备"));});
    connect(joystickButton_,&QToolButton::toggled,this,[this](bool on){if(!on){if(joystick_->connected())releaseControl(tr("停止手柄操纵"));return;}if(!controls_->manualControlActive()){stopJoystick();return;}const auto path=joystickDevices_->currentData().toString();if(path.isEmpty()){stopJoystick();status_->setText(tr("未发现可用手柄；可继续使用鼠标或键盘"));return;}keyboard_->setChecked(false);instruments_->cancelInteraction();joystick_->configure(instruments_->profile().joystickMapping);QString error;if(!joystick_->openDevice(path,error)){stopJoystick();status_->setText(error);}else{joystickButton_->setText(tr("手柄操纵中"));status_->setText(tr("已连接 %1；等待配置轴数据，按配置的释放键退出").arg(joystick_->name()));}});
    joystick_->axesChanged=[this](const std::array<double,4>& axes){if(joystickButton_->isChecked()&&controls_->manualControlActive())pendingJoystick_=axes;};
    auto* hardwareTick=new QTimer(this);hardwareTick->setInterval(20);connect(hardwareTick,&QTimer::timeout,this,[this]{if(pendingJoystick_&&joystick_->connected()&&joystickButton_->isChecked()&&controls_->manualControlActive()){const auto axes=*pendingJoystick_;pendingJoystick_.reset();const double gain=instruments_->profile().maximumAxisDemand;controls_->applyManualInputs(axes[0],axes[1]*gain,axes[2]*gain,axes[3]*gain);}});hardwareTick->start();
    joystick_->disconnected=[this](const QString& error){releaseControl(error);status_->setText(error);};joystick_->releasePressed=[this]{releaseControl(tr("手柄释放控制"));};
    connect(mode_,&QComboBox::currentIndexChanged,this,[this]{neutralize();instruments_->cancelInteraction();lookYaw_=lookPitch_=0;viewChanged();});connect(hud_,&QToolButton::toggled,this,[this]{viewChanged();});connect(fov_,&QSlider::valueChanged,this,[this]{viewChanged();});
    connect(camera_,&QComboBox::currentIndexChanged,this,[this]{lookYaw_=lookPitch_=0;viewChanged();});
    connect(reset,&QToolButton::clicked,this,[this]{lookYaw_=lookPitch_=0;viewChanged();});
    cameraBar->hide();connect(mode_,&QComboBox::currentIndexChanged,this,[this,cameraBar]{cameraBar->setVisible(mode_->currentData().toString()=="hud");});
    connect(keyboard_,&QToolButton::toggled,this,[this](bool on){if(on){stopJoystick();if(!controls_->manualControlActive()){keyboard_->setChecked(false);return;}throttle_=controls_->findChild<QDoubleSpinBox*>("flight_throttle")->value()/100.;setFocus();}else neutralize();});
    auto* home=new QShortcut(QKeySequence("F1"),this);home->setContext(Qt::WidgetWithChildrenShortcut);connect(home,&QShortcut::activated,reset,&QToolButton::click);
    inputTimer_=new QTimer(this);inputTimer_->setInterval(100);connect(inputTimer_,&QTimer::timeout,this,[this]{inputTick();});inputTimer_->start();qApp->installEventFilter(this);viewChanged();
}
AircraftCockpitPanel::~AircraftCockpitPanel(){inputTimer_->stop();qApp->removeEventFilter(this);releaseControl(tr("操作台已关闭"));if(sharedScene_){viewSettings_["view_mode"]="overview";viewSettings_["hud"]=false;updateScenePresentation();}}
bool AircraftCockpitPanel::loadCockpitProfile(const QString& path,QString& error){if(!instruments_->loadProfile(path,error))return false;releaseControl(tr("切换机型显控台"));profilePath_=path;profileTitle_->setText(instruments_->profile().name);lookYaw_=lookPitch_=0;{QSignalBlocker block(payloadSources_);payloadSources_->clear();}payloadPower_->setEnabled(false);payloadBar_->setVisible(instruments_->profile().category=="uav");viewChanged();return true;}
void AircraftCockpitPanel::useSharedSceneView(bool on){sharedScene_=on;viewChanged();}
void AircraftCockpitPanel::setSharedSceneTarget(EarthPanel* earth,viewport3d::Viewport3DView* viewport) {
    sharedEarth_=earth;sharedViewport_=viewport;
    if(sharedEarth_)sharedEarth_->enableSharedCockpit();
    useSharedSceneView(earth||viewport);
}
void AircraftCockpitPanel::stopJoystick(){pendingJoystick_.reset();if(joystick_)joystick_->closeDevice();if(joystickButton_){QSignalBlocker block(joystickButton_);joystickButton_->setChecked(false);joystickButton_->setText(tr("连接手柄"));}}
void AircraftCockpitPanel::updateState(const nlohmann::json& earth,const nlohmann::json& platforms,double time,bool online,bool running,const std::string& session,bool available,uint32_t preferred) {
    controls_->updateControlContext(session,available);controls_->updateTelemetry(platforms,online,running,preferred);
    const auto state=controls_->selectedTelemetry();const auto platform=state.value("platform_id",uint32_t(0));if(platform!=payloadPlatform_||session!=payloadSession_||online!=payloadOnline_){instruments_->setPayloadSession(nullptr);QSignalBlocker block(payloadSources_);payloadSources_->clear();payloadPower_->setEnabled(false);payloadStatus_->setText(tr("等待当前平台载荷链路"));}payloadPlatform_=platform;payloadSession_=session;payloadOnline_=online;instruments_->setTelemetry(state,controls_->telemetryFresh());synchronizeInputs();
    const auto domain=platformDomain(state);
    if(!state.empty()&&domain_!=domain) {
        domain_=domain;
        if(domain_!=PlatformDomain::Air)camera_->setCurrentIndex(camera_->findData("external"));
        camera_->setEnabled(domain_==PlatformDomain::Air);
        viewChanged();
    }
    profileTitle_->setText(domain==PlatformDomain::Sea?tr("舰船操作台"):domain==PlatformDomain::Ground?tr("地面车辆操作台"):instruments_->profile().name);
    payloadBar_->setVisible(domain==PlatformDomain::Air&&instruments_->profile().category=="uav"&&mode_->currentData().toString()=="panel");
    if(!sharedScene_){earth_->updateContext(earth,time,online,running);earth_->updateTelemetry(platforms,state.value("platform_id",preferred));}
    updateScenePresentation();
    keyboard_->setEnabled(controls_->manualControlActive());if(!controls_->manualControlActive()&&keyboard_->isChecked())keyboard_->setChecked(false);
    status_->setText(state.empty()?tr("等待平台姿态、速度与位置遥测"):
        tr("%1 · %2 · t=%3 s  |  %4")
        .arg(QString::fromStdString(state.value("name",std::string("Platform"))),controls_->telemetryFresh()?(running?tr("实时遥测"):tr("仿真暂停")):tr("保留最后一帧遥测")).arg(time,0,'f',2)
        .arg(domain==PlatformDomain::Air?tr("网格航向 / 局部高度 · Esc 释放控制"):tr("姿态和对地速度 · 当前为遥测观察")));
}
void AircraftCockpitPanel::updatePayloads(const std::vector<std::pair<EquipmentSession*,uint32_t>>& sources){
    if(instruments_->profile().category!="uav")return;
    std::vector<EquipmentSession*> eligible;
    for(const auto& [source,platform]:sources)if(source&&source->bound()&&payloadPlatform_&&platform==payloadPlatform_&&(source->sensorType()==SensorType::VIS||source->sensorType()==SensorType::IR))eligible.push_back(source);
    bool same=payloadSources_->count()==int(eligible.size());for(int n=0;same&&n<int(eligible.size());++n)same=payloadSources_->itemData(n).toUInt()==eligible[n]->equipmentId();
    if(!same){const auto previous=payloadSources_->currentData().toUInt();QSignalBlocker block(payloadSources_);payloadSources_->clear();for(auto* source:eligible)payloadSources_->addItem(QString("%1 #%2 · %3").arg(source->sensorType()==SensorType::IR?"IR":"EO").arg(source->equipmentId()).arg(QString::fromStdString(source->equipmentName())),source->equipmentId());const auto index=payloadSources_->findData(previous);if(index>=0)payloadSources_->setCurrentIndex(index);}
    EquipmentSession* selected=nullptr;for(auto* source:eligible)if(source->equipmentId()==payloadSources_->currentData().toUInt())selected=source;
    instruments_->setPayloadSession(payloadOnline_&&selected&&selected->connected()?selected:nullptr);
    payloadPower_->setEnabled(payloadOnline_&&selected&&selected->connected()&&!selected->powerRequestPending());
    payloadPower_->setText(selected?(selected->powered()?tr("关闭载荷"):tr("开启载荷")):tr("载荷电源"));
    payloadStatus_->setText(!payloadOnline_?tr("飞行链路离线 · 图像已清除"):selected?QString("#%1 · %2 · %3").arg(selected->equipmentId()).arg(selected->statusText(),selected->lastControlStatus()):tr("当前飞机没有已连接的 EO / IR 载荷；请点击连接载荷"));
}
QString AircraftCockpitPanel::cameraMode()const{return mode_->currentData().toString()=="hud"?camera_->currentData().toString():QString("panel");}
void AircraftCockpitPanel::setCabinCursor(QWidget* widget,bool active) {
    if(cabinCursor_&&(!active||cabinCursor_!=widget)){cabinCursor_->unsetCursor();cabinCursor_.clear();}
    if(active&&widget&&!cabinCursor_){widget->setCursor(Qt::PointingHandCursor);cabinCursor_=widget;}
}
void AircraftCockpitPanel::viewChanged(){
    setCabinCursor(nullptr,false);
    const auto mode=cameraMode();
    const bool threeD=mode!="panel";
    earth_->setVisible(!sharedScene_&&threeD);instruments_->setVisible(!threeD);
    sharedViewStatus_->setVisible(sharedScene_&&threeD);
    const auto& profile=instruments_->profile();
    const bool follow=mode=="pilot"||mode=="external";
    camera_->setItemText(camera_->findData("pilot"),profile.category=="uav"?tr("机载前视"):tr("座舱第一人称"));
    const bool hasHud=profile.hudAvailable&&(mode=="pilot"||(mode=="external"&&domain_!=PlatformDomain::Air));
    hud_->setVisible(hasHud);fov_->setVisible(follow);fovCaption_->setVisible(follow);
    payloadBar_->setVisible(!threeD&&profile.category=="uav"&&platformDomain(controls_->selectedTelemetry())==PlatformDomain::Air);
    viewSettings_={{"view_mode",follow?mode.toStdString():"overview"},{"look_yaw_deg",mode=="external"?std::remainder(lookYaw_+profile.chaseAzimuth,360.):lookYaw_},{"look_pitch_deg",lookPitch_},{"fov_deg",fov_->value()},{"hud",hasHud&&hud_->isChecked()},{"cockpit_category",profile.category.toStdString()},{"photo_panel",profile.photoPresentation()},
        {"model_id",profile.id.toStdString()},{"model_uri",profile.modelPath.toStdString()},{"model_up_axis",profile.modelUpAxis.toStdString()},{"model_kind",profile.modelKind.toStdString()},{"model_heading_deg",profile.modelHeading},{"model_max_dimension",profile.modelMaximumDimension},{"chase_distance",profile.chaseDistance}};
    updateScenePresentation();
}
void AircraftCockpitPanel::updateScenePresentation(){
    if(viewSettings_.empty())return;
    auto settings=viewSettings_;
    const auto state=controls_->selectedTelemetry();
    const auto domain=platformDomain(state);
    if(domain!=PlatformDomain::Air) {
        if(settings["view_mode"]=="pilot")settings["view_mode"]="external";
        settings["chase_distance"]=domain==PlatformDomain::Sea?165:25;
    }
    if(sharedScene_&&!isVisible()){settings["view_mode"]="overview";settings["hud"]=false;}
    if(auto* target=sharedScene_?sharedEarth_.data():earth_) {
        target->setCockpitTelemetry(state,controls_->telemetryFresh());
        target->setCockpitView(settings);
    } else if(sharedViewport_&&sharedViewport_->isReady()) {
        constexpr auto id=viewport3d::AircraftCockpitFeature::kId;
        settings["scene_exterior"]=true;
        if(!sharedViewport_->feature(id)&&settings["view_mode"]!="overview") {
            sharedViewport_->addFeature(id,settings);
            if(!sharedViewport_->feature(viewport3d::EngagementVisualFeature::kId))
                sharedViewport_->addFeature(viewport3d::EngagementVisualFeature::kId);
        }
        if(auto* feature=sharedViewport_->featureAs<viewport3d::AircraftCockpitFeature>(id)){
            feature->configure(settings);feature->setAircraft(state,controls_->telemetryFresh());feature->synchronizeCamera(sharedViewport_->context());
        }
    }
}
void AircraftCockpitPanel::synchronizeInputs(){
    auto value=[this](const char* name){return controls_->findChild<QDoubleSpinBox*>(name)->value()/100.;};
    instruments_->setControlState(controls_->manualControlActive(),value("flight_throttle"),value("flight_elevator"),value("flight_aileron"),value("flight_rudder"));
}
void AircraftCockpitPanel::neutralize(){const bool held=!keys_.empty();keys_.clear();looking_=false;if(held&&controls_->manualControlActive())controls_->applyManualInputs(throttle_,0,0,0);}
void AircraftCockpitPanel::releaseControl(const QString& reason){keys_.clear();looking_=false;stopJoystick();instruments_->cancelInteraction();controls_->releaseControl(reason);synchronizeInputs();keyboard_->setChecked(false);}
void AircraftCockpitPanel::hideEvent(QHideEvent* e){releaseControl(tr("操作台已隐藏"));QWidget::hideEvent(e);updateScenePresentation();}
void AircraftCockpitPanel::showEvent(QShowEvent* e){QWidget::showEvent(e);updateScenePresentation();}
void AircraftCockpitPanel::inputTick(){synchronizeInputs();commandStatus_->setText(controls_->commandStatusText());const bool manual=controls_->manualControlActive();joystickButton_->setEnabled(manual);if(!manual&&joystick_->connected())stopJoystick();keyboard_->setEnabled(manual);if(!keyboard_->isChecked())return;if(!manual){keyboard_->setChecked(false);return;}
    const auto axis=[&](int positive,int negative){return (keys_.contains(positive)?1.:0.)-(keys_.contains(negative)?1.:0.);};
    throttle_=std::clamp(throttle_+axis(Qt::Key_PageUp,Qt::Key_PageDown)*.025,0.,1.);controls_->applyManualInputs(throttle_,axis(Qt::Key_W,Qt::Key_S)*.6,axis(Qt::Key_D,Qt::Key_A)*.6,axis(Qt::Key_E,Qt::Key_Q)*.45);
}
bool AircraftCockpitPanel::eventFilter(QObject* object,QEvent* event) {
    auto* widget=qobject_cast<QWidget*>(object);
    if(isVisible()&&widget==window()&&event->type()==QEvent::WindowDeactivate){releaseControl(tr("座舱窗口失去焦点"));return false;}
    const bool sharedWorld=widget&&mode_->currentData().toString()!="panel"&&
        ((sharedEarth_&&(widget==sharedEarth_||sharedEarth_->isAncestorOf(widget)))||
         (sharedViewport_&&(widget==sharedViewport_||sharedViewport_->isAncestorOf(widget))));
    const bool inside=widget&&(widget==this||isAncestorOf(widget)||sharedWorld);if(!inside||!isVisible())return QWidget::eventFilter(object,event);
    if(event->type()==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){releaseControl(tr("操作员释放座舱控制"));return true;}
    if(keyboard_->isChecked()&&(widget==controls_||controls_->isAncestorOf(widget))&&(event->type()==QEvent::FocusIn||event->type()==QEvent::MouseButtonPress))keyboard_->setChecked(false);
    if(event->type()==QEvent::WindowDeactivate){releaseControl(tr("座舱窗口失去焦点"));return false;}
    const bool world=sharedWorld||widget==this||widget==earth_||earth_->isAncestorOf(widget)||widget==instruments_;
    if(world&&event->type()==QEvent::FocusOut){setCabinCursor(nullptr,false);neutralize();}
    if(world&&widget!=instruments_&&cameraMode()!="panel") {
        auto* view=sharedScene_?(sharedEarth_?sharedEarth_->viewport():sharedViewport_.data()):earth_->viewport();
        auto* feature=view?view->featureAs<viewport3d::AircraftCockpitFeature>(viewport3d::AircraftCockpitFeature::kId):nullptr;
        using Action=viewport3d::CabinAction;
        const bool cabinInput=cameraMode()=="pilot"&&feature&&view->widget()==widget;
        if(cabinInput&&event->type()==QEvent::Enter)widget->setMouseTracking(true);
        if(cabinInput&&(event->type()==QEvent::Leave||event->type()==QEvent::FocusOut)) {
            feature->setCabinHover(Action::None);setCabinCursor(widget,false);
        }
        if(cabinInput&&!looking_&&(event->type()==QEvent::MouseMove||event->type()==QEvent::MouseButtonPress)) {
            auto* mouse=static_cast<QMouseEvent*>(event);
            if(event->type()==QEvent::MouseMove||mouse->button()==Qt::LeftButton) {
                const auto p=mouse->position();
                const auto action=feature->cabinPick(p.x(),p.y(),widget->width(),widget->height());
                feature->setCabinHover(action);
                setCabinCursor(widget,action!=Action::None);
                if(event->type()==QEvent::MouseButtonPress&&action!=Action::None) {
                    using platform_animation::CabinSignal;
                    const auto* model=platform_animation::findCockpitModel(instruments_->profile().id.toStdString());
                    const auto command=model?model->command(action):platform_animation::CabinCommand{};
                    switch(command.signal) {
                    case CabinSignal::PanelLight:feature->cycleCabinLight();break;
                    case CabinSignal::Hud:hud_->toggle();break;
                    case CabinSignal::SelectPage:feature->selectCabinPage(action);break;
                    case CabinSignal::None:break;
                    default:
                        switch(dispatchCabinCommand(command,*controls_)) {
                        case CabinCommandResult::HoldSubmitted:keyboard_->setChecked(false);stopJoystick();break;
                        case CabinCommandResult::SystemRejected:
                            status_->setText(tr("开关未提交：请接管当前平台并等待上一条状态回报；平台须支持该开关。"));break;
                        case CabinCommandResult::HoldRejected:
                            status_->setText(tr("高度／航向保持：请接管当前平台，等待仿真确认；当前姿态和速度须在保持模式范围内。"));break;
                        default:break;
                        }
                        break;
                    }
                    view->markActivity();
                    return true;
                }
                if(action!=Action::None)view->markActivity();
            }
        }
        if(event->type()==QEvent::MouseButtonPress){auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()==Qt::RightButton){if(feature)feature->setCabinHover(Action::None);setCabinCursor(widget,false);looking_=true;lastMouse_=mouse->globalPosition().toPoint();return true;}}
        if(event->type()==QEvent::MouseMove&&looking_){auto* mouse=static_cast<QMouseEvent*>(event);const auto point=mouse->globalPosition().toPoint(),delta=point-lastMouse_;lastMouse_=point;lookYaw_=std::clamp(lookYaw_+delta.x()*.16,-100.,100.);lookPitch_=std::clamp(lookPitch_-delta.y()*.16,-55.,55.);viewChanged();return true;}
        if(event->type()==QEvent::MouseButtonRelease&&static_cast<QMouseEvent*>(event)->button()==Qt::RightButton){looking_=false;return true;}
        if(event->type()==QEvent::Wheel){fov_->setValue(fov_->value()-static_cast<QWheelEvent*>(event)->angleDelta().y()/40);return true;}
    }
    if(world&&keyboard_->isChecked()&&(event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease)) {
        auto* key=static_cast<QKeyEvent*>(event);const int k=key->key();if(key->isAutoRepeat())return true;
        if(k==Qt::Key_Escape){releaseControl(tr("操作员释放座舱控制"));return true;}
        if(k==Qt::Key_Space){neutralize();return true;}
        if(k==Qt::Key_W||k==Qt::Key_S||k==Qt::Key_A||k==Qt::Key_D||k==Qt::Key_Q||k==Qt::Key_E||k==Qt::Key_PageUp||k==Qt::Key_PageDown) {
            if(event->type()==QEvent::KeyPress)keys_.insert(k);else keys_.remove(k);inputTick();return true;
        }
    }
    return QWidget::eventFilter(object,event);
}
}
