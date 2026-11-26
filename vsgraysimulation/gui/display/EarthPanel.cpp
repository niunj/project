#include "viewport3d/VoxelSceneFeature.h"
#include "viewport3d/PlatformMotionFeature.h"
#include "EarthPanel.h"
#include <vsgraysim/earth/EarthDataService.h>
#include "base/WidgetFactory.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/ViewportConfig.h"
#include "viewport3d/EarthView.h"
#include "viewport3d/EnvFeatures.h"
#include "viewport3d/AircraftCockpitFeature.h"
#include "viewport3d/EngagementVisualFeature.h"
#include "viewport3d/VsgQtWidget.h"
#include <simcore/GeoFrame.h>
#include <simcore/UtcEpoch.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <vsgraysim/broker/BrokerNet.h>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <vsg/utils/Builder.h>
#include <vsg/state/ViewDependentState.h>
#include <vsg/maths/transform.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace vsgraysim::gui::display {
namespace {
using json=nlohmann::json;
bool number(const json& j,const char* key){return j.contains(key)&&j[key].is_number()&&std::isfinite(j[key].get<double>());}
bool validId(const json& j){return j.is_number_integer()&&j.get<double>()>=1&&j.get<double>()<=UINT32_MAX;}
std::string earthKey(const json& j){json out;for(const auto* key:{"geo_anchor","earth","terrain_ref","source_scenario","surface_revision"})if(j.contains(key)&&!(j.contains("voxel_scene")&&std::string(key)=="surface_revision"))out[key]=j[key];return out.dump();}
QString modelStateText(std::string_view state) {
    using Entry=std::pair<std::string_view,const char*>;
    static constexpr Entry labels[]={
        {"ready","待机"},{"idle","怠速"},{"driving","行驶"},{"underway","航行"},{"moored","停泊"},
        {"flying","飞行"},{"hovering","悬停"},{"climbing","爬升"},{"descending","下降"},
        {"landed","已落地"},{"spooling_up","旋翼升速"},{"taxiing","滑行"},{"launching","起飞"},{"staged","待发射"},
        {"attached","组合飞行"},{"stage_separated","级间分离"},{"fairing_separated","整流罩分离"},
        {"coasting","滑行段"},{"stowed","收拢"},{"payload_released","卫星已释放"},
        {"deploying","展开中"},{"deployed","已展开"},{"recorded_flight","飞行回放"},{"replay","位姿回放"},
        {"damaged","受损"},{"breaking_up","结构破碎"},{"destroyed","已毁损"},
        {"working","工作中"},{"standby","待订阅"},{"waiting","等待采样"},{"off","关机"},
        {"unavailable","不可用"},{"preview","仅显示"},
        {"surfaced","水面"},{"diving","下潜"},{"ascending","上浮"},{"submerged_cruise","水下巡航"},
        {"dry_transducer","换能器离水"},{"no_bottom","无海床回波"}
    };
    for(const auto& [key,label]:labels)
        if(key==state)return QCoreApplication::translate("EarthPanel",label);
    return QString::fromUtf8(state.data(),qsizetype(state.size()));
}
}
EarthPanel::EarthPanel(QWidget* parent):QWidget(parent) {
    setObjectName("earth_panel");setMinimumSize(300,260);
    native_rendering_=qApp&&QApplication::platformName()!="offscreen"&&QApplication::platformName()!="minimal";
    layout_=new QVBoxLayout(this);layout_->setContentsMargins(6,6,6,6);layout_->setSpacing(5);
    auto* bar=new QHBoxLayout;
    platforms_=ui::WidgetFactory::createComboBox(this,{});platforms_->setObjectName("earth_platform");platforms_->setAccessibleName(tr("地球态势平台"));bar->addWidget(platforms_,1);
    focus_=ui::WidgetFactory::createButton(this,tr("定位平台"));focus_->setObjectName("earth_focus");bar->addWidget(focus_);
    follow_=ui::WidgetFactory::createCheckBox(this,tr("跟随"));follow_->setObjectName("earth_follow");bar->addWidget(follow_);
    audio_=ui::WidgetFactory::createCheckBox(this,tr("音效"));audio_->setObjectName("earth_audio");bar->addWidget(audio_);layout_->addLayout(bar);
    connect(audio_,&QCheckBox::toggled,this,[this](bool on){
        if(viewport_){viewport_->widget()->setProperty("sceneAudioRequested",on);viewport_->markActivity();}
        updateAcousticMeter();
    });
    acoustic_meter_=new QLabel(this);acoustic_meter_->setWordWrap(true);acoustic_meter_->setTextFormat(Qt::PlainText);
    acoustic_meter_->setObjectName("earth_acoustic_meter");
    acoustic_probe_=ui::WidgetFactory::createComboBox(this,{});acoustic_probe_->setAccessibleName(tr("声学监听点"));
    acoustic_probe_->setObjectName("earth_acoustic_probe");
    acoustic_probe_->addItem(tr("相机听者"),QString());
    auto* acoustics=new QHBoxLayout;acoustics->addWidget(acoustic_probe_);acoustics->addWidget(acoustic_meter_,1);
    layout_->addLayout(acoustics);
    connect(acoustic_probe_,&QComboBox::currentIndexChanged,this,[this]{
        if(viewport_){viewport_->widget()->setProperty("sceneAcousticProbe",acoustic_probe_->currentData());viewport_->markActivity();}
        updateAcousticMeter();
    });
    poseBar_=new QWidget(this);
    auto* poseLayout=new QHBoxLayout(poseBar_);poseLayout->setContentsMargins(0,0,0,0);
    poseYaw_=new QDoubleSpinBox(poseBar_);poseYaw_->setRange(-90.,90.);poseYaw_->setDecimals(1);
    poseYaw_->setSuffix(QString::fromUtf8("°"));poseYaw_->setObjectName("m1a2_display_yaw");
    posePitch_=new QDoubleSpinBox(poseBar_);posePitch_->setRange(-12.,18.);posePitch_->setDecimals(1);
    posePitch_->setSuffix(QString::fromUtf8("°"));posePitch_->setObjectName("m1a2_display_pitch");
    poseApply_=ui::WidgetFactory::createButton(poseBar_,tr("应用显示姿态"));poseApply_->setObjectName("m1a2_display_apply");
    poseStatus_=new QLabel(poseBar_);poseStatus_->setObjectName("m1a2_display_result");poseStatus_->setWordWrap(true);
    poseLayout->addWidget(new QLabel(tr("炮塔转角"),poseBar_));poseLayout->addWidget(poseYaw_);
    poseLayout->addWidget(new QLabel(tr("炮管俯仰"),poseBar_));poseLayout->addWidget(posePitch_);
    poseLayout->addWidget(poseApply_);poseLayout->addWidget(poseStatus_,1);
    layout_->addWidget(poseBar_);poseBar_->hide();
    connect(poseApply_,&QPushButton::clicked,this,&EarthPanel::sendPresentationPose);
    placeholder_=new QLabel(this);placeholder_->setObjectName("earth_unavailable");placeholder_->setAlignment(Qt::AlignCenter);placeholder_->setWordWrap(true);placeholder_->setMinimumHeight(160);layout_->addWidget(placeholder_,1);
    position_=new QLabel(this);position_->setObjectName("earth_platform_position");position_->setTextFormat(Qt::PlainText);position_->setWordWrap(true);position_->setTextInteractionFlags(Qt::TextSelectableByMouse);layout_->addWidget(position_);
    status_=new QLabel(this);status_->setObjectName("earth_context_status");status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);layout_->addWidget(status_);
    connect(focus_,&QPushButton::clicked,this,&EarthPanel::focusSelected);
    connect(platforms_,&QComboBox::currentIndexChanged,this,[this]{updateStatus();if(follow_->isChecked())focusSelected();});
    connect(follow_,&QCheckBox::toggled,this,[this](bool on){if(on)focusSelected();});updateStatus();
}
EarthPanel::~EarthPanel(){if(viewport_)viewport_->setAutoFrame(false);}
void EarthPanel::setCockpitMode(bool on){cockpit_mode_=on;setMinimumHeight(on?200:260);platforms_->setVisible(!on);focus_->setVisible(!on);follow_->setVisible(!on);position_->setVisible(!on);}
void EarthPanel::setCockpitTelemetry(const json& state,bool fresh){if(cockpit_state_==state&&cockpit_fresh_==fresh)return;cockpit_state_=state;cockpit_fresh_=fresh;synchronizeCockpit();updateMarkers();if(viewport_)viewport_->markActivity();}
void EarthPanel::setCockpitView(const json& view){auto next=cockpit_view_;next.update(view);if(next==cockpit_view_)return;const auto previous=cockpit_view_.value("view_mode",std::string("pilot"));cockpit_view_=std::move(next);synchronizeCockpit();
    if(viewport_&&previous!="overview"&&cockpit_view_.value("view_mode",std::string("pilot"))=="overview")viewport_->resetCamera();
    updateMarkers();if(viewport_)viewport_->markActivity();}
void EarthPanel::synchronizeCockpit(){if(!viewport_||!viewport_->isReady()||!(cockpit_mode_||shared_cockpit_))return;
    if(auto* cockpit=viewport_->featureAs<viewport3d::AircraftCockpitFeature>(viewport3d::AircraftCockpitFeature::kId)){cockpit->configure(cockpit_view_);cockpit->setAircraft(cockpit_state_,cockpit_fresh_);cockpit->synchronizeCamera(viewport_->context());}}
bool EarthPanel::updateContext(const json& context,double time,bool online,bool running) {
    try {
        if(!context.is_object()||context.dump().size()>1024*1024||!std::isfinite(time)||time<0)throw std::invalid_argument("invalid Earth context or simulation time");
        for(const auto* key:{"geo_anchor","earth","simulation","atmosphere","ocean"})if(context.contains(key)&&!context[key].is_object())throw std::invalid_argument("invalid Earth configuration object");
        for(const auto* key:{"source_scenario","terrain_ref"})if(context.contains(key)&&!context[key].is_string())throw std::invalid_argument("invalid Earth resource reference");
        if(context.contains("terrain_display_paging")&&!context.at("terrain_display_paging").is_boolean())
            throw std::invalid_argument("invalid terrain display paging setting");
        if(context.contains("geo_anchor")) {
            const auto& a=context["geo_anchor"];simcore::geo::GeoAnchor anchor;
            anchor.lat_deg=a.value("lat",anchor.lat_deg);anchor.lon_deg=a.value("lon",anchor.lon_deg);anchor.alt_m=a.value("alt",0.);anchor.heading_deg=a.value("heading_deg",0.);simcore::geo::makeEnuToWorldMatrix(anchor);
        }
        if(invalid_context_){error_.clear();invalid_context_=false;}
        const auto key=context.dump();const bool changed=key!=context_key_,stateChanged=online_!=online||running_!=running,clockChanged=time!=sim_time_;
        context_=context;context_key_=key;online_=online;running_=running;sim_time_=time;
        if(changed){++context_revision_;error_.clear();}
        if(context_.empty()) {
            samples_=json::array();platforms_->clear();marker_nodes_.clear();if(markers_)markers_->children.clear();
            if(viewport_){viewport_->setAutoFrame(false);viewport_->hide();}applied_context_key_.clear();attempted_context_key_.clear();applied_earth_key_.clear();
        } else {
            ensureViewport();
            if(viewport_) {
                viewport_->widget()->setProperty("sceneAudioPlaying",online&&running);
                if(audio_->isChecked())viewport_->widget()->setProperty("sceneAudioEpoch",
                    QString::fromStdString(BrokerNet::instance().lastSimMeta().scene_session_id));
                applyContext();
                if(applied_context_key_==context_key_) viewport_->context().setSimulationTime(time);
                if(isVisible()&&online_&&error_.isEmpty()) {
                    viewport_->show();if(!viewport_->autoFrame())viewport_->setAutoFrame(true,30);
                    if(stateChanged||changed)viewport_->setIdleSuspend(!running_,1000);
                    if(clockChanged||changed||stateChanged)viewport_->markActivity();
                } else viewport_->setAutoFrame(false);
            }
        }
        updateStatus();return true;
    }catch(const std::exception& exception){invalid_context_=true;error_=tr("地球配置无效：%1").arg(QString::fromUtf8(exception.what()));if(viewport_)viewport_->setAutoFrame(false);updateStatus();return false;}
}
bool EarthPanel::surfaceReady(uint64_t revision) const {
    if(!isVisible() || !native_rendering_) return true;
    if(!error_.isEmpty() || !viewport_ || !viewport_->isReady()) return false;
    if(context_.contains("voxel_scene")){auto* voxel=viewport_->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene");return voxel&&voxel->snapshot()&&voxel->snapshot()->contentRevision==revision;}
    auto* view=viewport_->featureAs<viewport3d::EarthView>(viewport3d::EarthView::kId);
    auto surface=view ? view->surfaceSnapshot() : nullptr;
    return surface && surface->revision==revision;
}
void EarthPanel::ensureViewport() {
    if(viewport_||!native_rendering_||!isVisible()||context_.empty())return;
    viewport_=new viewport3d::Viewport3DView(this);layout_->insertWidget(1,viewport_,1);
    viewport_->widget()->setProperty("sceneAudioRequested",audio_->isChecked());
    viewport_->widget()->setProperty("sceneAcousticProbe",acoustic_probe_->currentData());
    viewport_->widget()->setImGuiEnabled(cockpit_mode_||shared_cockpit_);
    viewport_->setMinimumSize(180,160);viewport_->widget()->setMinimumSize(180,160);
    if(cockpit_mode_){viewport_->setMinimumSize(480,200);viewport_->widget()->setMinimumSize(480,200);}
    viewport_->context().setSimulationTime(sim_time_);
    connect(viewport_,&viewport3d::Viewport3DView::beforeFrameTick,this,[this]{
        applyContext();
        if(applied_context_key_==context_key_) viewport_->context().setSimulationTime(sim_time_);
        synchronizeCockpit();
    });
    connect(viewport_,&viewport3d::Viewport3DView::vsgReady,this,[this]{applyContext();
        if(cockpit_mode_)if(auto* view=viewport_->widget()->view();view&&view->viewDependentState){view->viewDependentState->maxShadowDistance=2000.;view->viewDependentState->shadowMapBias=.0005;}
        updateMarkers();updateStatus();});
    connect(viewport_,&viewport3d::Viewport3DView::frameTick,this,[this](const viewport3d::FrameTick&){if(cockpit_mode_)updateStatus();else if(audio_->isChecked())updateAcousticMeter();});
    auto configuration=viewport3d::ViewportConfig::fromPreset("earth_display");
    if(context_.contains("voxel_scene")){
        configuration.remove("rocky_ocean");
        const auto& anchor=context_.at("geo_anchor");configuration.add("rocky_map",{{"map_json",context_.at("earth").at("map")},{"lat",anchor.at("lat")},{"lon",anchor.at("lon")},{"alt",anchor.at("alt")},{"scene_context",true},{"cloud_objects",true},{"background_loading",true}});
        auto environment=context_.value("atmosphere",json::object());environment.update(context_.value("utc_origin",json::object()));environment["time_scale"]=1;environment["cover"]=environment.value("cloud_cover",0.);environment["cumulus_count"]=0;
        configuration.add("rocky_sky",environment);configuration.add("rocky_cloud",environment);
        json p={{"external_only",true},{"terrain_display_paging",context_.value("terrain_display_paging",false)}};if(context_.contains("voxel_camera"))p["camera_local"]=context_.at("voxel_camera");configuration.add("voxel_scene",p);configuration.add(viewport3d::ScenePlatformMotion::kId);
    }
    if(cockpit_mode_||shared_cockpit_){auto cockpit=cockpit_view_;cockpit["scene_exterior"]=context_.contains("voxel_scene");configuration.add(viewport3d::AircraftCockpitFeature::kId,cockpit);}
    if(context_.contains("voxel_scene")||cockpit_mode_||shared_cockpit_)configuration.add(viewport3d::EngagementVisualFeature::kId);
    viewport_->applyConfig(configuration);
    connect(viewport_->context().widget(),&gui::VsgQtWidget::initializationFailed,this,[this](const QString& message){error_=tr("三维显示初始化失败：%1").arg(message);viewport_->setAutoFrame(false);updateStatus();});
    viewport_->setIdleSuspend(!running_,1000);viewport_->setAutoFrame(online_,30);placeholder_->hide();
}
void EarthPanel::applyContext() {
    if(!viewport_||!viewport_->isReady()||context_.empty())return;
    if(context_.contains("voxel_scene")) {
        const auto readError=QString::fromStdString(voxel_view_.error());
        if(viewport_->property("publishedVoxelReadError").toString()!=readError)
            viewport_->setProperty("publishedVoxelReadError",readError);
        voxel_view_.submit(context_.at("voxel_scene"));
        json presentation,equipmentVisuals;
        if(auto snapshot=voxel_view_.take(&presentation,&equipmentVisuals))
            if(auto* feature=viewport_->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene")) {
                feature->bindSnapshot(std::move(snapshot),std::move(presentation),std::move(equipmentVisuals));
                viewport_->markActivity();
            }
    }
    if(attempted_context_key_==context_key_)return;
    attempted_context_key_=context_key_;
    try {
        if(context_.contains("voxel_scene")){
            viewport_->removeFeature("rocky_ocean");
        }
        if(auto* cockpit=viewport_->featureAs<viewport3d::AircraftCockpitFeature>(viewport3d::AircraftCockpitFeature::kId))cockpit->configure({{"scene_exterior",context_.contains("voxel_scene")}});
        const auto key=earthKey(context_);
        if(key!=applied_earth_key_) {
            auto* globe=viewport_->featureAs<viewport3d::EarthView>(viewport3d::EarthView::kId);std::string error;
            const auto source=context_.value("source_scenario",std::string());
            const bool streamed=globe && globe->surfaceSnapshot() && globe->surfaceSnapshot()->resident_tiles &&
                context_.contains("earth") && context_["earth"].contains("tile") && context_.contains("surface_revision");
            if(streamed) {
                if(!globe->streamSurface(key,[context=context_,source] {
                    auto surface=earth::EarthDataService::loadScenario(context,source);
                    if(!surface || context.at("surface_revision")!=surface->revision)
                        throw std::runtime_error("地表版本与仿真主控不一致，请检查部署数据");
                    return surface;
                })) { attempted_context_key_.clear(); return; }
            } else if(!globe || !globe->loadEarthScenario(context_,source,error)) {
                if(error=="Earth load pending"){attempted_context_key_.clear();return;}
                throw std::runtime_error(error.empty()?"shared Earth base is unavailable":error);
            }
            if(!context_.contains("voxel_scene")&&context_.contains("surface_revision")&&context_["surface_revision"].is_number_unsigned()) {
                const auto surface=globe->surfaceSnapshot();
                if(!surface||surface->revision!=context_["surface_revision"].get<uint64_t>())throw std::runtime_error("地表版本与仿真主控不一致，请检查部署数据");
            }
            applied_earth_key_=key;
        }
        if(context_.contains("voxel_scene")) {
            if(!viewport_->feature(viewport3d::EngagementVisualFeature::kId))viewport_->addFeature(viewport3d::EngagementVisualFeature::kId);
            json options = {{"terrain_display_paging", context_.value("terrain_display_paging",false)}};
            if (viewport_->feature("voxel_scene")) {
                viewport_->configureFeature("voxel_scene", options);
            } else {
                options["external_only"] = true;
                if (context_.contains("voxel_camera")) options["camera_local"] = context_.at("voxel_camera");
                viewport_->addFeature("voxel_scene", options);
            }
            voxel_view_.submit(context_.at("voxel_scene"));
        } else if(viewport_->feature("voxel_scene")){viewport_->removeFeature("voxel_scene");}
        auto atmosphere=context_.value("atmosphere",json::object());
        const auto simulation=context_.value("simulation",json::object());
        const auto epoch=simcore::parseUtcEpoch(simulation.value("epoch_utc",std::string("2026-09-10T04:00:00Z")));
        atmosphere["year"]=epoch.year;atmosphere["month"]=epoch.month;atmosphere["day"]=epoch.day;atmosphere["hours_utc"]=epoch.hours();
        if(context_.contains("voxel_scene")&&context_.contains("utc_origin"))atmosphere.update(context_.at("utc_origin"));
        atmosphere["cover"]=atmosphere.value("cloud_cover",.5);atmosphere["stratus_altitude"]=atmosphere.value("cloud_base",1200.0);atmosphere["cumulus_altitude"]=atmosphere.value("cloud_base",1200.0);
        for(const auto* id:{"rocky_sky","rocky_cloud"})viewport_->configureFeature(id,atmosphere);
        auto ocean=atmosphere;ocean.update(context_.value("ocean",json::object()));viewport_->configureFeature("rocky_ocean",ocean);
        if(auto* sky=viewport_->featureAs<viewport3d::SkyFeature>(viewport3d::SkyFeature::kId))sky->setAtmosphereParams(atmosphere);
        if(auto* cloud=viewport_->featureAs<viewport3d::CloudFeature>(viewport3d::CloudFeature::kId))cloud->setAtmosphereParams(atmosphere);
        if(auto* sea=viewport_->featureAs<viewport3d::OceanFeature>(viewport3d::OceanFeature::kId))sea->setAtmosphereParams(ocean);
        applied_context_key_=context_key_;error_.clear();updateMarkers();viewport_->markActivity();
    }catch(const std::exception& exception){error_=tr("地球基座加载失败：%1").arg(QString::fromUtf8(exception.what()));viewport_->setAutoFrame(false);}
}
void EarthPanel::updateTelemetry(const json& input,uint32_t preferred) {
    json valid=json::array();std::set<uint32_t> ids;
    if(input.is_array()&&input.size()<=4096)for(const auto& item:input) {
        if(!item.is_object()||!item.contains("platform_id")||!validId(item["platform_id"])||!item.contains("name")||!item["name"].is_string()||!item.contains("position")||!item["position"].is_array()||item["position"].size()!=3)continue;
        const auto id=item["platform_id"].get<uint32_t>();bool finite=true;for(const auto& value:item["position"])finite=finite&&value.is_number()&&std::isfinite(value.get<double>());
        if(!finite||!ids.insert(id).second)continue;
        json state={{"platform_id",id},{"name",item["name"]},{"position",item["position"]}};
        for(const auto* key:{"yaw","pitch","roll","speed"})if(number(item,key))state[key]=item[key];
        for(const auto* key:{"presentation","rotors","surface_motion","air_motion","time","navigation"})
            if(item.contains(key)&&item.at(key).is_object())state[key]=item.at(key);
        if(item.contains("model_id")&&item.at("model_id").is_string())
            state["model_id"]=item.at("model_id");
        if(item.contains("model_state")&&item.at("model_state").is_string())
            state["model_state"]=item.at("model_state");
        if(item.contains("submersible_motion")&&item.at("submersible_motion").is_object())
            state["submersible_motion"]=item.at("submersible_motion");
        valid.push_back(std::move(state));
    }
    const auto current=platforms_->currentData().toUInt();const bool changed=valid!=samples_;samples_=std::move(valid);
    bool listChanged=platforms_->count()!=int(samples_.size());
    for(int i=0;!listChanged&&i<platforms_->count();++i)
        listChanged=platforms_->itemData(i).toUInt()!=samples_[i]["platform_id"].get<uint32_t>()||platforms_->itemText(i)!=QString::fromStdString(samples_[i]["name"].get<std::string>());
    if(listChanged){QSignalBlocker block(platforms_);platforms_->clear();for(const auto& item:samples_)platforms_->addItem(QString::fromStdString(item["name"].get<std::string>()),item["platform_id"].get<uint32_t>());
        int index=platforms_->findData(current?current:preferred);if(index<0&&preferred)index=platforms_->findData(preferred);if(index>=0)platforms_->setCurrentIndex(index);}
    if(changed){updateMarkers();if(viewport_&&online_)viewport_->markActivity();if(follow_->isChecked())focusSelected();}updateStatus();
}
json EarthPanel::selected()const {
    const auto id=platforms_->currentData().toUInt();for(const auto& sample:samples_)if(sample["platform_id"].get<uint32_t>()==id)return sample;return json::object();
}
void EarthPanel::updateMarkers() {
    if(applied_context_key_!=context_key_) return;
    if(!viewport_||!viewport_->isReady()||!error_.isEmpty()||context_.empty())return;
    if(context_.contains("voxel_scene")) {
        // The authoritative mesh already represents each voxel platform. The
        // legacy 12m situation sphere can completely obscure a close aircraft.
        if(markers_&&!markers_->children.empty()){markers_->children.clear();marker_nodes_.clear();viewport_->context().requestCompile();}
        return;
    }
    if(!markers_){markers_=vsg::Group::create();viewport_->context().contentRoot()->addChild(markers_);}
    if(!marker_shape_){auto builder=vsg::Builder::create();vsg::GeometryInfo geometry;geometry.color=vsg::vec4(.15f,.8f,1.f,1.f);vsg::StateInfo state;state.lighting=false;marker_shape_=builder->createSphere(geometry,state);}
    bool membership=false;std::set<uint32_t> present;
    for(const auto& sample:samples_) {
        const auto id=sample["platform_id"].get<uint32_t>();
        // Do not create a placeholder entry for the pilot's own marker. Doing
        // so would erase it and request a full graph compile on every update.
        if((cockpit_mode_||shared_cockpit_)&&cockpit_view_.value("view_mode",std::string("pilot"))!="overview"&&cockpit_state_.value("platform_id",uint32_t(0))==id)continue;
        present.insert(id);auto& node=marker_nodes_[id];
        if(!node){node=vsg::MatrixTransform::create();node->addChild(marker_shape_);markers_->addChild(node);membership=true;}
        const auto& p=sample["position"];node->matrix=vsg::translate(p[0].get<double>(),p[1].get<double>(),p[2].get<double>())*vsg::scale(12.,12.,12.);
    }
    for(auto it=marker_nodes_.begin();it!=marker_nodes_.end();)if(!present.count(it->first)){auto& children=markers_->children;children.erase(std::remove(children.begin(),children.end(),it->second),children.end());it=marker_nodes_.erase(it);membership=true;}else ++it;
    if(membership)viewport_->context().requestCompile();
}
void EarthPanel::focusSelected() {
    if(shared_cockpit_&&cockpit_view_.value("view_mode",std::string("overview"))!="overview")return;
    if(applied_context_key_!=context_key_) return;
    const auto state=selected();if(!viewport_||!viewport_->isReady()||!online_||state.empty()||context_.empty()||!error_.isEmpty())return;
    const auto& p=state["position"];const auto world=viewport_->context().contentModel()*vsg::dvec4(p[0].get<double>(),p[1].get<double>(),p[2].get<double>(),1.0);
    viewport_->focusViewOn({world.x,world.y,world.z});viewport_->markActivity();
}
void EarthPanel::updateStatus() {
    updateAcousticMeter();
    focus_->setEnabled(viewport_&&viewport_->isReady()&&online_&&!samples_.empty()&&error_.isEmpty());
    follow_->setEnabled(native_rendering_&&online_&&!samples_.empty());
    if(!error_.isEmpty()){poseBar_->hide();status_->setText(error_);placeholder_->setText(error_);placeholder_->show();if(viewport_)viewport_->hide();return;}
    const auto label=online_?(running_?tr("运行中"):tr("已暂停")):tr("仿真离线，保留最后状态");
    status_->setText(tr("%1 · t=%2 s · %3 个平台 · 地球配置版本 %4").arg(label).arg(sim_time_,0,'f',3).arg(samples_.size()).arg(context_revision_));
    if(cockpit_mode_&&cockpit_view_.value("view_mode",std::string())=="external"&&viewport_&&viewport_->isReady())if(auto* cockpit=viewport_->featureAs<viewport3d::AircraftCockpitFeature>(viewport3d::AircraftCockpitFeature::kId);cockpit&&cockpit->exteriorReady())status_->setText(status_->text()+(cockpit->representativeExterior()?tr(" · 外观示意模型"):tr(" · 机型外形模型")));
    if(context_.contains("surface_quality") && context_["surface_quality"]=="reference")
        status_->setText(status_->text()+tr(" · 概略参考地表"));
    if(!native_rendering_)placeholder_->setText(tr("当前 Qt 离屏环境不提供三维 Vulkan 窗口\n地球配置与平台遥测仍按真实数据同步"));
    else if(context_.empty()) {placeholder_->show();placeholder_->setText(tr("等待仿真主控发布地球数据配置"));}
    else if(!viewport_||!viewport_->isReady()){placeholder_->show();placeholder_->setText(tr("正在初始化共享 Rocky 地球基座…"));}
    else placeholder_->hide();
    const auto sample=selected();updatePresentationControl(sample);if(sample.empty()){position_->setText(tr("等待平台遥测"));return;}
    const auto& p=sample["position"];
    auto detail=tr("%1 · 局部位置 (%2, %3, %4) m").arg(QString::fromStdString(sample["name"].get<std::string>()))
        .arg(p[0].get<double>(),0,'f',1).arg(p[1].get<double>(),0,'f',1).arg(p[2].get<double>(),0,'f',1);
    if(sample.contains("model_state"))detail+=QStringLiteral(" · ")+modelStateText(sample["model_state"].get<std::string>());
    const auto dive=sample.value("submersible_motion",json::object());
    if(number(dive,"depth_m")&&number(dive,"horizontal_speed_mps")&&number(dive,"vertical_speed_mps"))
        detail+=tr(" · 潜深 %1 m · 航速 %2 m/s · 垂向 %3 m/s")
            .arg(dive.at("depth_m").get<double>(),0,'f',1).arg(dive.at("horizontal_speed_mps").get<double>(),0,'f',2)
            .arg(dive.at("vertical_speed_mps").get<double>(),0,'f',2);
    if(sample.contains("time")&&context_.contains("voxel_scene")){
        const auto& scene=context_.at("voxel_scene");
        if(scene.is_object()&&scene.contains("scene_presentation")){
            const auto& statuses=scene.at("scene_presentation");
            if(statuses.is_object()&&statuses.contains("tick")&&sample.at("time").contains("tick")&&
                statuses.at("tick")==sample.at("time").at("tick")&&statuses.contains("equipment")&&statuses.at("equipment").is_array()){
                int count=0;
                for(const auto& item:statuses.at("equipment")){
                    if(!item.is_object()||!item.contains("platform_id")||item.at("platform_id")!=sample.at("platform_id")||
                        !item.contains("model_id")||!item.at("model_id").is_string()||!item.contains("state")||!item.at("state").is_string())continue;
                    detail+=tr(" · %1 %2").arg(QString::fromStdString(item.at("model_id").get<std::string>()),
                        modelStateText(item.at("state").get<std::string>()));
                    if(++count==4)break;
                }
            }
        }
    }
    const auto rotors=sample.value("rotors",json::object());
    for(auto next=rotors.begin();next!=rotors.end();++next)
        if(next->is_object()&&number(*next,"rpm")&&number(*next,"angle_deg"))
            detail+=tr(" · %1 %2 rpm / %3°").arg(QString::fromStdString(next.key()))
                .arg(next->at("rpm").get<double>(),0,'f',0).arg(next->at("angle_deg").get<double>(),0,'f',1);
    const auto ship=sample.value("surface_motion",json::object());
    if(number(ship,"speed_mps")&&number(ship,"roll_deg")&&number(ship,"pitch_deg")&&number(ship,"vertical_position_m"))
        detail+=tr(" · 航速 %1 m/s · 横摇 %2° · 纵摇 %3° · 垂向位置 %4 m")
            .arg(ship.at("speed_mps").get<double>(),0,'f',1).arg(ship.at("roll_deg").get<double>(),0,'f',1)
            .arg(ship.at("pitch_deg").get<double>(),0,'f',1).arg(ship.at("vertical_position_m").get<double>(),0,'f',2);
    const auto air=sample.value("air_motion",json::object());
    if(number(air,"horizontal_speed_mps")&&number(air,"vertical_speed_mps"))
        detail+=tr(" · 水平速度 %1 m/s · 升降率 %2 m/s")
            .arg(air.at("horizontal_speed_mps").get<double>(),0,'f',1)
            .arg(air.at("vertical_speed_mps").get<double>(),0,'f',1);
    const auto nav=sample.value("navigation",json::object());
    if(number(nav,"latitude_deg")&&number(nav,"longitude_deg")&&number(nav,"ellipsoid_height_m"))
        detail+=tr(" · 场景定位 %1°, %2° · 椭球高 %3 m")
            .arg(nav.at("latitude_deg").get<double>(),0,'f',6)
            .arg(nav.at("longitude_deg").get<double>(),0,'f',6)
            .arg(nav.at("ellipsoid_height_m").get<double>(),0,'f',1);
    position_->setText(detail);
}

void EarthPanel::updateAcousticMeter(){
    json audio=json::object();
    if(const auto scene=context_.find("voxel_scene");scene!=context_.end()&&scene->is_object())
        audio=scene->value("scene_presentation",json::object());
    if(!audio.is_object())audio=json::object();
    const auto probes=audio.value("acoustic_probes",json::array());
    QStringList names;
    if(probes.is_array())for(const auto& row:probes)
        if(row.is_object()&&row.contains("id")&&row.at("id").is_string())names<<QString::fromStdString(row.at("id").get<std::string>());
    bool changed=acoustic_probe_->count()!=names.size()+1;
    for(int i=0;!changed&&i<names.size();++i)changed=acoustic_probe_->itemData(i+1).toString()!=names[i];
    if(changed){
        const QSignalBlocker blocked(acoustic_probe_);
        const auto selected=acoustic_probe_->currentData();
        acoustic_probe_->clear();acoustic_probe_->addItem(tr("相机听者"),QString());
        for(const auto& id:names)acoustic_probe_->addItem(id,id);
        acoustic_probe_->setCurrentIndex(std::max(0,acoustic_probe_->findData(selected)));
        if(viewport_)viewport_->widget()->setProperty("sceneAcousticProbe",acoustic_probe_->currentData());
    }
    const bool visible=audio_->isChecked()||!names.isEmpty();
    acoustic_probe_->setVisible(visible);acoustic_meter_->setVisible(visible);
    if(!visible)return;
    const auto selection=acoustic_probe_->currentData().toString().toStdString();
    json row;
    if(viewport_&&audio_->isChecked()){
        const auto current=json::parse(viewport_->widget()->property("sceneAcousticsReading").toString().toStdString(),nullptr,false);
        if(current.is_object()&&current.value("probe",std::string())==(selection.empty()?"camera":selection))row=current;
    }
    if(row.is_null()&&!selection.empty()){
        const auto values=audio.value("acoustic_readings",json::array());
        if(values.is_array())for(const auto& item:values)
            if(item.is_object()&&item.contains("id")&&item.at("id")==selection){row=item;break;}
    }
    if(!number(row,"spl_db")){acoustic_meter_->setText(tr("测点尚无声压读数"));acoustic_meter_->setToolTip({});return;}
    const bool underwater=row.value("medium",std::string())=="water";
    auto text=(underwater?tr("%1 dB re 1 µPa（水下）"):tr("%1 dB SPL")).arg(row.at("spl_db").get<double>(),0,'f',1);
    if(number(row,"dba"))text+=tr(" · %1 dBA").arg(row.at("dba").get<double>(),0,'f',1);
    if(number(row,"laeq_db")&&number(row,"window_s"))text+=tr(" · LAeq %1 dBA / %2 s")
        .arg(row.at("laeq_db").get<double>(),0,'f',1).arg(row.at("window_s").get<double>(),0,'f',1);
    acoustic_meter_->setText(text+tr(" · 仿真估算"));
    QStringList details;
    if(number(row,"pressure_rms_pa"))details<<(underwater?tr("有效声压 %1 Pa · 水中参考值 1 µPa"):
        tr("有效声压 %1 Pa · 空气参考值 20 µPa")).arg(row.at("pressure_rms_pa").get<double>(),0,'g',4);
    if(number(row,"lafmax_estimate_db"))details<<tr("估算 LAFmax %1 dBA").arg(row.at("lafmax_estimate_db").get<double>(),0,'f',1);
    if(number(row,"sel_db"))details<<tr("窗口累计 SEL %1 dBA").arg(row.at("sel_db").get<double>(),0,'f',1);
    if(row.contains("band_hz")&&row.at("band_hz").is_array()&&row.contains("band_spl_db")&&row.at("band_spl_db").is_array()){
        const auto& band=row.at("band_hz");const auto& level=row.at("band_spl_db");
        for(size_t i=0;i<band.size()&&i<level.size()&&i<8;++i)
            if(band[i].is_number()&&level[i].is_number())details<<tr("%1 Hz  %2 dB SPL")
                .arg(band[i].get<double>(),0,'f',0).arg(level[i].get<double>(),0,'f',1);
    }
    acoustic_meter_->setToolTip(details.join('\n'));
}
void EarthPanel::sendPresentationPose() {
    if(!poseApply_->isEnabled()||!posePlatform_)return;
    auto& net=BrokerNet::instance();
    FlightControlCommand command;
    command.platform_id=posePlatform_;
    command.session_id=poseSession_;
    command.presentation_pose=PlatformPoseDemand{poseYaw_->value(),posePitch_->value()};
    poseRequest_=net.submitFlightControl(command);
    poseRequestAge_.restart();
    poseFeedback_=tr("等待仿真回报");
    updatePresentationControl(selected());
}
void EarthPanel::updatePresentationControl(const json& sample) {
    const auto data=sample.value("presentation",json::object());
    const bool capable=!cockpit_mode_&&data.contains("model")&&data.at("model")=="m1a2"&&
        number(data,"yaw_deg")&&number(data,"pitch_deg")&&number(data,"travel_m")&&
        data.contains("revision")&&data.at("revision").is_number_unsigned();
    poseBar_->setVisible(capable);
    if(!capable){posePlatform_=0;poseRequest_.clear();poseFeedback_.clear();return;}
    auto& net=BrokerNet::instance();
    const auto meta=net.lastSimMeta();
    const auto id=sample.at("platform_id").get<uint32_t>();
    if(id!=posePlatform_||meta.session_id!=poseSession_||!online_) {
        posePlatform_=id;poseSession_=meta.session_id;poseRequest_.clear();poseFeedback_.clear();
        poseYaw_->setValue(data.at("yaw_deg").get<double>());posePitch_->setValue(data.at("pitch_deg").get<double>());
    }
    if(!poseRequest_.empty()) {
        FlightControlResult result;
        const bool matched=net.flightControlResult(poseRequest_,&result)&&result.platform_id==id&&result.session_id==poseSession_;
        const auto revision=data.at("revision").get<uint64_t>();
        const auto request=data.find("last_request_id");
        const bool observed=request!=data.end()&&request->is_string()&&request->get<std::string>()==poseRequest_;
        if(observed||(matched&&result.stage=="applied"&&revision>=result.revision)) {
            poseFeedback_=tr("仿真已确认 · 修订 %1").arg(observed?revision:result.revision);
            poseRequest_.clear();
        } else if(matched&&result.stage!="queued"&&result.stage!="applied"&&result.stage!="unknown") {
            poseFeedback_=tr("回报 %1").arg(QString::fromStdString(result.code));poseRequest_.clear();
        } else if(poseRequestAge_.isValid()&&poseRequestAge_.elapsed()>5500) {
            poseFeedback_=tr("回报超时 · 当前值以遥测为准");poseRequest_.clear();
        }
    }
    poseApply_->setEnabled(online_&&meta.flight_control_available&&!poseSession_.empty()&&poseRequest_.empty());
    poseStatus_->setText(tr("%1 · 回读 %2° / %3° · 履带 %4 m").arg(poseFeedback_.isEmpty()?tr("显示姿态"):poseFeedback_)
        .arg(data.at("yaw_deg").get<double>(),0,'f',1).arg(data.at("pitch_deg").get<double>(),0,'f',1)
        .arg(data.at("travel_m").get<double>(),0,'f',2));
}
void EarthPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);ensureViewport();applyContext();if(viewport_&&online_&&!context_.empty()&&error_.isEmpty()){viewport_->setAutoFrame(true,30);viewport_->setIdleSuspend(!running_,1000);viewport_->markActivity();}updateStatus();
}
void EarthPanel::hideEvent(QHideEvent* event){if(viewport_)viewport_->setAutoFrame(false);QWidget::hideEvent(event);}
}
