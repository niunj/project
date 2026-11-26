#include "viewport3d/VoxelSceneFeature.h"
#include "viewport3d/PlatformMotionFeature.h"
/// @file SimulationControlWindow.cpp
/// @brief Simulation control implementation (8 个分类 Dock).
#include "SimulationControlWindow.h"
#include "SimulationWorkerClient.h"
#include "vsgraysim/core/ScenarioClockConfig.h"

#include "AssetBrowserWidget.h"
#include "SimMonitorPanels.h"
#include "base/DockBuilder.h"
#include "base/WidgetFactory.h"
#include "base/ThemeManager.h"
#include "base/ProductWorkflow.h"
#include "base/PropertySchema.h"
#include "base/PropertyTreeWidget.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/VsgQtWidget.h"
#include "viewport3d/EnvFeatures.h"   // W2: SkyFeature/CloudFeature/OceanFeature setAtmosphereParams
#include "viewport3d/EarthView.h"
#include <vsgraysim/earth/EarthDataService.h>
#include <simcore/Orientation.h>
#pragma push_macro("slots")
#undef slots
#include <rocky_env/SceneSource.h>
#pragma pop_macro("slots")
#include <vsg/app/Window.h>
#include "viewport3d/ViewportConfig.h"
#include "viewport3d/SceneAudioFeature.h"
#include "viewport3d/SimulationDisplayFacade.h"
#include "viewport3d/SceneEntities.h"
#include "viewport3d/DetectionEffectFeature.h"
#include "viewport3d/RelationshipVisualFeature.h"
#include "viewport3d/SensorFovFeature.h"
#include "viewport3d/ViewportKit.h"
#include "vsgraysim/broker/SimDataBroker.h"
#include "vsgraysim/broker/BrokerNet.h"

#include <vsg/maths/vec3.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <map>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include <functional>
#include <unistd.h>

#include "vsgraysim/core/SensorTypes.h"
#include "vsgraysim/workspace/Workspace.h"
#include "vsgraysim/broker/SimDataBroker.h"
#include <nlohmann/json.hpp>

namespace vsgraysim { namespace gui {

/// 节点类型 / 树 role / 属性分组 Schema 统一来自 shared PropertySchema 模块,
/// 与场景设计 / 统一显控共用 NodeKind (Scenario=0, Terrain=1, Entity=2,
/// Environment=4), 保证字段名与取值来源一致。
constexpr int kRoleSimObjectId = prop::RoleKeyObjectId;
constexpr int kRoleTreeKind    = prop::RoleKeyNodeKind;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
SimulationControlWindow::SimulationControlWindow(QWidget* parent)
    : ui::BaseWindow(tr("仿真控制软件 — 装备数据生成与分发"), parent)
{
    resize(1500, 900);

    // 中央: 3D 场景预览视口 (雷达回波显示已移除, 由显控软件负责显示)
    preview_viewport_ = new viewport3d::Viewport3DView(this);
    viewport3d::ViewportConfig initialView;initialView.add("lights");initialView.add("camera_guard");
    preview_viewport_->applyConfig(initialView);
    preview_viewport_->setAutoFrame(true, target_fps_);
    preview_viewport_->context().setSimulationTime(0.0);
    connect(preview_viewport_,&viewport3d::Viewport3DView::beforeFrameTick,this,[this] {
        if(preview_viewport_->widget()->property("sceneAudioRequested").toBool())
            preview_viewport_->widget()->setProperty("sceneAudioPlaying",playing_.load());
        earth::EarthSurfaceSnapshotPtr next;
        earth::VoxelSceneSnapshotPtr voxel;
        double time;
        nlohmann::json presentation=nlohmann::json::array(),equipmentVisuals=nlohmann::json::object();
        if(engine_worker_) {
            if(auto ready=remote_voxel_view_.take(&presentation,&equipmentVisuals)){
                std::lock_guard lock(earth_view_mutex_);pending_voxel_view_=std::move(ready);
            }
            const auto error=QString::fromStdString(remote_voxel_view_.error());
            if(preview_viewport_->property("publishedVoxelReadError").toString()!=error) {
                preview_viewport_->setProperty("publishedVoxelReadError",error);
                if(!error.isEmpty())spdlog::warn("Published voxel snapshot read: {}",error.toStdString());
            }
        }
        {
            std::lock_guard lock(earth_view_mutex_);
            next=std::move(pending_earth_view_);voxel=std::move(pending_voxel_view_);
            time=pending_earth_time_;
        }
        if(!engine_worker_&&voxel_source_&&voxel_source_->hasPresentationControls()) {
            for(const auto& sample:voxel_source_->platforms())
                if(sample.contains("presentation"))presentation.push_back(sample);
            if(!voxel)if(auto* feature=preview_viewport_->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene"))
                if(feature->presentation()!=presentation)voxel=voxel_source_->snapshot();
        }
        if(voxel) {
            if(!engine_worker_&&voxel_source_){
                equipmentVisuals=voxel_source_->equipmentVisuals(*voxel);
                if(auto* audio=preview_viewport_->featureAs<viewport3d::SceneAudioFeature>(viewport3d::SceneAudioFeature::kId))
                    audio->publishLocalBrokerState(voxel_source_->presentationState(*voxel));
            }
            try{if(auto* feature=preview_viewport_->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene"))feature->bindSnapshot(voxel,std::move(presentation),std::move(equipmentVisuals));preview_viewport_->context().setSimulationTime(voxel->timeSeconds());}
            catch(const std::exception& error){spdlog::warn("Published voxel viewport: {}",error.what());playing_=false;statusBar()->showMessage(tr("体素视图同步失败：%1").arg(QString::fromUtf8(error.what())));}
        }
        if(!next) return;
        if(auto* view=preview_viewport_->featureAs<viewport3d::EarthView>(viewport3d::EarthView::kId)) {
            try {
                if(view->surfaceSnapshot()!=next) {
                    view->bindSurface(next,false);
                    if(sb_earth_) sb_earth_->setToolTip(tr("分块 %1 · 版本 %2")
                        .arg(QString::fromStdString(next->declaration.at("tile").dump())).arg(next->revision));
                }
                preview_viewport_->context().setSimulationTime(time);
            } catch(const std::exception& e) {
                playing_=false;
                statusBar()->showMessage(tr("地形视口切换失败：%1").arg(QString::fromUtf8(e.what())));
            }
        }
    });
    sim_facade_ = new viewport3d::SimulationDisplayFacade(preview_viewport_, this);

    // 视口承载容器: 视口满布 + 顶部半透明播放控制浮层 (分层叠加)
    playback_host_ = new QWidget(this);
    auto* host_lay = new QGridLayout(playback_host_);
    host_lay->setContentsMargins(0, 0, 0, 0);
    host_lay->setSpacing(0);
    host_lay->addWidget(preview_viewport_, 0, 0);
    buildPlaybackOverlay();                  // 创建浮层 (播放/单步/连续/暂停/结束 + 动画)
    host_lay->addWidget(playback_bar_, 0, 0, Qt::AlignTop | Qt::AlignHCenter);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(playback_host_);
    splitter->setStretchFactor(0, 2);
    setCentralWidget(splitter);

    connect(preview_viewport_, &viewport3d::Viewport3DView::vsgReady, this, [this]() {
        statusBar()->showMessage(tr("3D 预览视口就绪"), 2000);
        // W2: 视口就绪后还原场景里的细粒度大气参数到 rocky_env 模块
        // (loadScenarioFile 可能在 vsgReady 前调用, 特性未挂载时跳过; 此处确保下发)。
        applyAtmosphereFromScenario();
        if(pending_voxel_initialization_)QTimer::singleShot(0,this,&SimulationControlWindow::onInitSimulation);
    });

    // 1. 8 个分类 Dock
    buildProductDocks();

    // 2. 公共菜单栏/工具栏/状态栏
    buildBaseStatusBar();
    buildBaseMenuBar();
    buildBaseToolBar();

    // 3. 扩展状态栏标签 (仿真状态 + 仿真时间, 帧和FPS由基类管理)
    auto* sb = statusBar();
    sb_state_ = new QLabel(tr("未初始化"));
    sb_time_  = new QLabel(tr("仿真时间 0.00 s"));
    sb_backend_ = new QLabel(tr("后端：未初始化"));
    for (auto* lbl : {sb_state_, sb_time_}) {
        lbl->setStyleSheet("padding: 2px 8px; color: #9aa5b8;"
                           "font-family: monospace;");
        sb->addWidget(lbl);
    }
    sb_backend_->setToolTip(tr("显示实际初始化的传感器执行后端；自动模式可能回退到 CPU 参考链"));
    sb->addPermanentWidget(sb_backend_);
    sb_earth_=new QLabel(tr("地表：未装订"));
    sb->addPermanentWidget(sb_earth_);

    // 4. 定时器驱动主循环
    connect(&tick_timer_, &QTimer::timeout, this, &SimulationControlWindow::onTick);
    tick_timer_.start(std::max(1, 1000 / target_fps_));

    // 5. BrokerNet 网关 (发现面/链接面/数据面/控制面, 仿真侧发布方):
    //    显控进程经此发现本进程装备并建链; 建链集合驱动传感器产帧门 —
    //    发现面没有链接时仿真不做 RT 散射与信号成帧 (数据面零传输)。
    BrokerNet::instance().startDisplayRole();
    BrokerNet::instance().setLinkGate([this](const std::set<uint32_t>& linked) {
        sim_.setSensorGate(true, linked);   // 门常开, 仅放行已链接装备
    });

    setPlaybackEnabled(false);
    setFlowStage(0);
    startSimWorker();                      // 仿真工作线程 (重计算与 UI/渲染分离)
    menuBar()->addAction(tr("重启后台并恢复检查点"),this,&SimulationControlWindow::onRestartWorker);
    if (log_panel_) log_panel_->info(tr("仿真控制软件启动"));
    spdlog::info("SimulationControlWindow: built ({}x{})", width(), height());
}

SimulationControlWindow::~SimulationControlWindow()
{
    stopSimWorker();            // 先汇合仿真线程 (其后再销毁其访问的对象)
    tick_timer_.stop();
    if (recording_) stopRecording();
    if (replay_active_) stopReplay(false);
    BrokerNet::instance().setLinkGate({});
    bus_.reset(); voxel_source_.reset(); // data sources go offline before the UI is destroyed
    engine_worker_.reset();
    sim_.stop();
    setServiceOnline(false);
}
nlohmann::json SimulationControlWindow::runtimeStatus() const {
    if(engine_worker_){auto out=engine_worker_->status();out["worker_process"]=true;out["ui_pid"]=::getpid();return out;}
    std::lock_guard lock(sim_mtx_);nlohmann::json out={{"initialized",sim_inited_.load()},{"running",playing_.load()},{"sim_time",sim_time_.load()},{"frames",frames_.load()},{"voxel_source",bool(voxel_source_)}};
    if(voxel_source_){out["source"]=voxel_source_->diagnostics();if(auto state=voxel_source_->snapshot()){out["content_revision"]=state->contentRevision;out["tick"]=state->tick;out["fixed_step_seconds"]=state->fixedStep;out["platforms"]=voxel_source_->platforms();}}
    // sim_mtx_ also excludes the producer of this bounded timing history.
    if(!frame_ms_history_.empty()){std::vector<double> sorted(frame_ms_history_.begin(),frame_ms_history_.end());std::sort(sorted.begin(),sorted.end());double sum=0;for(double value:sorted)sum+=value;out["reference_tick"]={{"samples",sorted.size()},{"mean_ms",sum/sorted.size()},{"p50_ms",sorted[(sorted.size()-1)/2]},{"p95_ms",sorted[size_t((sorted.size()-1)*.95)]}};}
    out["equipment"]=nlohmann::json::array();for(const auto& e:EquipmentRegistry::instance().listOnline()){const auto stats=SimDataBroker::instance().channelStats(e.equipment_id);out["equipment"].push_back({{"id",e.equipment_id},{"name",e.name},{"frames",stats.published_frames},{"subscribed",SimDataBroker::instance().isSubscribed(e.equipment_id)},{"powered",e.powered}});}return out;
}

void SimulationControlWindow::startVoxelWorker(const QString& path) {
    if(engine_worker_){statusBar()->showMessage(tr("后台已启动；重新初始化请使用“重启后台并恢复检查点”"),4000);return;}
    playing_=false;sim_inited_=false;service_online_=false;setPlaybackEnabled(false);
    {std::lock_guard lock(sim_mtx_);if(bus_)bus_->stop();bus_.reset();voxel_source_.reset();data_source_.reset();platform_mgr_.reset();}
    BrokerNet::instance().stop();BrokerNet::instance().startDisplayRole();
    engine_worker_=std::make_unique<SimulationWorkerClient>(this);engine_worker_->setObjectName("simulation_engine_supervisor");
    engine_worker_->changed=[this](const nlohmann::json& s){acceptWorkerStatus(s);};
    engine_worker_->start(path);statusBar()->showMessage(tr("后台加载台北地形、体素与传感器；界面可继续操作"));
}
void SimulationControlWindow::acceptWorkerStatus(const nlohmann::json& s) {
    if(s.contains("scene_session_id")&&s.at("scene_session_id").is_string())
        preview_viewport_->widget()->setProperty("sceneAudioEpoch",
            QString::fromStdString(s.at("scene_session_id").get<std::string>()));
    sim_inited_=s.value("initialized",false)&&s.value("state",std::string())!="fault";
    playing_=s.value("running",false);sim_time_=s.value("sim_time",0.);frames_=s.value("frames",size_t(0));
    service_online_=sim_inited_&&s.value("state",std::string())!="stopped";
    backend_description_=tr("独立后台 GPU 体素多装备 · PID %1").arg(s.value("pid",0));
    if(s.contains("tasks")){
        if(!task_pool_table_){auto* dock=new QDockWidget(tr("后台进程池与任务监控"),this);dock->setObjectName("simulation_task_pool");task_pool_table_=new QTableWidget(dock);task_pool_table_->setColumnCount(7);task_pool_table_->setHorizontalHeaderLabels({tr("任务 / 装备"),tr("任务类型"),tr("状态"),tr("PID"),tr("计算资源"),tr("周期 s"),tr("推进 / 产品数")});task_pool_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);task_pool_table_->horizontalHeader()->setStretchLastSection(true);dock->setWidget(task_pool_table_);addDockWidget(Qt::RightDockWidgetArea,dock);}
        const auto& rows=s.at("tasks");task_pool_table_->setRowCount(int(rows.size()));for(int row=0;row<int(rows.size());++row){const auto& t=rows[row];const QString cells[]={QString::fromStdString(t.value("name",t.at("id").get<std::string>())),QString::fromStdString(t.at("type").get<std::string>()),QString::fromStdString(t.at("state").get<std::string>()),QString::number(t.value("pid",0)),QString::fromStdString(t.at("backend").get<std::string>()),QString::number(t.at("period_seconds").get<double>(),'f',3),QString::number(t.value("published",uint64_t(0)))};for(int column=0;column<7;++column){auto* item=task_pool_table_->item(row,column);if(!item){item=new QTableWidgetItem;task_pool_table_->setItem(row,column,item);}item->setText(cells[column]);}}
        if(s.contains("process_pool"))task_pool_table_->setToolTip(QString::fromStdString(s.at("process_pool").dump(2)+"\n任务类型能力：\n"+s.value("task_types",nlohmann::json::array()).dump(2)));
    }
    setPlaybackEnabled(sim_inited_&&!s.contains("pending_seq")&&!s.value("unresponsive",false));equipment_registered_=sim_inited_;
    if(s.contains("earth")&&s.at("earth").contains("voxel_scene")) {
        const auto& earth=s.at("earth");const auto generation=s.at("generation").get<std::string>();
        if(worker_view_generation_!=generation) {
            worker_view_generation_=generation;
            auto config=viewport3d::ViewportConfig::fromPreset("earth_display");config.remove("rocky_ocean");
            const auto& a=earth.at("geo_anchor");
            config.add("rocky_map",{{"map_json",earth.at("earth").at("map")},{"lat",a.at("lat")},{"lon",a.at("lon")},{"alt",a.at("alt")},{"scene_context",true},{"cloud_objects",true},{"background_loading",true}});
            auto sky=earth.value("atmosphere",nlohmann::json::object());sky.update(earth.value("utc_origin",nlohmann::json::object()));sky["cover"]=0;sky["cumulus_count"]=0;sky["time_scale"]=1;
            config.add("rocky_sky",sky);config.add("rocky_cloud",sky);
            nlohmann::json p={{"external_only",true},{"terrain_display_paging",earth.value("terrain_display_paging",false)}};if(earth.contains("voxel_camera"))p["camera_local"]=earth.at("voxel_camera");config.add("voxel_scene",p);config.add(viewport3d::ScenePlatformMotion::kId);
            config.add("engagement_visual");
            config.add("trail");
            preview_viewport_->applyConfig(config);
        }
        remote_voxel_view_.submit(earth.at("voxel_scene"));preview_viewport_->markActivity();
    }
    if(s.contains("command_error"))statusBar()->showMessage(QString::fromStdString(s.at("command_error").get<std::string>()));
    else if(s.contains("error"))statusBar()->showMessage(QString::fromStdString(s.at("error").get<std::string>()));
    else if(!sim_inited_)statusBar()->showMessage(tr("后台载入：%1").arg(QString::fromStdString(s.value("stage",std::string("starting")))));
    else statusBar()->showMessage(tr("后台%1 · 数值整步 %2 ms · 已确认命令 %3").arg(playing_?tr("运行"):tr("暂停")).arg(s.value("worker_tick_ms",0.),0,'f',1).arg(s.value("ack",0)));
    if(act_record_)act_record_->setEnabled(false);if(act_replay_)act_replay_->setEnabled(false);
    if(sb_earth_)sb_earth_->setText(tr("台北新数据 · 后台真值 · 只读显示"));
    updateDistMonitor();
    updateStatusLabels();
}
void SimulationControlWindow::onRestartWorker(){if(engine_worker_)engine_worker_->restart();}

// ---------------------------------------------------------------------------
// Dock 1: 场景装载
// ---------------------------------------------------------------------------
// 3D 视口顶部半透明播放控制浮层 (播放/单步方正/连续仿真/暂停/结束 + 淡入动画)
// ---------------------------------------------------------------------------
void SimulationControlWindow::buildPlaybackOverlay()
{
    auto* bar = new QWidget(playback_host_);
    bar->setObjectName("PlaybackBar");
    bar->setStyleSheet(
        "#PlaybackBar { background: rgba(13, 17, 23, 200);"
        " border:1px solid #2d6fb8; border-radius:8px; }"
        "QPushButton { background:#1d4f8a; border:none; border-radius:4px;"
        " padding:6px 14px; color:#eaf2fc; font-weight:600; }"
        "QPushButton:hover { background:#2d6fb8; }"
        "QPushButton:disabled { background:#262e3b; color:#6b7688; }"
        "QLabel { color:#9aa5b8; font-size:12px; }"
        "QComboBox { background:#151b23; border:1px solid #262e3b;"
        " border-radius:4px; padding:2px 6px; color:#c9d4e3; }");
    bar->setAttribute(Qt::WA_TranslucentBackground, true);

    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(8);

    btn_play_  = new QPushButton(tr("▶ 播放"), bar);
    btn_step_  = new QPushButton(tr("单步推进"), bar);
    btn_cont_  = new QPushButton(tr("连续仿真"), bar);
    btn_pause_ = new QPushButton(tr("暂停"), bar);
    btn_stop_  = new QPushButton(tr("停止"), bar);
    btn_play_->setShortcut(QKeySequence(Qt::Key_F5));
    btn_step_->setShortcut(QKeySequence(Qt::Key_F6));
    btn_stop_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
    btn_play_->setToolTip(tr("播放 / 暂停（F5）；暂停时可装订飞控参数，状态在推进后变化"));
    btn_step_->setToolTip(tr("单步推进（F6）；先装订待处理命令，再推进一个仿真步长"));
    btn_stop_->setToolTip(tr("停止（Shift+F5）；保留当前场景状态并释放外部飞控，重新初始化可复位"));
    btn_play_->setStyleSheet("QPushButton { background:#2d8a4e; border:none;"
                             " border-radius:4px; padding:6px 14px;"
                             " color:#eaf6ee; font-weight:700; }"
                             "QPushButton:disabled { background:#262e3b; color:#6b7688; }");
    lay->addWidget(btn_play_);
    lay->addWidget(btn_step_);
    lay->addWidget(btn_cont_);
    lay->addWidget(btn_pause_);
    lay->addWidget(btn_stop_);

    // 倍速已移除: 仿真时钟 = 帧数 × 定步长, 保证时序确定可复现

    playback_bar_ = bar;

    connect(btn_play_,  &QPushButton::clicked, this, &SimulationControlWindow::onPlayPause);
    connect(btn_step_,  &QPushButton::clicked, this, &SimulationControlWindow::onStepOnce);
    connect(btn_pause_, &QPushButton::clicked, this, [this]() {
        if (playing_) onPlayPause();       // 暂停
    });
    connect(btn_cont_,  &QPushButton::clicked, this, [this]() {
        if (!playing_) onPlayPause();      // 连续仿真
    });
    connect(btn_stop_,  &QPushButton::clicked, this, &SimulationControlWindow::onStop);

    // 淡入动画 (浮层出现/切悬停态)
    auto* anim = new QPropertyAnimation(bar, "windowOpacity", this);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setDuration(350);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

// ---------------------------------------------------------------------------
// Dock 5: 装备数据分发
// ---------------------------------------------------------------------------
// 装备在线状态工作监控 (装备状态总览 + 数据分发 + 事件日志 合并为一个模块)
// ---------------------------------------------------------------------------
void SimulationControlWindow::buildEquipmentMonitorDock()
{
    auto* v = createDock(tr("装备在线状态工作监控"), Qt::RightDockWidgetArea, 300);

    // 数据服务在线状态
    lbl_dist_service_ = new QLabel(tr("数据服务: 离线"));
    lbl_dist_service_->setStyleSheet(
        "font-weight:bold; padding:2px 4px; color:#e9c062;");
    v->addWidget(lbl_dist_service_);

    // 装备数据分发状态: 雷达/SAR/红外
    auto* g_dist = new QGroupBox(tr("数据分发"));
    auto* vg_dist = new QVBoxLayout(g_dist);
    const char* dist_names[3] = {"雷达回波", "SAR 回波", "红外辐射亮度"};
    for (int i = 0; i < 3; ++i) {
        lbl_dist_[i] = new QLabel(tr(dist_names[i]) + tr(" · 等待仿真运行"));
        lbl_dist_[i]->setStyleSheet("font-family: monospace; color:#9aa5b8; padding:2px 2px;");
        vg_dist->addWidget(lbl_dist_[i]);
    }
    v->addWidget(g_dist);

    // 装备状态总览: 雷达/SAR/红外 × 开关机/帧率/流量/最近帧号
    auto* g_eq = new QGroupBox(tr("装备状态总览"));
    auto* vg_eq = new QVBoxLayout(g_eq);
    table_equip_ = new QTableWidget(3, 5, g_eq);
    table_equip_->setHorizontalHeaderLabels(
        {tr("装备"), tr("开关机"), tr("数据帧率 (Hz)"),
         tr("数据流量 (KB/s)"), tr("最近帧号")});
    table_equip_->verticalHeader()->setVisible(false);
    table_equip_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_equip_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_equip_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_equip_->setStyleSheet(
        "QTableWidget { background:#151b23; border:1px solid #262e3b;"
        " gridline-color:#262e3b; }"
        "QTableWidget::item { padding:4px; color:#c9d4e3; }"
        "QHeaderView::section { background:#1a2130; color:#9aa5b8;"
        " border:none; padding:5px; }");
    const char* eq_names[3] = {"雷达", "SAR", "红外"};
    for (int r = 0; r < 3; ++r) {
        auto* name = new QTableWidgetItem(tr(eq_names[r]));
        name->setTextAlignment(Qt::AlignCenter);
        table_equip_->setItem(r, 0, name);
        for (int c = 1; c < 5; ++c) {
            auto* it = new QTableWidgetItem(tr("—"));
            it->setTextAlignment(Qt::AlignCenter);
            table_equip_->setItem(r, c, it);
        }
    }
    table_equip_->setMaximumHeight(150);
    vg_eq->addWidget(table_equip_);
    v->addWidget(g_eq);

    auto* flight = new QGroupBox(tr("平台飞控执行状态"));
    auto* flightLayout = new QVBoxLayout(flight);
    table_flight_ = new QTableWidget(0, 4, flight);
    table_flight_->setObjectName("flight_control_monitor");
    table_flight_->setHorizontalHeaderLabels({tr("平台"), tr("模式"), tr("控制权"), tr("修订号")});
    table_flight_->verticalHeader()->hide();
    table_flight_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_flight_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_flight_->setMaximumHeight(160);
    flightLayout->addWidget(table_flight_);
    auto* releaseFlight = new QPushButton(tr("释放全部外部飞控"), flight);
    releaseFlight->setToolTip(tr("解除外部接管并稳定当前飞行状态；旧请求失效，显控需要重新接管。"));
    connect(releaseFlight, &QPushButton::clicked, this, [this] {
        std::lock_guard<std::mutex> lock(sim_mtx_);
        if (platform_mgr_) platform_mgr_->resetFlightControls();
        BrokerNet::instance().resetFlightControlSession();
        if (log_panel_) log_panel_->warn(tr("主控已释放全部外部飞控，显控需重新接管"));
    });
    flightLayout->addWidget(releaseFlight);
    v->addWidget(flight);

    // 事件日志
    auto* g_log = new QGroupBox(tr("事件日志"));
    auto* vl = new QVBoxLayout(g_log);
    log_panel_ = new EventLogPanel(g_log);
    vl->addWidget(log_panel_);
    v->addWidget(g_log, 1);
}

// ---------------------------------------------------------------------------
// Dock 7: 性能监控 (右侧)
// ---------------------------------------------------------------------------
void SimulationControlWindow::buildPerfMonitorDock()
{
    auto* v = createDock(tr("性能监控"), Qt::RightDockWidgetArea, 240);
    perf_panel_ = new PerfMonitorPanel;
    v->addWidget(perf_panel_, 1);      // 垂直拉伸填满整个 Dock
}

// ---------------------------------------------------------------------------
// 场景实体树 (只读) / 实体属性 (只读): 复用场景设计软件的查看能力
// ---------------------------------------------------------------------------
void SimulationControlWindow::buildEntityTreeDock()
{
    auto* v = createDock(tr("场景树 (只读)"), Qt::LeftDockWidgetArea, 250);

    // 场景文件打开/关闭: 内置在场景树中, 选中后属性表显示对应信息 (只读)。
    auto* row_bar = new QHBoxLayout;
    auto* btn_open_scene  = new QPushButton(tr("打开场景"));
    auto* btn_close_scene = new QPushButton(tr("关闭场景"));
    const QString btn_qss =
        "QPushButton{ background:#1d4f8a; border:none; border-radius:3px;"
        " padding:5px 10px; color:#eaf2fc; font-weight:600; }"
        "QPushButton:hover{ background:#2d6fb8; }"
        "QPushButton:disabled{ background:#262e3b; color:#6b7688; }";
    btn_open_scene->setStyleSheet(btn_qss);
    btn_close_scene->setStyleSheet(btn_qss);
    row_bar->addWidget(btn_open_scene);
    row_bar->addWidget(btn_close_scene);
    v->addLayout(row_bar);

    connect(btn_open_scene, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("打开场景"), "config/scenarios", tr("场景文件 (*.json *.vsimscene)"));
        if (!path.isEmpty()) loadScenarioFile(path);
    });
    connect(btn_close_scene, &QPushButton::clicked, this, [this]() {
        refreshEntityTree(nlohmann::json{});
        updateSceneProperties();
        statusBar()->showMessage(tr("场景树已关闭"), 2000);
    });

    tree_entities_ = new QTreeWidget;
    tree_entities_->setHeaderHidden(true);
    v->addWidget(tree_entities_, 1);   // 垂直拉伸填满整个 Dock (与场景设计一致)

    // 选中节点 → 刷新右侧属性表 (只读, 场景/地形/环境/实体均展示不同信息)。
    connect(tree_entities_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem*, QTreeWidgetItem*) { updateSceneProperties(); });
    // 双击实体节点 → 相机定位并跟随该实体运动
    connect(tree_entities_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (!item || !sim_facade_) return;
                const QString oid = item->data(0, kRoleSimObjectId).toString();
                if (oid.isEmpty()) return;
                sim_facade_->setFollowEntity(oid);
                statusBar()->showMessage(tr("已定位并跟随实体 %1").arg(oid), 2000);
            });
}

void SimulationControlWindow::buildScenePropertiesDock()
{
    auto* v = createDock(tr("实体属性 (只读)"), Qt::LeftDockWidgetArea, 280);
    tree_props_ = new base::PropertyTreeWidget;
    tree_props_->setEditable(false);   // 仿真控制: 只读 (与场景设计共用同一公共控件)
    v->addWidget(tree_props_, 1);      // 垂直拉伸填满整个 Dock
}

void SimulationControlWindow::refreshEntityTree(const nlohmann::json& scenario)
{
    if (!tree_entities_) return;
    const QSignalBlocker b(tree_entities_);
    tree_entities_->clear();
    if (!scenario.is_object()) {
        tree_entities_->addTopLevelItem(new QTreeWidgetItem({tr("(未装载场景)")}));
        return;
    }

    namespace fs = std::filesystem;
    auto* root = new QTreeWidgetItem({
        QString::fromStdString(scenario.value("scenario_name", std::string("未命名场景")))});
    root->setData(0, kRoleTreeKind, int(prop::NodeKind::Scenario));
    QFont rf = root->font(0);
    rf.setBold(true);
    root->setFont(0, rf);
    tree_entities_->addTopLevelItem(root);

    const auto terrain = scenario.value("earth", nlohmann::json::object());
    const std::string tref = terrain.value("map", scenario.value("terrain_ref", std::string()));
    auto* tnode = new QTreeWidgetItem({
        tref.empty()
            ? tr("默认地球底座")
            : tr("地形: %1").arg(QString::fromStdString(fs::path(tref).stem().string()))});
    tnode->setData(0, kRoleTreeKind, int(prop::NodeKind::Terrain));
    root->addChild(tnode);

    // 环境节点
    auto* enode = new QTreeWidgetItem({tr("环境")});
    enode->setData(0, kRoleTreeKind, int(prop::NodeKind::Environment));
    root->addChild(enode);

    for (const auto& jo : scenario.value("objects", nlohmann::json::array())) {
        const std::string oid = jo.value("object_id", jo.value("id", std::string()));
        if (oid.empty()) continue;
        auto* onode = new QTreeWidgetItem({
            QString("%1  [%2]")
                .arg(QString::fromStdString(oid))
                .arg(QString::fromStdString(
                    fs::path(jo.value("mesh_path", std::string())).filename().string()))});
        onode->setData(0, kRoleSimObjectId, QString::fromStdString(oid));
        onode->setData(0, kRoleTreeKind, int(prop::NodeKind::Entity));
        root->addChild(onode);
    }
    root->setExpanded(true);
}

nlohmann::json SimulationControlWindow::findScenarioObject(const QString& object_id) const
{
    if (!scenario_data_.is_object()) return {};
    const std::string key = object_id.toStdString();
    for (const auto& jo : scenario_data_.value("objects", nlohmann::json::array())) {
        const std::string oid = jo.value("object_id", jo.value("id", std::string()));
        if (oid == key) return jo;
    }
    return {};
}

void SimulationControlWindow::updateSceneProperties()
{
    if (!tree_props_ || !tree_entities_) return;
    auto* item = tree_entities_->currentItem();
    const int kind = item ? item->data(0, kRoleTreeKind).toInt() : -1;
    sel_object_id_ = (kind == int(prop::NodeKind::Entity))
        ? item->data(0, kRoleSimObjectId).toString() : QString();

    namespace fs = std::filesystem;
    const auto num = [](double d, int prec = 2) { return QString::number(d, 'f', prec); };
    const auto nm  = [](double x, double y, double z) {
        return QString("(%1, %2, %3) m").arg(x, 0, 'f', 1)
                                        .arg(y, 0, 'f', 1).arg(z, 0, 'f', 1);
    };
    const auto posStr = [&nm](const nlohmann::json& j, const nlohmann::json& fallback) {
        const auto& use = (j.is_array() && j.size() >= 3) ? j : fallback;
        if (use.is_array() && use.size() >= 3)
            return nm(use[0].get<double>(), use[1].get<double>(), use[2].get<double>());
        return QString(tr("—"));
    };

    std::function<QString(const prop::Item&)> valueOf;
    QVector<prop::Group> groupDefs;

    switch (kind) {
    case int(prop::NodeKind::Scenario): {   // 场景根
        valueOf = [this](const prop::Item& it) -> QString {
            if (it.key == QStringLiteral("scenario_name"))
                return QString::fromStdString(
                    scenario_data_.value("scenario_name", std::string("未命名场景")));
            if (it.key == QStringLiteral("description"))
                return QString::fromStdString(scenario_data_.value("description", std::string()));
            if (it.key == QStringLiteral("terrain_ref")) return scenario_path_;
            if (it.key==QStringLiteral("frame_dt") || it.key==QStringLiteral("physics_step") ||
                it.key==QStringLiteral("history_seconds") || it.key==QStringLiteral("epoch_utc")) {
                const auto clock=ScenarioClockConfig::fromJson(scenario_data_.value("simulation",nlohmann::json::object()));
                if (it.key==QStringLiteral("epoch_utc")) return QString::fromStdString(clock.epoch_utc);
                return QString::number(it.key==QStringLiteral("frame_dt") ? clock.frame_dt :
                    it.key==QStringLiteral("physics_step") ? clock.physics_step : clock.history_seconds,'g',12);
            }
            if (it.key == QStringLiteral("entity_count")) return QString::number(
                scenario_data_.value("objects", nlohmann::json::array()).size());
            if (it.key == QStringLiteral("binding_count")) {
                const auto b = scenario_data_.value("bindings",nlohmann::json::object());
                return QString::number(b.value("guidance", nlohmann::json::array()).size());
            }
            if (it.key == QStringLiteral("sensor_count")) {
                const auto sl = scenario_data_.value("sensors",nlohmann::json::object());
                return QString::number(sl.value("sensor_list", nlohmann::json::array()).size());
            }
            return QString();
        };
        groupDefs = prop::groups(prop::NodeKind::Scenario);
        break;
    }
    case int(prop::NodeKind::Terrain): {    // 地形 (配置 + 地形工程参数)
        // 解析地形工程 (.vsgterrain) 配置, 与地形编辑软件落盘字段一致。
        const std::string tref = scenario_data_.value("terrain_ref", std::string());
        nlohmann::json tj;
        if (!tref.empty()) {
            const std::string resolved = Workspace::resolveTerrainRef(
                scenario_path_.toStdString(),
                resolveWorkspaceRoot().toStdString());
            std::ifstream in(resolved);
            try { if (in) in >> tj; } catch (const std::exception&) { tj = nlohmann::json{}; }
        }
        const bool has_ter = tj.is_object();
        valueOf = [tref, tj, has_ter, num](const prop::Item& it) -> QString {
            if (it.key == QStringLiteral("terrain_type"))
                return tref.empty()
                    ? tr("rocky 地球底座")
                    : (has_ter ? prop::terrainTypeName(tj.value("terrain_type", -1))
                               : QString::fromStdString(
                                     std::filesystem::path(tref).stem().string()));
            if (it.key == QStringLiteral("terrain_ref"))
                return tref.empty() ? tr("—") : QString::fromStdString(tref);
            // ---- 地形配置组 (地形工程 JSON) ----
            if (!has_ter) return tr("—");
            if (it.key == QStringLiteral("terrain_grid"))
                return QString::number(tj.value("grid", 0));
            if (it.key == QStringLiteral("terrain_extent"))
                return num(tj.value("extent", 0.0), 0);
            if (it.key == QStringLiteral("terrain_height_scale"))
                return num(tj.value("height_scale", 0.0), 1);
            if (it.key == QStringLiteral("terrain_seed"))
                return QString::number(tj.value("seed", 0));
            if (it.key == QStringLiteral("terrain_water"))
                return prop::waterTypeName(tj.value("water_type", 0));
            if (it.key == QStringLiteral("terrain_water_level"))
                return num(tj.value("water_level", 0.0), 2);
            return QString();
        };
        groupDefs = prop::groups(prop::NodeKind::Terrain);
        break;
    }
    case int(prop::NodeKind::Environment): {  // 环境
        valueOf = [this, &num](const prop::Item& it) -> QString {
            const auto& atm = scenario_data_.contains("atmosphere")
                ? scenario_data_["atmosphere"] : nlohmann::json::object();
            if (it.key == QStringLiteral("weather"))
                return QString::fromStdString(atm.value("weather_scenario", std::string("clarify")));
            if (it.key == QStringLiteral("severity"))
                return num(atm.value("weather_severity", 0.0) * 100.0, 0);
            if (atm.contains(it.key.toStdString()) && atm[it.key.toStdString()].is_number())
                return num(atm[it.key.toStdString()].get<double>());
            return QString();
        };
        groupDefs = prop::groups(prop::NodeKind::Environment);
        break;
    }
    case int(prop::NodeKind::Entity): {     // 实体 (配置 + 运行状态)
        if (sel_object_id_.isEmpty()) { tree_props_->clear(); return; }
        const nlohmann::json jo = findScenarioObject(sel_object_id_);
        if (jo.is_null()) { tree_props_->clear(); return; }

        // 运行时平台快照 (按名称匹配; 未创建平台则空)
        nlohmann::json plat = nullptr;
        if (platform_mgr_) {
            const auto arr = platform_mgr_->getAllPlatformsSnapshot();
            for (const auto& p : arr) {
                const std::string pname = p.value("name", std::string());
                if (pname == sel_object_id_.toStdString() ||
                    pname == jo.value("name", std::string()) ||
                    pname == jo.value("id", std::string()) ||
                    pname == jo.value("object_id", std::string())) { plat = p; break; }
            }
        }

        // 归属本实体的传感器数 (sensor_list.platform_object == object_id)
        int sensor_count = 0;
        if (scenario_data_.contains("sensors")) {
            const auto& sl = scenario_data_["sensors"].value("sensor_list", nlohmann::json::array());
            const std::string oid = sel_object_id_.toStdString();
            for (const auto& js : sl)
                if (js.value("platform_object", std::string()) == oid) ++sensor_count;
        }
        const auto& posj    = jo.contains("position") ? jo["position"] : nlohmann::json::array();
        const auto& phys    = jo.contains("physics") ? jo["physics"] : nlohmann::json::object();
        const int waypoint_count = int(jo.value("waypoints", nlohmann::json::array()).size());

        valueOf = [this, jo, plat, posj, phys, num, nm, sensor_count, waypoint_count, posStr](const prop::Item& it) -> QString {
            if (it.key==QStringLiteral("motion_authority"))
                return jo.value("motion_params",nlohmann::json::object()).value("motion_authority",std::string("dynamic"))=="kinematic" ? tr("规定轨迹") : tr("动力学");
            if (it.key == QStringLiteral("object_id")) return sel_object_id_;
            if (it.key == QStringLiteral("mesh_path"))
                return QString::fromStdString(
                    fs::path(jo.value("mesh_path", std::string())).filename().string());
            if (it.key == QStringLiteral("type"))
                return QString::fromStdString(jo.value("type", jo.value("class", std::string("—"))));
            if (it.key == QStringLiteral("faction"))
                return QString::fromStdString(jo.value("faction", std::string("—")));
            if (it.key == QStringLiteral("scale"))
                return jo.contains("scale") ? num(jo["scale"].get<double>()) : QString(tr("—"));
            if (it.key == QStringLiteral("position"))
                return posStr(posj, phys.value("initial_position", nlohmann::json::array()));
            if (it.key == QStringLiteral("yaw_deg")) {
                const auto& rot = phys.value("initial_rotation_deg", nlohmann::json::array());
                if (rot.is_array() && rot.size() >= 3) return num(rot[1].get<double>());
                return QString(tr("—"));
            }
            if (it.key == QStringLiteral("mass"))
                return phys.contains("mass") ? num(phys["mass"].get<double>(), 0) : QString(tr("—"));
            if (it.key == QStringLiteral("sensor_count")) return QString::number(sensor_count);
            if (it.key == QStringLiteral("waypoint_count")) return QString::number(waypoint_count);
            // ---- 运行状态组 ----
            if (it.key == QStringLiteral("runtime_alive"))
                return !plat.is_null() ? (plat.value("alive", false) ? tr("存活") : tr("摧毁")) : QString(tr("—"));
            if (it.key == QStringLiteral("runtime_position")) {
                if (!plat.is_null()) {
                    const auto& p = plat.value("position", nlohmann::json::array());
                    if (p.size() >= 3) return nm(p[0].get<double>(), p[1].get<double>(), p[2].get<double>());
                }
                return QString(tr("—"));
            }
            if (it.key == QStringLiteral("runtime_speed"))
                return !plat.is_null() ? num(plat.value("speed", 0.0), 1) + tr(" m/s") : QString(tr("—"));
            if (it.key == QStringLiteral("runtime_health"))
                return !plat.is_null() ? QString("%1%").arg(plat.value("health", 0.0) * 100.0, 0, 'f', 0) : QString(tr("—"));
            return QString();
        };

        groupDefs = prop::groups(prop::NodeKind::Entity);
        // 追加"运行状态"组: 仅在仿真软件叠加实时数据 (Source=Runtime)。
        prop::Group rt;
        rt.id = QStringLiteral("runtime");
        rt.title = tr("运行状态");
        rt.items = {
            {QStringLiteral("runtime_alive"),    tr("状态"),     prop::Type::Enum,   QString(), rt.id, int(prop::Source::Runtime), false},
            {QStringLiteral("runtime_position"), tr("实时位置"), prop::Type::Vec3,   QStringLiteral("m"), rt.id, int(prop::Source::Runtime), false},
            {QStringLiteral("runtime_speed"),    tr("速度"),     prop::Type::Number, QStringLiteral("m/s"), rt.id, int(prop::Source::Runtime), false},
            {QStringLiteral("runtime_health"),   tr("生命值"),   prop::Type::Number, QString(), rt.id, int(prop::Source::Runtime), false},
        };
        groupDefs.append(rt);
        break;
    }
    default:
        tree_props_->clear();
        return;
    }

    tree_props_->setGroups(groupDefs, valueOf);
}

// ---------------------------------------------------------------------------
// 覆写 BaseWindow 扩展点
// ---------------------------------------------------------------------------
void SimulationControlWindow::buildProductDocks()
{
    buildEntityTreeDock();              // 场景树 + 场景装载 (合并)
    buildScenePropertiesDock();         // 实体属性 (只读, 与场景设计软件一致)
    buildEquipmentMonitorDock();        // 装备在线状态工作监控 (状态+分发+日志合并)
    buildPerfMonitorDock();             // 性能监控

    // 布局不 Tab: 各侧 Dock 上下垂直堆叠, 避免单一侧标签页切换。
}

void SimulationControlWindow::buildProductMenuItems(QMenuBar* mb)
{
    auto* mSim = mb->addMenu(tr("&仿真"));
    auto* audio=mSim->addAction(tr("场景音效"));
    audio->setCheckable(true);
    connect(audio,&QAction::toggled,this,[this](bool on){
        preview_viewport_->widget()->setProperty("sceneAudioRequested",on);
        preview_viewport_->markActivity();
    });
    mSim->addAction(tr("&打开场景..."), this,
                    &SimulationControlWindow::onBrowseScenario,
                    QKeySequence::Open);
    mSim->addAction(tr("&初始化"), this,
                    &SimulationControlWindow::onInitSimulation);
    auto* workflow = mb->addMenu(tr("工作流程"));
    workflow->addAction(tr("在场景设计器中打开当前场景"), this, &SimulationControlWindow::onOpenDesigner);
    act_launch_display_ = workflow->addAction(tr("启动显控工作台"), this, &SimulationControlWindow::onLaunchDisplay);
    act_launch_display_->setEnabled(false);
    act_launch_display_->setStatusTip(tr("初始化完成后启动显控，选择装备并建立数据链"));
}

void SimulationControlWindow::buildProductToolBarItems(QToolBar* tb)
{
    tb->addAction(tr("打开场景"), this, &SimulationControlWindow::onBrowseScenario);
    tb->addAction(tr("初始化"), this, &SimulationControlWindow::onInitSimulation);
    if (act_launch_display_) tb->addAction(act_launch_display_);
    tb->addSeparator();
    act_record_ = tb->addAction(tr("● 录制数据"));
    act_record_->setCheckable(true);
    act_replay_ = tb->addAction(tr("▶ 回放数据"));
    tb->addSeparator();
    act_clear_log_  = tb->addAction(tr("清空日志"));
    act_export_log_ = tb->addAction(tr("导出日志"));

    connect(act_record_, &QAction::toggled,
            this, &SimulationControlWindow::onToggleRecord);
    connect(act_replay_, &QAction::triggered,
            this, &SimulationControlWindow::onReplayData);
    connect(act_clear_log_, &QAction::triggered,
            this, &SimulationControlWindow::onClearLog);
    connect(act_export_log_, &QAction::triggered,
            this, &SimulationControlWindow::onExportLog);
}

void SimulationControlWindow::onLaunchDisplay()
{
    if (!sim_inited_ || !service_online_) {
        statusBar()->showMessage(tr("请先成功初始化场景，确认装备发现服务在线"), 5000);
        return;
    }
    if (ui::ProductWorkflow::launch(this, QStringLiteral("vsgraysim_control"), {}, tr("显控工作台")))
        statusBar()->showMessage(tr("显控已启动。选择在线装备并建链，再播放仿真。"), 7000);
}

void SimulationControlWindow::onOpenDesigner()
{
    if (scenario_path_.isEmpty()) {
        statusBar()->showMessage(tr("请先打开场景文件"), 4000);
        return;
    }
    // Scene designer's positional contract is <terrain-template> <scenario>.
    ui::ProductWorkflow::launch(this, QStringLiteral("vsgraysim_scene_designer"),
        {QString(), QFileInfo(scenario_path_).absoluteFilePath()}, tr("场景设计器"));
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
std::string SimulationControlWindow::resolveConfigPath(const QString& p) const
{
    namespace fs = std::filesystem;
    const std::string s = p.toStdString();
    if (s.empty() || fs::exists(s)) return s;
    const fs::path exe = QCoreApplication::applicationDirPath().toStdString();
    for (const fs::path& base : {exe, exe.parent_path()}) {
        const fs::path c = base / s;
        if (fs::exists(c)) return c.string();
    }
    return s;
}

void SimulationControlWindow::onBrowseScenario()
{
    vsgraysim::Workspace ws(resolveWorkspaceRoot().toStdString());
    const QString p = QFileDialog::getOpenFileName(
        this, tr("选择场景文件"),
        QString::fromStdString(ws.sceneDir()),
        tr("场景文件 (*.json *.vsimscene)"));
    if (!p.isEmpty()) loadScenarioFile(p);
}

void SimulationControlWindow::updateSim3DVisuals()
{
    if(voxel_source_)return;
    if (!preview_viewport_ || scenario_data_.empty() || scenario_data_.value("format",std::string())=="earth-scene-topic-v1") return;
    const auto platforms = platform_mgr_ ? platform_mgr_->getAllPlatformsSnapshot()
                                         : nlohmann::json::array();
    const auto objects = scenario_data_.value("objects", nlohmann::json::array());
    const auto sensors = scenario_data_.value("sensors", nlohmann::json::object())
                                  .value("sensor_list", nlohmann::json::array());
    const auto facInt = [](const std::string& f) -> int {
        if (f == "red" || f.find("红") != std::string::npos) return 2;
        if (f == "blue" || f.find("蓝") != std::string::npos) return 1;
        return 0;
    };
    const auto facText = [](int f) -> std::string {
        return f == 2 ? "红方" : f == 1 ? "蓝方" : "中立";
    };
    const auto catText = [](const std::string& c) -> std::string {
        if (c == "Missile")       return "导弹";
        if (c == "Aircraft")      return "飞机";
        if (c == "GroundVehicle") return "坦克";
        if (c == "NavalVessel")   return "舰船";
        if (c == "Static")        return "设施";
        return "装备";
    };

    std::vector<viewport3d::RelationshipVisualFeature::Marker> markers;
    std::vector<viewport3d::DetectionEffectFeature::Effect>    fx;

    // 交战快照前置 (锁定状态梯度 + 导引头特效需要)。
    const auto era = platform_mgr_ ? platform_mgr_->engagementsSnapshot()
                                   : nlohmann::json::array();

    // 构建 name → 世界位置/速度方向的索引 (仅存活平台参与交战/轨迹)。
    std::map<std::string, vsg::vec3> plat_pos;
    std::map<std::string, vsg::dvec3> plat_vel;
    std::map<std::string, vsg::dquat> plat_rotation;
    for (const auto& p : platforms) {
        if (!p.value("alive", true)) continue;
        const auto& pp = p["position"];
        if (pp.size() < 3) continue;
        const std::string nm = p.value("name", std::string());
        plat_pos[nm] = vsg::vec3(
            float(pp[0].get<double>()), float(pp[1].get<double>()),
            float(pp[2].get<double>()));
        const auto qr=p.value("rotation",nlohmann::json::array());
        plat_rotation[nm]=qr.size()==4 ? simcore::normalized(vsg::dquat(
            qr[0].get<double>(),qr[1].get<double>(),qr[2].get<double>(),qr[3].get<double>())) :
            simcore::headingRotation(p.value("yaw",0.0),p.value("pitch",0.0),p.value("roll",0.0));
        if (p.contains("velocity") && p["velocity"].is_array() &&
            p["velocity"].size() >= 3) {
            const auto& vv = p["velocity"];
            plat_vel[nm] = {vv[0].get<double>(),vv[1].get<double>(),vv[2].get<double>()};
        }
    }
    // 交战/平台 → 阵营 (匹配场景对象; 查不到攻击方默认红 2)。
    const auto factionOf = [&](const std::string& nm) -> int {
        for (const auto& o : objects)
            if (o.value("object_id", std::string()) == nm ||
                o.value("name", std::string()) == nm ||
                o.value("id", std::string()) == nm)
                return facInt(o.value("faction", std::string("")));
        return 2;
    };

    for (const auto& p : platforms) {
        const std::string pname = p.value("name", std::string());
        if (!p.value("alive", true)) continue;
        // 匹配场景对象 (取阵营 / 初始航向 / 传感器参数)。
        nlohmann::json jo = nlohmann::json::object();
        for (const auto& o : objects)
            if (o.value("object_id", std::string()) == pname ||
                o.value("name", std::string()) == pname ||
                o.value("id", std::string()) == pname) { jo = o; break; }
        const auto& pos = p["position"];
        const vsg::dvec3 wpos(pos[0].get<double>(), pos[1].get<double>(),
                              pos[2].get<double>());
        const int fi = facInt(jo.value("faction", std::string("")));
        const std::string cat = p.value("category", std::string());

        // 平台标注 (红方导弹 / 蓝方坦克 / 蓝方飞机 ...)。
        viewport3d::RelationshipVisualFeature::Marker m;
        m.id = pname;
        m.pos = vsg::vec3(float(wpos.x), float(wpos.y), float(wpos.z));
        m.faction = fi;
        m.label = facText(fi) + catText(cat);
        markers.push_back(m);

        // 探测特效: 范围随健康状态动态缩放 (性能衰减 → 探测距离缩短)。
        const double health = std::max(0.0, p.value("health", 1.0));
        const double range_scale = 0.5 + 0.5 * health;
        const double yaw0=simcore::headingAngles(plat_rotation[pname]).x;
        for (const auto& js : sensors) {
            if (js.value("platform_object", std::string()) != pname) continue;
            const auto ap = js.value("aperture", nlohmann::json::object());
            viewport3d::DetectionEffectFeature::Effect e;
            e.entity_id = pname;
            e.sensor_idx = int(fx.size());
            e.pos = wpos;
            e.yaw = yaw0;
            // 方位/俯仰: 优先显式 azimuth_deg/elevation_deg, 否则由
            // aperture.direction 换算 (与 dirFromAzEl 约定一致: az 绕 +Y)。
            const auto dirAzEl = [&]() -> std::pair<double, double> {
                if (ap.contains("direction")) {
                    const auto& d = ap["direction"];
                    if (d.is_array() && d.size() >= 3 &&
                        d[0].is_number() && d[1].is_number() && d[2].is_number()) {
                        const double dx = d[0].get<double>();
                        const double dy = d[1].get<double>();
                        const double dz = d[2].get<double>();
                        const double len = std::sqrt(dx*dx + dy*dy + dz*dz);
                        if (len > 1e-6)
                            return {std::atan2(dx/len, -dz/len) * 180.0 / M_PI,
                                    std::asin(std::clamp(dy/len, -1.0, 1.0)) * 180.0 / M_PI};
                    }
                }
                return {0.0, 0.0};
            };
            const auto [az_, el_] = dirAzEl();
            e.az  = ap.contains("azimuth_deg")  ? ap.value("azimuth_deg", 0.0)  : az_;
            e.el  = ap.contains("elevation_deg") ? ap.value("elevation_deg", 0.0) : el_;
            e.fov_h = ap.value("fov_horizontal", 60.0);
            e.fov_v = ap.value("fov_vertical", 30.0);
            e.range = ap.value("range_m", 8000.0) * range_scale;
            e.color = viewport3d::factionColor(
                fi == 2 ? viewport3d::Faction::Red
                : fi == 1 ? viewport3d::Faction::Blue
                : viewport3d::Faction::Neutral);
            // 锁定状态梯度 (搜索→跟踪→锁定):
            //  制导雷达: 本方有在飞交战=锁定(指向目标), 已指派未发射=跟踪;
            //  搜索雷达: 敌方存活目标进入探测范围=跟踪。
            const std::string role = js.value("role", std::string("search"));
            e.lock_state = viewport3d::DetectionEffectFeature::Effect::LockState::Search;
            if (role == "guidance") {
                for (const auto& ge : era) {
                    if (factionOf(ge.value("interceptor_name", std::string())) != fi)
                        continue;
                    if (ge.value("in_flight", false)) {
                        const auto tp2 = plat_pos.find(
                            ge.value("target_name", std::string()));
                        e.lock_state = viewport3d::DetectionEffectFeature::Effect::LockState::Lock;
                        if (tp2 != plat_pos.end())
                            e.track_az = std::atan2(tp2->second.x - e.pos.x,
                                                   tp2->second.z - e.pos.z) *
                                        180.0 / M_PI;
                        break;
                    }
                    if (ge.value("launched", false) || !ge.value("impacted", false))
                        e.lock_state = viewport3d::DetectionEffectFeature::Effect::LockState::Track;
                }
            } else {
                for (const auto& op : platforms) {
                    if (!op.value("alive", true)) continue;
                    const std::string onm = op.value("name", std::string());
                    const int ofi = factionOf(onm);
                    if (ofi == fi || ofi == 0) continue;   // 只看敌方
                    const auto& opp = op["position"];
                    if (opp.size() < 3) continue;
                    const double dx = opp[0].get<double>() - wpos.x;
                    const double dy = opp[1].get<double>() - wpos.y;
                    const double dz = opp[2].get<double>() - wpos.z;
                    if (std::sqrt(dx * dx + dy * dy + dz * dz) <= e.range) {
                        e.lock_state = viewport3d::DetectionEffectFeature::Effect::LockState::Track;
                        break;
                    }
                }
            }
            fx.push_back(e);
        }

        // 导引头探测范围: 在飞导弹按 motion_params.seeker_range 显示,
        // 视轴随速度方向 (导引中), 锁定态橙红高亮。
        if (cat == "Missile") {
            for (const auto& ge : era) {
                if (ge.value("interceptor_name", std::string()) != pname) continue;
                if (!ge.value("in_flight", false)) break;
                viewport3d::DetectionEffectFeature::Effect e;
                e.entity_id = pname;
                e.sensor_idx = int(fx.size());
                e.pos = wpos;
                // 视轴: 优先速度方向, 回退指向交战目标。
                double az_d = 0.0, el_d = 0.0;
                if (p.contains("velocity") && p["velocity"].is_array() &&
                    p["velocity"].size() >= 3) {
                    const auto& vv = p["velocity"];
                    const double vx = vv[0].get<double>(), vy = vv[1].get<double>(),
                                 vz = vv[2].get<double>();
                    const double hl = std::sqrt(vx * vx + vy * vy + vz * vz);
                    if (hl > 0.5) {
                        az_d = std::atan2(vx, vz) * 180.0 / M_PI;
                        el_d = std::asin(std::clamp(vy / hl, -1.0, 1.0)) * 180.0 / M_PI;
                    }
                }
                e.yaw = 0.0; e.az = az_d; e.el = el_d;
                e.fov_h = 12.0; e.fov_v = 12.0;          // 导引头窄视场
                const auto mp = jo.value("motion_params", nlohmann::json::object());
                e.range = mp.value("seeker_range", 15000.0);
                e.color = viewport3d::factionColor(
                    fi == 2 ? viewport3d::Faction::Red
                    : fi == 1 ? viewport3d::Faction::Blue
                    : viewport3d::Faction::Neutral);
                e.volume_shape = viewport3d::DetectionEffectFeature::Effect::VolumeShape::SectorHemisphere;
                e.rt_occlusion = false;                  // 空中导引头: 免地形遮挡查询
                e.scan_animation = false;                // 导引中不扫描
                e.lock_state = viewport3d::DetectionEffectFeature::Effect::LockState::Lock;
                e.track_az = az_d;
                fx.push_back(e);
                break;
            }
        }
    }

    if (auto* rv = preview_viewport_->featureAs<
            viewport3d::RelationshipVisualFeature>("relationship_visual"))
        rv->setGraph(markers);
    if (auto* det = preview_viewport_->featureAs<
            viewport3d::DetectionEffectFeature>("detection_effect"))
        det->setEffects(fx);

    // 视锥跟随: 平台载传感器视锥随实时位姿刷新 (位置随平台,
    // 视轴方向随航向旋转), 替代场景装载时的静态视锥。
    {
        std::vector<viewport3d::SensorFovFeature::WorldFov> fovs;
        for (const auto& js : sensors) {
            const std::string plat = js.value("platform_object", std::string());
            const auto pp = plat_pos.find(plat);
            if (pp == plat_pos.end()) continue;   // 平台不在场/被摧毁: 视锥消失
            const auto ap = js.value("aperture", nlohmann::json::object());
            vsg::dvec3 off(0, 2.0, 0), dir(0, 0, 1);
            const auto po = ap.value("position_offset", nlohmann::json::array());
            if (po.is_array() && po.size() == 3)
                off = vsg::dvec3(po[0].get<double>(), po[1].get<double>(),
                                 po[2].get<double>());
            const auto dr = ap.value("direction", nlohmann::json::array());
            if (dr.is_array() && dr.size() == 3)
                dir = vsg::dvec3(dr[0].get<double>(), dr[1].get<double>(),
                                 dr[2].get<double>());
            const auto rotation=plat_rotation[plat];
            viewport3d::SensorFovFeature::WorldFov f;
            f.origin = vsg::dvec3(pp->second) + rotation*off;
            f.fwd = rotation*dir;
            f.fov_h = ap.value("fov_horizontal", 60.0);
            f.fov_v = ap.value("fov_vertical", 30.0);
            f.range = std::max(1.0, ap.value("range_m", 8000.0));
            f.color = gui::ViewportKit::sensorTypeColor(
                js.value("type", std::string("radar")));
            fovs.push_back(f);
        }
        if (auto* fovf = preview_viewport_->featureAs<
                viewport3d::SensorFovFeature>("sensor_fov"))
            fovf->setWorldFovs(fovs);
    }

    // ---- 交战过程视觉 (制导连线 + 弹道 + 命中爆炸 + 摧毁残骸) ----
    // ---- 实体运动轨迹拖尾 ----
    if (!sim_facade_) return;

    std::vector<viewport3d::EngagementVisualFeature::Engagement> engs;
    std::vector<viewport3d::EngagementVisualFeature::Plume> plumes;
    std::vector<viewport3d::EngagementVisualFeature::DamageSmoke> dmg_smokes;
    // Trigger the airframe burst only on an observed alive -> destroyed edge.
    // Opening a paused scene must not replay explosions from an existing wreck.
    for(const auto& aircraft:platforms) {
        if(aircraft.value("category",std::string{})!="Aircraft")continue;
        const auto id=aircraft.value("platform_id",uint32_t(0));
        if(id==0)continue;
        const bool alive=aircraft.value("alive",true);
        auto [previous,first]=aircraft_fx_alive_.try_emplace(id,alive);
        if(!first&&previous->second&&!alive) {
            const auto pos=aircraft.find("position");
            if(pos!=aircraft.end()&&pos->is_array()&&pos->size()==3)
                sim_facade_->addImpact({pos->at(0).get<float>(),pos->at(1).get<float>(),pos->at(2).get<float>()},0);
        }
        previous->second=alive;
    }
    // Particle presentation consumes existing platform truth only. At
    // ordinary thrust the clean jet has no luminous plume.
    for(const auto& aircraft:platforms) {
        if(aircraft.value("category",std::string{})!="Aircraft"||!aircraft.value("alive",true))continue;
        const auto name=aircraft.value("name",std::string{});
        const auto pos=plat_pos.find(name);
        if(pos==plat_pos.end())continue;
        const auto object=std::find_if(objects.begin(),objects.end(),[&](const auto& o){
            return o.value("object_id",std::string{})==name||o.value("name",std::string{})==name||o.value("id",std::string{})==name;
        });
        if(object==objects.end())continue;
        const auto emitter=object->find("visual_jet_exhaust");
        if(emitter==object->end()||!emitter->is_object())continue;
        const auto offset=emitter->value("nozzle_local_m",nlohmann::json::array());
        if(!offset.is_array()||offset.size()!=3||!offset[0].is_number()||!offset[1].is_number()||!offset[2].is_number())continue;
        const auto inputs=aircraft.value("flight_control",nlohmann::json::object()).value("actual",nlohmann::json::object());
        const auto throttle=inputs.value("throttle",0.);
        if(throttle<=.73)continue;
        const auto attitude=plat_rotation[name];
        plumes.push_back({"jet:"+name,pos->second+vsg::vec3(attitude*vsg::dvec3(offset[0].get<double>(),offset[1].get<double>(),offset[2].get<double>())),
            vsg::vec3(attitude*vsg::dvec3(0,0,-1)),factionOf(name),true,
            float(std::clamp((throttle-.73)/.27,0.,1.)),.54f});
    }
    // 损伤冒烟: 健康度 < 0.6 的存活平台持续冒烟 (损伤越重烟越大)。
    for (const auto& p : platforms) {
        if (!p.value("alive", true)) continue;
        const double hp = p.value("health", 1.0);
        if (hp >= 0.6) continue;
        const auto nm = p.value("name", std::string());
        const auto pp = plat_pos.find(nm);
        if (pp == plat_pos.end()) continue;
        viewport3d::EngagementVisualFeature::DamageSmoke ds;
        ds.id = nm;
        ds.pos = pp->second;
        ds.severity = float(std::clamp(1.0 - hp, 0.0, 1.0));
        dmg_smokes.push_back(ds);
    }
    for (const auto& e : era) {
        const std::string iname = e.value("interceptor_name", std::string());
        const std::string tname = e.value("target_name", std::string());
        const bool launched   = e.value("launched", false);
        const bool inflight   = e.value("in_flight", false);
        const bool impacted   = e.value("impacted", false);
        const bool tdestroyed = e.value("target_destroyed", false);

        auto ip = plat_pos.find(iname);
        auto tp = plat_pos.find(tname);

        // 发射瞬间: 起推闪光 + 扬尘 + 烟柱 (幂等, 位置取拦截弹当前位置)。
        if (launched && !launch_handled_.count(iname)) {
            launch_handled_.insert(iname);
            if (ip != plat_pos.end())
                sim_facade_->addLaunch(ip->second, factionOf(iname));
        }
        // 在飞: 制导连线 + 弹道累积 + 尾焰羽流。
        if (inflight && ip != plat_pos.end()) {
            if(tp != plat_pos.end()) {
                viewport3d::EngagementVisualFeature::Engagement en;
                en.interceptor_id   = iname;
                en.target_id        = tname;
                en.interceptor_pos  = ip->second;
                en.target_pos       = tp->second;
                en.faction          = factionOf(iname);
                engs.push_back(en);
            }
            viewport3d::EngagementVisualFeature::Plume pl;
            pl.id      = iname;
            pl.pos     = ip->second;
            auto vd    = plat_vel.find(iname);
            if (vd!=plat_vel.end() && vsg::length(vd->second)>1e-3)
                pl.dir=vsg::vec3(vsg::normalize(vd->second));
            pl.faction = factionOf(iname);
            plumes.push_back(pl);
        }
        // 命中: 一次性爆炸 (幂等), 爆炸点取拦击弹命中位置。
        if (impacted && (ip != plat_pos.end() || tp != plat_pos.end()) && !impact_handled_.count(iname)) {
            impact_handled_.insert(iname);
            const vsg::vec3 pos = (ip != plat_pos.end()) ? ip->second
                                 : (tp != plat_pos.end()) ? tp->second
                                                          : vsg::vec3(0, 0, 0);
            sim_facade_->addImpact(pos, factionOf(iname));
            // 命中但未摧毁 → 脱靶标注 (落点 X 标记)。
            if (!tdestroyed && !miss_handled_.count(iname)) {
                miss_handled_.insert(iname);
                sim_facade_->addMiss(pos, factionOf(iname));
            }
        }
        // 击毁靶机: 在场留灰色残骸。
        if (tdestroyed && tp != plat_pos.end())
            sim_facade_->addWreck(tname, tp->second, factionOf(tname));
    }
    sim_facade_->setEngagements(engs);
    sim_facade_->setPlumes(plumes);
    sim_facade_->setDamageSmokes(dmg_smokes);

    // Use simulation time for visual history so a paused world adds no samples.
    const double trailTime=sim_time_.load();
    for (const auto& p : platforms) {
        if (!p.value("alive",true)) continue;
        const auto& point=p.at("position");
        if (!point.is_array() || point.size()<3) continue;
        const auto name=p.value("name",std::string{});
        const auto id=p.value("platform_id",uint32_t(0));
        const auto velocity=plat_vel.find(name);
        sim_facade_->addTrailPoint(id?std::to_string(id):name,{point[0].get<double>(),point[1].get<double>(),point[2].get<double>()},
            factionOf(name),trailTime,velocity==plat_vel.end()?std::nullopt:std::optional<vsg::dvec3>(velocity->second));
    }

    // ---- 位姿桥接: 平台快照位姿 → 3D 实体节点 (playback 外部驱动口) ----
    // 平台以 name 为键, 3D 实体以 object_id 注册, 此处按场景对象多字段匹配。
    for (const auto& p : platforms) {
        if (!p.value("alive", true)) continue;
        const auto& pp = p["position"];
        if (!pp.is_array() || pp.size() < 3) continue;
        const std::string nm = p.value("name", std::string());
        std::string oid = nm;
        for (const auto& o : objects) {
            if (o.value("object_id", std::string()) == nm ||
                o.value("name", std::string()) == nm ||
                o.value("id", std::string()) == nm) {
                oid = o.value("object_id", nm);
                break;
            }
        }
        if (p.contains("rotation") && p["rotation"].size()==4) {
            const auto& q=p["rotation"];
            sim_facade_->updateEntityPose(QString::fromStdString(oid),
                {pp[0].get<double>(),pp[1].get<double>(),pp[2].get<double>()},
                vsg::dquat(q[0].get<double>(),q[1].get<double>(),q[2].get<double>(),q[3].get<double>()));
        } else sim_facade_->updateEntityPose(QString::fromStdString(oid),
            pp[0].get<double>(),pp[1].get<double>(),pp[2].get<double>(),p.value("yaw",0.0));
    }
}

void SimulationControlWindow::setFlowStage(int stage)
{
    for (int i = 0; i < 3; ++i) {
        if (!lbl_flow_[i]) continue;
        const bool active = (i == stage);
        const bool done   = (i < stage);
        lbl_flow_[i]->setStyleSheet(
            active ? "QLabel { background:#2d6fb8; color:white; border-radius:4px;"
                     "padding:4px; font-weight:bold; }"
            : done ? "QLabel { color:#54c46a; padding:4px; font-weight:bold; }"
                   : "QLabel { color:#5d6675; padding:4px; }");
    }
}

void SimulationControlWindow::buildScenarioSummary(const QString& path)
{
    Q_UNUSED(path);   // 场景概要树已并入「场景树(只读)」, 本方法不再使用。
}

void SimulationControlWindow::onInitSimulation() try
{
    if(scenario_data_.value("format",std::string())=="earth-scene-topic-v1" && !std::getenv("VSGRAYSIM_INPROCESS_REFERENCE")){
        startVoxelWorker(scenario_path_);return;
    }
    if(scenario_data_.value("format",std::string())=="earth-scene-topic-v1"&&!preview_viewport_->isReady()){
        pending_voxel_initialization_=true;statusBar()->showMessage(tr("等待三维设备就绪，再初始化体素装备"));return;
    }
    pending_voxel_initialization_=false;
    if(!engine_worker_){BrokerNet::instance().stop();BrokerNet::instance().startSimRole();BrokerNet::instance().setLinkGate([this](const std::set<uint32_t>& linked){sim_.setSensorGate(true,linked);});}
    if (replay_active_) stopReplay(false);   // 重新初始化前中止回放
    if (recording_)     stopRecording();     // 重新初始化前结束录制
    playing_ = false;
    btn_play_->setText(tr("▶ 播放"));

    // Tear down controllers while their old physics world still exists.
    // Keep reload, scene mounts and kernel construction under the worker lock.
    std::unique_lock<std::mutex> reinitializing(sim_mtx_);
    sim_inited_=false;
    {std::lock_guard lock(earth_view_mutex_); pending_earth_view_.reset();pending_voxel_view_.reset();}
    sim_.setEarthCommitHandler({});
    sim_.setEarthViewPreparation({});
    backend_description_.clear();
    if(sb_earth_)sb_earth_->setText(tr("地表：未装订"));
    if (bus_) bus_->stop();
    bus_.reset(); data_source_.reset(); platform_mgr_.reset();voxel_source_.reset();
    sim_.stop();
    setServiceOnline(false);
    if(preview_viewport_ && preview_viewport_->context().window())
        sim_.setSensorDevice(preview_viewport_->context().window()->getOrCreateDevice());

    // 重新初始化: 清空装备注册表 (新场景的装备在首帧后重新注册)
    EquipmentRegistry::instance().clear();
    BrokerNet::instance().clearLinks();   // 场景切换: 断开全部显控链接并关门
    BrokerNet::instance().setPlatformTelemetry("[]");
    equipment_registered_ = false;

    // 优先使用场景装载路径(已装载), 其次传统目标配置路径。
    QString obj_path_q = scenario_path_;
    if (obj_path_q.isEmpty() && edt_object_) obj_path_q = edt_object_->text();
    const std::string obj_path = resolveConfigPath(obj_path_q);

    if(scenario_data_.value("format",std::string())=="earth-scene-topic-v1") {
        voxel_source_=std::make_shared<VoxelEquipmentSource>(VoxelSceneProject::open(obj_path),preview_viewport_->context().window()->getDevice());
        buildTimingBus();sim_inited_=true;playing_=false;
        const auto initial=voxel_source_->snapshot();sim_time_=initial->timeSeconds();frames_=0;
        {std::lock_guard lock(earth_view_mutex_);pending_voxel_view_=initial;}
        backend_description_=tr("GPU 体素多装备 · 正式仿真源");equipment_registered_=true;
        setPlaybackEnabled(true);setFlowStage(1);setServiceOnline(true);
        if(act_record_)act_record_->setEnabled(false);if(act_replay_)act_replay_->setEnabled(false);
        if(sb_earth_)sb_earth_->setText(tr("台北新数据 · 体素真值"));
        updateStatusLabels();updateEquipmentTable();
        statusBar()->showMessage(tr("体素场景和装备已上线；在原显控中发现并链接后播放"),6000);return;
    }
    if(act_record_)act_record_->setEnabled(true);if(act_replay_)act_replay_->setEnabled(true);

    // 场景文件 (磁盘对接产物, 含 "objects"): 一步装载全部配置。
    {
        std::ifstream probe_in(obj_path);
        nlohmann::json probe;
        bool is_scenario = false;
        try {
            if (probe_in) { probe_in >> probe; is_scenario = probe.is_object() && probe.contains("objects"); }
        } catch (const std::exception&) { is_scenario = false; }
        if (is_scenario) {
            earth::EarthSurfaceSnapshotPtr previewSurface;
            sim_.setEarthSurfaceOverride({});
            if(preview_viewport_) {
                if(auto* globe=preview_viewport_->featureAs<viewport3d::EarthView>(viewport3d::EarthView::kId)) {
                    std::string error;
                    if(!globe->loadEarthScenario(probe,obj_path,error))
                        throw std::runtime_error("Earth initialization failed: "+error);
                    previewSurface = globe->surfaceSnapshot();
                    sim_.setEarthSurfaceOverride(previewSurface);
                }
            }
            if (sim_.initFromScenario(obj_path)) {
                if (previewSurface && sim_.earthSurface() != previewSurface) {
                    sim_.stop();
                    throw std::runtime_error("Earth source changed during scene initialization");
                }
                sim_inited_ = true;
                backend_description_ = sim_.gpuSensorBackendActive() ? tr("GPU 测量") : tr("CPU 参考");
                setPlaybackEnabled(true);
                setFlowStage(1);
                sim_time_ = 0.0;
                if (preview_viewport_) preview_viewport_->context().setSimulationTime(0.0);
                frames_ = 0;
                buildTimingBus();
                registerEquipmentFromScenario();  // 发现面: 装载即注册 (无需等首帧)
                spdlog::info("SimulationControl: scenario backend initialised ('{}')", obj_path);
                statusBar()->showMessage(tr("场景装载成功，仿真管线已初始化"), 3000);
                if (log_panel_)
                    log_panel_->info(tr("场景装载成功, 仿真管线已初始化"));
            } else {
                sim_inited_ = false;
                setPlaybackEnabled(false);
                if (log_panel_)
                    log_panel_->error(tr("场景初始化失败, 请检查场景文件"));
                QMessageBox::warning(this, tr("装载失败"),
                                     tr("场景初始化失败，请检查场景文件。"));
            }
            updateStatusLabels();
            return;
        }
    }

    // 传统三件套配置路径。
    SimulationConfig cfg;
    cfg.headless = true;                 // GUI owns no VSG viewer here
    cfg.per_frame_detail = false;
    cfg.window_width  = width();
    cfg.window_height = height();
    cfg.max_frames = 0;
    cfg.object_config_path     = obj_path;
    cfg.sensor_config_path     = edt_sensor_ ? resolveConfigPath(edt_sensor_->text())
                                                              : std::string();
    cfg.atmosphere_config_path = edt_atm_    ? resolveConfigPath(edt_atm_->text())
                                                              : std::string();
    cfg.shader_dir             = resolveConfigPath("shaders");

    sim_time_ = 0.0;
    if (preview_viewport_) preview_viewport_->context().setSimulationTime(0.0);
    frames_ = 0;

    if (sim_.init(cfg)) {
        sim_inited_ = true;
        backend_description_ = sim_.gpuSensorBackendActive() ? tr("GPU 测量") : tr("CPU 参考");
        setPlaybackEnabled(true);
        setFlowStage(1);
        buildTimingBus();
        spdlog::info("SimulationControl: backend initialised");
        statusBar()->showMessage(tr("仿真后端已初始化"), 3000);
        if (log_panel_) log_panel_->info(tr("仿真后端已初始化"));
    } else {
        sim_inited_ = false;
        setPlaybackEnabled(false);
        if (log_panel_)
            log_panel_->error(tr("仿真后端初始化失败, 请检查配置文件路径"));
        QMessageBox::warning(this, tr("初始化失败"),
                             tr("仿真后端初始化失败，请检查配置文件路径。"));
    }
    updateStatusLabels();
} catch (const std::exception& e) {
    std::lock_guard<std::mutex> lock(sim_mtx_);
    sim_inited_=false; playing_=false;
    backend_description_.clear();
    setServiceOnline(false);
    if (bus_) bus_->stop();
    setPlaybackEnabled(false);
    if (log_panel_) log_panel_->error(tr("初始化失败: %1").arg(e.what()));
    statusBar()->showMessage(tr("初始化失败: %1").arg(e.what()),5000);
}

void SimulationControlWindow::loadScenarioFile(const QString& path)
{
    if((path.endsWith(".vtopic.json",Qt::CaseInsensitive)||path.endsWith(".vsimscene",Qt::CaseInsensitive))&&!std::getenv("VSGRAYSIM_INPROCESS_REFERENCE")) {
        engine_worker_.reset();worker_view_generation_.clear();
        scenario_path_=QFileInfo(path).absoluteFilePath();scenario_data_={{"format","earth-scene-topic-v1"}};setWindowFilePath(scenario_path_);
        startVoxelWorker(scenario_path_);return;
    }
    nlohmann::json candidate;
    try {
        std::ifstream in(path.toStdString());
        if (!in) throw std::runtime_error("cannot open scenario file");
        in >> candidate;
        if (!candidate.is_object()) throw std::runtime_error("scenario must be a JSON object");
        ScenarioClockConfig::fromJson(candidate.value("simulation",nlohmann::json::object()));
    } catch (const std::exception& e) {
        if (log_panel_) log_panel_->error(tr("场景读取失败: %1").arg(e.what()));
        statusBar()->showMessage(tr("场景读取失败: %1").arg(e.what()),5000);
        return;
    }
    if(candidate.value("format",std::string())=="earth-scene-topic-v1"||candidate.value("format",std::string())=="vsgraysim-scene-package-v1") {
        if(!std::getenv("VSGRAYSIM_INPROCESS_REFERENCE")){scenario_path_=QFileInfo(path).absoluteFilePath();scenario_data_={{"format","earth-scene-topic-v1"}};startVoxelWorker(scenario_path_);return;}
        try {
            const auto project=VoxelSceneProject::open(path.toStdString());project.equipmentConfig();
            auto config=viewport3d::ViewportConfig::fromTopicFile(project.path,"earth_display");
            config.remove("voxel_scene");nlohmann::json external={{"external_only",true}};
            if(project.packageManifest.contains("terrain_pages"))external["terrain_display_paging"]=true;
            if(project.document.contains("camera"))external["camera_local"]=project.document.at("camera");config.add("voxel_scene",external);config.add(viewport3d::ScenePlatformMotion::kId);
            playing_=false;{std::lock_guard lock(sim_mtx_);sim_inited_=false;}
            scenario_path_=QString::fromStdString(project.path);scenario_data_=project.document;setWindowFilePath(scenario_path_);
            preview_viewport_->applyConfig(config);preview_viewport_->markActivity();
            setFlowStage(0);onInitSimulation();return;
        }catch(const std::exception& error){statusBar()->showMessage(tr("体素场景装载失败：%1").arg(QString::fromUtf8(error.what())));if(log_panel_)log_panel_->error(QString::fromUtf8(error.what()));return;}
    }
    engine_worker_.reset();preview_viewport_->applyConfig(viewport3d::ViewportConfig::fromPreset("simulation_display"));
    scenario_path_ = QFileInfo(path).absoluteFilePath();
    setWindowFilePath(scenario_path_);
    if (edt_object_) edt_object_->setText(scenario_path_);
    scenario_data_ = std::move(candidate);
    setFlowStage(0);
    refreshEntityTree(scenario_data_);
    updateSceneProperties();
    if (preview_viewport_) updateSim3DVisuals();   // 首次装载即刷新 3D 探测特效 + 平台标注
    // 3D 预览: 门面内部构建 (实体+地形+视锥; geo_anchor 重锚在门面装载路径);
    // vsgReady 前自动排队。
    if (sim_facade_) {
        sim_facade_->clearEngagements();   // 重置交战/爆炸/残骸
        sim_facade_->clearTrails();        // 重置轨迹拖尾
        sim_facade_->loadScenarioFile(path, resolveWorkspaceRoot());
    }
    impact_handled_.clear();
    aircraft_fx_alive_.clear();
    launch_handled_.clear();
    miss_handled_.clear();
    onInitSimulation();
}

// W2: 把场景 JSON 里的细粒度大气参数还原下发到 preview_viewport_ 的 rocky_env 模块。
// 场景设计软件导出的 atmosphere JSON 含 cloud_cover/cloud_base/rain/snow/visibility/
// wind_speed/wind_az/sun_el/sun_az/wave_height; 此处在视口就绪后经 vsgReady 一次性下发。
// 视口特性未挂载时各 featureAs 返回 nullptr, setAtmosphereParams 为空操作, 安全跳过。
void SimulationControlWindow::applyAtmosphereFromScenario()
{
    if (!preview_viewport_ || scenario_data_.empty()) return;
    const auto& atm = scenario_data_.contains("atmosphere")
        ? scenario_data_["atmosphere"] : nlohmann::json::object();

    nlohmann::json p;
    const auto clock=ScenarioClockConfig::fromJson(scenario_data_.value("simulation",nlohmann::json::object()));
    const auto epoch=simcore::parseUtcEpoch(clock.epoch_utc);
    p["year"]=epoch.year; p["month"]=epoch.month; p["day"]=epoch.day; p["hours_utc"]=epoch.hours();
    p["cloud_cover"] = atm.value("cloud_cover", 0.5);
    p["cloud_base"]  = atm.value("cloud_base", 1200.0);
    p["stratus_altitude"] = atm.value("cloud_base", 1200.0);
    p["rain"]        = atm.value("rain", 0.0);
    p["snow"]        = atm.value("snow", 0.0);
    p["visibility"]  = atm.value("visibility", 10000.0);
    p["wind_speed"]  = atm.value("wind_speed", 6.0);
    p["wind_az"]     = atm.value("wind_az", 80.0);
    p["sun_el"]      = atm.value("sun_el", 62.0);
    p["sun_az"]      = atm.value("sun_az", 175.0);
    p["wave_height"] = atm.value("wave_height", 0.0);
    p["cover"]       = atm.value("cloud_cover", 0.5);

    if (auto* sky = preview_viewport_->featureAs<viewport3d::SkyFeature>(viewport3d::SkyFeature::kId))
        sky->setAtmosphereParams(p);
    if (auto* cloud = preview_viewport_->featureAs<viewport3d::CloudFeature>(viewport3d::CloudFeature::kId))
        cloud->setAtmosphereParams(p);
    if (auto* ocean = preview_viewport_->featureAs<viewport3d::OceanFeature>(viewport3d::OceanFeature::kId))
    {
        auto sea=p;sea.update(scenario_data_.value("ocean",nlohmann::json::object()));
        ocean->setAtmosphereParams(sea);
    }
}

// ---------------------------------------------------------------------------
// Timing subsystem + data distribution service
// ---------------------------------------------------------------------------
void SimulationControlWindow::buildTimingBus()
{
    // 时序子系统: 固定步长主循环, 装备数据源作为其子系统被调度
    bus_ = std::make_unique<TimingBus>();
    if(voxel_source_) {
        // Naming/resetting a run clears BrokerNet's previous Earth context.
        // Do that before the source publishes its initial canonical geometry.
        BrokerNet::instance().setSimMeta(QFileInfo(scenario_path_).fileName().toStdString(),0,0,false);
        if(!voxel_source_->initialize(nlohmann::json::object()))throw std::runtime_error(voxel_source_->diagnostics().value("error",std::string("voxel equipment initialization failed")));
        simulation_step_=voxel_source_->fixedStep();bus_->configureFixedStep(simulation_step_.load(),1);
        bus_->registerSubsystem(voxel_source_,0);
        if(!bus_->initialize())throw std::runtime_error("voxel timing source initialization failed");
        bus_->start();bus_->setPaused(true);
        BrokerNet::instance().setSimMeta(QFileInfo(scenario_path_).fileName().toStdString(),voxel_source_->snapshot()->timeSeconds(),0,false);
        BrokerNet::instance().setFlightControlAvailable(voxel_source_->hasPresentationControls());return;
    }
    simulation_step_.store(sim_.config().frame_dt);
    bus_->configureFixedStep(sim_.config().frame_dt,1);
    bus_->setMaxFrames(sim_.config().max_frames);
    bus_->setMaxSimTime(sim_.config().total_duration);

    // 平台管理器: 管理所有平台运动模型与装备联动 (优先级 -10, 在 EquipmentDataSource 之前)
    platform_mgr_ = std::make_shared<platform::PlatformManager>();
    {
        nlohmann::json mgr_config;
        const auto atm=scenario_data_.value("atmosphere",nlohmann::json::object());
        const auto ocean=scenario_data_.value("ocean",nlohmann::json::object());
        mgr_config["environment"]["sea_level"] = ocean.value("sea_level",0.0);
        mgr_config["environment"]["air_density"] = atm.value("air_density",1.225);
        mgr_config["environment"]["wind_speed"] = atm.value("wind_speed",6.0);
        mgr_config["environment"]["wind_az"] = atm.value("wind_az",80.0);
        platform_mgr_->initialize(mgr_config);
    }

    // 将物理引擎传递给平台管理器 (Bullet3 物理集成)
    if (sim_.physics()) {
        platform_mgr_->setPhysicsEngine(sim_.physics().get());
    }
    bus_->registerSubsystem(platform_mgr_, -10);

    // 从场景 JSON 创建平台 (如果场景已装载)
    if (!scenario_path_.isEmpty()) {
        try {
            std::ifstream f(scenario_path_.toStdString());
            nlohmann::json scene_json;
            if (f && (f >> scene_json, true)) {
                // 从场景 JSON 的 entities 字段创建平台
                if (scene_json.contains("entities")) {
                    for (const auto& entity_json : scene_json["entities"]) {
                        platform::PlatformMotionFactory::createFromJSON(
                            entity_json, *platform_mgr_, next_equip_id_);
                    }
                }
                // 从场景 JSON 的 objects 字段创建平台 (兼容旧格式)
                if (scene_json.contains("objects")) {
                    for (const auto& obj_json : scene_json["objects"]) {
                        platform::PlatformMotionFactory::createFromObjectJSON(
                            obj_json, *platform_mgr_, next_equip_id_);
                    }
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("PlatformManager: failed to load platforms from scene: {}", e.what());
        }
    }

    data_source_ = std::make_shared<EquipmentDataSource>(&sim_);
    bus_->registerSubsystem(data_source_, 0);
    bus_->initialize();
    bus_->start();
    bus_->setPaused(true);           // 初始化后暂停, 等待播放

    // 初始能力和状态先发布，再允许显控装订，避免同名复位短暂复用旧遥测。
    auto platforms = platform_mgr_->getAllPlatformsSnapshot();
    for (auto& platform : platforms) platform["sim_time"] = sim_time_.load();
    BrokerNet::instance().setSimMeta(QFileInfo(scenario_path_).fileName().toStdString(), sim_time_.load(), 0, false);
    BrokerNet::instance().setPlatformTelemetry(platforms.dump());
    // The display workstation receives the exact dataset declaration and
    // simulation epoch, rather than constructing another default Earth.
    nlohmann::json earthContext=nlohmann::json::object();
    if(scenario_data_.is_object() && !scenario_path_.isEmpty()) {
        const auto source=QFileInfo(scenario_path_).absoluteFilePath().toStdString();
        const auto anchor=sim_.physics()->snapshot()->anchor;
        earthContext["geo_anchor"]={{"lat",anchor.lat_deg},{"lon",anchor.lon_deg},{"alt",anchor.alt_m},{"heading_deg",anchor.heading_deg}};
        earthContext["earth"]=scenario_data_.value("earth",nlohmann::json::object());
        if(preview_viewport_)if(auto* globe=preview_viewport_->featureAs<viewport3d::EarthView>(viewport3d::EarthView::kId))
            if(!globe->earthConfiguration().empty())earthContext["earth"]=globe->earthConfiguration();
        const auto map=earth::EarthDataService::resolveMapPath(earthContext,source);
        if(!map.empty())earthContext["earth"]["map"]=map;
        const auto reference=scenario_data_.value("terrain_ref",std::string());
        if(!reference.empty())earthContext["terrain_ref"]=earth::EarthDataService::resolveUri(reference,source);
        earthContext["simulation"]={{"epoch_utc",sim_.config().epoch_utc}};
        earthContext["atmosphere"]=scenario_data_.value("atmosphere",nlohmann::json::object());
        earthContext["atmosphere"].update(scenario_data_.value("environment",nlohmann::json::object()));
        earthContext["ocean"]=scenario_data_.value("ocean",nlohmann::json::object());
        earthContext["source_scenario"]=source;
        if(const auto surface=sim_.earthSurface()) {
            earthContext["surface_revision"]=surface->revision;
            earthContext["surface_quality"]=surface->provenance_assumed ? "reference" : "declared";
            if(sb_earth_) {
                sb_earth_->setText(surface->provenance_assumed ? tr("地表：概略参考") : tr("地表：已装订"));
                sb_earth_->setToolTip(tr("来源：%1\n采样间距：%2 m\n版本：%3\n%4")
                    .arg(QString::fromStdString(surface->source)).arg(surface->resolution_m,0,'f',1).arg(surface->revision)
                    .arg(surface->provenance_assumed ? tr("垂向基准为显式参考假设，需核实源数据") : tr("按数据包声明的基准与精度使用")));
            }
        } else if(sb_earth_)sb_earth_->setText(tr("地表：本地参考平面"));
    }
    BrokerNet::instance().setEarthContext(earthContext.dump());
    if(auto* view=preview_viewport_->featureAs<viewport3d::EarthView>(viewport3d::EarthView::kId))
        sim_.setEarthViewPreparation(view->captureViewPreparation());
    sim_.setEarthCommitHandler([this,context=earthContext](earth::EarthSurfaceSnapshotPtr next) mutable {
        context["earth"]=next->declaration;
        context["surface_revision"]=next->revision;
        context["surface_valid_from"]=sim_.earthValidFrom();
        BrokerNet::instance().setEarthContext(context.dump());
    });
    // 装备数据分发服务上线 (各显控软件可开始取数)
    setServiceOnline(true);
    if (recording_ && data_source_)     // 初始化晚于录制开始时补挂录制器
        data_source_->attachFrameRecorders(&recorders_[0], &recorders_[1],
                                           &recorders_[2]);
    updateDistMonitor();
}

void SimulationControlWindow::updateDistMonitor()
{
    if (!lbl_dist_service_) return;
    if(engine_worker_){
        const auto s=engine_worker_->status();
        lbl_dist_service_->setText(service_online_?tr("数据服务: 后台在线"):tr("数据服务: 后台未就绪"));
        lbl_dist_service_->setStyleSheet(service_online_?"color:#54c46a; font-weight:bold;":"color:#e9c062; font-weight:bold;");
        const auto pool=s.value("source",nlohmann::json::object()).value("gpu_pool",nlohmann::json::object());
        if(lbl_dist_[0])lbl_dist_[0]->setText(tr("世界进程 PID %1 · %2").arg(s.value("pid",0)).arg(QString::fromStdString(s.value("state",std::string("starting")))));
        if(lbl_dist_[1])lbl_dist_[1]->setText(tr("GPU 计算 PID %1 · %2").arg(pool.value("pid",0)).arg(QString::fromStdString(pool.value("state",std::string("idle")))));
        if(lbl_dist_[2])lbl_dist_[2]->setText(tr("数值整步均值 %1 ms · P95 %2 ms").arg(s.value("simulation_cycle_mean_ms",0.),0,'f',2).arg(s.value("simulation_cycle_p95_ms",0.),0,'f',2));
        return;
    }
    auto& broker = SimDataBroker::instance();
    const bool online = broker.isServerOnline();
    lbl_dist_service_->setText(online ? tr("数据服务: 在线") : tr("数据服务: 离线"));
    lbl_dist_service_->setStyleSheet(online
        ? "color:#54c46a; font-weight:bold;"
        : "color:#e9c062; font-weight:bold;");

    const SensorType types[3] = {SensorType::RADAR, SensorType::SAR, SensorType::IR};
    for (int i = 0; i < 3; ++i) {
        if (!lbl_dist_[i]) continue;
        const uint32_t eq_id = broker.firstEquipmentOfType(types[i]);
        const ChannelStats st = broker.channelStats(eq_id);
        const GpuBufferHandle h = broker.channelHandle(eq_id);
        QString loc;
        if (h.device_ptr)      loc = tr("GPU %1 KB · %2")
                    .arg(h.size_bytes / 1024)
                    .arg(h.ipc_valid ? tr("IPC 句柄") : tr("设备指针"));
        else if (h.size_bytes) loc = tr("主机后备 %1 KB").arg(h.size_bytes / 1024);
        else                   loc = tr("—");

        const bool client_alive = st.last_client_poll_ms >= 0;
        lbl_dist_[i]->setText(tr("%1 · %2 帧 · %3 · %4 · %5")
            .arg(i == 0 ? tr("雷达回波") : i == 1 ? tr("SAR 回波") : tr("红外辐射亮度"))
            .arg(st.published_frames)
            .arg(st.powered ? tr("开机") : tr("关机"))
            .arg(client_alive ? tr("显控在线") : tr("显控离线"))
            .arg(loc));
    }
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------
void SimulationControlWindow::setPlaybackEnabled(bool running)
{
    btn_play_->setEnabled(running);
    btn_step_->setEnabled(running);
    btn_cont_->setEnabled(running);
    btn_pause_->setEnabled(running);
    btn_stop_->setEnabled(running);
}

void SimulationControlWindow::onPlayPause()
{
    if(engine_worker_){if(sim_inited_)engine_worker_->command(engine_worker_->status().value("run_requested",playing_.load())?"pause":"play");return;}
    if (!sim_inited_ || !bus_) return;
    if (replay_active_) {               // 回放期间禁止实时播放
        statusBar()->showMessage(tr("回放进行中，请先停止回放"), 3000);
        if (log_panel_) log_panel_->warn(tr("回放进行中, 无法播放实时仿真"));
        return;
    }
    playing_ = !playing_.load();
    btn_play_->setText(playing_ ? tr("⏸ 暂停") : tr("▶ 播放"));
    {
        std::lock_guard<std::mutex> lk(sim_mtx_);   // 与工作线程的推进互斥
        if (!bus_->isRunning()) {           // 停止后重新启动
            bus_->start();
        }
        setServiceOnline(true);            // 回放结束后总线可能仍在暂停，但服务已下线
        bus_->setPaused(!playing_.load());  // 时序子系统控制播放/暂停
    }
    if (playing_) {
        setFlowStage(2);
        statusBar()->showMessage(tr("仿真运行中..."), 2000);
        if (log_panel_) log_panel_->info(tr("仿真播放"));
    } else if (log_panel_) {
        log_panel_->info(tr("仿真暂停"));
    }
    updateDistMonitor();
}

void SimulationControlWindow::onStepOnce()
{
    if(engine_worker_){if(sim_inited_)engine_worker_->command("step");return;}
    if (!sim_inited_ || !bus_) return;
    if (replay_active_) {
        if (log_panel_) log_panel_->warn(tr("回放进行中, 单步不可用"));
        return;
    }
    playing_ = false;
    btn_play_->setText(tr("▶ 播放"));
    {
        std::lock_guard<std::mutex> lk(sim_mtx_);   // 与工作线程互斥后手动推进一步
        if (!bus_->isRunning()) bus_->start();
        setServiceOnline(true);
        bus_->setPaused(false);             // tick() 在暂停时直接返回
        pollControlCommandsWorker();       // 单步也先装订请求，避免与工作线程时序不同
        advanceOneFrame();
        bus_->setPaused(true);
    }
    if (log_panel_) log_panel_->info(tr("单步推进一帧"));
}

void SimulationControlWindow::onStop()
{
    if(engine_worker_){engine_worker_->command("stop");return;}
    playing_ = false;
    if (replay_active_) stopReplay(false);
    btn_play_->setText(tr("▶ 播放"));
    {
        std::lock_guard<std::mutex> lk(sim_mtx_);   // 等待在途推进结束后停止
        if (bus_) bus_->stop();             // -> 数据服务下线
        if (platform_mgr_) platform_mgr_->resetFlightControls();
        BrokerNet::instance().setFlightControlAvailable(false);
    }
    setServiceOnline(false);            // 同步状态标记 + 事件日志 (幂等)
    updateStatusLabels();
    updateDistMonitor();
    statusBar()->showMessage(tr("仿真已停止，保留当前状态"), 2000);
    if (log_panel_) log_panel_->info(tr("仿真停止，重新初始化可复位"));
}

void SimulationControlWindow::advanceOneFrame()
{
    if (!bus_) return;
    // 定步长 (已去倍速): 步长终身恒为 1/target_fps (与 buildTimingBus 初始配置一致),
    // 仿真时间 = 帧数 × dt, 严格确定, 不受墙钟抖动影响。
    // 调用方须持 sim_mtx_ (仿真工作线程或 UI 单步)。
    const auto t0 = std::chrono::steady_clock::now();
    const bool keepRunning=bus_->tick();
    pushFrameMs(std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0).count());
    if(voxel_source_&&voxel_source_->isHealthy()) {
        auto state=voxel_source_->snapshot();sim_time_=state->timeSeconds();frames_.fetch_add(1);
        std::lock_guard lock(earth_view_mutex_);pending_voxel_view_=std::move(state);
    }
    if (data_source_ && data_source_->lastTickOk()) {
        sim_time_ = bus_->currentTick().sim_time;
        if(auto surface=sim_.earthSurface();surface && surface->resident_tiles) {
            std::lock_guard lock(earth_view_mutex_);
            pending_earth_view_=std::move(surface);
            pending_earth_time_=sim_time_.load();
        } else preview_viewport_->context().setSimulationTime(sim_time_.load());
        frames_.fetch_add(1);
        pending_register_ = true;       // 首帧后把装备注册到注册表 (UI 线程执行)
    }
    if (!keepRunning || (data_source_ && !data_source_->lastTickOk()) || (voxel_source_&&!voxel_source_->isHealthy())) {
        playing_=false;
        bus_->stop();
        if (platform_mgr_) platform_mgr_->resetFlightControls();
        BrokerNet::instance().setFlightControlAvailable(false);
    }
}

void SimulationControlWindow::onTick()
{
    if(engine_worker_){engine_worker_->poll();btn_play_->setText(engine_worker_->status().value("run_requested",playing_.load())?tr("⏸ 暂停"):tr("▶ 播放"));updateStatusLabels();updateEquipmentTable();return;}
    // 实时仿真的命令轮询/交战闭环/帧推进已移至仿真工作线程 (#6),
    // UI 线程仅排空工作线程日志 + 显示刷新。
    drainWorkerLogs();
    const bool brokerOnline = SimDataBroker::instance().isServerOnline();
    if (service_online_ != brokerOnline) setServiceOnline(brokerOnline);
    btn_play_->setText(playing_ ? tr("⏸ 暂停") : tr("▶ 播放"));
    if (pending_register_.exchange(false))
        registerEquipment();            // 首帧完成 → 装备注册 (QWidget 仅可 UI 线程)

    if (replay_active_) {               // 回放优先接管数据分发 (仍在 UI 线程)
        if (replay_playing_) {
            const auto t0 = std::chrono::steady_clock::now();
            replayTick();
            pushFrameMs(std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - t0).count());
        }
    }

    // 3D 预览视口由 Viewport3DView 内部帧循环驱动, 无需在此渲染。

    const int64_t now = QDateTime::currentDateTime().toMSecsSinceEpoch();
    if (fps_state_.last_ms == 0) fps_state_.last_ms = now;
    fps_state_.frames++;
    const int64_t delta = now - fps_state_.last_ms;
    if (delta >= 500) {
        fps_state_.fps = double(fps_state_.frames) * 1000.0 / double(delta);
        fps_state_.frames = 0;
        fps_state_.last_ms = now;
        // 发现面心跳元数据: 场景名/仿真时间/FPS/运行状态随心跳广播给显控
        BrokerNet::instance().setSimMeta(
            QFileInfo(scenario_path_).fileName().toStdString(),
            sim_time_, fps_state_.fps, playing_ && !replay_active_);
        std::string platformTelemetry;
        {
            std::unique_lock<std::mutex> lock(sim_mtx_, std::try_to_lock);
            if (lock.owns_lock()) {
                // Sensor recordings do not contain platform truth. Never label
                // a paused live-world pose with the replay clock.
                auto platforms = voxel_source_?voxel_source_->platforms():platform_mgr_ && !replay_active_
                    ? platform_mgr_->getAllPlatformsSnapshot() : nlohmann::json::array();
                for (auto& platform : platforms) platform["sim_time"] = sim_time_.load();
                platformTelemetry = platforms.dump();
            }
        }
        if (!platformTelemetry.empty()) BrokerNet::instance().setPlatformTelemetry(platformTelemetry);
        updateStatusLabels();
        updateDistMonitor();     // 分发面板与 FPS 同节奏刷新
        updateChannelRates();    // 各装备帧率/流量统计
        updateEquipmentTable();  // 装备状态总览
        updateFlightControlMonitor();
        updatePerfStats();       // 性能监控面板
        updateSceneProperties(); // 实体属性: 同步运行时位置/速度等实时状态
        updateSim3DVisuals();    // 3D 探测特效 + 平台中文标注: 随平台快照动态刷新
    }
}

void SimulationControlWindow::updateStatusLabels()
{
    if (act_launch_display_) act_launch_display_->setEnabled(sim_inited_ && service_online_);
    if (sb_backend_) sb_backend_->setText(sim_inited_ ? tr("后端：%1").arg(backend_description_) : tr("后端：未初始化"));
    if (sb_state_)
        sb_state_->setText(!sim_inited_     ? tr("未初始化")
                           : replay_active_ ? (replay_playing_ ? tr("回放中")
                                                               : tr("回放暂停"))
                           : playing_       ? tr("运行中")
                                            : tr("已暂停"));
    if(sb_state_&&engine_worker_&&engine_worker_->status().value("state",std::string())=="waiting_for_leases")sb_state_->setText(tr("等待任务租约"));
    if (sb_time_)
        sb_time_->setText(tr("仿真时间 %1 s").arg(sim_time_.load(), 0, 'f', 2));
    setFrameLabel(frames_);
    setFpsLabel(fps_state_.fps);
}

// ---------------------------------------------------------------------------
// Monitors
// ---------------------------------------------------------------------------
void SimulationControlWindow::registerEquipment()
{
    if (!sim_inited_ || voxel_source_) return;
    auto& registry = EquipmentRegistry::instance();

    // 从场景 JSON 读取各传感器的角色 (radar: role=search/guidance),
    // 使显控软件能据此初始化搜索/制导界面功能。
    std::unordered_map<uint32_t, int> sensor_role;
    if (!scenario_path_.isEmpty()) {
        try {
            std::ifstream f(scenario_path_.toStdString());
            nlohmann::json sj;
            if (f && (f >> sj, true)) {
                for (const auto& js : sj.value("sensors", nlohmann::json::object())
                                        .value("sensor_list", nlohmann::json::array())) {
                    const uint32_t sid = js.value("sensor_id", 0u);
                    if (sid == 0) continue;
                    std::string role = js.value("role", std::string("search"));
                    std::transform(role.begin(), role.end(), role.begin(), ::tolower);
                    sensor_role[sid] = (role == "guidance" || role == "制导") ? 1 : 0;
                }
            }
        } catch (const std::exception&) { /* 角色解析失败则默认搜索 */ }
    }

    int newly = 0;
    for (const auto& [sid, frame] : sim_.lastFrames()) {
        if (registry.findById(sid)) continue;   // 已注册
        EquipmentInfo info;
        info.equipment_id  = sid;
        info.type          = frame.sensor_type;
        info.name          = std::string(sensorTypeName(frame.sensor_type)) +
                             "_" + std::to_string(sid);
        info.platform_id   = 1;
        info.platform_name = tr("平台_01").toStdString();
        info.frame_rate    = float(1.0/sim_.config().frame_dt);
        info.description   = QString("%1x%2 信号帧")
                                 .arg(frame.width).arg(frame.height)
                                 .toStdString();
        info.active        = true;
        // 仅雷达传感器携带角色; 其它类型默认搜索。
        if (frame.sensor_type == SensorType::RADAR) {
            auto it = sensor_role.find(sid);
            info.role = (it != sensor_role.end()) ? it->second : 0;
        }
        registry.registerEquipment(info);
        ++newly;
    }
    if (newly > 0) {
        equipment_registered_ = true;
        if (log_panel_)
            log_panel_->info(tr("装备注册: 新增 %1 台, 共 %2 台在线 (显控软件可扫描链接)")
                .arg(newly).arg(int(registry.count())));
    }
}

void SimulationControlWindow::registerEquipmentFromScenario()
{
    if(voxel_source_)return;
    if (scenario_data_.empty()) { registerEquipment(); return; }   // 传统配置路径回退
    auto& registry = EquipmentRegistry::instance();

    // Platform IDs come from the running manager, matching telemetry and
    // controls even when terrain/static objects were skipped during creation.
    struct Plat { uint32_t id; int faction; std::string kind; std::string label; };
    std::map<std::string, Plat> plats;
    const auto facInt = [](const std::string& f) -> int {
        if (f == "red" || f.find("红") != std::string::npos) return 2;   // 红方
        if (f == "blue" || f.find("蓝") != std::string::npos) return 1;  // 蓝方
        return 0;                                                        // 中立
    };
    const auto facText = [](int f) -> const char* {
        return f == 2 ? "红方" : f == 1 ? "蓝方" : "中立";
    };
    const auto kindText = [](const std::string& type,
                             const std::string& object_id) -> std::string {
        if (type == "missile")                return "导弹";
        if (type == "uav")                    return "无人机";
        if (type == "aircraft" || type == "plane") return "飞机";
        if (type == "tank" || type == "vehicle")   return "坦克";
        if (type == "ship" || type == "naval")     return "舰船";
        if (object_id.find("radar") != std::string::npos) return "雷达站";
        if (object_id.find("sam")  != std::string::npos)  return "防空阵地";
        return "设施";
    };
    std::map<std::string, uint32_t> runtimeIds;
    if (platform_mgr_) for (const auto& platform : platform_mgr_->getAllPlatformsSnapshot())
        runtimeIds[platform.value("name", std::string())] = platform.value("platform_id", 0u);
    for (const auto& o : scenario_data_.value("objects", nlohmann::json::array())) {
        const std::string oid = o.value("object_id", std::string());
        if (oid.empty()) continue;
        const std::string type = o.value("type", std::string());
        const int fi = facInt(o.value("faction", std::string()));
        plats[oid] = {runtimeIds.count(oid) ? runtimeIds.at(oid) : 0u, fi,
                      type.empty() ? oid : type,
                      std::string(facText(fi)) + kindText(type, oid)};
    }

    // ---- 装备注册: 场景传感器列表 (装载即注册, 不依赖首帧产帧) ----
    std::map<uint32_t, rockyenv::sensor::SensorDesc> gpuDescriptions;
    if (sim_.gpuSensorBackendActive())
        for (const auto& description : rockyenv::sensor::parseSensorDescriptions(scenario_data_.dump()))
            gpuDescriptions[uint32_t(description.sensor_id)] = description;
    int newly = 0;
    for (const auto& js : scenario_data_.value("sensors", nlohmann::json::object())
                              .value("sensor_list", nlohmann::json::array())) {
        const uint32_t sid = js.value("sensor_id", 0u);
        if (sid == 0 || registry.findById(sid)) continue;

        // 类型字符串 -> SensorType
        std::string ts = js.value("type", std::string("radar"));
        std::transform(ts.begin(), ts.end(), ts.begin(), ::tolower);
        SensorType st = SensorType::RADAR;
        if (ts == "ir" || ts == "infrared")          st = SensorType::IR;
        else if (ts == "sar")                        st = SensorType::SAR;
        else if (ts == "vis" || ts == "camera")      st = SensorType::VIS;
        else if (ts == "laser")                      st = SensorType::LASER;

        EquipmentInfo info;
        info.equipment_id  = sid;
        info.type          = st;
        info.name          = std::string(sensorTypeName(st)) + "_" + std::to_string(sid);
        std::string role_s = js.value("role", std::string("search"));
        std::transform(role_s.begin(), role_s.end(), role_s.begin(), ::tolower);
        info.role          = (st == SensorType::RADAR &&
                              (role_s == "guidance" || role_s == "制导")) ? 1 : 0;
        const auto signal=js.value("signal_params",nlohmann::json::object());
        double nominalRate = 1.0 / sim_.config().frame_dt;
        if (st == SensorType::IR || st == SensorType::VIS) {
            const double period = signal.value("frame_interval_s", 0.0);
            if (period > 0) nominalRate = 1.0 / period;
        }
        if (gpuDescriptions.count(sid)) {
            const auto& description = gpuDescriptions.at(sid);
            const auto frame = js.value("frame", nlohmann::json::object());
            if (st == SensorType::RADAR || st == SensorType::SAR) {
                const double prf = frame.contains("rate_hz") ? description.rate_hz : description.pulse_rate;
                const uint32_t pulses = frame.contains("cpi_pulses") ? description.cpi_pulses
                    : signal.value("doppler_bins", description.cpi_pulses);
                nominalRate = pulses > 0 ? prf / pulses : 0.0;
            } else if (frame.contains("rate_hz") || signal.value("frame_interval_s", 0.0) <= 0)
                nominalRate = description.rate_hz;
        }
        info.frame_rate = float(nominalRate);
        const auto ap      = js.value("aperture", nlohmann::json::object());
        info.range_max     = float(ap.value("range_m", 0.0));
        info.description   = js.value("band", std::string()) + " 波段传感器";

        // 平台归属 (真实映射, 替代历史硬编码 平台_01)
        const std::string pobj = js.value("platform_object", std::string());
        auto pit = plats.find(pobj);
        if (pit != plats.end()) {
            info.platform_id   = pit->second.id;
            info.platform_name = pit->second.label;
            info.faction       = pit->second.faction;
            info.platform_kind = pit->second.kind;
        }

        registry.registerEquipment(info);
        ++newly;
    }
    if (newly > 0) {
        equipment_registered_ = true;
        if (log_panel_)
            log_panel_->info(tr("装备注册(场景驱动): %1 台在线, 平台映射来自场景实体 (显控软件可扫描链接)")
                .arg(newly));
        spdlog::info("SimulationControl: {} equipment registered from scenario",
                     newly);
    }
}

// ---------------------------------------------------------------------------
// Stage-2: service online / control commands / recording / replay
// ---------------------------------------------------------------------------
void SimulationControlWindow::setServiceOnline(bool online)
{
    if(engine_worker_){service_online_=online;return;}
    SimDataBroker::instance().serverOnline(online);
    BrokerNet::instance().setFlightControlAvailable(
        online && sim_inited_ && (platform_mgr_||(voxel_source_&&voxel_source_->hasPresentationControls())) && !replay_active_);
    if (service_online_ == online) return;   // 状态未变 (broker 幂等)
    service_online_ = online;
    if (online)
        for (int i = 0; i < 3; ++i)          // 服务上线后装备默认开机
            equip_powered_[i] = true;
    if (log_panel_)
        log_panel_->info(online ? tr("数据服务上线 (装备数据开始分发)")
                                : tr("数据服务下线"));
}

void SimulationControlWindow::pollControlCommandsWorker()
{
    // 仿真线程执行: 先于本帧产帧取走命令 (与原单线程顺序一致);
    // 电源状态已在 broker 立即生效, 此处仅记录状态与日志 (入队交 UI 线程)。
    SimDataBroker& broker = SimDataBroker::instance();
    ControlCommand cmd;
    while (broker.pollControl(&cmd)) {
        if (cmd.type != ControlCommand::Type::PowerOn && cmd.type != ControlCommand::Type::PowerOff)
            continue;
        int ch = -1;
        switch (cmd.equipment_type) {
        case SensorType::RADAR: ch = 0; break;
        case SensorType::SAR:   ch = 1; break;
        case SensorType::IR:    ch = 2; break;
        default: break;
        }
        if (ch < 0) continue;
        const bool power_on = (cmd.type == ControlCommand::Type::PowerOn);
        {
            std::lock_guard<std::mutex> lk(powered_mtx_);
            equip_powered_[ch] = power_on;
        }
        {
            std::lock_guard<std::mutex> lk(log_mtx_);
            log_queue_.push_back({0, tr("装备控制命令: %1 %2 (序号 %3)")
                .arg(ch == 0 ? tr("雷达") : ch == 1 ? tr("SAR") : tr("红外"))
                .arg(power_on ? tr("开机") : tr("关机"))
                .arg(cmd.seq)});
            powered_dirty_ = true;
        }
    }
    auto& net = BrokerNet::instance();
    if(voxel_source_){voxel_source_->servicePresentationCommands();return;}
    FlightControlCommand flight;
    for (int count = 0; count < 64 && net.pollFlightControl(&flight); ++count) {
        FlightControlResult result;
        if (platform_mgr_ && sim_inited_ && broker.isServerOnline() && !replay_active_) {
            result = platform_mgr_->applyFlightControl(flight, sim_time_.load());
            if (result.stage == "applied" && !playing_ && !flight.release)
                result.message += "；仿真暂停，控制器已装订，待推进";
        } else {
            result.request_id = flight.request_id;
            result.client_id = flight.client_id;
            result.session_id = flight.session_id;
            result.platform_id = flight.platform_id;
            result.stage = "rejected";
            result.code = "unavailable";
            result.message = "实时飞控执行器未上线或正在回放";
            result.sim_time = sim_time_.load();
        }
        net.completeFlightControl(result);
        if (result.stage != "applied") {
            std::lock_guard<std::mutex> lock(log_mtx_);
            log_queue_.push_back({1, tr("飞控请求 %1：%2")
                .arg(QString::fromStdString(result.request_id), QString::fromStdString(result.message))});
        }
    }
    if (platform_mgr_) platform_mgr_->updateFlightControlLeases(sim_time_.load());
}

void SimulationControlWindow::updateFlightControlMonitor()
{
    if (!table_flight_) return;
    nlohmann::json platforms;
    {
        std::unique_lock<std::mutex> lock(sim_mtx_, std::try_to_lock);
        if (!lock.owns_lock()) return;
        platforms = platform_mgr_ && !replay_active_
            ? platform_mgr_->getAllPlatformsSnapshot() : nlohmann::json::array();
    }
    int row = 0;
    for (const auto& platform : platforms) {
        if (platform.value("category", std::string()) != "Aircraft") continue;
        if (!platform.contains("flight_control")) continue;
        const auto& control = platform["flight_control"];
        if (!control.is_object()) continue;
        if (row >= table_flight_->rowCount()) table_flight_->insertRow(row);
        const auto mode = control.value("mode", std::string("route"));
        const auto owner = control.value("owner", std::string());
        const QStringList values{QString::fromStdString(platform.value("name", std::string())),
            mode == "manual" ? tr("手动") : mode == "altitude_hold" ? tr("高度/航向保持")
                : mode == "stabilized" ? tr("当前状态保持") : tr("航迹/自主"),
            !control.value("available", false) ? tr("不可接管") : owner.empty() ? tr("自主") : tr("外部接管"),
            QString::number(control.value("revision", uint64_t(0)))};
        for (int column = 0; column < values.size(); ++column) {
            auto* item = table_flight_->item(row, column);
            if (!item) { item = new QTableWidgetItem; table_flight_->setItem(row, column, item); }
            item->setText(values[column]);
            item->setToolTip(column == 2 ? QString::fromStdString(owner) : values[column]);
        }
        ++row;
    }
    table_flight_->setRowCount(row);
}

void SimulationControlWindow::updateEngagementLoopWorker()
{
    // 仿真线程执行 (调用方持 sim_mtx_, 与平台重建/单步互斥)
    if (!platform_mgr_) return;
    SimDataBroker& broker = SimDataBroker::instance();

    // 1) 消费制导雷达的发射请求 → 指派红方地空导弹拦截
    EngagementRequest req;
    while (broker.pollEngagement(&req)) {
        if (!req.fire) continue;
        const vsg::dvec3 pos{req.target_x, req.target_y, req.target_z};
        const uint32_t target = platform_mgr_->requestEngagement(pos, req.gate_m, sim_time_);
        std::lock_guard<std::mutex> lk(log_mtx_);
        log_queue_.push_back({0, target != 0
            ? tr("交战触发: 红方拦截弹发射, 锁定蓝方目标 #%1").arg(target)
            : tr("交战请求收到, 但无可用目标/拦截弹 (gate=%1m)").arg(req.gate_m)});
    }

    // 2) 回传拦截弹状态 (供制导雷达界面展示"为导弹提供信息")
    std::vector<InterceptorStatus> statuses;
    const nlohmann::json arr = platform_mgr_->engagementsSnapshot();
    for (const auto& j : arr) {
        InterceptorStatus s;
        s.interceptor_id = j.value("interceptor_id", 0u);
        s.name = j.value("interceptor_name", std::string(""));
        s.target_id = j.value("target_id", 0u);
        s.launched = j.value("launched", false);
        s.in_flight = j.value("in_flight", false);
        s.impacted = j.value("impacted", false);
        s.target_destroyed = j.value("target_destroyed", false);
        s.range_km = float(j.value("range_km", 0.0));
        s.sim_time = sim_time_;
        statuses.push_back(s);
    }
    broker.publishInterceptorStatus(statuses);
}

void SimulationControlWindow::drainWorkerLogs()
{
    // UI 线程: 排空仿真线程日志队列 → 事件日志面板 (QWidget 不可跨线程操作)
    std::deque<LogEntry> drained;
    bool powered_changed = false;
    {
        std::lock_guard<std::mutex> lk(log_mtx_);
        drained.swap(log_queue_);
        powered_changed = powered_dirty_;
        powered_dirty_ = false;
    }
    for (const auto& e : drained) {
        if (!log_panel_) break;
        switch (e.level) {
        case 0:  log_panel_->info(e.msg);  break;
        case 1:  log_panel_->warn(e.msg);  break;
        default: log_panel_->error(e.msg); break;
        }
    }
    if (powered_changed) updateEquipmentTable();   // 开关机状态即时刷新
}

void SimulationControlWindow::startSimWorker()
{
    if (sim_worker_.joinable()) return;
    sim_running_.store(true);
    sim_worker_ = std::thread([this] {
        auto deadline = std::chrono::steady_clock::now();
        while (sim_running_.load()) {
            const auto step=std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(simulation_step_.load()));
            deadline+=step;
            std::this_thread::sleep_until(deadline);   // 墙钟 pacing (定步长序列不变)
            if (sim_inited_ && !replay_active_) {
                // 与原单线程同序: 先消费命令, 再交战闭环, 最后定步长推进。
                // 持锁与重新初始化/单步/停止互斥。
                std::lock_guard<std::mutex> lk(sim_mtx_);
                if (sim_inited_ && bus_ && !replay_active_) {
                    pollControlCommandsWorker();
                    updateEngagementLoopWorker();
                    if (playing_) advanceOneFrame();
                }
            }
            const auto now = std::chrono::steady_clock::now();
            if (deadline < now) deadline = now;   // 超载重锚定: 不追帧也不额外延迟一拍
        }
    });
}

void SimulationControlWindow::stopSimWorker()
{
    if (!sim_worker_.joinable()) return;
    sim_running_.store(false);
    sim_worker_.join();
}

void SimulationControlWindow::onToggleRecord(bool checked)
{
    if (checked) {
        if (replay_active_) {                // 回放期间不允许录制
            if (log_panel_)
                log_panel_->warn(tr("回放进行中, 不能开始录制"));
            statusBar()->showMessage(tr("回放进行中，不能开始录制"), 3000);
            QSignalBlocker blocker(act_record_);
            act_record_->setChecked(false);
            return;
        }
        if (!startRecording()) {             // 启动失败: 回退按钮勾选状态
            QSignalBlocker blocker(act_record_);
            act_record_->setChecked(false);
        }
    } else {
        stopRecording();
    }
}

bool SimulationControlWindow::startRecording()
{
    const QString dir = resolveWorkspaceRoot() + "/recordings";
    const QString stamp =
        QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const char* names[3] = {"radar", "sar", "ir"};
    int opened = 0;
    for (int i = 0; i < 3; ++i) {
        const QString file =
            QString("%1/%2_%3.vsrd").arg(dir, names[i], stamp);
        if (recorders_[i].start(file.toStdString())) ++opened;
    }
    if (opened == 0) {
        if (log_panel_)
            log_panel_->error(tr("录制启动失败: 无法创建录制文件 (%1)").arg(dir));
        return false;
    }
    recording_ = true;
    if (data_source_)                        // 挂接到数据分发处 (publish 后录制)
        data_source_->attachFrameRecorders(&recorders_[0], &recorders_[1],
                                           &recorders_[2]);
    if (log_panel_)
        log_panel_->info(tr("录制开始: %1 个通道 → %2").arg(opened).arg(dir));
    if (!playing_ && log_panel_)
        log_panel_->warn(tr("仿真未在运行, 数据产生后才会写入录制文件"));
    return true;
}

void SimulationControlWindow::stopRecording()
{
    if (!recording_) return;
    recording_ = false;
    if (data_source_) data_source_->detachFrameRecorders();
    for (auto& rec : recorders_) rec.stop(); // 回填帧计数并落盘
    if (act_record_) {                       // 复位工具栏按钮 (不触发 toggled)
        QSignalBlocker blocker(act_record_);
        act_record_->setChecked(false);
    }
    if (log_panel_)
        log_panel_->info(tr("录制结束: 雷达 %1 帧 · SAR %2 帧 · 红外 %3 帧")
            .arg(recorders_[0].recordedFrames())
            .arg(recorders_[1].recordedFrames())
            .arg(recorders_[2].recordedFrames()));
}

void SimulationControlWindow::onReplayData()
{
    if (!replay_active_) { startReplay(); return; }
    // 回放会话进行中: 同一按钮切换 暂停/继续
    replay_playing_ = !replay_playing_;
    if (act_replay_)
        act_replay_->setText(replay_playing_ ? tr("⏸ 暂停回放")
                                             : tr("▶ 继续回放"));
    if (log_panel_)
        log_panel_->info(replay_playing_ ? tr("回放继续") : tr("回放暂停"));
    updateStatusLabels();
}

void SimulationControlWindow::startReplay()
{
    const QString dir = resolveWorkspaceRoot() + "/recordings";
    const QString file = QFileDialog::getOpenFileName(
        this, tr("选择回放文件 (任选雷达/SAR/红外录制文件)"),
        QFileInfo::exists(dir) ? dir : QString(),
        tr("录制数据 (*.vsrd);;所有文件 (*)"));
    if (file.isEmpty()) return;

    // 同批录制的三个通道文件: <前缀>_radar/_sar/_ir.vsrd
    static const char* tags[3] = {"radar", "sar", "ir"};
    const QFileInfo fi(file);
    QString base = fi.completeBaseName();
    int tag = -1;
    for (int i = 0; i < 3; ++i) {
        const QString suffix = QString::fromLatin1(tags[i]);
        if (base.endsWith(QString("_") + suffix)) {
            tag = i;
            base.chop(suffix.size() + 1);
            break;
        }
    }

    int opened = 0;
    uint64_t total_frames = 0;
    for (int i = 0; i < 3; ++i) {
        players_[i].close();
        QString f;
        if (tag >= 0)
            f = fi.absolutePath() + "/" + base + "_" + tags[i] + ".vsrd";
        else if (i == 0)                     // 命名不规范: 只回放所选文件
            f = file;
        if (f.isEmpty() || !QFileInfo::exists(f)) continue;
        const uint64_t n = players_[i].open(f.toStdString());
        if (n > 0) { ++opened; total_frames += n; }
        else players_[i].close();
    }
    if (opened == 0) {
        if (log_panel_)
            log_panel_->error(tr("回放失败: 无法打开录制文件"));
        return;
    }

    if (recording_) stopRecording();         // 回放与录制互斥
    if (playing_) {                          // 回放接管分发, 暂停实时仿真
        playing_ = false;
        btn_play_->setText(tr("▶ 播放"));
        {
            std::lock_guard<std::mutex> lk(sim_mtx_);
            bus_->setPaused(true);
        }
        if (log_panel_)
            log_panel_->warn(tr("实时仿真已暂停 (回放接管数据分发)"));
    }

    {
        std::lock_guard<std::mutex> lock(sim_mtx_);
        if (platform_mgr_) platform_mgr_->resetFlightControls();
        replay_active_ = true;
        BrokerNet::instance().setFlightControlAvailable(false);
    }
    replay_playing_ = true;
    replay_frames_  = 0;
    frames_         = 0;                     // 状态栏计数改为回放会话
    sim_time_       = 0.0;
    setServiceOnline(true);                  // 显控软件可开始取回放数据
    if (act_replay_) act_replay_->setText(tr("⏸ 暂停回放"));
    if (log_panel_)
        log_panel_->info(tr("回放开始: %1 个通道 · 共 %2 帧")
                             .arg(opened)
                             .arg(QString::number(total_frames)));
    updateStatusLabels();
}

void SimulationControlWindow::stopReplay(bool finished)
{
    if (!replay_active_) return;
    replay_active_  = false;
    replay_playing_ = false;
    for (auto& p : players_) p.close();
    if (act_replay_) act_replay_->setText(tr("▶ 回放数据"));
    if (log_panel_)
        log_panel_->info(finished
            ? tr("回放结束: 共 %1 帧").arg(QString::number(replay_frames_))
            : tr("回放停止: 已回放 %1 帧").arg(QString::number(replay_frames_)));
    if (!playing_) setServiceOnline(false);  // 实时仿真未运行则服务下线
    updateStatusLabels();
}

void SimulationControlWindow::replayTick()
{
    // 各通道顺序读取下一帧并重新发布到 broker，由当前数据通道分发。
    SimDataBroker& broker = SimDataBroker::instance();
    for (int i = 0; i < 3; ++i) {
        if (!players_[i].isOpen()) continue;
        SignalFrame frame;
        if (!players_[i].nextFrame(&frame)) {
            players_[i].close();             // 该通道回放完毕
            continue;
        }
        ++replay_frames_;
        ++frames_;
        sim_time_ = frame.sim_time;
        preview_viewport_->context().setSimulationTime(frame.sim_time);
        if (frame.empty()) continue;
        broker.publish(frame);
    }
    if (!players_[0].isOpen() && !players_[1].isOpen() && !players_[2].isOpen())
        stopReplay(true);                    // 全部通道播放到结尾
}

// ---------------------------------------------------------------------------
// Stage-2: statistics (各装备帧率/流量 + 装备状态总览 + 性能面板)
// ---------------------------------------------------------------------------
void SimulationControlWindow::updateChannelRates()
{
    auto& broker = SimDataBroker::instance();
    const SensorType types[3] = {SensorType::RADAR, SensorType::SAR,
                                 SensorType::IR};
    const int64_t now = QDateTime::currentDateTime().toMSecsSinceEpoch();
    for (int i = 0; i < 3; ++i) {
        const uint32_t eq_id = broker.firstEquipmentOfType(types[i]);
        const ChannelStats st = broker.channelStats(eq_id);
        const GpuBufferHandle h = broker.channelHandle(eq_id);
        ChannelRate& r = ch_rate_[i];
        if (r.last_ms > 0) {
            const double dt = double(now - r.last_ms) / 1000.0;
            if (dt > 0.2) {                  // 窗口过短时保持上次值
                const double frames =
                    double(st.published_frames - r.last_frames);
                r.fps  = frames / dt;
                r.kbps = frames * double(h.size_bytes) / 1024.0 / dt;
            }
        }
        r.last_frames   = st.published_frames;
        r.last_frame_id = st.last_frame_id;
        r.last_ms       = now;
    }
}

void SimulationControlWindow::updateEquipmentTable()
{
    if (!table_equip_) return;
    if(engine_worker_){
        const auto rows=engine_worker_->status().value("equipment",nlohmann::json::array());table_equip_->setRowCount(int(rows.size()));
        table_equip_->setHorizontalHeaderLabels({tr("装备"),tr("电源"),tr("后台发布帧数"),tr("显控建链"),tr("进程")});
        for(int row=0;row<int(rows.size());++row){const auto& e=rows[row];const QString values[]={QString::fromStdString(e.at("name").get<std::string>())+QString(" #%1").arg(e.at("id").get<int>()),e.value("powered",false)?tr("开机"):tr("关机"),QString::number(e.value("frames",uint64_t(0))),e.value("subscribed",false)?tr("已链接"):tr("未链接"),tr("独立后台")};for(int col=0;col<5;++col){auto* item=table_equip_->item(row,col);if(!item){item=new QTableWidgetItem;table_equip_->setItem(row,col,item);}item->setText(values[col]);}}return;
    }
    if(voxel_source_) {
        table_equip_->setHorizontalHeaderLabels({tr("装备"),tr("电源"),tr("配置帧率 (Hz)"),tr("已发布帧数"),tr("最近帧号")});
        const auto equipment=EquipmentRegistry::instance().listOnline();table_equip_->setRowCount(int(equipment.size()));
        for(int row=0;row<int(equipment.size());++row){const auto& e=equipment[row];const auto stats=SimDataBroker::instance().channelStats(e.equipment_id);
            const QString values[]={QString::fromStdString(e.name)+QString(" #%1").arg(e.equipment_id),e.powered?tr("开机"):tr("关机"),QString::number(e.frame_rate,'f',1)+tr(" 配置"),tr("累计 %1 帧").arg(stats.published_frames),stats.published_frames?QString::number(stats.last_frame_id):tr("等待首帧")};
            for(int column=0;column<5;++column){auto* item=table_equip_->item(row,column);if(!item){item=new QTableWidgetItem;table_equip_->setItem(row,column,item);}item->setText(values[column]);}
        }return;
    }
    bool powered[3];
    {
        std::lock_guard<std::mutex> lk(powered_mtx_);   // 仿真线程写保护
        for (int i = 0; i < 3; ++i) powered[i] = equip_powered_[i];
    }
    for (int i = 0; i < 3; ++i) {
        if (auto* it = table_equip_->item(i, 1)) {
            const bool on = powered[i];
            it->setText(on ? tr("开机") : tr("关机"));
            it->setForeground(on ? QColor(0x54, 0xc4, 0x6a)
                                 : QColor(0x8a, 0x92, 0x9e));
        }
        if (auto* it = table_equip_->item(i, 2))
            it->setText(QString::number(ch_rate_[i].fps, 'f', 1));
        if (auto* it = table_equip_->item(i, 3))
            it->setText(QString::number(ch_rate_[i].kbps, 'f', 1));
        if (auto* it = table_equip_->item(i, 4))
            it->setText(QString::number(ch_rate_[i].last_frame_id));
    }
}

void SimulationControlWindow::updatePerfStats()
{
    if (!perf_panel_) return;
    perf_panel_->pushFps(fps_state_.fps);    // FPS 曲线 + 数值

    double avg = 0.0;                        // 平均帧耗时 (最近 120 帧)
    {
        std::lock_guard<std::mutex> lk(frame_mtx_);
        if (!frame_ms_history_.empty()) {
            double sum = 0.0;
            for (double v : frame_ms_history_) sum += v;
            avg = sum / double(frame_ms_history_.size());
        }
    }
    perf_panel_->setAvgFrameMs(avg);

    // 仿真时间/真实时间比率 (本统计窗口内)
    const int64_t now = QDateTime::currentDateTime().toMSecsSinceEpoch();
    const double sim_now = sim_time_.load();
    double ratio = 0.0;
    if (ratio_last_ms_ > 0) {
        const double dt_real = double(now - ratio_last_ms_) / 1000.0;
        if (dt_real > 0.05)
            ratio = (sim_now - ratio_last_sim_) / dt_real;
    }
    ratio_last_ms_  = now;
    ratio_last_sim_ = sim_now;
    perf_panel_->setTimeRatio(ratio);

    for (int i = 0; i < 3; ++i)              // 各装备数据生成帧率
        perf_panel_->setEquipmentFps(i, ch_rate_[i].fps);
}

void SimulationControlWindow::pushFrameMs(double ms)
{
    std::lock_guard<std::mutex> lk(frame_mtx_);   // 仿真/回放线程与 UI 读共写保护
    frame_ms_history_.push_back(ms);
    while (frame_ms_history_.size() > 120) frame_ms_history_.pop_front();
}

// ---------------------------------------------------------------------------
// Stage-2: event log toolbar actions
// ---------------------------------------------------------------------------
void SimulationControlWindow::onClearLog()
{
    if (!log_panel_) return;
    log_panel_->clearLog();
    log_panel_->info(tr("事件日志已清空"));
}

void SimulationControlWindow::onExportLog()
{
    if (!log_panel_) return;
    const QString def = QDateTime::currentDateTime()
                            .toString("yyyyMMdd_HHmmss") + "_eventlog.txt";
    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出事件日志"), def, tr("文本文件 (*.txt);;所有文件 (*)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        log_panel_->error(tr("日志导出失败: 无法写入 %1").arg(path));
        QMessageBox::warning(this, tr("导出失败"),
                             tr("无法写入文件: %1").arg(path));
        return;
    }
    f.write(log_panel_->plainText().toUtf8());
    f.close();
    log_panel_->info(tr("事件日志已导出 → %1").arg(path));
    statusBar()->showMessage(tr("事件日志已导出"), 3000);
}

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------
void SimulationControlWindow::onAbout()
{
    QMessageBox::information(this, tr("关于"),
        tr("仿真控制软件 (装备数据生成与分发)\n\n"
           "流程: 装载场景 → 初始化管线 → 时序子系统驱动播放\n"
           "数据生成: 按场景配置使用 GPU 测量管线或 CPU 后端，状态栏显示实际选择。\n"
           "数据分发: 雷达 RD、SAR 原始回波、红外辐亮度和可见光图像\n"
           "经本地 IPC 发布给显控工作台，平台遥测随心跳传输。\n"
           "装备电源命令返回仿真端执行；其余控制以实际支持能力为准。\n\n"
           "Qt %1").arg(QString::fromUtf8(qVersion())));
}

}} // namespace vsgraysim::gui
