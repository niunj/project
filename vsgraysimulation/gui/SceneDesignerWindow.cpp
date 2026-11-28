#include "voxel/ModelLibraryDialog.h"
#include <vsg/app/Camera.h>
#include <simcore/GeoFrame.h>
#include <vsg/maths/transform.h>
#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include "viewport3d/VoxelSceneFeature.h"
#include "viewport3d/EarthViewFeature.h"
#include "viewport3d/TerrainLayerFeature.h"
/// @file SceneDesignerWindow.cpp
/// @brief Scene designer implementation (7 个分类 Dock).
#include "SceneDesignerWindow.h"
#include "viewport3d/VsgQtWidget.h"
#include <vsgraysim/earth/VoxelUsdEdits.h>
#include <vsgraysim/earth/VoxelScenePackage.h>
#include "voxel/SceneAuthoringPanel.h"
#include "voxel/SmartDeploymentPanel.h"
#include "base/DockBuilder.h"
#include "base/WidgetFactory.h"
#include "base/ThemeManager.h"
#include "base/ProductWorkflow.h"
#include "base/FlightConfiguration.h"
#include "platform/PlatformTypes.h"

#include "vsgraysim/layer1_scene/TerrainSystem.h"

#include <algorithm>
#include <future>

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QRegularExpression>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProcess>
#include <QRadioButton>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QShortcut>
#include <QTimer>
#include <QActionGroup>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <future>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <utility>

#include <nlohmann/json.hpp>

#include <spdlog/spdlog.h>

#include <vsg/core/Version.h>
#include <vsg/maths/transform.h>
#include <vsg/maths/vec3.h>

#include "vsgraysim/core/SensorTypes.h"
#include "PreviewRenderer.h"
#include "AssetBrowserWidget.h"
#include "SceneValidator.h"

#include "viewport3d/Viewport3DView.h"
#include "viewport3d/ViewportConfig.h"
#include "viewport3d/SceneDesignFacade.h"
#include "viewport3d/SceneEntities.h"
#include "viewport3d/TerrainEditFacade.h"
#include "viewport3d/EarthView.h"
#include <vsgraysim/earth/EarthDataService.h>
#include "viewport3d/EnvFeatures.h"   // W2: SkyFeature/CloudFeature/OceanFeature setAtmosphereParams

#include "base/PropertySchema.h"
#include "base/PropertyTreeWidget.h"
#include "display/RelationshipGraphView.h"
#include "viewport3d/RelationshipVisualFeature.h"
#include "viewport3d/DetectionEffectFeature.h"

namespace vsgraysim { namespace gui {

namespace {

void voxelTopicUi(QMainWindow& window,bool voxel){
    const QStringList legacy={QStringLiteral("地形生成"),QStringLiteral("地形雕刻"),QStringLiteral("地形笔刷"),QStringLiteral("水面系统"),QStringLiteral("模型库"),QStringLiteral("地形分析"),QStringLiteral("地形文件")};
    for(auto* dock:window.findChildren<QDockWidget*>()){
        if(legacy.contains(dock->windowTitle())){dock->setVisible(!voxel);dock->widget()->setEnabled(!voxel);dock->toggleViewAction()->setEnabled(!voxel);}
        if(dock->objectName()=="voxelAuthoringDock"){dock->setVisible(voxel);dock->toggleViewAction()->setEnabled(voxel);}
    }
    for(auto* label:window.findChildren<QLabel*>()){
        if(label->text().contains(QStringLiteral("实体添加:"))&&!label->property("ordinarySceneHint").isValid())label->setProperty("ordinarySceneHint",label->text());
        if(label->property("ordinarySceneHint").isValid())label->setText(voxel?QStringLiteral("体素对象与装备列表。\n使用右侧体素面板导入模型、编辑地形和赋值参数。"):label->property("ordinarySceneHint").toString());
    }
    for(auto* action:window.findChildren<QAction*>()){
        const auto text=action->text();if(text.startsWith(QStringLiteral("保存地形工程"))||text.startsWith(QStringLiteral("打开地形工程"))||text.startsWith(QStringLiteral("加载地球地形包"))||text.startsWith(QStringLiteral("导出 rocky 地形包")))action->setEnabled(!voxel);
    }
}

const char* sensorTypeNameFromInt(int t)
{
    return sensorTypeName(SensorType(t));
}

// 航迹类型: 0过点/1巡逻/2巡航/3拦截/4护送/5待命 (与仿真 software 默认一致)。
QString routeTypeName(int t)
{
    switch (t) {
        case 0:  return QStringLiteral("过点 (Waypoint)");
        case 1:  return QStringLiteral("巡逻 (Patrol)");
        case 2:  return QStringLiteral("巡航 (Cruise)");
        case 3:  return QStringLiteral("拦截/攻击 (Intercept)");
        case 4:  return QStringLiteral("护送 (Convoy)");
        case 5:  return QStringLiteral("待命 (Hold)");
        default: return QStringLiteral("过点 (Waypoint)");
    }
}

int routeTypeFromName(const QString& s)
{
    if (s.contains(QStringLiteral("巡逻")))        return 1;
    if (s.contains(QStringLiteral("巡航")))        return 2;
    if (s.contains(QStringLiteral("拦截")) || s.contains(QStringLiteral("攻"))) return 3;
    if (s.contains(QStringLiteral("护送")))        return 4;
    if (s.contains(QStringLiteral("待命")))        return 5;
    return 0;
}

// 节点类型 / 树 role / 属性分组 Schema 统一来自 shared PropertySchema 模块,
// 与仿真控制 / 统一显控共用同一套定义, 保证字段名与取值来源一致。
constexpr int kRoleNodeKind  = prop::RoleKeyNodeKind;
constexpr int kRoleObjectId  = prop::RoleKeyObjectId;
constexpr int kRoleSensorIdx = prop::RoleKeySensorIdx;

int sensorTypeFromName(const std::string& s)
{
    if (s == "vis")   return int(SensorType::VIS);
    if (s == "laser") return int(SensorType::LASER);
    if (s == "ir")    return int(SensorType::IR);
    if (s == "sar")   return int(SensorType::SAR);
    return int(SensorType::RADAR);
}

// ---------------------------------------------------------------------------
// §14.3 旧字段 → frame 块推导 (读取旧场景时补齐新 schema, 写回时物化)
// ---------------------------------------------------------------------------
// type/band → frame.kind (laser 不在 SensorFrameType 集合内, 返回空 = 不落 frame 块)
QString deriveFrameKind(int type, const QString& band)
{
    switch (SensorType(type)) {
        case SensorType::RADAR:
        case SensorType::SAR:   return QStringLiteral("RADAR");
        case SensorType::VIS:   return QStringLiteral("VIS");
        case SensorType::LASER: return QString();
        case SensorType::IR:    break;
    }
    if (band.compare(QLatin1String("SWIR"), Qt::CaseInsensitive) == 0) return QStringLiteral("SWIR");
    if (band.compare(QLatin1String("LWIR"), Qt::CaseInsensitive) == 0) return QStringLiteral("LWIR");
    if (band.compare(QLatin1String("VIS"),  Qt::CaseInsensitive) == 0) return QStringLiteral("VIS");
    return QStringLiteral("MWIR");   // ir + X/C/Ku/MWIR/未知 → MWIR 兜底
}

// kind → 波段上下限 (radar 用 tx_params.wavelength 单点)
double deriveLambdaMin(const QString& kind, double wavelength)
{
    if (kind == QLatin1String("VIS"))  return 0.38e-6;
    if (kind == QLatin1String("SWIR")) return 1.0e-6;
    if (kind == QLatin1String("MWIR")) return 3.0e-6;
    if (kind == QLatin1String("LWIR")) return 8.0e-6;
    return wavelength;               // RADAR: λ_min = λ_max = 载波波长
}
double deriveLambdaMax(const QString& kind, double wavelength)
{
    if (kind == QLatin1String("VIS"))  return 0.78e-6;
    if (kind == QLatin1String("SWIR")) return 3.0e-6;
    if (kind == QLatin1String("MWIR")) return 5.0e-6;
    if (kind == QLatin1String("LWIR")) return 12.0e-6;
    return wavelength;
}

// kind → 帧率: 雷达=PRF (相干时序以 PRF 计), 成像=1/integration_ms (积分时间决定帧序)
double deriveRateHz(const QString& kind, double pulse_rate, double integration_ms)
{
    if (kind == QLatin1String("RADAR")) return pulse_rate;
    if (integration_ms > 1e-3) return 1000.0 / integration_ms;
    return 0.0;
}

QColor factionColor(Faction f)
{
    switch (f) {
        case Faction::Red:  return QColor(0xff, 0x52, 0x52);
        case Faction::Blue: return QColor(0x44, 0x8a, 0xff);
        case Faction::Neutral: break;
    }
    return QColor(0x9e, 0x9e, 0x9e);
}

const char* factionToString(Faction f)
{
    switch (f) {
        case Faction::Red:  return "red";
        case Faction::Blue: return "blue";
        case Faction::Neutral: break;
    }
    return "neutral";
}

Faction factionFromString(const std::string& s)
{
    if (s == "red")  return Faction::Red;
    if (s == "blue") return Faction::Blue;
    return Faction::Neutral;
}

QString weatherLabel(const QString& code)
{
    if (code == QLatin1String("rain")) return QObject::tr("降雨 rain");
    if (code == QLatin1String("fog"))  return QObject::tr("雾 fog");
    if (code == QLatin1String("snow")) return QObject::tr("降雪 snow");
    return QObject::tr("晴朗 clear");
}

QString weatherCodeFromLabel(const QString& label)
{
    if (label.contains(QLatin1String("雨"))) return QLatin1String("rain");
    if (label.contains(QLatin1String("雾"))) return QLatin1String("fog");
    if (label.contains(QLatin1String("雪"))) return QLatin1String("snow");
    return QLatin1String("clear");
}

viewport3d::Faction toLibFaction(Faction f)
{
    return viewport3d::Faction(int(f));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
SceneDesignerWindow::SceneDesignerWindow(const QString& terrain_template,
                                         const QString& open_scenario,
                                         QWidget* parent,const QString& topic)
    : ui::BaseWindow(tr("场景设计器"), parent)
{
    topic_path_=topic;
    if(!topic.isEmpty()){topic_document_=VoxelSceneProject::open(topic.toStdString()).document;topic_baseline_pending_=true;}
    terrain_template_ = terrain_template.toStdString();
    open_scenario_on_start_ = open_scenario;

    // 1. 中央 3D 视口
    buildCentralWidget();

    // 2. 7 个分类 Dock
    buildProductDocks();

    auto* voxelDock=new QDockWidget(tr("体素地形与多波段属性"),this);voxelDock->setObjectName("voxelAuthoringDock");
    voxelDock->setWidget(new ui::SceneAuthoringPanel([this]()->earth::VoxelScene*{auto* f=viewport_->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene");return f?f->runtime():nullptr;},[this]{viewport_->markActivity();refreshSceneTree();},voxelDock));addDockWidget(Qt::RightDockWidgetArea,voxelDock);

    // 3. 公共菜单栏/工具栏/状态栏
    buildBaseStatusBar();
    buildBaseMenuBar();
    buildBaseToolBar();

    voxelTopicUi(*this,!topic_path_.isEmpty());

    // 4. 扩展状态栏标签
    auto* sb = statusBar();
    sb_objects_ = new QLabel(tr("实体 0"));
    sb_sensors_ = new QLabel(tr("传感器 0"));
    sb_terrain_ = new QLabel(tr("地形 -"));
    sb_water_   = new QLabel(tr("水面 -"));
    for (auto* lbl : {sb_objects_, sb_sensors_, sb_terrain_, sb_water_}) {
        lbl->setStyleSheet("padding: 2px 8px; color: #9aa5b8;"
                           "font-family: monospace;");
        sb->addWidget(lbl);
    }

    // 启动注入: 地形模板经门面排队, 场景就绪后自动装载。
    if (!terrain_template_.empty())
        facade_->loadTerrainTemplate(terrain_template_);
    if (!open_scenario_on_start_.isEmpty()) {
        const QString p = open_scenario_on_start_;
        open_scenario_on_start_.clear();
        connect(viewport_, &viewport3d::Viewport3DView::vsgReady, this,
                [this, p]() { saved_document_ = sceneDocument().dump(); loadScenarioFromFile(p); });
    }
    if(!topic_path_.isEmpty()) {
        edt_scenario_name_->setText(QString::fromStdString(topic_document_.value("name",std::string("体素专题"))));
    }
    connect(viewport_,&viewport3d::Viewport3DView::frameTick,this,[this](const auto&){
            auto* feature=viewport_->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene");
            if(topic_baseline_pending_&&feature&&feature->runtime()){
                feature->runtime()->setPaused(true);saved_document_=sceneDocument().dump();topic_baseline_pending_=false;voxelTopicUi(*this,true);if(terrain_facade_){terrain_facade_->setBrush(0,25,25,0);terrain_facade_->setAnalysisMode(0);}if(facade_)facade_->setPlacementEnabled(false);updateUndoActions();viewport_->setIdleSuspend(true,3000);refreshSceneTree();
                sb_objects_->setText(tr("体素对象 %1").arg(feature->runtime()->snapshot()->objects.size()));
                sb_sensors_->setText(tr("装备 %1").arg(topic_document_.contains("equipment")?topic_document_["equipment"]["equipment"].size():0));
                statusBar()->showMessage(tr("体素专题已就绪，可保存完整状态并交付正式仿真"),5000);
            }
        });
    saved_document_ = sceneDocument().dump();
    spdlog::info("SceneDesignerWindow: built ({}x{})", width(), height());
}

SceneDesignerWindow::~SceneDesignerWindow() = default;

void SceneDesignerWindow::closeEvent(QCloseEvent* event)
{
    if (publishing_scene_ || !confirmUnsavedChanges()) { event->ignore(); return; }
    ui::BaseWindow::closeEvent(event);
}

bool SceneDesignerWindow::confirmUnsavedChanges()
{
    try {
        if (saved_document_ == sceneDocument().dump()) return true;
    } catch (const std::exception&) {
        // An invalid edit must still be retained when the user cancels closing.
    }
    const auto choice = QMessageBox::question(this, tr("场景尚未保存"),
        tr("当前场景有未保存的更改，是否先保存？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel) return false;
    if (choice == QMessageBox::Discard) return true;
    QString destination = current_scenario_path_;
    if (destination.isEmpty()) {
        vsgraysim::Workspace workspace(resolveWorkspaceRoot().toStdString()); workspace.ensureDirs();
        destination = QFileDialog::getSaveFileName(this, tr("保存场景"),
            QString::fromStdString(workspace.sceneDir()) + "/scenario.scenario.json", tr("场景文件 (*.json)"));
    }
    return !destination.isEmpty() && saveScenarioToPath(destination);
}

// ---------------------------------------------------------------------------
// Central widget
// ---------------------------------------------------------------------------
void SceneDesignerWindow::buildCentralWidget()
{
    viewport_ = new viewport3d::Viewport3DView(this);
    auto config = topic_path_.isEmpty() ? viewport3d::ViewportConfig::fromPreset("scene_designer")
        : viewport3d::ViewportConfig::fromTopicFile(topic_path_.toStdString(),"scene_designer");
    config.useWorkspaceTerrain();
    viewport_->applyConfig(config);
    if(!topic_path_.isEmpty())viewport_->configureFeature("voxel_scene",{{"paused",true}});
    connect(viewport_,&viewport3d::Viewport3DView::beforeFrameTick,this,[this]{
        if(!topic_path_.isEmpty()){auto* feature=viewport_->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene");viewport_->context().setSimulationTime(feature&&feature->runtime()?feature->runtime()->snapshot()->timeSeconds():0.);}
    });
    viewport_->setAutoFrame(true, /*target_fps=*/60);
    viewport_->setIdleSuspend(topic_path_.isEmpty(), /*idle_ms=*/3000);   // 编辑器按需渲染: 空闲停帧

    auto* central_tabs = new QTabWidget(this);
    central_tabs->setObjectName("CentralTabs");
    central_tabs->setStyleSheet(
        "QTabWidget::pane { border:1px solid #262e3b; background:#0d1117; }"
        "QTabBar::tab { background:#151b23; color:#9aa5b8; padding:6px 14px;"
        "  border:1px solid #262e3b; border-bottom:none; }"
        "QTabBar::tab:selected { background:#1d4f8a; color:#eaf2fc; }"
        "QTabBar::tab:hover { background:#222b3c; }");
    auto* globe = new viewport3d::Viewport3DView(central_tabs);
    globe->widget()->setImGuiEnabled(false);
    connect(globe,&viewport3d::Viewport3DView::frameTick,this,[globe,surface=earth::EarthSurfaceSnapshotPtr{}](const auto&) mutable {
        auto* feature = globe->featureAs<viewport3d::EarthViewFeature>("rocky_map");
        auto ready = feature ? feature->surfaceSnapshot() : nullptr;
        if (ready && ready != surface) globe->setIdleSuspend(true,3000);
        surface = std::move(ready);
    });
    const int globeTab = central_tabs->addTab(globe,tr("地球"));
    const int terrainTab = central_tabs->addTab(viewport_, tr("局部地形 TB"));
    central_tabs->setCurrentIndex(terrainTab);
    central_tabs->setTabToolTip(globeTab,tr("地球 (Home)"));
    central_tabs->setTabToolTip(terrainTab,tr("局部地形 TB (End)"));
    connect(central_tabs,&QTabWidget::currentChanged,this,
        [this,globe,globeTab,terrainTab,central_tabs,source=QString(),configured=false](int tab) mutable {
            const bool earthVisible = tab == globeTab;
            if (earthVisible && (!configured || source != topic_path_)) {
                auto globeConfig = topic_path_.isEmpty() ? viewport3d::ViewportConfig::fromPreset("earth_display")
                    : viewport3d::ViewportConfig::fromTopicFile(topic_path_.toStdString(),"earth_display");
                globeConfig.useWorkspaceGlobe();
                globe->setIdleSuspend(false,3000);
                globe->applyConfig(globeConfig);
                configured = true;
                source = topic_path_;
            }
            globe->setAutoFrame(earthVisible,30);
            viewport_->setAutoFrame(tab == terrainTab,60);
            if (tab == globeTab || tab == terrainTab) {
                auto* current = tab == globeTab ? globe : viewport_;
                current->markActivity();
                current->widget()->setFocus();
            }
        });
    for (auto* view : {globe,viewport_}) {
        for (const auto& [key,tab] : {std::pair{Qt::Key_Home,globeTab},std::pair{Qt::Key_End,terrainTab}}) {
            auto* shortcut = new QShortcut(QKeySequence(key),view->widget());
            shortcut->setContext(Qt::WidgetShortcut);
            connect(shortcut,&QShortcut::activated,central_tabs,[central_tabs,tab] { central_tabs->setCurrentIndex(tab); });
        }
    }

    graph_bindings_ = new display::RelationshipGraphView(central_tabs);
    graph_bindings_->setEditable(true);   // 场景设计软件: 可编辑协作关系
    central_tabs->addTab(graph_bindings_, tr("关系图"));

    connect(graph_bindings_, &display::RelationshipGraphView::nodeClicked,
            this, &SceneDesignerWindow::selectObjectInTree);
    connect(graph_bindings_, &display::RelationshipGraphView::edgeRequested,
            this, [this](const QString& f, const QString& t) {
                onBindingAddGraph(f, t);
            });
    connect(graph_bindings_, &display::RelationshipGraphView::edgeRemoved,
            this, [this](const QString& f, const QString& t) {
                onBindingRemoveGraph(f, t);
            });

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(4);
    layout->addWidget(central_tabs,1);
    SmartDeploymentPanel::Host host{
        [this]()->earth::VoxelScene*{
            if(topic_baseline_pending_||publishing_scene_)return nullptr;
            auto* feature=viewport_->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene");
            return feature?feature->runtime():nullptr;
        },
        [this]()->nlohmann::json&{return topic_document_;},
        [this]{return topic_path_;},
        [this]{
            const auto* row=tree_scene_?tree_scene_->currentItem():nullptr;
            return row?row->data(0,kRoleObjectId).toString():QString{};
        },
        [this]{
            viewport_->markActivity();
            refreshSceneTree();
            updateUndoActions();
            const auto equipment=topic_document_.value("equipment",nlohmann::json::object());
            sb_sensors_->setText(tr("装备 %1").arg(equipment.value("equipment",nlohmann::json::array()).size()));
        }
    };
    auto* deployment = new SmartDeploymentPanel(std::move(host),viewport_->widget(),central);
    layout->addWidget(deployment);
    connect(central_tabs,&QTabWidget::currentChanged,deployment,[deployment,globeTab,terrainTab](int tab) {
        deployment->setVisible(tab == globeTab || tab == terrainTab);
    });
    setCentralWidget(central);

    facade_ = new viewport3d::SceneDesignFacade(viewport_, this);
    // 地形编辑门面 (程序化生成/笔刷/水面/分析/工程存取): 依赖 scene_designer
    // preset 中的 terrain_layer / brush_edit / analysis_overlay 特性。
    terrain_facade_ = new viewport3d::TerrainEditFacade(viewport_, this);

    connect(viewport_, &viewport3d::Viewport3DView::vsgReady, this, [this]() {
        statusBar()->showMessage(tr("VSG 窗口就绪"), 3000);
        if(topic_path_.isEmpty())viewport_->resetCamera();
        viewport_->markActivity();
        refreshSceneTree();
    });

    connect(viewport_, &viewport3d::Viewport3DView::frameTick, this,
            [this](const viewport3d::FrameTick& tick) {
                setFrameLabel(tick.frame_index);
            });
    connect(viewport_, &viewport3d::Viewport3DView::fpsUpdated, this,
            [this](double fps) {
                setFpsLabel(fps);
            });

    // ---- 地形编辑门面回馈 → UI (自原地形编辑软件迁入) ----
    connect(terrain_facade_, &viewport3d::TerrainEditFacade::undoRedoChanged,
            this, &SceneDesignerWindow::updateUndoActions);
    connect(terrain_facade_, &viewport3d::TerrainEditFacade::statsReady, this,
            [this](double min_h, double max_h, double mean_h) {
                if (lbl_elev_stats_)
                    lbl_elev_stats_->setText(
                        tr("最高 %1 | 最低 %2 | 平均 %3 (m)")
                            .arg(max_h, 0, 'f', 1)
                            .arg(min_h, 0, 'f', 1)
                            .arg(mean_h, 0, 'f', 1));
            });
    connect(terrain_facade_, &viewport3d::TerrainEditFacade::heightsChanged,
            this, [this] {
                if (auto* t = terrain_facade_->terrain()) {
                    if (t->terrainType() == TerrainType::Ocean ||
                        t->terrainType() == TerrainType::Swamp) {
                        QSignalBlocker b1(cmb_water_), b2(sld_water_level_);
                        cmb_water_->setCurrentIndex(int(t->waterType()));
                        sld_water_level_->setValue(int(t->waterLevel()));
                    }
                }
                updateTerrainStatus();
            });
    connect(terrain_facade_, &viewport3d::TerrainEditFacade::analysisResult,
            this, [this](const QString& kind, const QString& text) {
                if (lbl_analysis_result_) lbl_analysis_result_->setText(text);
                if (kind == "los")
                    statusBar()->showMessage(
                        text.startsWith(QLatin1Char(0x2713))
                            ? tr("通视分析: 可见")
                            : tr("通视分析: 被遮挡"), 4000);
                else if (kind == "profile")
                    statusBar()->showMessage(tr("剖面分析完成"), 4000);
                else if (kind == "distance")
                    statusBar()->showMessage(tr("距离测量完成"), 4000);
                else if (kind == "pick_start")
                    statusBar()->showMessage(tr("已记录起点, 请点击终点"), 3000);
                else if (kind == "pick_miss")
                    statusBar()->showMessage(tr("未拾取到地形表面, 请点击地形"),
                                             3000);
            });
    connect(terrain_facade_, &viewport3d::TerrainEditFacade::profileReady,
            this, &SceneDesignerWindow::onProfileReady);

    connect(facade_, &viewport3d::SceneDesignFacade::groundPickClicked, this,
            [this](double x, double z) {
                if (!chk_place_ || !chk_place_->isChecked()) return;
                const int idx = selectedObjectIndex();
                if (idx < 0) return;
                SceneObject& obj = objects_[size_t(idx)];
                obj.pos[0] = x;
                obj.pos[2] = z;
                applyTransform(obj);
                syncEditorsFromSelection();
            });

    // 模型库卡片拖放 → 在拖放点地面位置创建实体。
    connect(viewport_, &viewport3d::Viewport3DView::modelDrop,
            this, &SceneDesignerWindow::onModelDropped);
    // 视口内点击实体 → 同步选中场景树, 属性 Dock 随之刷新。
    connect(facade_, &viewport3d::SceneDesignFacade::entityPicked,
            this, &SceneDesignerWindow::onEntityPicked);
}

// ---------------------------------------------------------------------------
// Dock 1: 场景信息
// ---------------------------------------------------------------------------
void SceneDesignerWindow::buildScenarioInfoDock()
{
    // 「场景信息」已并入右侧「属性变量」分组面板 (见 buildPropertiesDock)。
}

// ---------------------------------------------------------------------------
// Dock 2: 场景树 (默认左侧首位; 空则显示空, 非空显示树信息)
// ---------------------------------------------------------------------------
void SceneDesignerWindow::buildSceneTreeDock()
{
    auto* v = createDock(tr("场景树"), Qt::LeftDockWidgetArea);

    // 场景打开/关闭经工具栏与菜单入口 (工具栏「打开场景」/「保存场景」);
    // 场景树面板专注层次展示与导航, 不再内置场景文件按钮。

    auto* tip = new QLabel(
        tr("双击实体可定位 3D 视角; 选中实体后, 右侧「属性」Dock 可查看并编辑其配置。\n"
           "实体添加: 从右侧「模型库」拖动卡片到 3D 视口即可。"));
    tip->setWordWrap(true);
    tip->setStyleSheet("color:#8a95a8; font-size:11px;");
    v->addWidget(tip);

    tree_scene_ = new QTreeWidget;
    tree_scene_->setHeaderHidden(true);
    tree_scene_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_scene_->setContextMenuPolicy(Qt::CustomContextMenu);
    v->addWidget(tree_scene_, 1);   // 垂直拉伸填满整个场景树 Dock

    connect(tree_scene_, &QTreeWidget::itemSelectionChanged,
            this, &SceneDesignerWindow::onSceneTreeSelectionChanged);
    // 双击平台/目标节点 → 3D 视口聚焦该平台视角。
    connect(tree_scene_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (!item) return;
                const QString oid = item->data(0, kRoleObjectId).toString();
                if(oid.isEmpty())return;
                if(!topic_path_.isEmpty()){
                    auto* feature=viewport_->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene");
                    if(!feature||!feature->runtime()||!viewport_->context().camera())return;
                    const auto scene=feature->runtime()->snapshot();
                    const auto object=scene->objects.find(oid.toStdString());
                    auto look=viewport_->context().camera()->viewMatrix.cast<vsg::LookAt>();
                    if(object==scene->objects.end()||!look)return;
                    const auto bounds=object->second.localBounds();
                    if(!bounds.valid())return;
                    const auto centre=object->second.pose(scene->tick,scene->fixedStep)*((bounds.min+bounds.max)*.5);
                    const auto axis=look->eye-look->center;
                    if(vsg::length(axis)<1e-6)return;
                    look->center=simcore::geo::localToECEF(scene->anchor,centre);
                    look->eye=look->center+vsg::normalize(axis)*std::max(2.5,2.8*vsg::length(bounds.max-bounds.min));
                    viewport_->markActivity();
                    return;
                }
                if(facade_)facade_->focusOnEntity(oid);
            });
    // 右键菜单: 场景树内增删 传感器/电磁分组/航路点/实体 (属性变量面板不再提供增删)。
    connect(tree_scene_, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                auto* item = tree_scene_->itemAt(pos);
                if (!item) return;
                const int kind  = item->data(0, kRoleNodeKind).toInt();
                const QString oid = item->data(0, kRoleObjectId).toString();
                const int sub   = item->data(0, kRoleSensorIdx).toInt();
                const auto idxOf = [this](const QString& id) {
                    for (size_t i = 0; i < objects_.size(); ++i)
                        if (objects_[i].id == id) return int(i);
                    return -1;
                };
                auto* menu = new QMenu(this);
                QAction* a = nullptr;
                if (kind == int(prop::NodeKind::Entity)) {
                    tree_scene_->setCurrentItem(item);
                    a = menu->addAction(tr("添加传感器"));
                    connect(a, &QAction::triggered, this, &SceneDesignerWindow::onAddSensor);
                    a = menu->addAction(tr("添加电磁分组"));
                    connect(a, &QAction::triggered, this, &SceneDesignerWindow::onAddFaceGroup);
                    a = menu->addAction(tr("添加热部件"));
                    connect(a, &QAction::triggered, this, &SceneDesignerWindow::onAddThermalPart);
                    a = menu->addAction(tr("添加航路点"));
                    connect(a, &QAction::triggered, this, &SceneDesignerWindow::onWaypointAdd);
                    a = menu->addAction(tr("删除该实体"));
                    connect(a, &QAction::triggered, this, &SceneDesignerWindow::onRemoveObject);
                } else if (kind == int(prop::NodeKind::Sensor)) {
                    tree_scene_->setCurrentItem(item);
                    a = menu->addAction(tr("删除该传感器"));
                    connect(a, &QAction::triggered, this, [this, idxOf, oid, sub]() {
                        const int oi = idxOf(oid);
                        if (oi < 0) return;
                        auto& sensors = objects_[size_t(oi)].sensors;
                        if (sub < 0 || size_t(sub) >= sensors.size()) return;
                        sensors.erase(sensors.begin() + sub);
                        SceneObject& o = objects_[size_t(oi)];
                        refreshSceneTree();
                        updateStatusCounts();
                        refreshPropsTable();
                    });
                } else if (kind == int(prop::NodeKind::FaceGroup)) {
                    tree_scene_->setCurrentItem(item);
                    a = menu->addAction(tr("删除该电磁分组"));
                    connect(a, &QAction::triggered, this, [this, idxOf, oid, sub]() {
                        const int oi = idxOf(oid);
                        if (oi < 0) return;
                        auto& fgs = objects_[size_t(oi)].face_groups;
                        if (sub < 0 || size_t(sub) >= fgs.size()) return;
                        if (fgs.size() <= 1) {
                            statusBar()->showMessage(tr("至少保留一个电磁分组"), 2000);
                            return;
                        }
                        fgs.erase(fgs.begin() + sub);
                        refreshSceneTree();
                        onFaceGroupEdited();
                    });
                } else if (kind == int(prop::NodeKind::Waypoint)) {
                    tree_scene_->setCurrentItem(item);
                    a = menu->addAction(tr("删除该航路点"));
                    connect(a, &QAction::triggered, this, [this, idxOf, oid, sub]() {
                        const int oi = idxOf(oid);
                        if (oi < 0) return;
                        auto& wps = objects_[size_t(oi)].waypoints;
                        if (sub < 0 || size_t(sub) >= wps.size()) return;
                        wps.erase(wps.begin() + sub);
                        SceneObject& o = objects_[size_t(oi)];
                        refreshSceneTree();
                        syncTrajectoryToFacade(o);
                        refreshPropsTable();
                    });
                }
                if (menu->isEmpty()) { menu->deleteLater(); return; }
                menu->exec(tree_scene_->viewport()->mapToGlobal(pos));
                menu->deleteLater();
            });
}

// ---------------------------------------------------------------------------
// 右侧 Dock: 模型库卡片 (拖动卡片到 3D 视口创建实体; 双击亦可)
// ---------------------------------------------------------------------------
void SceneDesignerWindow::buildModelLibraryDock()
{
    auto* v = createDock(tr("模型库"), Qt::RightDockWidgetArea);

    auto* hint = new QLabel(
        tr("拖动模型卡片到 3D 场景即可添加实体 (在落点地面放置)。\n"
           "双击卡片也可添加 (自动进入点击放置模式)。"));
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#8a95a8; font-size:11px;");
    v->addWidget(hint);

    auto* asset_browser = new AssetBrowserWidget;
    asset_browser->setMinimumHeight(320);
    auto assets = std::vector<vsgraysim::WorkspaceEntry>{};
    QDir model_root(QString::fromStdString("models"));
    if (model_root.exists()) {
        QDirIterator it(model_root.absolutePath(),
                        QStringList() << "*.obj" << "*.gltf" << "*.glb"
                                      << "*.fbx" << "*.dae" << "*.ply" << "*.stl" << "*.3ds" << "*.vsgt",
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            QFileInfo fi(path);
            vsgraysim::WorkspaceEntry e;
            e.path = path.toStdString();
            e.name = fi.completeBaseName().toStdString();
            e.preview.clear();
            assets.push_back(e);
        }
    }
    if (assets.empty()) {
        static const std::array<QString, 4> fallback = {
            QStringLiteral("models/M1A2/M1A2.obj"),
            QStringLiteral("models/AGM-114/AGM-114.obj"),
            QStringLiteral("models/Mountain/mountain.obj"),
            QStringLiteral("models/AxisTest/axis_test.obj")
        };
        for (const auto& p : fallback) {
            if (QFileInfo::exists(p)) {
                vsgraysim::WorkspaceEntry e;
                e.path = p.toStdString();
                e.name = QFileInfo(p).completeBaseName().toStdString();
                assets.push_back(e);
            }
        }
    }
    std::sort(assets.begin(), assets.end(),
              [](const vsgraysim::WorkspaceEntry& a,
                 const vsgraysim::WorkspaceEntry& b) {
                  return a.name < b.name;
              });
    asset_browser->setEntries(assets);
    v->addWidget(asset_browser, 1);

    // 双击: 在原点添加并进入点击放置模式。
    connect(asset_browser, &AssetBrowserWidget::activated,
            this, [this](const vsgraysim::WorkspaceEntry& entry) {
                if (entry.path.empty()) return;
                const QString path = QString::fromStdString(entry.path);
                const QString id = QString("object_%1").arg(objects_.size() + 1);
                const double pos[3] = {0.0, 0.0, 0.0};
                if (!addObjectToScene(id, path, pos, 0.0, 1.0)) {
                    QMessageBox::warning(this, tr("加载失败"),
                                         tr("无法加载模型: %1").arg(path));
                    return;
                }
                if (chk_place_) chk_place_->setChecked(true);
                if (facade_) facade_->setPlacementEnabled(true);
                statusBar()->showMessage(tr("已部署资产: %1").arg(path), 3000);
            });
}

// ---------------------------------------------------------------------------
// 右侧 Dock: 属性 (Tab 整合 变换/传感器/电磁光学/航迹/绑定; 可查看+编辑)
// ---------------------------------------------------------------------------
QVBoxLayout* SceneDesignerWindow::beginPropsTab(const QString& title)
{
    // 可折叠分组头 (首组默认展开; 折叠后隐藏内容, 仅保留标题条)。
    auto* head = new QToolButton;
    head->setCheckable(true);
    head->setChecked(true);
    head->setText(title);
    head->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    head->setArrowType(Qt::DownArrow);
    head->setStyleSheet(
        "QToolButton{ background:#1a2130; border:1px solid #262e3b;"
        " border-radius:4px; padding:6px 8px; color:#c9d4e3; font-weight:600; }"
        "QToolButton:checked{ color:#7fc8ff; border-color:#2d6fb8; }"
        "QToolButton:hover{ background:#222b3c; }");
    auto* content = new QWidget;
    content->setVisible(true);
    auto* lay = new QVBoxLayout(content);
    lay->setContentsMargins(4, 6, 4, 2);
    lay->setSpacing(4);
    connect(head, &QToolButton::toggled, this, [content, head](bool on) {
        content->setVisible(on);
        head->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });
    if (props_container_layout_) {
        props_container_layout_->addWidget(head);
        props_container_layout_->addWidget(content);
    }
    return lay;
}

void SceneDesignerWindow::buildPropertiesDock()
{
    auto* v = createDock(tr("属性变量"), Qt::RightDockWidgetArea);

    // 单个滚动分组面板: 场景信息 + 工具按钮 + 属性参数分组树。
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    props_container_layout_ = new QVBoxLayout(content);
    props_container_layout_->setContentsMargins(2, 2, 2, 2);
    props_container_layout_->setSpacing(6);
    scroll->setWidget(content);
    v->addWidget(scroll, 1);

    // 场景信息 (替代独立的「场景信息」Dock; 点击场景树根节点亦可在此查看/编辑)。
    {
        auto* g_scn = new QGroupBox(tr("场景"));
        auto* f = new QFormLayout(g_scn);
        edt_scenario_name_ = new QLineEdit(tr("未命名场景"));
        edt_scenario_desc_ = new QLineEdit;
        edt_scenario_desc_->setPlaceholderText(tr("场景用途简述 (可选)"));
        f->addRow(tr("名称"), edt_scenario_name_);
        f->addRow(tr("描述"), edt_scenario_desc_);
        props_container_layout_->addWidget(g_scn);
        connect(edt_scenario_name_, &QLineEdit::textChanged, this,
                [this](const QString& t) {
                    if (tree_scene_ && tree_scene_->topLevelItemCount() > 0)
                        tree_scene_->topLevelItem(0)->setText(0, t);
                });
    }

    // 属性参数分组树 (公共 PropertyTreeWidget; 选中场景树不同节点显示对应属性分组)。
    tree_props_ = new base::PropertyTreeWidget;
    tree_props_->setEditable(true);   // 场景设计: 可编辑, 编辑经 valueEdited 回写
    props_container_layout_->addWidget(tree_props_, 1);
    connect(tree_props_, &base::PropertyTreeWidget::valueEdited,
            this, &SceneDesignerWindow::onPropTreeEdited);
    // 传感器/电磁分组/航路点的增删已迁至场景树右键菜单, 属性面板仅做值设置。
}

// ---------------------------------------------------------------------------
// 属性·变换
// ---------------------------------------------------------------------------
void SceneDesignerWindow::buildTransformDock()
{
    auto* v = beginPropsTab(tr("变换"));

    auto make_dbl = [this](double lo, double hi, double val, int dec = 2, double step = 1.0) {
        auto* s = new QDoubleSpinBox;
        s->setRange(lo, hi);
        s->setDecimals(dec);
        s->setSingleStep(step);
        s->setValue(val);
        return s;
    };

    chk_place_ = new QCheckBox(tr("点击放置模式 (在 3D 视口中左键放置所选实体)"));
    chk_place_->setChecked(true);
    v->addWidget(chk_place_);

    auto* f_tr = new QFormLayout;
    cmb_faction_ = new QComboBox;
    cmb_faction_->addItem(tr("红方"), int(Faction::Red));
    cmb_faction_->addItem(tr("蓝方"), int(Faction::Blue));
    cmb_faction_->addItem(tr("中立"), int(Faction::Neutral));
    f_tr->addRow(tr("阵营"), cmb_faction_);
    spn_pos_x_ = make_dbl(-100000, 100000, 0.0);
    spn_pos_y_ = make_dbl(-10000, 100000, 0.0);
    spn_pos_z_ = make_dbl(-100000, 100000, 0.0);
    spn_yaw_   = make_dbl(-360, 360, 0.0, 1);
    spn_scale_ = make_dbl(0.001, 10000, 1.0, 3);
    spn_mass_  = make_dbl(0, 1e9, 1000.0, 1, 100.0);
    f_tr->addRow(tr("位置 X (m)"), spn_pos_x_);
    f_tr->addRow(tr("位置 Y (m)"), spn_pos_y_);
    f_tr->addRow(tr("位置 Z (m)"), spn_pos_z_);
    f_tr->addRow(tr("朝向 Yaw (°)"), spn_yaw_);
    f_tr->addRow(tr("缩放"), spn_scale_);
    f_tr->addRow(tr("质量 (kg)"), spn_mass_);
    v->addLayout(f_tr);

    connect(chk_place_, &QCheckBox::toggled, this, &SceneDesignerWindow::onPlaceToggled);
    connect(cmb_faction_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SceneDesignerWindow::onFactionEdited);
    for (auto* s : {spn_pos_x_, spn_pos_y_, spn_pos_z_, spn_yaw_, spn_scale_, spn_mass_})
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &SceneDesignerWindow::onTransformEdited);

    v->addStretch(1);
}

// ---------------------------------------------------------------------------
// Dock 4: 传感器配置
// ---------------------------------------------------------------------------
void SceneDesignerWindow::buildSensorDock()
{
    auto* v = beginPropsTab(tr("传感器"));

    lst_sensors_ = new QListWidget;
    lst_sensors_->setMinimumHeight(80);
    v->addWidget(lst_sensors_);

    auto* row_s_type = new QHBoxLayout;
    cmb_sensor_type_ = new QComboBox;
    cmb_sensor_type_->addItem(tr("雷达 radar"), int(SensorType::RADAR));
    cmb_sensor_type_->addItem(tr("红外 ir"),    int(SensorType::IR));
    cmb_sensor_type_->addItem(tr("激光 laser"), int(SensorType::LASER));
    cmb_sensor_type_->addItem(tr("SAR"),       int(SensorType::SAR));
    cmb_sensor_type_->addItem(tr("可见光 vis"), int(SensorType::VIS));
    cmb_sensor_band_ = new QComboBox;
    for (const char* b : {"X", "C", "Ku", "Ka", "S", "L",
                          "SWIR", "MWIR", "LWIR", "1.064um", "1.55um", "VIS"})
        cmb_sensor_band_->addItem(QString(b));
    row_s_type->addWidget(cmb_sensor_type_, 1);
    row_s_type->addWidget(cmb_sensor_band_, 1);
    v->addLayout(row_s_type);

    auto* row_s_role = new QHBoxLayout;
    cmb_sensor_role_ = new QComboBox;
    cmb_sensor_role_->addItem(tr("搜索 search"),   QStringLiteral("search"));
    cmb_sensor_role_->addItem(tr("制导 guidance"), QStringLiteral("guidance"));
    row_s_role->addWidget(new QLabel(tr("角色")));
    row_s_role->addWidget(cmb_sensor_role_, 1);
    v->addLayout(row_s_role);

    auto* f_sen = new QFormLayout;
    auto make_dbl = [this](double lo, double hi, double val, int dec = 2, double step = 1.0) {
        auto* s = new QDoubleSpinBox;
        s->setRange(lo, hi);
        s->setDecimals(dec);
        s->setSingleStep(step);
        s->setValue(val);
        return s;
    };
    spn_s_power_    = make_dbl(0, 1e9, 1000.0, 1, 100.0);
    spn_s_wavelen_  = make_dbl(1e-9, 10, 0.03, 6, 0.001);
    spn_s_beam_     = make_dbl(0.0001, 3.2, 0.05, 4, 0.01);
    spn_s_prf_      = make_dbl(0, 1e6, 1000.0, 1, 100.0);
    spn_s_duty_     = make_dbl(0, 1, 0.05, 4, 0.01);
    spn_s_fovh_     = make_dbl(0.1, 360, 60.0, 1);
    spn_s_fovv_     = make_dbl(0.1, 180, 30.0, 1);
    spn_s_offset_y_ = make_dbl(-100, 1000, 2.0);
    spn_s_az_       = make_dbl(-360, 360, 0.0, 1);
    spn_s_el_       = make_dbl(-90, 90, 0.0, 1);
    spn_s_range_    = make_dbl(1, 1e6, 500.0, 1, 50.0);
    f_sen->addRow(tr("发射功率 (W)"), spn_s_power_);
    f_sen->addRow(tr("波长 (m)"), spn_s_wavelen_);
    f_sen->addRow(tr("波束宽度 (rad)"), spn_s_beam_);
    f_sen->addRow(tr("脉冲重频 (Hz)"), spn_s_prf_);
    f_sen->addRow(tr("占空比"), spn_s_duty_);
    f_sen->addRow(tr("水平 FOV (°)"), spn_s_fovh_);
    f_sen->addRow(tr("垂直 FOV (°)"), spn_s_fovv_);
    f_sen->addRow(tr("安装高度 (m)"), spn_s_offset_y_);
    f_sen->addRow(tr("方位角 (°)"), spn_s_az_);
    f_sen->addRow(tr("俯仰角 (°)"), spn_s_el_);
    f_sen->addRow(tr("作用距离 (m)"), spn_s_range_);
    v->addLayout(f_sen);

    connect(lst_sensors_, &QListWidget::currentRowChanged,
            this, &SceneDesignerWindow::onSensorSelected);
    for (auto* s : {spn_s_power_, spn_s_wavelen_, spn_s_beam_, spn_s_prf_,
                    spn_s_duty_, spn_s_fovh_, spn_s_fovv_, spn_s_offset_y_,
                    spn_s_az_, spn_s_el_, spn_s_range_})
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &SceneDesignerWindow::onSensorEdited);
    connect(cmb_sensor_type_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { onSensorEdited(); });
    connect(cmb_sensor_band_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { onSensorEdited(); });
    connect(cmb_sensor_role_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (suppress_edit_signals_) return;
        const int oi = selectedObjectIndex();
        const int si = selectedSensorIndex();
        if (oi < 0 || si < 0 ||
            size_t(si) >= objects_[size_t(oi)].sensors.size()) return;
        objects_[size_t(oi)].sensors[size_t(si)].role =
            cmb_sensor_role_->currentData().toString();
        onSensorEdited();
    });

    v->addStretch(1);
}

// ---------------------------------------------------------------------------
// Dock 5: 电磁光学属性
// ---------------------------------------------------------------------------
void SceneDesignerWindow::buildFaceGroupDock()
{
    auto* v = beginPropsTab(tr("电磁光学"));

    auto make_dbl = [this](double lo, double hi, double val, int dec = 2, double step = 1.0) {
        auto* s = new QDoubleSpinBox;
        s->setRange(lo, hi);
        s->setDecimals(dec);
        s->setSingleStep(step);
        s->setValue(val);
        return s;
    };

    auto* row_fg_sel = new QHBoxLayout;
    cmb_face_group_ = new QComboBox;
    row_fg_sel->addWidget(cmb_face_group_, 1);
    v->addLayout(row_fg_sel);

    auto* f_fg = new QFormLayout;
    auto* row_fr = new QHBoxLayout;
    spn_fg_face_begin_ = new QSpinBox;
    spn_fg_face_begin_->setRange(0, 0x7FFFFFFE);
    spn_fg_face_end_ = new QSpinBox;
    spn_fg_face_end_->setRange(-1, 0x7FFFFFFE);
    spn_fg_face_end_->setValue(-1);
    spn_fg_face_end_->setSpecialValueText(tr("全部"));
    row_fr->addWidget(spn_fg_face_begin_, 1);
    row_fr->addWidget(new QLabel(tr("~")));
    row_fr->addWidget(spn_fg_face_end_, 1);
    cmb_brdf_ = new QComboBox;
    for (const char* b : {"diffuse", "roughmetal", "rubber", "rock", "metal"})
        cmb_brdf_->addItem(QString(b));
    spn_fg_albedo_     = make_dbl(0, 1, 0.25, 3, 0.05);
    spn_fg_rough_      = make_dbl(0, 1, 0.5, 3, 0.05);
    spn_fg_emiss_      = make_dbl(0, 1, 0.9, 3, 0.01);
    spn_fg_temp_       = make_dbl(100, 2000, 300.15, 2, 1.0);
    spn_fg_dielec_r_   = make_dbl(0, 200, 18.0, 2, 0.5);
    spn_fg_dielec_i_   = make_dbl(0, 200, 5.0, 2, 0.5);
    spn_fg_sigma0_     = make_dbl(-60, 20, -15.0, 1, 0.5);
    spn_fg_surf_rough_ = make_dbl(0, 10, 0.4, 3, 0.05);
    f_fg->addRow(tr("面片范围"), row_fr);
    f_fg->addRow(tr("光学 BRDF"), cmb_brdf_);
    f_fg->addRow(tr("反照率 albedo"), spn_fg_albedo_);
    f_fg->addRow(tr("粗糙度"), spn_fg_rough_);
    f_fg->addRow(tr("红外发射率"), spn_fg_emiss_);
    f_fg->addRow(tr("基础温度 (K)"), spn_fg_temp_);
    f_fg->addRow(tr("介电常数 实部"), spn_fg_dielec_r_);
    f_fg->addRow(tr("介电常数 虚部"), spn_fg_dielec_i_);
    f_fg->addRow(tr("σ0 (dB)"), spn_fg_sigma0_);
    f_fg->addRow(tr("表面粗糙度 (m)"), spn_fg_surf_rough_);
    v->addLayout(f_fg);

    connect(cmb_face_group_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SceneDesignerWindow::onFaceGroupSelected);
    for (auto* s : {spn_fg_albedo_, spn_fg_rough_, spn_fg_emiss_, spn_fg_temp_,
                    spn_fg_dielec_r_, spn_fg_dielec_i_, spn_fg_sigma0_,
                    spn_fg_surf_rough_})
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double) { onFaceGroupEdited(); });
    for (auto* s : {spn_fg_face_begin_, spn_fg_face_end_})
        connect(s, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { onFaceGroupEdited(); });
    connect(cmb_brdf_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { onFaceGroupEdited(); });

    // ---- §14.3 热部件 (thermal.parts) + 面组选择器 ----
    auto* tp_head = new QHBoxLayout;
    tp_head->addWidget(new QLabel(tr("热部件 (thermal.parts)")));
    tp_head->addStretch(1);
    v->addLayout(tp_head);
    lst_thermal_parts_ = new QListWidget;
    lst_thermal_parts_->setMinimumHeight(60);
    v->addWidget(lst_thermal_parts_);
    auto* f_tp = new QFormLayout;
    edt_tp_name_ = new QLineEdit;
    cmb_tp_face_group_ = new QComboBox;   // 面组选择器: 选项 = 当前实体 face_groups
    spn_tp_temp_  = make_dbl(100, 3000, 450.0, 1, 10.0);
    spn_tp_emiss_ = make_dbl(0, 1, 0.85, 3, 0.01);
    f_tp->addRow(tr("部件名"), edt_tp_name_);
    f_tp->addRow(tr("关联面组"), cmb_tp_face_group_);
    f_tp->addRow(tr("温度 (K)"), spn_tp_temp_);
    f_tp->addRow(tr("发射率"), spn_tp_emiss_);
    v->addLayout(f_tp);
    btn_tp_del_ = new QPushButton(tr("－ 删除热部件"));
    v->addWidget(btn_tp_del_);

    connect(lst_thermal_parts_, &QListWidget::currentRowChanged,
            this, &SceneDesignerWindow::onThermalPartSelected);
    connect(edt_tp_name_, &QLineEdit::textEdited,
            this, [this]() { onThermalPartEdited(); });
    connect(cmb_tp_face_group_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { onThermalPartEdited(); });
    for (auto* s : {spn_tp_temp_, spn_tp_emiss_})
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double) { onThermalPartEdited(); });
    connect(btn_tp_del_, &QPushButton::clicked,
            this, &SceneDesignerWindow::onRemoveThermalPart);

    v->addStretch(1);
}

// ---------------------------------------------------------------------------
// Dock 7: 航迹编辑
// ---------------------------------------------------------------------------
void SceneDesignerWindow::buildWaypointDock()
{
    auto* v = beginPropsTab(tr("航迹"));

    // 航迹规划参数
    auto* route_box = new QHBoxLayout;
    route_box->addWidget(new QLabel(tr("平台类型")));
    cmb_platform_cat_ = new QComboBox;
    cmb_platform_cat_->addItem(tr("地面车辆"), 0);
    cmb_platform_cat_->addItem(tr("空中目标"), 1);
    cmb_platform_cat_->addItem(tr("海面目标"), 2);
    cmb_platform_cat_->addItem(tr("导弹"), 3);
    route_box->addWidget(cmb_platform_cat_, 1);
    v->addLayout(route_box);

    auto* route_box2 = new QHBoxLayout;
    route_box2->addWidget(new QLabel(tr("航迹类型")));
    cmb_route_type_ = new QComboBox;
    cmb_route_type_->addItem(tr("过点 (Waypoint)"), 0);
    cmb_route_type_->addItem(tr("巡逻 (Patrol)"), 1);
    cmb_route_type_->addItem(tr("巡航 (Cruise)"), 2);
    cmb_route_type_->addItem(tr("拦截/攻击 (Intercept)"), 3);
    cmb_route_type_->addItem(tr("护送 (Convoy)"), 4);
    cmb_route_type_->addItem(tr("待命 (Hold)"), 5);
    route_box2->addWidget(cmb_route_type_, 1);
    v->addLayout(route_box2);

    auto* route2 = new QHBoxLayout;
    chk_route_loop_ = new QCheckBox(tr("循环"));
    route2->addWidget(chk_route_loop_);
    route2->addWidget(new QLabel(tr("启动延迟 (s)")));
    spn_route_delay_ = new QDoubleSpinBox;
    spn_route_delay_->setRange(0.0, 3600.0);
    spn_route_delay_->setDecimals(1);
    spn_route_delay_->setValue(0.0);
    route2->addWidget(spn_route_delay_, 1);
    v->addLayout(route2);

    tbl_waypoints_ = new QTableWidget(0, 5);
    tbl_waypoints_->setHorizontalHeaderLabels(
        {tr("时间 (s)"), tr("速度 (m/s)"), tr("X (m)"), tr("Y (m)"), tr("Z (m)")});
    tbl_waypoints_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tbl_waypoints_->setMinimumHeight(150);
    tbl_waypoints_->setSelectionBehavior(QAbstractItemView::SelectRows);
    v->addWidget(tbl_waypoints_);

    connect(tbl_waypoints_, &QTableWidget::itemChanged,
            this, &SceneDesignerWindow::onWaypointItemChanged);
    connect(cmb_route_type_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SceneDesignerWindow::onRouteTypeChanged);
    connect(cmb_platform_cat_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SceneDesignerWindow::onPlatformCatChanged);
    connect(chk_route_loop_, &QCheckBox::toggled,
            this, &SceneDesignerWindow::onRouteLoopToggled);
    connect(spn_route_delay_, &QDoubleSpinBox::valueChanged,
            this, &SceneDesignerWindow::onRouteDelayEdited);

    v->addStretch(1);
}

// ---------------------------------------------------------------------------
// Dock 8: 绑定/导引关系 (挂载绑定 EQUIPPED_ON 自动派生 + 导引/协同关系编辑)
// ---------------------------------------------------------------------------
namespace {
const char* kBindingRelations[] = {"guides", "fire_control_of", "target_of", "fusion_peer"};
const char* kBindingRelLabels[] = {"制导 (GUIDES)", "火力控制 (FIRE_CONTROL_OF)", "目标 (TARGET_OF)", "协同 (FUSION_PEER)"};
constexpr int kBindingRelCount = 4;
}

void SceneDesignerWindow::buildBindingsDock()
{
    auto* v = beginPropsTab(tr("绑定"));

    // 只读概览: 装备挂载 + 已建立的导引关系
    tree_bindings_ = new QTreeWidget;
    tree_bindings_->setColumnCount(1);
    tree_bindings_->setHeaderLabels({tr("绑定关系")});
    tree_bindings_->setMinimumHeight(140);
    v->addWidget(tree_bindings_);

    v->addSpacing(6);
    v->addWidget(new QLabel(tr("新增导引/协同关系:"), this));

    auto* row_from = new QHBoxLayout;
    row_from->addWidget(new QLabel(tr("源")));
    cmb_b_from_ = new QComboBox;
    row_from->addWidget(cmb_b_from_, 1);
    v->addLayout(row_from);

    auto* row_rel = new QHBoxLayout;
    row_rel->addWidget(new QLabel(tr("关系")));
    cmb_b_relation_ = new QComboBox;
    for (int i = 0; i < kBindingRelCount; ++i)
        cmb_b_relation_->addItem(tr(kBindingRelLabels[i]), QString::fromLatin1(kBindingRelations[i]));
    row_rel->addWidget(cmb_b_relation_, 1);
    v->addLayout(row_rel);

    auto* row_to = new QHBoxLayout;
    row_to->addWidget(new QLabel(tr("目标")));
    cmb_b_to_ = new QComboBox;
    row_to->addWidget(cmb_b_to_, 1);
    v->addLayout(row_to);

    auto* row_add = new QHBoxLayout;
    auto* btn_add = new QPushButton(tr("添加"));
    btn_b_del_ = new QPushButton(tr("删除所选"));
    row_add->addWidget(btn_add);
    row_add->addWidget(btn_b_del_);
    v->addLayout(row_add);

    lst_b_links_ = new QListWidget;
    lst_b_links_->setMinimumHeight(80);
    v->addWidget(lst_b_links_);

    connect(btn_add, &QPushButton::clicked, this, &SceneDesignerWindow::onBindingAdd);
    connect(btn_b_del_, &QPushButton::clicked, this, &SceneDesignerWindow::onBindingRemove);

    v->addStretch(1);
}

void SceneDesignerWindow::refreshBindingsPanel()
{
    // 1) 概览树: 装备挂载(EQUIPPED_ON, 自动) + 导引关系
    if (tree_bindings_) {
        tree_bindings_->clear();
        auto* root_eq = new QTreeWidgetItem(tree_bindings_,
            {tr("装备挂载 (自动, EQUIPPED_ON)")});
        for (const auto& o : objects_) {
            if (o.sensors.empty()) continue;
            auto* obj = new QTreeWidgetItem(root_eq, {o.id});
            for (const auto& s : o.sensors)
                new QTreeWidgetItem(obj, {QStringLiteral("# %1 [%2] → %3")
                    .arg(QString::fromUtf8(sensorTypeNameFromInt(s.type)))
                    .arg(s.band).arg(o.id)});
        }
        tree_bindings_->expandAll();

        auto* root_rel = new QTreeWidgetItem(tree_bindings_,
            {tr("导引/协同关系")});
        for (const auto& b : binding_links_)
            new QTreeWidgetItem(root_rel,
                {QStringLiteral("%1 --%2--> %3")
                    .arg(b.from_id, b.relation, b.to_id)});
        if (binding_links_.empty())
            new QTreeWidgetItem(root_rel, {tr("(未设置)")});
        tree_bindings_->expandAll();
    }

    // 1b) 关系图 (中央 Tab): 阵营分区 + 分层线型
    if (graph_bindings_) {
        const auto gFac = [](Faction f) -> int {
            return (f == Faction::Red) ? 2 : (f == Faction::Blue) ? 1 : 0;
        };
        QVector<display::GraphNode> nodes;
        nodes.reserve(objects_.size());
        for (const auto& o : objects_) {
            display::GraphNode n;
            n.id = o.id;
            n.label = QString("%1").arg(o.id);
            n.kind = int(prop::NodeKind::Entity);
            n.faction = gFac(o.faction);
            n.isMissile = (o.platform_cat == 3);
            nodes.append(n);
        }
        QVector<display::GraphEdge> edges;
        edges.reserve(binding_links_.size());
        for (const auto& b : binding_links_) {
            display::GraphEdge e;
            e.fromId = b.from_id;
            e.toId = b.to_id;
            e.relation = b.relation;
            edges.append(e);
        }
        graph_bindings_->setGraph(nodes, edges);
    }

    // 1c) 3D 平台标注: 同一拓扑喂给 VSG 特性 (仅标注, 关系连线只在关系图 Dock 展示)。
    if (viewport_) {
        if (auto* rv = viewport_->featureAs<
                viewport3d::RelationshipVisualFeature>("relationship_visual")) {
            std::vector<viewport3d::RelationshipVisualFeature::Marker> markers;
            markers.reserve(objects_.size());
            const auto facText = [](Faction f) -> QString {
                return (f == Faction::Red) ? QStringLiteral("红方")
                     : (f == Faction::Blue) ? QStringLiteral("蓝方")
                     : QStringLiteral("中立");
            };
            const auto catText = [](int cat) -> QString {
                switch (cat) {
                    case 0: return QStringLiteral("坦克");   // 地面车辆
                    case 1: return QStringLiteral("飞机");   // 空中
                    case 2: return QStringLiteral("舰船");   // 水面
                    case 3: return QStringLiteral("导弹");
                    default: return QStringLiteral("装备");
                }
            };
            for (const auto& o : objects_) {
                viewport3d::RelationshipVisualFeature::Marker m;
                m.id = o.id.toStdString();
                m.pos = vsg::vec3(float(o.pos[0]), float(o.pos[1]), float(o.pos[2]));
                m.faction = (o.faction == Faction::Red) ? 2
                           : (o.faction == Faction::Blue) ? 1 : 0;
                m.label = (facText(o.faction) + catText(o.platform_cat)).toStdString();
                markers.push_back(m);
            }
            rv->setGraph(markers);

            // 绑定/导引关系 → 3D 链路拱线 (制导橙/通信青, 端点锚平台顶部)。
            std::map<QString, vsg::vec3> pos_by_id;
            for (const auto& m : markers) pos_by_id[QString::fromStdString(m.id)] = m.pos;
            std::vector<viewport3d::RelationshipVisualFeature::Link> links;
            links.reserve(binding_links_.size());
            for (const auto& b : binding_links_) {
                const auto itf = pos_by_id.find(b.from_id);
                const auto itt = pos_by_id.find(b.to_id);
                if (itf == pos_by_id.end() || itt == pos_by_id.end()) continue;
                viewport3d::RelationshipVisualFeature::Link lk;
                lk.relation = b.relation.toStdString();
                lk.from = itf->second + vsg::vec3(0.0f, 4.0f, 0.0f);
                lk.to   = itt->second + vsg::vec3(0.0f, 4.0f, 0.0f);
                links.push_back(std::move(lk));
            }
            rv->setLinks(links);
        }

        // 1d) 探测特效 (探测范围+搜索角度+视口范围): 由场景树实体/传感器静态配置驱动。
        if (auto* det = viewport_->featureAs<
                viewport3d::DetectionEffectFeature>("detection_effect")) {
            std::vector<viewport3d::DetectionEffectFeature::Effect> fx;
            for (const auto& o : objects_) {
                const QColor col = viewport3d::factionColor(
                    static_cast<viewport3d::Faction>(int(o.faction)));
                for (int i = 0; i < int(o.sensors.size()); ++i) {
                    viewport3d::DetectionEffectFeature::Effect e;
                    const auto& s = o.sensors[size_t(i)];
                    e.entity_id = o.id.toStdString();
                    e.sensor_idx = i;
                    e.pos = vsg::dvec3(o.pos[0], o.pos[1], o.pos[2]);
                    e.yaw = o.yaw_deg;
                    e.az  = s.az_deg;
                    e.el  = s.el_deg;
                    e.fov_h = s.fov_h;
                    e.fov_v = s.fov_v;
                    e.range = s.range_m;
                    e.color = col;
                    fx.push_back(e);
                }
            }
            det->setEffects(fx);
        }
    }

    // 2) 候选下拉框
    if (!cmb_b_from_) return;
    {
        const QSignalBlocker bf(cmb_b_from_), bt(cmb_b_to_);
        cmb_b_from_->clear();
        cmb_b_to_->clear();
        for (const auto& o : objects_) {
            const QChar cat = o.platform_cat == 3 ? QLatin1Char('M') : QLatin1Char('P');
            const QString label = QStringLiteral("%1  [%2]").arg(o.id).arg(cat);
            cmb_b_from_->addItem(label, o.id);
            // 目标下拉略标注导弹 (M)
            cmb_b_to_->addItem(label, o.id);
        }
    }

    // 3) 关系列表 (仅绑定数据)
    if (lst_b_links_) {
        lst_b_links_->clear();
        for (const auto& b : binding_links_)
            lst_b_links_->addItem(QStringLiteral("%1 --%2--> %3")
                .arg(b.from_id, b.relation, b.to_id));
    }
}

void SceneDesignerWindow::onBindingAdd()
{
    if (!cmb_b_from_ || objects_.empty()) return;
    BindingLink b;
    b.from_id = cmb_b_from_->currentData().toString();
    b.relation = cmb_b_relation_->currentData().toString();
    b.to_id = cmb_b_to_->currentData().toString();
    if (b.from_id.isEmpty() || b.to_id.isEmpty()) return;

    // 去重
    for (const auto& e : binding_links_)
        if (e.from_id == b.from_id && e.relation == b.relation && e.to_id == b.to_id)
            return;
    binding_links_.push_back(std::move(b));
    refreshBindingsPanel();
}

void SceneDesignerWindow::onBindingRemove()
{
    if (!lst_b_links_ || lst_b_links_->currentRow() < 0) return;
    const int row = lst_b_links_->currentRow();
    if (row >= 0 && size_t(row) < binding_links_.size())
        binding_links_.erase(binding_links_.begin() + row);
    refreshBindingsPanel();
}

// 关系图建边 (拖拽): 复用关系下拉的当前关系类型。
void SceneDesignerWindow::onBindingAddGraph(const QString& from_id,
                                            const QString& to_id)
{
    QString rel = cmb_b_relation_ && cmb_b_relation_->currentData().isValid()
        ? cmb_b_relation_->currentData().toString()
        : QStringLiteral("target_of");
    BindingLink b;
    b.from_id = from_id;
    b.relation = rel;
    b.to_id = to_id;
    for (const auto& e : binding_links_)
        if (e.from_id == b.from_id && e.relation == b.relation && e.to_id == b.to_id)
            return;
    binding_links_.push_back(std::move(b));
    refreshBindingsPanel();
}

// 关系图删边 (双击): 删除匹配 from/to 的全部关系 (跨不同关系类型)。
void SceneDesignerWindow::onBindingRemoveGraph(const QString& from_id,
                                               const QString& to_id)
{
    for (auto it = binding_links_.begin(); it != binding_links_.end();) {
        if (it->from_id == from_id && it->to_id == to_id)
            it = binding_links_.erase(it);
        else
            ++it;
    }
    refreshBindingsPanel();
}

// ---------------------------------------------------------------------------
// 覆写 BaseWindow 扩展点
// ---------------------------------------------------------------------------
void SceneDesignerWindow::buildProductDocks()
{
    // 左: 场景树 (环境/地形属性已并入属性面板节点) + 地形编辑三 Dock
    buildSceneTreeDock();
    buildTerrainGenDock();
    buildBrushDock();
    buildWaterDock();
    // 右: 模型库卡片 / 属性变量 (统一分组面板: 场景信息 + 属性参数分组树)
    //     + 地形分析 / 地形文件操作
    buildModelLibraryDock();
    buildPropertiesDock();
    buildAnalysisDock();
    buildTerrainIODock();

    // 布局不 Tab: 各侧 Dock 上下垂直堆叠, 减少标签页切换。

    // 布局宽度适配: 侧边 Dock 自适应内部控件 (设置最小宽度), 并给出初始左右比例。
    QDockWidget* dock_tree = nullptr;
    QDockWidget* dock_lib  = nullptr;
    QDockWidget* dock_prop = nullptr;
    for (auto* d : findChildren<QDockWidget*>()) {
        const QString t = d->windowTitle();
        if (t == tr("场景树")) dock_tree = d;
        else if (t == tr("模型库")) dock_lib = d;
        else if (t == tr("属性变量")) dock_prop = d;
    }
    if (dock_tree) dock_tree->setMinimumWidth(300);   // 场景树: 容纳层次+图标
    if (dock_lib)  dock_lib->setMinimumWidth(250);    // 模型库卡片
    if (dock_prop) dock_prop->setMinimumWidth(360);   // 属性变量: 属性树两列
    if (dock_tree) resizeDocks({dock_tree}, {360}, Qt::Horizontal);
    if (dock_lib && dock_prop) resizeDocks({dock_lib, dock_prop}, {520, 400}, Qt::Vertical);
    setMinimumSize(1400, 800);   // 保证中央 3D 视口可用宽度
}

void SceneDesignerWindow::buildProductMenuItems(QMenuBar* mb)
{
    auto* mFile = mb->actions().size() > 0 ? mb->actions()[0]->menu() : nullptr;
    if (mFile) {
        auto* aSave = mFile->addAction(tr("&保存场景"));
        aSave->setShortcut(QKeySequence::Save);
        connect(aSave, &QAction::triggered, this, &SceneDesignerWindow::onSaveScenario);
        auto* aSaveAs = mFile->addAction(tr("场景另存为..."));
        aSaveAs->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
        connect(aSaveAs, &QAction::triggered, this, &SceneDesignerWindow::onSaveScenarioAs);
        auto* aOpen = mFile->addAction(tr("&打开场景..."));
        aOpen->setShortcut(QKeySequence::Open);
        connect(aOpen, &QAction::triggered, this, &SceneDesignerWindow::onLoadScenario);
        mFile->addSeparator();
        auto* aSaveProj = mFile->addAction(tr("保存地形工程..."));
        aSaveProj->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
        connect(aSaveProj, &QAction::triggered, this, &SceneDesignerWindow::onSaveTerrainProject);
        mFile->addAction(tr("加载地球地形包..."), this, [this] {
            const auto file = QFileDialog::getOpenFileName(this, tr("加载地球地形包"), {},
                                                           tr("地球地形包 (*.map.json)"));
            if (file.isEmpty()) return;
            if (!facade_->loadEarthPackage(file.toStdString())) {
                QMessageBox::warning(this, tr("地形加载失败"), tr("请检查地形包格式、数据来源和覆盖范围。"));
                return;
            }
            if (auto* layer = viewport_->featureAs<viewport3d::TerrainLayerFeature>(viewport3d::TerrainLayerFeature::kId))
                layer->clearTerrain();
            viewport_->markActivity();
            statusBar()->showMessage(tr("已加载地球地形包 %1").arg(QFileInfo(file).fileName()), 5000);
        });
        auto* aOpenProj = mFile->addAction(tr("打开地形工程..."));
        aOpenProj->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
        connect(aOpenProj, &QAction::triggered, this, &SceneDesignerWindow::onLoadTerrainProject);
    }

    // 编辑菜单: 地形编辑撤销/重做 (插在文件菜单之后)
    auto* editMenu = mb->addMenu(tr("&编辑"));
    act_undo_ = editMenu->addAction(tr("撤销"));
    act_undo_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z));
    connect(act_undo_, &QAction::triggered, this, &SceneDesignerWindow::onUndo);
    act_redo_ = editMenu->addAction(tr("重做"));
    act_redo_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
    connect(act_redo_, &QAction::triggered, this, &SceneDesignerWindow::onRedo);
    updateUndoActions();

    auto* topicMenu=mb->addMenu(tr("场景专题"));topicMenu->addAction(tr("打开地球联合场景专题…"),this,[this]{const auto path=QFileDialog::getOpenFileName(this,tr("打开场景专题"),"config/topics",tr("体素场景包 / 专题 (*.vsimscene *.vtopic.json)"));if(path.isEmpty())return;if(!QProcess::startDetached(QCoreApplication::applicationFilePath(),{"--scene-topic",QFileInfo(path).absoluteFilePath()}))QMessageBox::warning(this,tr("场景专题"),tr("无法打开专题窗口"));});
    auto* audio=topicMenu->addAction(tr("场景音效"));
    audio->setCheckable(true);
    connect(audio,&QAction::toggled,this,[this](bool on){
        if(viewport_){viewport_->widget()->setProperty("sceneAudioRequested",on);viewport_->markActivity();}
    });
    topicMenu->addAction(tr("从台北体素与装备模板开始…"),this,[this]{
        const auto source=QFileInfo("config/topics/taipei-equipment.vtopic.json").absoluteFilePath();
        if(!QProcess::startDetached(QCoreApplication::applicationFilePath(),{"--scene-topic",source}))QMessageBox::warning(this,tr("场景专题"),tr("无法打开台北体素模板"));
    });
    const auto publishVoxelScene = [this](bool terrainPages) {
        if (topic_path_.isEmpty()) {
            statusBar()->showMessage(tr("请先打开地球体素场景专题"), 4000);
            return;
        }
        auto path = QFileDialog::getSaveFileName(this, tr("发布体素场景包"), current_scenario_path_,
                                                 tr("体素场景包 (*.vsimscene)"));
        if (path.isEmpty()) return;
        if (!earth::VoxelScenePackage::extension(path.toStdString())) path += ".vsimscene";
        saveScenarioToPath(path, terrainPages);
    };
    topicMenu->addAction(tr("智能部署…"),this,[this]{
        if(topic_path_.isEmpty()||topic_baseline_pending_||publishing_scene_){
            statusBar()->showMessage(tr("请先打开并加载地球体素专题"),4000);
            return;
        }
        auto* tabs = findChild<QTabWidget*>("CentralTabs");
        if (tabs->currentIndex() > 1) tabs->setCurrentWidget(viewport_);
        if (auto* input = findChild<QPlainTextEdit*>("deploymentInstruction")) input->setFocus();
    });
    topicMenu->addAction(tr("发布体素场景包…"), this, [publishVoxelScene] { publishVoxelScene(false); });
    topicMenu->addAction(tr("发布分块体素场景包…"), this, [publishVoxelScene] { publishVoxelScene(true); });
    if (earth::voxelUsdEditsAvailable()) {
        topicMenu->addAction(tr("保存 USD 目标编辑层…"), this, [this] {
            auto path = QFileDialog::getSaveFileName(this, tr("保存 USD 目标编辑层"),
                QStringLiteral("target-edits.usda"), tr("OpenUSD 编辑层 (*.usda *.usd *.usdc)"));
            if (path.isEmpty()) return;
            if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".usda");
            saveUsdTargetEdits(path);
        });
        topicMenu->addAction(tr("应用 USD 目标编辑层…"), this, [this] {
            const auto path = QFileDialog::getOpenFileName(this, tr("应用 USD 目标编辑层"), {},
                tr("OpenUSD 编辑层 (*.usda *.usd *.usdc)"));
            if (!path.isEmpty()) applyUsdTargetEdits(path);
        });
    }
    topicMenu->addAction(tr("绑定体素装备配置…"),this,[this]{
        if(topic_path_.isEmpty()){statusBar()->showMessage(tr("请先打开地球体素场景专题"),4000);return;}
        const auto path=QFileDialog::getOpenFileName(this,tr("绑定已有装备配置"),"config/equipment",tr("装备配置 (*.vequipment.json *.json)"));if(path.isEmpty())return;
        try{std::ifstream input(path.toStdString());auto config=nlohmann::json::parse(input);earth::VoxelEquipmentConfig::parse(config,path.toStdString());config.erase("topic");topic_document_["equipment"]=config;sb_sensors_->setText(tr("装备 %1").arg(config.at("equipment").size()));statusBar()->showMessage(tr("装备配置已绑定，保存专题后交付正式仿真"),5000);}
        catch(const std::exception& error){QMessageBox::warning(this,tr("装备配置无效"),QString::fromUtf8(error.what()));}
    });
    auto* mTools = mb->addMenu(tr("&工具"));
    mTools->addAction(tr("模型工作台…"),this,[this] {
        auto* library=new ModelLibraryDialog(this);
        library->setAttribute(Qt::WA_DeleteOnClose);
        library->show();
    });
    mTools->addAction(tr("验证场景"), this, &SceneDesignerWindow::onValidateScene);
    mTools->addAction(tr("体素场景工坊…"), this, [this] {
        const auto executable = QCoreApplication::applicationDirPath() + "/vsgraysim_voxel_workshop";
        if (!QProcess::startDetached(executable, QStringList{}))
            QMessageBox::warning(this, tr("体素场景工坊"), tr("无法启动 %1，请先构建 vsgraysim_voxel_workshop。").arg(executable));
    });
    mTools->addAction(tr("导出 rocky 地形包..."), this,
                      &SceneDesignerWindow::onExportGeoPackage);
    auto* workflow = mb->addMenu(tr("工作流程"));
    auto* launch = workflow->addAction(tr("校验、保存并启动仿真"), this, &SceneDesignerWindow::onLaunchSimulation);
    launch->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    launch->setStatusTip(tr("将当前编辑状态保存为场景文件，再交给仿真主控初始化"));
    auto* backend = workflow->addMenu(tr("传感器执行后端"));
    auto* group = new QActionGroup(backend);
    for (const auto& choice : {std::pair<const char*, const char*>{"auto", "自动选择"},
                               {"gpu", "GPU 测量（严格）"}, {"cpu", "CPU 参考"}}) {
        auto* action = backend->addAction(QString::fromUtf8(choice.second));
        action->setCheckable(true); action->setData(QString::fromLatin1(choice.first)); group->addAction(action);
        connect(action, &QAction::triggered, this, [this, action]() {
            clock_config_.extra["sensor_backend"] = action->data().toString().toStdString();
            statusBar()->showMessage(tr("后端选择将在保存并重新初始化仿真后生效"), 4000);
        });
    }
    connect(backend, &QMenu::aboutToShow, this, [this, group]() {
        const auto& setting = clock_config_.extra;
        const auto current = setting.contains("sensor_backend") && setting["sensor_backend"].is_string()
            ? QString::fromStdString(setting["sensor_backend"].get<std::string>()) : QStringLiteral("auto");
        for (auto* action : group->actions()) action->setChecked(action->data().toString() == current);
    });
}

void SceneDesignerWindow::buildProductToolBarItems(QToolBar* tb)
{
    tb->addAction(tr("保存场景"), this, &SceneDesignerWindow::onSaveScenario);
    tb->addAction(tr("打开场景"), this, &SceneDesignerWindow::onLoadScenario);
    auto* launch = tb->addAction(tr("保存并启动仿真"), this, &SceneDesignerWindow::onLaunchSimulation);
    launch->setToolTip(tr("校验当前场景、原子保存，再启动仿真主控（Ctrl+Enter）"));
    tb->addSeparator();
    tb->addAction(tr("撤销"), this, &SceneDesignerWindow::onUndo);
    tb->addAction(tr("重做"), this, &SceneDesignerWindow::onRedo);
    tb->addSeparator();
    tb->addAction(tr("保存地形工程"), this, &SceneDesignerWindow::onSaveTerrainProject);
    tb->addAction(tr("打开地形工程"), this, &SceneDesignerWindow::onLoadTerrainProject);
}

// ---------------------------------------------------------------------------
// Entity management (保持原有逻辑不变)
// ---------------------------------------------------------------------------
int SceneDesignerWindow::selectedObjectIndex() const
{
    if (!tree_scene_) return -1;
    auto* item = tree_scene_->currentItem();
    if (!item) return -1;
    const int kind = item->data(0, kRoleNodeKind).toInt();
    // 实体/传感器/电磁分组/航路点子节点均可解析归属实体。
    if (kind != int(prop::NodeKind::Entity) &&
        kind != int(prop::NodeKind::Sensor) &&
        kind != int(prop::NodeKind::FaceGroup) &&
        kind != int(prop::NodeKind::Waypoint)) return -1;
    const QString oid = item->data(0, kRoleObjectId).toString();
    for (size_t i = 0; i < objects_.size(); ++i)
        if (objects_[i].id == oid) return int(i);
    return -1;
}

int SceneDesignerWindow::selectedSensorIndex() const
{
    return lst_sensors_ ? lst_sensors_->currentRow() : -1;
}

int SceneDesignerWindow::selectedFaceGroupIndex() const
{
    if (!cmb_face_group_) return -1;
    const QVariant d = cmb_face_group_->currentData();
    return d.isValid() ? d.toInt() : -1;
}

void SceneDesignerWindow::applyTransform(SceneObject& obj)
{
    facade_->setEntityTransform(obj.id, obj.pos, obj.yaw_deg, obj.scale);
    if (viewport_) viewport_->markActivity();   // 外部内容变更 → 唤醒渲染
}

bool SceneDesignerWindow::addObjectToScene(const QString& id, const QString& path,
                                           const double pos[3], double yaw_deg,
                                           double scale)
{
    // 与门面贴地规则保持一致: 低于地表抬到表面 (数据模型/属性/保存同步)。
    double p[3] = {pos[0], pos[1], pos[2]};
    if (auto* t = facade_->terrain()) {
        const double h = t->heightAtWorld(pos[0], pos[2]);
        if (p[1] < h - 0.05) p[1] = h;
    }
    if (!facade_->addEntity(id, path, p, yaw_deg, scale))
        return false;
    if (viewport_) viewport_->markActivity();
    SceneObject obj;
    obj.id = id;
    obj.mesh_path = path;
    obj.pos[0] = p[0]; obj.pos[1] = p[1]; obj.pos[2] = p[2];
    obj.yaw_deg = yaw_deg;
    obj.scale = scale;
    obj.face_groups.push_back(FaceGroupDef{});
    objects_.push_back(std::move(obj));
    refreshSceneTree();
    selectObjectInTree(id);
    return true;
}

void SceneDesignerWindow::clearObjects()
{
    facade_->clearEntities();
    objects_.clear();
    binding_links_.clear();
    refreshSceneTree();
}

void SceneDesignerWindow::syncTrajectoryToFacade(const SceneObject& obj)
{
    std::vector<viewport3d::Waypoint> wps;
    wps.reserve(obj.waypoints.size());
    for (const auto& w : obj.waypoints) {
        viewport3d::Waypoint wp;
        wp.time = w.time;
        wp.pos[0] = w.pos[0]; wp.pos[1] = w.pos[1]; wp.pos[2] = w.pos[2];
        wps.push_back(wp);
    }
    facade_->setWaypoints(obj.id, wps);
}

void SceneDesignerWindow::refreshSceneTree()
{
    if (!tree_scene_) return;
    if(!topic_path_.isEmpty()) {
        const auto selected=tree_scene_->currentItem()
            ?tree_scene_->currentItem()->data(0,kRoleObjectId).toString():QString{};
        const QSignalBlocker blocker(tree_scene_);
        tree_scene_->clear();
        auto* root=new QTreeWidgetItem(tree_scene_,{edt_scenario_name_->text()});
        root->setData(0,kRoleNodeKind,int(prop::NodeKind::Scenario));
        std::map<std::string,QTreeWidgetItem*> rows;
        std::map<uint32_t,std::string> mounted;
        std::map<std::string,QString> platformNames;
        const auto equipment=topic_document_.value("equipment",nlohmann::json::object());
        for(const auto& platform:equipment.value("platforms",nlohmann::json::array())){
            const auto id=platform.at("object").get<std::string>();
            mounted.emplace(platform.at("id").get<uint32_t>(),id);
            platformNames.emplace(id,QString::fromStdString(platform.at("name").get<std::string>()));
        }
        auto* feature=viewport_?viewport_->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene"):nullptr;
        if(feature&&feature->runtime())for(const auto& [id,object]:feature->runtime()->snapshot()->objects){
            const auto objectId=QString::fromStdString(id);
            const auto type=object.earth?tr("新数据地形"):tr("体素目标");
            const auto name=platformNames.count(id)?platformNames.at(id)+QString(" [%1]").arg(objectId):objectId;
            auto* row=new QTreeWidgetItem(root,{name+QString(" · ")+type});
            rows.emplace(id,row);
            row->setData(0,kRoleNodeKind,-1);
            row->setData(0,kRoleObjectId,objectId);
            if(objectId==selected) tree_scene_->setCurrentItem(row);
            if(object.poseTrack){
                auto* route=new QTreeWidgetItem(row,{tr("航迹 · %1 个关键点").arg(object.poseTrack->size())});
                route->setData(0,kRoleNodeKind,-1);
                route->setData(0,kRoleObjectId,objectId);
                row->setExpanded(true);
            }
        }
        for(const auto* list:{"equipment","navigation","depth_sounders"}){
            for(const auto& sensor:equipment.value(list,nlohmann::json::array())){
                auto* parent=root;
                auto mount=mounted.find(sensor.value("platform_id",uint32_t(0)));
                if(mount!=mounted.end()&&rows.count(mount->second))parent=rows.at(mount->second);
                const auto name=QString::fromStdString(sensor.at("name").get<std::string>());
                auto* row=new QTreeWidgetItem(parent,{name+QString(" #%1").arg(sensor.at("id").get<uint32_t>())});
                row->setData(0,kRoleNodeKind,-1);
                parent->setExpanded(true);
            }
        }
        root->setExpanded(true);
        return;
    }
    QString prev_id;
    if (auto* cur = tree_scene_->currentItem())
        prev_id = cur->data(0, kRoleObjectId).toString();

    const QSignalBlocker b(tree_scene_);
    tree_scene_->clear();
    auto* root = new QTreeWidgetItem({edt_scenario_name_->text()});
    root->setData(0, kRoleNodeKind, int(prop::NodeKind::Scenario));
    QFont rf = root->font(0);
    rf.setBold(true);
    root->setFont(0, rf);

    namespace fs = std::filesystem;
    const std::string terrain_ref = facade_ ? facade_->terrainRef() : std::string();
    const QString terrain_label = terrain_ref.empty()
        ? tr("rocky 地球底座")
        : tr("地形: %1").arg(QString::fromStdString(fs::path(terrain_ref).stem().string()));
    auto* tnode = new QTreeWidgetItem({terrain_label});
    tnode->setData(0, kRoleNodeKind, int(prop::NodeKind::Terrain));
    root->addChild(tnode);

    // 环境节点 (可点击查看环境属性)。
    auto* enode = new QTreeWidgetItem({
        tr("环境: %1").arg(weatherLabel(env_weather_))});
    enode->setData(0, kRoleNodeKind, int(prop::NodeKind::Environment));
    root->addChild(enode);

    for (const auto& o : objects_) {
        const QColor fc = factionColor(o.faction);
        auto* onode = new QTreeWidgetItem(
            {QString("%1  [%2]").arg(o.id).arg(QFileInfo(o.mesh_path).fileName())});
        onode->setData(0, kRoleNodeKind, int(prop::NodeKind::Entity));
        onode->setData(0, kRoleObjectId, o.id);
        onode->setForeground(0, fc);
        int sn = 1;
        for (size_t si = 0; si < o.sensors.size(); ++si) {
            const auto& s = o.sensors[si];
            auto* snode = new QTreeWidgetItem(
                {QString("#%1 %2 · %3").arg(sn++).arg(sensorTypeNameFromInt(s.type)).arg(s.band)});
            snode->setData(0, kRoleNodeKind, int(prop::NodeKind::Sensor));
            snode->setData(0, kRoleObjectId, o.id);
            snode->setData(0, kRoleSensorIdx, int(si));
            snode->setForeground(0, fc);
            onode->addChild(snode);
        }
        for (size_t fi = 0; fi < o.face_groups.size(); ++fi) {
            auto* fnode = new QTreeWidgetItem(
                {QString("电磁: #%1 %2").arg(fi + 1).arg(o.face_groups[fi].group_id)});
            fnode->setData(0, kRoleNodeKind, int(prop::NodeKind::FaceGroup));
            fnode->setData(0, kRoleObjectId, o.id);
            fnode->setData(0, kRoleSensorIdx, int(fi));
            fnode->setForeground(0, fc);
            onode->addChild(fnode);
        }
        for (size_t wi = 0; wi < o.waypoints.size(); ++wi) {
            auto* wnode = new QTreeWidgetItem(
                {QString("航路点: #%1").arg(wi + 1)});
            wnode->setData(0, kRoleNodeKind, int(prop::NodeKind::Waypoint));
            wnode->setData(0, kRoleObjectId, o.id);
            wnode->setData(0, kRoleSensorIdx, int(wi));
            wnode->setForeground(0, fc);
            onode->addChild(wnode);
        }
        root->addChild(onode);
    }
    tree_scene_->addTopLevelItem(root);
    root->setExpanded(true);
    for (int i = 0; i < root->childCount(); ++i)
        root->child(i)->setExpanded(true);

    if (!prev_id.isEmpty()) {
        selectObjectInTree(prev_id);
    } else {
        // 优先选中首个实体节点, 以便「属性变量」表格默认有内容。
        for (int i = 0; i < root->childCount(); ++i) {
            if (root->child(i)->data(0, kRoleNodeKind).toInt() == 2) {
                tree_scene_->setCurrentItem(root->child(i));
                break;
            }
        }
    }
    updateStatusCounts();
    refreshBindingsPanel();
}

void SceneDesignerWindow::selectObjectInTree(const QString& object_id)
{
    if (!tree_scene_ || tree_scene_->topLevelItemCount() == 0) return;
    auto* root = tree_scene_->topLevelItem(0);
    for (int i = 0; i < root->childCount(); ++i) {
        auto* it = root->child(i);
        if (it->data(0, kRoleNodeKind).toInt() == 2 &&
            it->data(0, kRoleObjectId).toString() == object_id) {
            const QSignalBlocker b(tree_scene_);
            tree_scene_->setCurrentItem(it);
            it->setExpanded(true);
            syncEditorsFromSelection();
            refreshPropsTable();
            refreshSensorList();
            refreshFaceGroupList();
            refreshWaypointTable();
            return;
        }
    }
}

void SceneDesignerWindow::updateStatusCounts()
{
    size_t ns = 0;
    for (const auto& o : objects_) ns += o.sensors.size();
    if (sb_objects_) sb_objects_->setText(tr("实体 %1").arg(objects_.size()));
    if (sb_sensors_) sb_sensors_->setText(tr("传感器 %1").arg(ns));
}

// W2: 把细粒度大气参数打包成 JSON, 实时下发到视口 rocky_env Control 接口。
// 视口未就绪 (vsgReady 前) 时静默跳过; 各 Feature 未挂载时其 setAtmosphereParams 为空操作。
void SceneDesignerWindow::pushAtmosphereToViewport()
{
    if (!viewport_ || !topic_path_.isEmpty()) return;
    nlohmann::json p;
    const auto epoch=simcore::parseUtcEpoch(clock_config_.epoch_utc);
    p["year"]=epoch.year; p["month"]=epoch.month; p["day"]=epoch.day; p["hours_utc"]=epoch.hours();
    p["cloud_cover"] = env_cloud_cover_;
    p["cloud_base"]  = env_cloud_base_;
    p["stratus_altitude"] = env_cloud_base_;   // 层云高度 = 云底
    p["rain"]        = env_rain_;
    p["snow"]        = env_snow_;
    p["visibility"]  = env_visibility_;
    p["wind_speed"]  = env_wind_speed_;
    p["wind_az"]     = env_wind_az_;
    p["sun_el"]      = env_sun_el_;
    p["sun_az"]      = env_sun_az_;
    p["wave_height"] = env_wave_height_;
    p["cover"]       = env_cloud_cover_;       // EnvFeatureBase 共用成员
    if (auto* sky = viewport_->featureAs<viewport3d::SkyFeature>(viewport3d::SkyFeature::kId))
        sky->setAtmosphereParams(p);
    if (auto* cloud = viewport_->featureAs<viewport3d::CloudFeature>(viewport3d::CloudFeature::kId))
        cloud->setAtmosphereParams(p);
    if (auto* ocean = viewport_->featureAs<viewport3d::OceanFeature>(viewport3d::OceanFeature::kId))
    {
        auto sea=p;sea.update(ocean_config_);
        ocean->setAtmosphereParams(sea);
    }
}

void SceneDesignerWindow::syncEditorsFromSelection()
{
    // 旧折叠编辑面板已移除 (改由属性树统一编辑), 此函数保留为空实现以避免空指针。
    // 如需同步 3D 变换由 onPropTreeEdited 直接处理。
    const int idx = selectedObjectIndex();
    if (idx < 0) return;
    const SceneObject& o = objects_[size_t(idx)];
    if (cmb_faction_) {
        const QSignalBlocker b(cmb_faction_);
        const int fi = cmb_faction_->findData(int(o.faction));
        cmb_faction_->setCurrentIndex(fi >= 0 ? fi : int(Faction::Neutral));
    }
    suppress_edit_signals_ = false;
}

void SceneDesignerWindow::refreshPropsTable()
{
    if (!tree_props_ || !tree_scene_) return;
    auto* item = tree_scene_->currentItem();
    if (!item) { tree_props_->clear(); return; }

    const auto num = [](double d, int prec = 2) { return QString::number(d, 'f', prec); };
    const auto nm  = [](double x, double y, double z) {
        return QString("(%1, %2, %3) m").arg(x, 0, 'f', 1)
                                        .arg(y, 0, 'f', 1).arg(z, 0, 'f', 1);
    };
    const auto factionLabel = [](Faction f) -> QString {
        switch (f) {
        case Faction::Red:     return tr("红方");
        case Faction::Blue:    return tr("蓝方");
        default:               return tr("中立");
        }
    };
    static const char* kCatNames[4] = {"地面", "空中", "水面", "导弹"};

    const int kind = item->data(0, kRoleNodeKind).toInt();
    QVector<prop::Group> groups;
    std::function<QString(const prop::Item&)> valueOf;
    prop::NodeKind nk;

    switch (kind) {
    case int(prop::NodeKind::Scenario): {   // 场景根
        nk = prop::NodeKind::Scenario;
        valueOf = [this](const prop::Item& it) -> QString {
            if (it.key == QStringLiteral("scenario_name"))
                return edt_scenario_name_ ? edt_scenario_name_->text() : QString();
            if (it.key == QStringLiteral("description"))
                return edt_scenario_desc_ ? edt_scenario_desc_->text() : QString();
            if (it.key == QStringLiteral("terrain_ref"))
                return QString::fromStdString(facade_ ? facade_->terrainRef() : std::string());
            if (it.key==QStringLiteral("frame_dt")) return QString::number(clock_config_.frame_dt,'g',12);
            if (it.key==QStringLiteral("physics_step")) return QString::number(clock_config_.physics_step,'g',12);
            if (it.key==QStringLiteral("history_seconds")) return QString::number(clock_config_.history_seconds,'g',12);
            if (it.key==QStringLiteral("epoch_utc")) return QString::fromStdString(clock_config_.epoch_utc);
            if (it.key == QStringLiteral("entity_count")) return QString::number(objects_.size());
            size_t ns = 0; for (const auto& o : objects_) ns += o.sensors.size();
            if (it.key == QStringLiteral("sensor_count")) return QString::number(ns);
            if (it.key == QStringLiteral("binding_count")) return QString::number(binding_links_.size());
            return QString();
        };
        break;
    }
    case int(prop::NodeKind::Terrain): {    // 地形 (配置 + 地形工程参数)
        nk = prop::NodeKind::Terrain;
        const vsgraysim::TerrainSystem* ter = facade_ ? facade_->terrain() : nullptr;
        const std::string tref = facade_ ? facade_->terrainRef() : std::string();
        valueOf = [ter, &tref, &num](const prop::Item& it) -> QString {
            if (it.key == QStringLiteral("terrain_type"))
                return tref.empty()
                    ? tr("rocky 地球底座")
                    : (ter ? prop::terrainTypeName(int(ter->terrainType()))
                           : QString::fromStdString(
                                 std::filesystem::path(tref).stem().string()));
            if (it.key == QStringLiteral("terrain_ref"))
                return tref.empty() ? tr("—") : QString::fromStdString(tref);
            // ---- 地形配置组 (实时 TerrainSystem; 无模板时显示 —) ----
            if (!ter || tref.empty()) return tr("—");
            if (it.key == QStringLiteral("terrain_grid"))
                return QString::number(ter->grid());
            if (it.key == QStringLiteral("terrain_extent"))
                return num(ter->extent(), 0);
            if (it.key == QStringLiteral("terrain_height_scale"))
                return num(ter->heightScale(), 1);
            if (it.key == QStringLiteral("terrain_seed"))
                return QString::number(ter->seed());
            if (it.key == QStringLiteral("terrain_water"))
                return prop::waterTypeName(int(ter->waterType()));
            if (it.key == QStringLiteral("terrain_water_level"))
                return num(ter->waterLevel(), 2);
            return QString();
        };
        break;
    }
    case int(prop::NodeKind::Environment): {  // 环境
        nk = prop::NodeKind::Environment;
        valueOf = [this](const prop::Item& it) -> QString {
            if (it.key == QStringLiteral("weather"))
                return weatherLabel(env_weather_);
            if (it.key == QStringLiteral("severity"))
                return QString::number(env_severity_);
            // W2: 细粒度大气参数显示
            auto num = [](double v) { return QString::number(v, 'f', 2); };
            if (it.key == QStringLiteral("cloud_cover"))  return num(env_cloud_cover_);
            if (it.key == QStringLiteral("cloud_base"))   return num(env_cloud_base_);
            if (it.key == QStringLiteral("rain"))         return num(env_rain_);
            if (it.key == QStringLiteral("snow"))         return num(env_snow_);
            if (it.key == QStringLiteral("visibility"))   return num(env_visibility_);
            if (it.key == QStringLiteral("wind_speed"))   return num(env_wind_speed_);
            if (it.key == QStringLiteral("wind_az"))      return num(env_wind_az_);
            if (it.key == QStringLiteral("sun_el"))       return num(env_sun_el_);
            if (it.key == QStringLiteral("sun_az"))       return num(env_sun_az_);
            if (it.key == QStringLiteral("wave_height"))  return num(env_wave_height_);
            return QString();
        };
        break;
    }
    case int(prop::NodeKind::Entity):      // 实体 (含全部集合分组: 航迹/传感器/电磁/绑定)
    case int(prop::NodeKind::FaceGroup):   // 电磁分组/航路点选中 → 展示归属实体的全部分组
    case int(prop::NodeKind::Waypoint): {
        const int idx = selectedObjectIndex();
        if (idx < 0 || size_t(idx) >= objects_.size()) { tree_props_->clear(); return; }
        const SceneObject& o = objects_[size_t(idx)];
        const int cat = std::max(0, std::min(3, int(o.platform_cat)));
        nk = prop::NodeKind::Entity;

        valueOf = [&o, cat, &num, &nm, &factionLabel](const prop::Item& it) -> QString {
            const QString k = it.key;
            // 基础字段
            if (k == QLatin1String("object_id"))  return o.id;
            if (k == QLatin1String("mesh_path"))  return QFileInfo(o.mesh_path).fileName();
            if (k == QLatin1String("type"))       return tr(kCatNames[cat]);
            if (k == QLatin1String("faction"))    return factionLabel(o.faction);
            if (k == QLatin1String("position"))   return nm(o.pos[0], o.pos[1], o.pos[2]);
            if (k == QLatin1String("yaw_deg"))    return num(o.yaw_deg);
            if (k == QLatin1String("scale"))      return num(o.scale);
            if (k == QLatin1String("mass"))       return num(o.mass, 0);
            if (k==QLatin1String("motion_authority")) return o.motion_authority==QStringLiteral("kinematic") ? tr("规定轨迹") : tr("动力学");
            if (k == QLatin1String("flight_control_enabled")) {
                const auto v = o.motion_params.value("flight_control_enabled", nlohmann::json(true));
                return v.is_boolean() ? (v.get<bool>() ? tr("是") : tr("否")) : tr("无效");
            }
            if (k == QLatin1String("flight_control_model")) return tr("简化固定翼 · 质点与姿态控制");
            if (k == QLatin1String("flight_airframe")) return o.airframe_kind == "uav" ? tr("无人机") : tr("飞机");
            if (k.startsWith(QLatin1String("flight_"))) {
                for (const auto& parameter : flightParameters()) if (k.mid(7) == QLatin1String(parameter.key)) {
                    const auto v = o.motion_params.value(parameter.key, nlohmann::json(parameter.initial));
                    return v.is_number() && std::isfinite(v.get<double>()) ? num(v.get<double>()) : tr("无效");
                }
            }
            if (k == QLatin1String("sensor_count"))  return QString::number(o.sensors.size());
            if (k == QLatin1String("waypoint_count")) return QString::number(o.waypoints.size());
            // 航迹规划
            if (k == QLatin1String("route_type")) return routeTypeName(o.route_type);
            if (k == QLatin1String("route_loop")) return o.route_loop ? tr("是") : tr("否");
            if (k == QLatin1String("route_start_delay")) return num(o.route_start_delay, 1);
            // 航路点 (key: wp_N_time / wp_N_speed / wp_N_x..z)
            if (k.startsWith(QStringLiteral("wp_"))) {
                const int i = k.mid(3, k.indexOf('_', 3) - 3).toInt();
                if (i < 0 || size_t(i) >= o.waypoints.size()) return QString();
                const auto& w = o.waypoints[size_t(i)];
                if (k.endsWith(QStringLiteral("_time")))  return num(w.time);
                if (k.endsWith(QStringLiteral("_speed"))) return num(w.speed);
                if (k.endsWith(QStringLiteral("_x")))     return num(w.pos[0]);
                if (k.endsWith(QStringLiteral("_y")))     return num(w.pos[1]);
                if (k.endsWith(QStringLiteral("_z")))     return num(w.pos[2]);
            }
            // 传感器 (key: s_N_xxx)
            if (k.startsWith(QStringLiteral("s_"))) {
                const int i = k.mid(2, k.indexOf('_', 2) - 2).toInt();
                if (i < 0 || size_t(i) >= o.sensors.size()) return QString();
                const auto& s = o.sensors[size_t(i)];
                const QString f = k.mid(k.indexOf('_', 2) + 1);
                if (f == QLatin1String("type"))       return sensorTypeNameFromInt(s.type);
                if (f == QLatin1String("band"))       return s.band;
                if (f == QLatin1String("power"))      return num(s.power);
                if (f == QLatin1String("wavelength")) return num(s.wavelength, 4);
                if (f == QLatin1String("beam_width")) return num(s.beam_width);
                if (f == QLatin1String("pulse_rate")) return num(s.pulse_rate, 1);
                if (f == QLatin1String("duty_cycle")) return num(s.duty_cycle);
                if (f == QLatin1String("fov_h"))      return num(s.fov_h);
                if (f == QLatin1String("fov_v"))      return num(s.fov_v);
                if (f == QLatin1String("range_m"))    return num(s.range_m);
                if (f == QLatin1String("range_gates"))      return QString::number(s.range_gates);
                if (f == QLatin1String("doppler_bins"))     return QString::number(s.doppler_bins);
                if (f == QLatin1String("integration_time")) return num(s.integration_time, 4);
                if (f == QLatin1String("frame_interval"))   return num(s.frame_interval, 3);
                // §14.3 frame/profile/stages (key: s_N_kind / s_N_f_number / ...)
                if (f == QLatin1String("kind"))           return s.frame_kind;
                if (f == QLatin1String("lambda_min"))     return QString::number(s.lambda_min, 'e', 3);
                if (f == QLatin1String("lambda_max"))     return QString::number(s.lambda_max, 'e', 3);
                if (f == QLatin1String("res_w"))          return QString::number(s.res_w);
                if (f == QLatin1String("res_h"))          return QString::number(s.res_h);
                if (f == QLatin1String("fov_deg"))        return num(s.fov_deg);
                if (f == QLatin1String("rate_hz"))        return num(s.rate_hz, 1);
                if (f == QLatin1String("f_number"))       return num(s.f_number);
                if (f == QLatin1String("transmission"))   return num(s.transmission, 3);
                if (f == QLatin1String("psf_sigma_px"))   return num(s.psf_sigma_px);
                if (f == QLatin1String("vignetting"))     return num(s.vignetting, 3);
                if (f == QLatin1String("pixel_pitch_um")) return num(s.pixel_pitch_um);
                if (f == QLatin1String("qe"))             return num(s.qe, 3);
                if (f == QLatin1String("integration_ms")) return num(s.integration_ms, 2);
                if (f == QLatin1String("netd_mk"))        return num(s.netd_mk, 1);
                if (f == QLatin1String("well_ke"))        return num(s.well_ke, 0);
                if (f == QLatin1String("read_noise_e"))   return num(s.read_noise_e, 0);
                if (f == QLatin1String("bits"))           return QString::number(s.bits);
                if (f == QLatin1String("agc_low_pct"))    return num(s.agc_low_pct, 4);
                if (f == QLatin1String("agc_high_pct"))   return num(s.agc_high_pct, 4);
                if (f == QLatin1String("gamma"))          return num(s.gamma, 2);
                if (f == QLatin1String("stages"))         return s.stages;
                if (f == QLatin1String("radar_publish"))  return s.radar_stages_publish;
                // §7.2 扫描机构 (B7)
                if (f == QLatin1String("scan_mode"))      return s.scan_mode;
                if (f == QLatin1String("scan_az_center")) return num(s.scan_az_center);
                if (f == QLatin1String("scan_el_center")) return num(s.scan_el_center);
                if (f == QLatin1String("scan_az_extent")) return num(s.scan_az_extent);
                if (f == QLatin1String("scan_el_extent")) return num(s.scan_el_extent);
                if (f == QLatin1String("scan_az_rate"))   return num(s.scan_az_rate, 2);
                if (f == QLatin1String("scan_el_rate"))   return num(s.scan_el_rate, 2);
                if (f == QLatin1String("scan_target"))    return s.scan_target_ref;
            }
            // 热属性 (§14.3, key: thermal_base / thermal_states / tp_N_*)
            if (k == QLatin1String("thermal_base"))
                return o.thermal_base_temp_k > 0.0 ? num(o.thermal_base_temp_k, 2)
                                                   : QStringLiteral("(未设置)");
            if (k == QLatin1String("thermal_states"))
                return o.thermal_states.isEmpty() ? QStringLiteral("(未设置)")
                                                  : o.thermal_states;
            if (k.startsWith(QStringLiteral("tp_"))) {
                const int i = k.mid(3, k.indexOf('_', 3) - 3).toInt();
                if (i < 0 || size_t(i) >= o.thermal_parts.size()) return QString();
                const auto& p = o.thermal_parts[size_t(i)];
                const QString f = k.mid(k.indexOf('_', 3) + 1);
                if (f == QLatin1String("name"))       return p.name;
                if (f == QLatin1String("temp_k"))     return num(p.temp_k, 1);
                if (f == QLatin1String("emissivity")) return num(p.emissivity, 3);
                if (f == QLatin1String("face_group")) return p.face_group;
            }
            // 电磁分组 (key: fg_N_xxx)
            if (k.startsWith(QStringLiteral("fg_"))) {
                const int i = k.mid(3, k.indexOf('_', 3) - 3).toInt();
                if (i < 0 || size_t(i) >= o.face_groups.size()) return QString();
                const auto& g = o.face_groups[size_t(i)];
                const QString f = k.mid(k.indexOf('_', 3) + 1);
                if (f == QLatin1String("group_id"))    return g.group_id;
                if (f == QLatin1String("brdf_type"))   return g.brdf_type;
                if (f == QLatin1String("albedo"))      return num(g.albedo);
                if (f == QLatin1String("roughness"))   return num(g.roughness);
                if (f == QLatin1String("emissivity"))  return num(g.emissivity);
                if (f == QLatin1String("base_temp"))   return num(g.base_temp);
                if (f == QLatin1String("dielectric_real")) return num(g.dielectric_real, 4);
                if (f == QLatin1String("dielectric_imag")) return num(g.dielectric_imag, 4);
                if (f == QLatin1String("sigma0_db"))   return num(g.sigma0_db);
                if (f == QLatin1String("surface_rough")) return num(g.surface_rough);
            }
            return QString();
        };

        // 构建分组: 基础Schema + 航迹规划 + 航路点 + 传感器 + 电磁分组 + 绑定关系
        groups = prop::entityGroups();
        auto addItem = [](prop::Group& g, const QString& key, const QString& label,
                          prop::Type type, bool editable, const QString& unit = {}) {
            prop::Item it;
            it.key = key; it.label = label; it.type = type; it.unit = unit;
            it.group = g.id; it.source = int(prop::Source::Config);
            it.editableOnDesign = editable;
            g.items.append(it);
        };
        if (cat == 1) {
            prop::Group flight{QStringLiteral("飞行控制"), tr("飞行控制与包线"), {}};
            addItem(flight, QStringLiteral("flight_airframe"), tr("操作台类别"), prop::Type::Enum, true);
            addItem(flight, QStringLiteral("flight_control_model"), tr("执行模型"), prop::Type::Text, false);
            addItem(flight, QStringLiteral("flight_control_enabled"), tr("允许显控接管"), prop::Type::Bool, true);
            for (const auto& parameter : flightParameters())
                addItem(flight, QStringLiteral("flight_") + QString::fromLatin1(parameter.key),
                    tr(parameter.label), prop::Type::Number, true, QString::fromUtf8(parameter.unit));
            groups.append(flight);
        }
        // 航迹规划
        prop::Group gRoute{QStringLiteral("航迹规划"), QStringLiteral("航迹规划"), {}};
        addItem(gRoute, QStringLiteral("route_type"), QStringLiteral("航迹类型"), prop::Type::Enum, true);
        addItem(gRoute, QStringLiteral("route_loop"), QStringLiteral("循环"), prop::Type::Bool, true);
        addItem(gRoute, QStringLiteral("route_start_delay"), QStringLiteral("起始延迟"), prop::Type::Number, true, QStringLiteral("s"));
        groups.append(gRoute);
        // 航路点 (始终显示; 空数据时给占位提示, 配合「＋/－ 航路点」增删)
        {
            prop::Group gWp{QStringLiteral("航路点"), QStringLiteral("航路点"), {}};
            if (o.waypoints.empty()) {
                prop::Item it;
                it.key = QStringLiteral("__wp_empty__");
                it.label = tr("（无航路点 — 点击「＋ 航路点」添加）");
                it.group = gWp.id; it.source = int(prop::Source::Derived);
                it.editableOnDesign = false;
                gWp.items.append(it);
            } else {
                for (int i = 0; i < int(o.waypoints.size()); ++i) {
                    const QString p = QStringLiteral("wp_%1").arg(i);
                    addItem(gWp, p + QStringLiteral("_time"),  QStringLiteral("#%1 时间").arg(i), prop::Type::Number, true, QStringLiteral("s"));
                    addItem(gWp, p + QStringLiteral("_speed"), QStringLiteral("#%1 速度").arg(i), prop::Type::Number, true, QStringLiteral("m/s"));
                    addItem(gWp, p + QStringLiteral("_x"), QStringLiteral("#%1 X").arg(i), prop::Type::Number, true, QStringLiteral("m"));
                    addItem(gWp, p + QStringLiteral("_y"), QStringLiteral("#%1 Y").arg(i), prop::Type::Number, true, QStringLiteral("m"));
                    addItem(gWp, p + QStringLiteral("_z"), QStringLiteral("#%1 Z").arg(i), prop::Type::Number, true, QStringLiteral("m"));
                }
            }
            groups.append(gWp);
        }
        // 传感器 (始终显示; 空则占位, 有则每组一个)
        if (o.sensors.empty()) {
            prop::Group gSE{QStringLiteral("传感器"), QStringLiteral("传感器"), {}};
            prop::Item it;
            it.key = QStringLiteral("__sensor_empty__");
            it.label = tr("（无传感器 — 点击「＋ 传感器」添加）");
            it.group = gSE.id; it.source = int(prop::Source::Derived);
            it.editableOnDesign = false;
            gSE.items.append(it);
            groups.append(gSE);
        }
        for (int i = 0; i < int(o.sensors.size()); ++i) {
            const SensorDef& s = o.sensors[size_t(i)];
            const QString typeName = sensorTypeNameFromInt(s.type);
            prop::Group gS{QStringLiteral("传感器_%1").arg(i + 1),
                           QStringLiteral("传感器 #%1 (%2)").arg(i + 1).arg(typeName), {}};
            const QString p = QStringLiteral("s_%1").arg(i);
            addItem(gS, p + QStringLiteral("_type"), QStringLiteral("类型"), prop::Type::Enum, true);
            addItem(gS, p + QStringLiteral("_band"), QStringLiteral("频段"), prop::Type::Text, true);
            addItem(gS, p + QStringLiteral("_power"), QStringLiteral("发射功率"), prop::Type::Number, true, QStringLiteral("W"));
            addItem(gS, p + QStringLiteral("_wavelength"), QStringLiteral("波长"), prop::Type::Number, true, QStringLiteral("m"));
            addItem(gS, p + QStringLiteral("_beam_width"), QStringLiteral("波束宽度"), prop::Type::Number, true, QStringLiteral("°"));
            addItem(gS, p + QStringLiteral("_pulse_rate"), QStringLiteral("脉冲重频"), prop::Type::Number, true, QStringLiteral("Hz"));
            addItem(gS, p + QStringLiteral("_duty_cycle"), QStringLiteral("占空比"), prop::Type::Number, true);
            addItem(gS, p + QStringLiteral("_fov_h"), QStringLiteral("水平视场"), prop::Type::Number, true, QStringLiteral("°"));
            addItem(gS, p + QStringLiteral("_fov_v"), QStringLiteral("垂直视场"), prop::Type::Number, true, QStringLiteral("°"));
            addItem(gS, p + QStringLiteral("_range_m"), QStringLiteral("作用距离"), prop::Type::Number, true, QStringLiteral("m"));
            addItem(gS, p + QStringLiteral("_range_gates"), QStringLiteral("距离门数"), prop::Type::Number, true);
            addItem(gS, p + QStringLiteral("_doppler_bins"), QStringLiteral("多普勒门数"), prop::Type::Number, true);
            addItem(gS, p + QStringLiteral("_integration_time"), QStringLiteral("积分时间"), prop::Type::Number, true, QStringLiteral("s"));
            addItem(gS, p + QStringLiteral("_frame_interval"), QStringLiteral("帧周期(0=主时钟)"), prop::Type::Number, true, QStringLiteral("s"));
            // §14.3 帧时序 (frame)
            addItem(gS, p + QStringLiteral("_kind"),           QStringLiteral("帧·类型"), prop::Type::Text, true);
            addItem(gS, p + QStringLiteral("_lambda_min"),     QStringLiteral("帧·λ下限"), prop::Type::Number, true, QStringLiteral("m"));
            addItem(gS, p + QStringLiteral("_lambda_max"),     QStringLiteral("帧·λ上限"), prop::Type::Number, true, QStringLiteral("m"));
            addItem(gS, p + QStringLiteral("_res_w"),          QStringLiteral("帧·分辨率宽"), prop::Type::Number, true, QStringLiteral("px"));
            addItem(gS, p + QStringLiteral("_res_h"),          QStringLiteral("帧·分辨率高"), prop::Type::Number, true, QStringLiteral("px"));
            addItem(gS, p + QStringLiteral("_fov_deg"),        QStringLiteral("帧·视场"), prop::Type::Number, true, QStringLiteral("°"));
            addItem(gS, p + QStringLiteral("_rate_hz"),        QStringLiteral("帧·帧率(雷达=PRF)"), prop::Type::Number, true, QStringLiteral("Hz"));
            // §14.3 profile·光学 (P1→P2)
            addItem(gS, p + QStringLiteral("_f_number"),       QStringLiteral("光学·F#"), prop::Type::Number, true);
            addItem(gS, p + QStringLiteral("_transmission"),   QStringLiteral("光学·透过率"), prop::Type::Number, true);
            addItem(gS, p + QStringLiteral("_psf_sigma_px"),   QStringLiteral("光学·PSF σ"), prop::Type::Number, true, QStringLiteral("px"));
            addItem(gS, p + QStringLiteral("_vignetting"),     QStringLiteral("光学·渐晕"), prop::Type::Number, true);
            // §14.3 profile·探测器 (P2→P3)
            addItem(gS, p + QStringLiteral("_pixel_pitch_um"), QStringLiteral("探测器·像元间距"), prop::Type::Number, true, QStringLiteral("μm"));
            addItem(gS, p + QStringLiteral("_qe"),             QStringLiteral("探测器·量子效率"), prop::Type::Number, true);
            addItem(gS, p + QStringLiteral("_integration_ms"), QStringLiteral("探测器·积分时间"), prop::Type::Number, true, QStringLiteral("ms"));
            addItem(gS, p + QStringLiteral("_netd_mk"),        QStringLiteral("探测器·NETD"), prop::Type::Number, true, QStringLiteral("mK"));
            addItem(gS, p + QStringLiteral("_well_ke"),        QStringLiteral("探测器·满阱"), prop::Type::Number, true, QStringLiteral("ke-"));
            addItem(gS, p + QStringLiteral("_read_noise_e"),   QStringLiteral("探测器·读出噪声"), prop::Type::Number, true, QStringLiteral("e-"));
            addItem(gS, p + QStringLiteral("_bits"),           QStringLiteral("探测器·ADC 位数"), prop::Type::Number, true);
            // §14.3 profile·电子学 (P3→P4)
            addItem(gS, p + QStringLiteral("_agc_low_pct"),    QStringLiteral("电子学·AGC 下分位"), prop::Type::Number, true);
            addItem(gS, p + QStringLiteral("_agc_high_pct"),   QStringLiteral("电子学·AGC 上分位"), prop::Type::Number, true);
            addItem(gS, p + QStringLiteral("_gamma"),          QStringLiteral("电子学·Gamma"), prop::Type::Number, true);
            // §14.3 产品发布级 (逗号分隔)
            addItem(gS, p + QStringLiteral("_stages"),         QStringLiteral("发布级(IR/VIS)"), prop::Type::Text, true);
            addItem(gS, p + QStringLiteral("_radar_publish"),  QStringLiteral("雷达发布段"), prop::Type::Text, true);
            // §7.2 扫描机构 (B7, scan 段)
            addItem(gS, p + QStringLiteral("_scan_mode"),      QStringLiteral("扫描·模式"), prop::Type::Text, true);
            addItem(gS, p + QStringLiteral("_scan_az_center"), QStringLiteral("扫描·中心方位"), prop::Type::Number, true, QStringLiteral("°"));
            addItem(gS, p + QStringLiteral("_scan_el_center"), QStringLiteral("扫描·中心俯仰"), prop::Type::Number, true, QStringLiteral("°"));
            addItem(gS, p + QStringLiteral("_scan_az_extent"), QStringLiteral("扫描·方位范围"), prop::Type::Number, true, QStringLiteral("°"));
            addItem(gS, p + QStringLiteral("_scan_el_extent"), QStringLiteral("扫描·俯仰范围"), prop::Type::Number, true, QStringLiteral("°"));
            addItem(gS, p + QStringLiteral("_scan_az_rate"),   QStringLiteral("扫描·方位速率(rpm)"), prop::Type::Number, true, QStringLiteral("°/s"));
            addItem(gS, p + QStringLiteral("_scan_el_rate"),   QStringLiteral("扫描·俯仰速率"), prop::Type::Number, true, QStringLiteral("°/s"));
            addItem(gS, p + QStringLiteral("_scan_target"),    QStringLiteral("扫描·指向目标(slaved)"), prop::Type::Text, true);
            groups.append(gS);
        }
        // 热属性 (§14.3: 基础温度/工况倍率/热部件; 空部件给占位提示)
        {
            prop::Group gT{QStringLiteral("热属性"), QStringLiteral("热属性 (§14.3)"), {}};
            addItem(gT, QStringLiteral("thermal_base"),   QStringLiteral("基础温度 (0=未设置)"), prop::Type::Number, true, QStringLiteral("K"));
            addItem(gT, QStringLiteral("thermal_states"), QStringLiteral("工况倍率(名:系数CSV)"), prop::Type::Text, true);
            if (o.thermal_parts.empty()) {
                prop::Item it;
                it.key = QStringLiteral("__tp_empty__");
                it.label = tr("（无热部件 — 右键实体「添加热部件」）");
                it.group = gT.id; it.source = int(prop::Source::Derived);
                it.editableOnDesign = false;
                gT.items.append(it);
            }
            for (int i = 0; i < int(o.thermal_parts.size()); ++i) {
                const QString q = QStringLiteral("tp_%1").arg(i);
                addItem(gT, q + QStringLiteral("_name"),       QStringLiteral("#%1 部件名").arg(i), prop::Type::Text, true);
                addItem(gT, q + QStringLiteral("_temp_k"),     QStringLiteral("#%1 温度").arg(i), prop::Type::Number, true, QStringLiteral("K"));
                addItem(gT, q + QStringLiteral("_emissivity"), QStringLiteral("#%1 发射率").arg(i), prop::Type::Number, true);
                addItem(gT, q + QStringLiteral("_face_group"), QStringLiteral("#%1 关联面组").arg(i), prop::Type::Text, true);
            }
            groups.append(gT);
        }
        // 电磁光学 (始终显示; 空则占位, 有则每组一个)
        if (o.face_groups.empty()) {
            prop::Group gFE{QStringLiteral("电磁光学"), QStringLiteral("电磁光学"), {}};
            prop::Item it;
            it.key = QStringLiteral("__fg_empty__");
            it.label = tr("（无电磁分组 — 点击「＋ 电磁分组」添加）");
            it.group = gFE.id; it.source = int(prop::Source::Derived);
            it.editableOnDesign = false;
            gFE.items.append(it);
            groups.append(gFE);
        }
        for (int i = 0; i < int(o.face_groups.size()); ++i) {
            prop::Group gF{QStringLiteral("电磁_%1").arg(i + 1),
                           QStringLiteral("电磁光学 #%1").arg(i + 1), {}};
            const QString p = QStringLiteral("fg_%1").arg(i);
            addItem(gF, p + QStringLiteral("_group_id"), QStringLiteral("分组 ID"), prop::Type::Text, true);
            addItem(gF, p + QStringLiteral("_brdf_type"), QStringLiteral("BRDF 类型"), prop::Type::Text, true);
            addItem(gF, p + QStringLiteral("_albedo"), QStringLiteral("反照率"), prop::Type::Number, true);
            addItem(gF, p + QStringLiteral("_roughness"), QStringLiteral("表面粗糙"), prop::Type::Number, true);
            addItem(gF, p + QStringLiteral("_emissivity"), QStringLiteral("发射率"), prop::Type::Number, true);
            addItem(gF, p + QStringLiteral("_base_temp"), QStringLiteral("基准温度"), prop::Type::Number, true, QStringLiteral("K"));
            addItem(gF, p + QStringLiteral("_dielectric_real"), QStringLiteral("介电常数实部"), prop::Type::Number, true);
            addItem(gF, p + QStringLiteral("_dielectric_imag"), QStringLiteral("介电常数虚部"), prop::Type::Number, true);
            addItem(gF, p + QStringLiteral("_sigma0_db"), QStringLiteral("后向散射σ0"), prop::Type::Number, true, QStringLiteral("dB"));
            addItem(gF, p + QStringLiteral("_surface_rough"), QStringLiteral("表面粗糙度"), prop::Type::Number, true);
            groups.append(gF);
        }
        // 绑定关系 (只读; 始终显示)
        {
            prop::Group gB{QStringLiteral("绑定关系"), QStringLiteral("绑定关系"), {}};
            if (binding_links_.empty()) {
                prop::Item it;
                it.key = QStringLiteral("__bind_empty__");
                it.label = tr("（无绑定关系 — 可在「关系图」页拖拽建立）");
                it.group = gB.id; it.source = int(prop::Source::Derived);
                it.editableOnDesign = false;
                gB.items.append(it);
            }
            for (int i = 0; i < int(binding_links_.size()); ++i) {
                const auto& b = binding_links_[size_t(i)];
                prop::Item it;
                it.group = gB.id; it.source = int(prop::Source::Derived);
                it.editableOnDesign = false;
                it.label = QStringLiteral("%1 → %2").arg(b.from_id, b.to_id);
                it.key = QStringLiteral("bind_%1").arg(i);
                gB.items.append(it);
            }
            groups.append(gB);
        }
        break;
    }
    case int(prop::NodeKind::Sensor): {     // 传感器
        const int oi = selectedObjectIndex();
        const int si = item->data(0, kRoleSensorIdx).toInt();
        if (oi < 0 || size_t(oi) >= objects_.size()) { tree_props_->clear(); return; }
        const auto& ss = objects_[size_t(oi)].sensors;
        if (si < 0 || size_t(si) >= ss.size()) { tree_props_->clear(); return; }
        const SensorDef& s = ss[size_t(si)];
        nk = prop::NodeKind::Sensor;
        valueOf = [&s, &num](const prop::Item& it) -> QString {
            if (it.key == QStringLiteral("type"))       return sensorTypeNameFromInt(s.type);
            if (it.key == QStringLiteral("role"))       return s.role;
            if (it.key == QStringLiteral("band"))       return s.band;
            if (it.key == QStringLiteral("power"))      return num(s.power);
            if (it.key == QStringLiteral("wavelength")) return num(s.wavelength, 4);
            if (it.key == QStringLiteral("beam_width")) return num(s.beam_width);
            if (it.key == QStringLiteral("pulse_rate")) return num(s.pulse_rate, 1);
            if (it.key == QStringLiteral("duty_cycle")) return num(s.duty_cycle);
            if (it.key == QStringLiteral("fov_h"))      return num(s.fov_h);
            if (it.key == QStringLiteral("fov_v"))      return num(s.fov_v);
            if (it.key == QStringLiteral("range_m"))    return num(s.range_m);
            if (it.key == QStringLiteral("range_gates"))      return QString::number(s.range_gates);
            if (it.key == QStringLiteral("doppler_bins"))     return QString::number(s.doppler_bins);
            if (it.key == QStringLiteral("integration_time")) return num(s.integration_time, 4);
            if (it.key == QStringLiteral("frame_interval"))   return num(s.frame_interval, 3);
            // §14.3 frame/profile/stages
            if (it.key == QStringLiteral("kind"))           return s.frame_kind;
            if (it.key == QStringLiteral("lambda_min"))     return QString::number(s.lambda_min, 'e', 3);
            if (it.key == QStringLiteral("lambda_max"))     return QString::number(s.lambda_max, 'e', 3);
            if (it.key == QStringLiteral("res_w"))          return QString::number(s.res_w);
            if (it.key == QStringLiteral("res_h"))          return QString::number(s.res_h);
            if (it.key == QStringLiteral("fov_deg"))        return num(s.fov_deg);
            if (it.key == QStringLiteral("rate_hz"))        return num(s.rate_hz, 1);
            if (it.key == QStringLiteral("f_number"))       return num(s.f_number);
            if (it.key == QStringLiteral("transmission"))   return num(s.transmission, 3);
            if (it.key == QStringLiteral("psf_sigma_px"))   return num(s.psf_sigma_px);
            if (it.key == QStringLiteral("vignetting"))     return num(s.vignetting, 3);
            if (it.key == QStringLiteral("pixel_pitch_um")) return num(s.pixel_pitch_um);
            if (it.key == QStringLiteral("qe"))             return num(s.qe, 3);
            if (it.key == QStringLiteral("integration_ms")) return num(s.integration_ms, 2);
            if (it.key == QStringLiteral("netd_mk"))        return num(s.netd_mk, 1);
            if (it.key == QStringLiteral("well_ke"))        return num(s.well_ke, 0);
            if (it.key == QStringLiteral("read_noise_e"))   return num(s.read_noise_e, 0);
            if (it.key == QStringLiteral("bits"))           return QString::number(s.bits);
            if (it.key == QStringLiteral("agc_low_pct"))    return num(s.agc_low_pct, 4);
            if (it.key == QStringLiteral("agc_high_pct"))   return num(s.agc_high_pct, 4);
            if (it.key == QStringLiteral("gamma"))          return num(s.gamma, 2);
            if (it.key == QStringLiteral("stages"))         return s.stages;
            if (it.key == QStringLiteral("radar_publish"))  return s.radar_stages_publish;
            // §7.2 扫描机构 (B7)
            if (it.key == QStringLiteral("scan_mode"))      return s.scan_mode;
            if (it.key == QStringLiteral("scan_az_center")) return num(s.scan_az_center);
            if (it.key == QStringLiteral("scan_el_center")) return num(s.scan_el_center);
            if (it.key == QStringLiteral("scan_az_extent")) return num(s.scan_az_extent);
            if (it.key == QStringLiteral("scan_el_extent")) return num(s.scan_el_extent);
            if (it.key == QStringLiteral("scan_az_rate"))   return num(s.scan_az_rate, 2);
            if (it.key == QStringLiteral("scan_el_rate"))   return num(s.scan_el_rate, 2);
            if (it.key == QStringLiteral("scan_target"))    return s.scan_target_ref;
            return QString();
        };
        break;
    }
    default:
        tree_props_->clear();
        return;
    }

    // 实体/传感器分支已在 switch 内构建动态全部分组; 其余用共享 Schema。
    if (kind != int(prop::NodeKind::Entity) && kind != int(prop::NodeKind::Sensor))
        groups = prop::groups(nk);
    tree_props_->setGroups(groups, valueOf);
}

// 属性树编辑回写: 依据字段 key 将文本解析回写到场景对象/面板控件并同步 3D。
void SceneDesignerWindow::onPropTreeEdited(const QString& /*groupId*/,
                                           const QString& key,
                                           const QString& text)
{
    if (key == QStringLiteral("scenario_name") && edt_scenario_name_) {
        edt_scenario_name_->setText(text);   // 内部 textChanged 会同步场景树根标签
        return;
    }
    if (key == QStringLiteral("description") && edt_scenario_desc_) {
        edt_scenario_desc_->setText(text);
        return;
    }

    if(!topic_path_.isEmpty()){statusBar()->showMessage(tr("专题时序、环境和体素属性以专题及体素工坊配置为准；此次编辑未应用"),5000);return;}

    // 环境参数 (数据存于成员, 由属性面板编辑)
    if (key==QStringLiteral("frame_dt") || key==QStringLiteral("physics_step") ||
        key==QStringLiteral("history_seconds") || key==QStringLiteral("epoch_utc")) {
        try {
            auto candidate=clock_config_;
            if (key==QStringLiteral("epoch_utc")) candidate.epoch_utc=text.trimmed().toStdString();
            else {
                bool ok=false; const double value=text.toDouble(&ok);
                if (!ok) throw std::invalid_argument("invalid clock value");
                if (key==QStringLiteral("frame_dt")) candidate.frame_dt=value;
                if (key==QStringLiteral("physics_step")) candidate.physics_step=value;
                if (key==QStringLiteral("history_seconds")) candidate.history_seconds=value;
            }
            candidate.validate(); clock_config_=candidate; pushAtmosphereToViewport();
        } catch (const std::exception& e) { statusBar()->showMessage(tr("时序配置无效: %1").arg(e.what()),5000); }
        refreshPropsTable(); return;
    }
    if (key == QStringLiteral("weather")) {
        env_weather_ = weatherCodeFromLabel(text);
        // 同步场景树「环境」节点标签 (child0=地形, child1=环境)。
        if (auto* root = tree_scene_ && tree_scene_->topLevelItemCount() > 0
                ? tree_scene_->topLevelItem(0) : nullptr)
            if (root->childCount() >= 2)
                root->child(1)->setText(0, tr("环境: %1").arg(weatherLabel(env_weather_)));
        refreshPropsTable();   // 刷新属性变量面板的环境分组
        return;
    }
    if (key == QStringLiteral("severity")) {
        env_severity_ = std::max(0, std::min(100, text.toInt()));
        return;
    }
    // W2: 细粒度大气参数编辑 (更新成员 + 实时下发到 rocky_env Control 接口)
    auto setEnvDouble = [&](double& member, const QString& k, const QString& t) -> bool {
        bool ok = false;
        const double v = t.toDouble(&ok);
        if (!ok || k != key) return false;
        if (!std::isfinite(v) || ((k==QStringLiteral("cloud_cover") || k==QStringLiteral("rain") || k==QStringLiteral("snow")) && (v<0 || v>1)) ||
            ((k==QStringLiteral("cloud_base") || k==QStringLiteral("wind_speed") || k==QStringLiteral("wave_height")) && v<0) ||
            (k==QStringLiteral("visibility") && v<=0)) {
            statusBar()->showMessage(tr("环境参数超出有效范围"),3000); refreshPropsTable(); return true;
        }
        member = v;
        pushAtmosphereToViewport();   // 实时下发到视口 rocky_env 模块
        return true;
    };
    if (setEnvDouble(env_cloud_cover_,  QStringLiteral("cloud_cover"),  text)) return;
    if (setEnvDouble(env_cloud_base_,   QStringLiteral("cloud_base"),   text)) return;
    if (setEnvDouble(env_rain_,         QStringLiteral("rain"),         text)) return;
    if (setEnvDouble(env_snow_,         QStringLiteral("snow"),         text)) return;
    if (setEnvDouble(env_visibility_,   QStringLiteral("visibility"),   text)) return;
    if (setEnvDouble(env_wind_speed_,   QStringLiteral("wind_speed"),   text)) return;
    if (setEnvDouble(env_wind_az_,      QStringLiteral("wind_az"),      text)) return;
    if (setEnvDouble(env_sun_el_,       QStringLiteral("sun_el"),       text)) return;
    if (setEnvDouble(env_sun_az_,       QStringLiteral("sun_az"),       text)) return;
    if (setEnvDouble(env_wave_height_,  QStringLiteral("wave_height"),  text)) return;

    // 实体/传感器属性
    int oi = selectedObjectIndex();
    if (oi < 0 || size_t(oi) >= objects_.size()) return;
    SceneObject& o = objects_[size_t(oi)];
    if (key == QStringLiteral("flight_airframe") && o.platform_cat == 1) {
        const auto value = text.trimmed().toLower();
        if (value == tr("无人机") || value == "uav") o.airframe_kind = "uav";
        else if (value == tr("飞机") || value == "aircraft") o.airframe_kind = "aircraft";
        else statusBar()->showMessage(tr("操作台类别请选择 飞机 或 无人机"), 3000);
        refreshPropsTable(); return;
    }
    if (key == QStringLiteral("flight_control_enabled")) {
        if (o.platform_cat != 1) return;
        const auto value = text.trimmed().toLower();
        if (value == tr("是") || value == "true" || value == "1") o.motion_params["flight_control_enabled"] = true;
        else if (value == tr("否") || value == "false" || value == "0") o.motion_params["flight_control_enabled"] = false;
        else statusBar()->showMessage(tr("显控接管许可请选择 是 或 否"), 3000);
        refreshPropsTable(); return;
    }
    if (key.startsWith(QLatin1String("flight_")) && o.platform_cat == 1) {
        for (const auto& parameter : flightParameters()) if (key.mid(7) == QLatin1String(parameter.key)) {
            bool ok = false;
            const double value = text.toDouble(&ok);
            auto changed = o.motion_params;
            if (ok) changed[parameter.key] = value;
            const auto error = ok ? validateFlightConfiguration(changed) : tr("请输入有效数值");
            if (error.isEmpty()) o.motion_params = std::move(changed);
            else statusBar()->showMessage(error, 5000);
            refreshPropsTable(); return;
        }
    }
    if (key==QStringLiteral("motion_authority")) {
        if (text==tr("规定轨迹") || text==QStringLiteral("kinematic")) o.motion_authority=QStringLiteral("kinematic");
        else if (text==tr("动力学") || text==QStringLiteral("dynamic")) o.motion_authority=QStringLiteral("dynamic");
        else statusBar()->showMessage(tr("运动方式请选择 动力学 或 规定轨迹"),3000);
        refreshPropsTable(); return;
    }

    if (key == QStringLiteral("type")) {          // 平台类别
        static const char* kCatNames[4] = {"地面", "空中", "水面", "导弹"};
        const QString t = text.trimmed();
        for (int c = 0; c < 4; ++c)
            if (t == tr(kCatNames[c])) { o.platform_cat = c; break; }
        refreshPropsTable();
        return;
    }
    if (key == QStringLiteral("faction")) {
        const QString t = text.trimmed();
        if (t == tr("红方")) o.faction = Faction::Red;
        else if (t == tr("蓝方")) o.faction = Faction::Blue;
        else o.faction = Faction::Neutral;
        if (cmb_faction_) {
            const QSignalBlocker b(cmb_faction_);
            cmb_faction_->setCurrentIndex(int(o.faction));
        }
        refreshSceneTree();
        refreshPropsTable();
        return;
    }
    if (key == QStringLiteral("position")) {      // "(x, y, z) m"
        QRegularExpression re("^\\s*\\(?\\s*([-+0-9.eE]+)\\s*[, ]+"
                              "\\s*([-+0-9.eE]+)\\s*[, ]+\\s*([-+0-9.eE]+)\\s*\\)?");
        const auto m = re.match(text);
        if (m.hasMatch()) {
            o.pos[0] = m.captured(1).toDouble();
            o.pos[1] = m.captured(2).toDouble();
            o.pos[2] = m.captured(3).toDouble();
            applyTransform(o);
        }
        refreshPropsTable();
        return;
    }
    if (key == QStringLiteral("yaw_deg")) { o.yaw_deg = text.toDouble(); applyTransform(o); refreshPropsTable(); return; }
    if (key == QStringLiteral("scale"))  { o.scale  = text.toDouble(); applyTransform(o); refreshPropsTable(); return; }
    if (key == QStringLiteral("mass"))   { o.mass   = text.toDouble(); refreshPropsTable(); return; }

    // 航迹规划
    if (key == QStringLiteral("route_type")) {
        o.route_type = routeTypeFromName(text.trimmed());
        if (o.route_type == 3 || o.route_type == 5) o.route_loop = false;
        refreshPropsTable();
        return;
    }
    if (key == QStringLiteral("route_loop")) {
        o.route_loop = (text.trimmed() == tr("是"));
        refreshPropsTable();
        return;
    }
    if (key == QStringLiteral("route_start_delay")) {
        o.route_start_delay = std::max(0.0, text.toDouble());
        refreshPropsTable();
        return;
    }

    // 航路点 (key: wp_N_*)
    if (key.startsWith(QStringLiteral("wp_"))) {
        const int i = key.mid(3, key.indexOf('_', 3) - 3).toInt();
        if (i >= 0 && size_t(i) < o.waypoints.size()) {
            auto& w = o.waypoints[size_t(i)];
            if (key.endsWith(QStringLiteral("_time")))  w.time = text.toDouble();
            else if (key.endsWith(QStringLiteral("_speed"))) w.speed = text.toDouble();
            else if (key.endsWith(QStringLiteral("_x"))) w.pos[0] = text.toDouble();
            else if (key.endsWith(QStringLiteral("_y"))) w.pos[1] = text.toDouble();
            else if (key.endsWith(QStringLiteral("_z"))) w.pos[2] = text.toDouble();
            refreshPropsTable();
            syncTrajectoryToFacade(o);
        }
        return;
    }

    // 电磁分组 (key: fg_N_*)
    if (key.startsWith(QStringLiteral("fg_"))) {
        const int i = key.mid(3, key.indexOf('_', 3) - 3).toInt();
        if (i >= 0 && size_t(i) < o.face_groups.size()) {
            auto& g = o.face_groups[size_t(i)];
            const QString f = key.mid(key.indexOf('_', 3) + 1);
            const double v = text.toDouble();
            if (f == QLatin1String("group_id"))       g.group_id = text;
            else if (f == QLatin1String("brdf_type")) g.brdf_type = text;
            else if (f == QLatin1String("albedo"))    g.albedo = v;
            else if (f == QLatin1String("roughness")) g.roughness = v;
            else if (f == QLatin1String("emissivity")) g.emissivity = v;
            else if (f == QLatin1String("base_temp")) g.base_temp = v;
            else if (f == QLatin1String("dielectric_real")) g.dielectric_real = v;
            else if (f == QLatin1String("dielectric_imag")) g.dielectric_imag = v;
            else if (f == QLatin1String("sigma0_db")) g.sigma0_db = v;
            else if (f == QLatin1String("surface_rough")) g.surface_rough = v;
            refreshPropsTable();
        }
        return;
    }

    // 传感器 (key: s_N_*)
    if (key.startsWith(QStringLiteral("s_"))) {
        const int i = key.mid(2, key.indexOf('_', 2) - 2).toInt();
        if (i >= 0 && size_t(i) < o.sensors.size()) {
            auto& s = o.sensors[size_t(i)];
            const QString f = key.mid(key.indexOf('_', 2) + 1);
            if (f == QLatin1String("type")) {
                const QString txt = text.trimmed();
                if (txt.contains(QLatin1String("ir"), Qt::CaseInsensitive))        s.type = int(SensorType::IR);
                else if (txt.contains(QLatin1String("laser"), Qt::CaseInsensitive)) s.type = int(SensorType::LASER);
                else if (txt.contains(QLatin1String("sar"), Qt::CaseInsensitive))   s.type = int(SensorType::SAR);
                else if (txt.contains(QLatin1String("vis"), Qt::CaseInsensitive))   s.type = int(SensorType::VIS);
                else s.type = int(SensorType::RADAR);
            } else if (f == QLatin1String("role")) {
                const QString txt = text.trimmed().toLower();
                s.role = (txt == QLatin1String("guidance") || txt == QStringLiteral("制导"))
                             ? QStringLiteral("guidance") : QStringLiteral("search");
            } else if (f == QLatin1String("band")) {
                s.band = text;
            } else if (f == QLatin1String("power"))       s.power = text.toDouble();
            else if (f == QLatin1String("wavelength"))    s.wavelength = text.toDouble();
            else if (f == QLatin1String("beam_width"))    s.beam_width = text.toDouble();
            else if (f == QLatin1String("pulse_rate"))    s.pulse_rate = text.toDouble();
            else if (f == QLatin1String("duty_cycle"))    s.duty_cycle = text.toDouble();
            else if (f == QLatin1String("fov_h"))         s.fov_h = text.toDouble();
            else if (f == QLatin1String("fov_v"))         s.fov_v = text.toDouble();
            else if (f == QLatin1String("range_m"))       s.range_m = text.toDouble();
            else if (f == QLatin1String("range_gates"))   s.range_gates = std::max(16, text.toInt());
            else if (f == QLatin1String("doppler_bins"))  s.doppler_bins = std::max(16, text.toInt());
            else if (f == QLatin1String("integration_time")) s.integration_time = std::max(1e-6, text.toDouble());
            else if (f == QLatin1String("frame_interval"))   s.frame_interval = std::max(0.0, text.toDouble());
            // §14.3 frame/profile/stages 回写
            else if (f == QLatin1String("kind")) {
                const QString t = text.trimmed().toUpper();
                if (t == QLatin1String("VIS") || t == QLatin1String("SWIR") ||
                    t == QLatin1String("MWIR") || t == QLatin1String("LWIR") ||
                    t == QLatin1String("RADAR"))
                    s.frame_kind = t;
            }
            else if (f == QLatin1String("lambda_min"))    s.lambda_min = std::max(0.0, text.toDouble());
            else if (f == QLatin1String("lambda_max"))    s.lambda_max = std::max(0.0, text.toDouble());
            else if (f == QLatin1String("res_w"))         s.res_w = std::clamp(text.toInt(), 8, 16384);
            else if (f == QLatin1String("res_h"))         s.res_h = std::clamp(text.toInt(), 8, 16384);
            else if (f == QLatin1String("fov_deg"))       s.fov_deg = std::clamp(text.toDouble(), 0.0, 360.0);
            else if (f == QLatin1String("rate_hz"))       s.rate_hz = std::max(0.0, text.toDouble());
            else if (f == QLatin1String("f_number"))      s.f_number = std::clamp(text.toDouble(), 0.1, 100.0);
            else if (f == QLatin1String("transmission"))  s.transmission = std::clamp(text.toDouble(), 0.0, 1.0);
            else if (f == QLatin1String("psf_sigma_px"))  s.psf_sigma_px = std::max(0.0, text.toDouble());
            else if (f == QLatin1String("vignetting"))    s.vignetting = std::clamp(text.toDouble(), 0.0, 1.0);
            else if (f == QLatin1String("pixel_pitch_um")) s.pixel_pitch_um = std::clamp(text.toDouble(), 1.0, 100.0);
            else if (f == QLatin1String("qe"))            s.qe = std::clamp(text.toDouble(), 0.0, 1.0);
            else if (f == QLatin1String("integration_ms")) s.integration_ms = std::clamp(text.toDouble(), 0.001, 1000.0);
            else if (f == QLatin1String("netd_mk"))       s.netd_mk = std::max(0.0, text.toDouble());
            else if (f == QLatin1String("well_ke"))       s.well_ke = std::max(1.0, text.toDouble());
            else if (f == QLatin1String("read_noise_e"))  s.read_noise_e = std::max(0.0, text.toDouble());
            else if (f == QLatin1String("bits"))          s.bits = std::clamp(text.toInt(), 8, 32);
            else if (f == QLatin1String("agc_low_pct"))   s.agc_low_pct = std::clamp(text.toDouble(), 0.0, 0.5);
            else if (f == QLatin1String("agc_high_pct"))  s.agc_high_pct = std::clamp(text.toDouble(), 0.5, 1.0);
            else if (f == QLatin1String("gamma"))         s.gamma = std::clamp(text.toDouble(), 0.1, 10.0);
            else if (f == QLatin1String("stages"))        s.stages = text;
            else if (f == QLatin1String("radar_publish")) s.radar_stages_publish = text;
            // §7.2 扫描机构 (B7): mode 白名单校验, 缺省 fixed
            else if (f == QLatin1String("scan_mode")) {
                const QString t = text.trimmed().toLower();
                if (t == QLatin1String("fixed") || t == QLatin1String("raster") ||
                    t == QLatin1String("sector") || t == QLatin1String("circular") ||
                    t == QLatin1String("rosette") || t == QLatin1String("conical") ||
                    t == QLatin1String("spiral") || t == QLatin1String("slaved"))
                    s.scan_mode = t;
            }
            else if (f == QLatin1String("scan_az_center")) s.scan_az_center = text.toDouble();
            else if (f == QLatin1String("scan_el_center")) s.scan_el_center = text.toDouble();
            else if (f == QLatin1String("scan_az_extent")) s.scan_az_extent = std::max(0.0, text.toDouble());
            else if (f == QLatin1String("scan_el_extent")) s.scan_el_extent = std::max(0.0, text.toDouble());
            else if (f == QLatin1String("scan_az_rate"))   s.scan_az_rate = text.toDouble();
            else if (f == QLatin1String("scan_el_rate"))   s.scan_el_rate = text.toDouble();
            else if (f == QLatin1String("scan_target"))    s.scan_target_ref = text.trimmed();
            refreshPropsTable();
        }
        return;
    }

    // 热属性 (§14.3, key: thermal_base / thermal_states / tp_N_*)
    if (key == QStringLiteral("thermal_base")) {
        o.thermal_base_temp_k = std::max(0.0, text.toDouble());
        refreshPropsTable();
        return;
    }
    if (key == QStringLiteral("thermal_states")) {
        o.thermal_states = text.trimmed();
        refreshPropsTable();
        return;
    }
    if (key.startsWith(QStringLiteral("tp_"))) {
        const int i = key.mid(3, key.indexOf('_', 3) - 3).toInt();
        if (i >= 0 && size_t(i) < o.thermal_parts.size()) {
            auto& p = o.thermal_parts[size_t(i)];
            const QString f = key.mid(key.indexOf('_', 3) + 1);
            if (f == QLatin1String("name"))            p.name = text;
            else if (f == QLatin1String("temp_k"))     p.temp_k = std::max(0.0, text.toDouble());
            else if (f == QLatin1String("emissivity")) p.emissivity = std::clamp(text.toDouble(), 0.0, 1.0);
            else if (f == QLatin1String("face_group")) p.face_group = text.trimmed();
            refreshPropsTable();
            refreshThermalParts();   // 同步电磁光学 Tab 热部件列表/面组选择器
        }
        return;
    }

    // 兼容选中传感器子节点的原始字段编辑 (保留)。

    // 传感器字段 (选中实体下的传感器子节点)
    if (key == QStringLiteral("role")) {
        const int si = tree_scene_ && tree_scene_->currentItem()
            ? tree_scene_->currentItem()->data(0, kRoleSensorIdx).toInt() : -1;
        if (si >= 0 && size_t(si) < o.sensors.size()) {
            const QString txt = text.trimmed().toLower();
            o.sensors[size_t(si)].role =
                (txt == QLatin1String("guidance") || txt == QStringLiteral("制导"))
                    ? QStringLiteral("guidance") : QStringLiteral("search");
            onSensorEdited();
        }
        return;
    }
    if (key == QStringLiteral("band")) {
        const int si = tree_scene_ && tree_scene_->currentItem()
            ? tree_scene_->currentItem()->data(0, kRoleSensorIdx).toInt() : -1;
        if (si >= 0 && size_t(si) < o.sensors.size()) {
            o.sensors[size_t(si)].band = text;
            onSensorEdited();
        }
        return;
    }
    if (key == QStringLiteral("type") && tree_scene_ &&
        tree_scene_->currentItem() &&
        tree_scene_->currentItem()->data(0, kRoleNodeKind).toInt() ==
            int(prop::NodeKind::Sensor)) {
        const int si = tree_scene_->currentItem()->data(0, kRoleSensorIdx).toInt();
        if (si >= 0 && size_t(si) < o.sensors.size()) {
            int t = 0;
            const QString txt = text.trimmed();
            if (txt.contains(QLatin1String("ir"), Qt::CaseInsensitive))      t = int(SensorType::IR);
            else if (txt.contains(QLatin1String("laser"), Qt::CaseInsensitive)) t = int(SensorType::LASER);
            else if (txt.contains(QLatin1String("sar"), Qt::CaseInsensitive))  t = int(SensorType::SAR);
            else if (txt.contains(QLatin1String("vis"), Qt::CaseInsensitive))  t = int(SensorType::VIS);
            else t = int(SensorType::RADAR);
            o.sensors[size_t(si)].type = t;
            onSensorEdited();
        }
        return;
    }
    // 数值型传感器字段
    static const struct { const char* key; double SensorDef::*m; }
        kSensorNums[] = {
            {"power",      &SensorDef::power},
            {"wavelength", &SensorDef::wavelength},
            {"beam_width", &SensorDef::beam_width},
            {"pulse_rate", &SensorDef::pulse_rate},
            {"duty_cycle", &SensorDef::duty_cycle},
            {"fov_h",      &SensorDef::fov_h},
            {"fov_v",      &SensorDef::fov_v},
            {"range_m",    &SensorDef::range_m},
        };
    for (const auto& e : kSensorNums) {
        if (key == QLatin1String(e.key)) {
            const int si = tree_scene_ && tree_scene_->currentItem()
                ? tree_scene_->currentItem()->data(0, kRoleSensorIdx).toInt() : -1;
            if (si >= 0 && size_t(si) < o.sensors.size()) {
                o.sensors[size_t(si)].*(e.m) = text.toDouble();
                onSensorEdited();
            }
            return;
        }
    }
}

void SceneDesignerWindow::onModelDropped(const QString& path, const QPoint& local_pos)
{
    if(!topic_path_.isEmpty()){statusBar()->showMessage(tr("专题目标几何沿用体素场景工坊编辑，避免加入未进入体素真值的预览模型"),5000);return;}
    if (path.isEmpty()) return;
    // 在拖放点地面 (y=0) 位置创建实体。
    double pos[3] = {0.0, 0.0, 0.0};
    if (viewport_) {
        vsg::dvec3 origin, dir;
        if (viewport_->mouseRay(local_pos, origin, dir)) {
            if (std::fabs(dir.y) > 1e-6) {
                const double t = -origin.y / dir.y;
                if (t > 0.0) {
                    pos[0] = origin.x + dir.x * t;
                    pos[2] = origin.z + dir.z * t;
                }
            }
        }
    }
    const QString id = QString("object_%1").arg(objects_.size() + 1);
    if (!addObjectToScene(id, path, pos, 0.0, 1.0)) {
        QMessageBox::warning(this, tr("加载失败"),
                             tr("无法加载模型: %1").arg(path));
        return;
    }
    statusBar()->showMessage(
        tr("实体已创建: %1 @ (%.1f, %.1f)")
            .arg(id).arg(pos[0]).arg(pos[2]), 3000);
}

void SceneDesignerWindow::onEntityPicked(const QString& id)
{
    if (id.isEmpty()) {
        // 未命中 → 取消选中, 属性 Dock 清空。
        if (tree_scene_) tree_scene_->clearSelection();
        return;
    }
    selectObjectInTree(id);
}

void SceneDesignerWindow::onRemoveObject()
{
    const int idx = selectedObjectIndex();
    if (idx < 0) return;
    facade_->removeEntity(objects_[size_t(idx)].id);
    objects_.erase(objects_.begin() + idx);
    if (viewport_) viewport_->markActivity();
    refreshSceneTree();
    statusBar()->showMessage(tr("实体已删除"), 2000);
}

void SceneDesignerWindow::onSceneTreeSelectionChanged()
{
    if (!tree_scene_) return;
    auto* item = tree_scene_->currentItem();
    if (!item) {
        refreshPropsTable();
        syncEditorsFromSelection();
        refreshSensorList();
        refreshFaceGroupList();
        refreshWaypointTable();
        return;
    }
    const int kind = item->data(0, kRoleNodeKind).toInt();
    // 场景/地形/环境节点: 仅刷新「属性变量」表格, 无详细可编辑项。
    if (kind != 2 && kind != 3) {
        refreshPropsTable();
        return;
    }
    refreshPropsTable();
    syncEditorsFromSelection();
    refreshSensorList();
    refreshFaceGroupList();
    refreshWaypointTable();
    if (kind == 3) {
        const int si = item->data(0, kRoleSensorIdx).toInt();
        if (lst_sensors_ && si >= 0 && si < lst_sensors_->count()) {
            const QSignalBlocker b(lst_sensors_);
            lst_sensors_->setCurrentRow(si);
        }
        onSensorSelected(si);
    } else if (lst_sensors_ && lst_sensors_->count() > 0) {
        lst_sensors_->setCurrentRow(0);
    }
}

void SceneDesignerWindow::onTransformEdited()
{
    if (suppress_edit_signals_) return;
    const int idx = selectedObjectIndex();
    if (idx < 0) return;
    SceneObject& o = objects_[size_t(idx)];
    o.pos[0] = spn_pos_x_->value();
    o.pos[1] = spn_pos_y_->value();
    o.pos[2] = spn_pos_z_->value();
    o.yaw_deg = spn_yaw_->value();
    o.scale = spn_scale_->value();
    o.mass = spn_mass_->value();
    applyTransform(o);
    refreshPropsTable();
}

void SceneDesignerWindow::onPlaceToggled(bool on)
{
    if (facade_) facade_->setPlacementEnabled(on);
    // 与地形笔刷互斥: 摆放开启时笔刷切回「关闭」 (两者都占用视口左键)。
    if (on && !rb_brush_.empty() && rb_brush_[0]) {
        QSignalBlocker b(rb_brush_[0]);
        rb_brush_[0]->setChecked(true);
        pushBrushParams();
    }
    if (on) statusBar()->showMessage(
        tr("点击放置已启用: 在 3D 视口中左键放置所选实体"), 4000);
}

// ---------------------------------------------------------------------------
// Sensor management
// ---------------------------------------------------------------------------
void SceneDesignerWindow::refreshSensorList()
{
    if (!lst_sensors_) return;
    QSignalBlocker b(lst_sensors_);
    lst_sensors_->clear();
    const int idx = selectedObjectIndex();
    if (idx < 0) return;
    int n = 1;
    for (const auto& s : objects_[size_t(idx)].sensors)
        lst_sensors_->addItem(QString("#%1  %2  [%3]").arg(n++).arg(sensorTypeNameFromInt(s.type)).arg(s.band));
}

void SceneDesignerWindow::onAddSensor()
{
    const int idx = selectedObjectIndex();
    if (idx < 0) { statusBar()->showMessage(tr("请先选择实体"), 2000); return; }
    SensorDef s;
    s.type = int(SensorType::RADAR);      // 默认雷达
    s.band = QStringLiteral("X");
    s.platform = objects_[size_t(idx)].id;
    objects_[size_t(idx)].sensors.push_back(s);
    refreshSceneTree();
    updateStatusCounts();
    refreshPropsTable();
    statusBar()->showMessage(tr("传感器已添加: %1").arg(sensorTypeNameFromInt(s.type)), 2000);
}

void SceneDesignerWindow::onSensorSelected(int row)
{
    // 旧折叠编辑面板已移除, 属性树内编辑由 onPropTreeEdited 直接处理。
    if (!cmb_sensor_type_ || !lst_sensors_) return;
    const int oi = selectedObjectIndex();
    suppress_edit_signals_ = true;
    if (oi < 0 || row < 0 || size_t(row) >= objects_[size_t(oi)].sensors.size()) {
        suppress_edit_signals_ = false;
        return;
    }
    const SensorDef& s = objects_[size_t(oi)].sensors[size_t(row)];
    const QSignalBlocker b1(cmb_sensor_type_), b2(cmb_sensor_band_),
                         b3(cmb_sensor_role_);
    const int ti = cmb_sensor_type_->findData(s.type);
    cmb_sensor_type_->setCurrentIndex(ti >= 0 ? ti : 0);
    const int bi = cmb_sensor_band_->findText(s.band);
    cmb_sensor_band_->setCurrentIndex(bi >= 0 ? bi : 0);
    const int ri = cmb_sensor_role_->findData(s.role);
    cmb_sensor_role_->setCurrentIndex(ri >= 0 ? ri : 0);
    spn_s_power_->setValue(s.power);
    spn_s_wavelen_->setValue(s.wavelength);
    spn_s_beam_->setValue(s.beam_width);
    spn_s_prf_->setValue(s.pulse_rate);
    spn_s_duty_->setValue(s.duty_cycle);
    spn_s_fovh_->setValue(s.fov_h);
    spn_s_fovv_->setValue(s.fov_v);
    spn_s_offset_y_->setValue(s.offset_y);
    spn_s_az_->setValue(s.az_deg);
    spn_s_el_->setValue(s.el_deg);
    spn_s_range_->setValue(s.range_m);
    suppress_edit_signals_ = false;
}

void SceneDesignerWindow::onSensorEdited()
{
    // 属性树内传感器字段编辑已由 onPropTreeEdited 直接写回 objects_, 此函数仅刷新列表显示。
    const int oi = selectedObjectIndex();
    if (oi < 0) return;
    refreshPropsTable();
    if (cmb_sensor_type_ && lst_sensors_) {
        const int si = selectedSensorIndex();
        if (si >= 0 && size_t(si) < objects_[size_t(oi)].sensors.size()) {
            const auto& s = objects_[size_t(oi)].sensors[size_t(si)];
            if (auto* item = lst_sensors_->item(si))
                item->setText(QString("#%1  %2  [%3]")
                    .arg(si + 1).arg(sensorTypeNameFromInt(s.type)).arg(s.band));
        }
    }
}

// ---------------------------------------------------------------------------
// Face groups
// ---------------------------------------------------------------------------
void SceneDesignerWindow::refreshFaceGroupList()
{
    if (!cmb_face_group_) return;
    const QSignalBlocker b(cmb_face_group_);
    cmb_face_group_->clear();
    const int oi = selectedObjectIndex();
    if (oi < 0) { syncFaceGroupEditors(); refreshThermalParts(); return; }
    const auto& fgs = objects_[size_t(oi)].face_groups;
    for (size_t i = 0; i < fgs.size(); ++i)
        cmb_face_group_->addItem(fgs[i].group_id, int(i));
    if (!fgs.empty()) cmb_face_group_->setCurrentIndex(0);
    syncFaceGroupEditors();
    refreshThermalParts();   // §14.3: 面组变化 → 热部件面组选择器选项同步
}

void SceneDesignerWindow::syncFaceGroupEditors()
{
    suppress_edit_signals_ = true;
    const int oi = selectedObjectIndex();
    const int fi = selectedFaceGroupIndex();
    const bool valid = oi >= 0 && fi >= 0 && size_t(fi) < objects_[size_t(oi)].face_groups.size();
    const FaceGroupDef dflt;
    const FaceGroupDef& g = valid ? objects_[size_t(oi)].face_groups[size_t(fi)] : dflt;
    const int bi = cmb_brdf_->findText(g.brdf_type);
    cmb_brdf_->setCurrentIndex(bi >= 0 ? bi : 0);
    spn_fg_face_begin_->setValue(g.face_begin);
    spn_fg_face_end_->setValue(g.face_end);
    spn_fg_albedo_->setValue(g.albedo);
    spn_fg_rough_->setValue(g.roughness);
    spn_fg_emiss_->setValue(g.emissivity);
    spn_fg_temp_->setValue(g.base_temp);
    spn_fg_dielec_r_->setValue(g.dielectric_real);
    spn_fg_dielec_i_->setValue(g.dielectric_imag);
    spn_fg_sigma0_->setValue(g.sigma0_db);
    spn_fg_surf_rough_->setValue(g.surface_rough);
    const std::initializer_list<QWidget*> fg_widgets = {cmb_brdf_, spn_fg_face_begin_, spn_fg_face_end_,
        spn_fg_albedo_, spn_fg_rough_, spn_fg_emiss_, spn_fg_temp_, spn_fg_dielec_r_, spn_fg_dielec_i_,
        spn_fg_sigma0_, spn_fg_surf_rough_};
    for (QWidget* w : fg_widgets) w->setEnabled(valid);
    suppress_edit_signals_ = false;
}

void SceneDesignerWindow::onFaceGroupSelected(int) { syncFaceGroupEditors(); }

void SceneDesignerWindow::onAddFaceGroup()
{
    const int oi = selectedObjectIndex();
    if (oi < 0) { statusBar()->showMessage(tr("请先选择实体"), 2000); return; }
    FaceGroupDef g;
    g.group_id = QString("group_%1").arg(objects_[size_t(oi)].face_groups.size() + 1);
    objects_[size_t(oi)].face_groups.push_back(g);
    refreshSceneTree();
    refreshPropsTable();
    refreshThermalParts();   // §14.3: 面组选择器选项同步
    statusBar()->showMessage(tr("电磁分组已添加: %1").arg(g.group_id), 2000);
}

void SceneDesignerWindow::onFaceGroupEdited()
{
    // 属性树内电磁分组字段编辑由 onPropTreeEdited 直接写回 objects_, 此处仅刷新显示。
    const int oi = selectedObjectIndex();
    if (oi < 0) return;
    refreshPropsTable();
}

// ---------------------------------------------------------------------------
// Thermal parts (§14.3 thermal.parts + 面组选择器)
// ---------------------------------------------------------------------------
void SceneDesignerWindow::onAddThermalPart()
{
    const int oi = selectedObjectIndex();
    if (oi < 0) { statusBar()->showMessage(tr("请先选择实体"), 2000); return; }
    ThermalPartDef p;
    p.name = QString("part_%1").arg(objects_[size_t(oi)].thermal_parts.size() + 1);
    // 默认关联第一个面组 (SceneSource 按 group_id 名匹配覆盖温度/发射率)
    if (!objects_[size_t(oi)].face_groups.empty())
        p.face_group = objects_[size_t(oi)].face_groups.front().group_id;
    objects_[size_t(oi)].thermal_parts.push_back(p);
    refreshSceneTree();
    refreshPropsTable();
    refreshThermalParts();
    statusBar()->showMessage(tr("热部件已添加: %1").arg(p.name), 2000);
}

void SceneDesignerWindow::onRemoveThermalPart()
{
    const int oi = selectedObjectIndex();
    const int pi = lst_thermal_parts_ ? lst_thermal_parts_->currentRow() : -1;
    if (oi < 0 || pi < 0 || size_t(pi) >= objects_[size_t(oi)].thermal_parts.size()) return;
    const QString nm = objects_[size_t(oi)].thermal_parts[size_t(pi)].name;
    auto& parts = objects_[size_t(oi)].thermal_parts;
    parts.erase(parts.begin() + pi);
    refreshSceneTree();
    refreshPropsTable();
    refreshThermalParts();
    statusBar()->showMessage(tr("热部件已删除: %1").arg(nm), 2000);
}

void SceneDesignerWindow::onThermalPartSelected(int row)
{
    if (!lst_thermal_parts_) return;
    const int oi = selectedObjectIndex();
    const bool valid = oi >= 0 && row >= 0 && row < int(objects_[size_t(oi)].thermal_parts.size());
    const ThermalPartDef dflt;
    const ThermalPartDef& p = valid ? objects_[size_t(oi)].thermal_parts[size_t(row)] : dflt;
    suppress_edit_signals_ = true;
    if (edt_tp_name_) edt_tp_name_->setText(p.name);
    if (cmb_tp_face_group_ && !p.face_group.isEmpty()) {
        const int fi = cmb_tp_face_group_->findText(p.face_group);
        if (fi >= 0) cmb_tp_face_group_->setCurrentIndex(fi);
    }
    if (spn_tp_temp_)  spn_tp_temp_->setValue(p.temp_k);
    if (spn_tp_emiss_) spn_tp_emiss_->setValue(p.emissivity);
    suppress_edit_signals_ = false;
}

void SceneDesignerWindow::onThermalPartEdited()
{
    if (suppress_edit_signals_) return;
    const int oi = selectedObjectIndex();
    if (oi < 0 || !lst_thermal_parts_) return;
    const int pi = lst_thermal_parts_->currentRow();
    if (pi < 0 || size_t(pi) >= objects_[size_t(oi)].thermal_parts.size()) return;
    auto& p = objects_[size_t(oi)].thermal_parts[size_t(pi)];
    if (edt_tp_name_) p.name = edt_tp_name_->text();
    if (cmb_tp_face_group_ && cmb_tp_face_group_->currentIndex() >= 0)
        p.face_group = cmb_tp_face_group_->currentText();
    if (spn_tp_temp_)  p.temp_k = spn_tp_temp_->value();
    if (spn_tp_emiss_) p.emissivity = spn_tp_emiss_->value();
    if (auto* item = lst_thermal_parts_->item(pi))
        item->setText(QStringLiteral("%1 → %2 (%3 K)")
            .arg(p.name, p.face_group.isEmpty() ? QStringLiteral("(未关联)") : p.face_group)
            .arg(p.temp_k, 0, 'f', 1));
    refreshPropsTable();
}

void SceneDesignerWindow::refreshThermalParts()
{
    if (!lst_thermal_parts_) return;
    const int oi = selectedObjectIndex();
    const bool valid = oi >= 0;
    // 面组选择器选项 = 当前实体 face_groups
    if (cmb_tp_face_group_) {
        const QSignalBlocker bc(cmb_tp_face_group_);
        cmb_tp_face_group_->clear();
        if (valid)
            for (const auto& g : objects_[size_t(oi)].face_groups)
                cmb_tp_face_group_->addItem(g.group_id);
    }
    const QSignalBlocker bl(lst_thermal_parts_);
    lst_thermal_parts_->clear();
    if (valid) {
        for (const auto& p : objects_[size_t(oi)].thermal_parts)
            lst_thermal_parts_->addItem(QStringLiteral("%1 → %2 (%3 K)")
                .arg(p.name, p.face_group.isEmpty() ? QStringLiteral("(未关联)") : p.face_group)
                .arg(p.temp_k, 0, 'f', 1));
        if (!objects_[size_t(oi)].thermal_parts.empty())
            lst_thermal_parts_->setCurrentRow(0);
    }
    const bool has = valid && !objects_[size_t(oi)].thermal_parts.empty();
    const std::initializer_list<QWidget*> tp_widgets = {edt_tp_name_, cmb_tp_face_group_,
        spn_tp_temp_, spn_tp_emiss_, btn_tp_del_};
    for (QWidget* w : tp_widgets) w->setEnabled(valid && has);
    syncThermalPartEditors();
}

void SceneDesignerWindow::syncThermalPartEditors()
{
    if (!lst_thermal_parts_) return;
    onThermalPartSelected(lst_thermal_parts_->currentRow());
}

// ---------------------------------------------------------------------------
// Waypoints
// ---------------------------------------------------------------------------
void SceneDesignerWindow::refreshWaypointTable()
{
    if (!tbl_waypoints_) return;
    wp_syncing_ = true;
    const int oi = selectedObjectIndex();
    tbl_waypoints_->setRowCount(0);
    if (oi >= 0) {
        const auto& wps = objects_[size_t(oi)].waypoints;
        tbl_waypoints_->setRowCount(int(wps.size()));
        for (size_t i = 0; i < wps.size(); ++i) {
            const auto& w = wps[i];
            const double vals[5] = {w.time, w.speed, w.pos[0], w.pos[1], w.pos[2]};
            for (int c = 0; c < 5; ++c) {
                auto* it = new QTableWidgetItem(QString::number(vals[c], 'f', 2));
                it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                tbl_waypoints_->setItem(int(i), c, it);
            }
        }
    }
    const bool en = oi >= 0;
    tbl_waypoints_->setEnabled(en);
    if (cmb_route_type_) cmb_route_type_->setEnabled(en);
    if (cmb_platform_cat_) cmb_platform_cat_->setEnabled(en);
    if (chk_route_loop_) chk_route_loop_->setEnabled(en);
    if (spn_route_delay_) spn_route_delay_->setEnabled(en);
    // 同步航迹规划参数到 UI
    if (oi >= 0) {
        const SceneObject& o = objects_[size_t(oi)];
        if (cmb_platform_cat_) {
            int idx = cmb_platform_cat_->findData(o.platform_cat);
            if (idx >= 0) cmb_platform_cat_->setCurrentIndex(idx);
        }
        if (cmb_route_type_) {
            int idx = cmb_route_type_->findData(o.route_type);
            if (idx >= 0) cmb_route_type_->setCurrentIndex(idx);
        }
        if (chk_route_loop_) chk_route_loop_->setChecked(o.route_loop);
        if (spn_route_delay_) {
            QSignalBlocker b(spn_route_delay_);
            spn_route_delay_->setValue(o.route_start_delay);
        }
    }
    wp_syncing_ = false;
}

void SceneDesignerWindow::onWaypointAdd()
{
    const int oi = selectedObjectIndex();
    if (oi < 0) { statusBar()->showMessage(tr("请先选择实体"), 2000); return; }
    auto& wps = objects_[size_t(oi)].waypoints;
    WaypointDef w;
    w.time = wps.empty() ? 0.0 : wps.back().time + 1.0;
    w.pos[0] = objects_[size_t(oi)].pos[0];
    w.pos[1] = objects_[size_t(oi)].pos[1];
    w.pos[2] = objects_[size_t(oi)].pos[2];
    wps.push_back(w);
    refreshPropsTable();
    syncTrajectoryToFacade(objects_[size_t(oi)]);
    statusBar()->showMessage(tr("航路点已添加 (t=%1 s)").arg(w.time, 0, 'f', 1), 2000);
}

// =============================================================================
void SceneDesignerWindow::onWaypointItemChanged(QTableWidgetItem* item)
{
    if (wp_syncing_ || !item) return;
    const int oi = selectedObjectIndex();
    if (oi < 0) return;
    auto& wps = objects_[size_t(oi)].waypoints;
    const int row = item->row();
    if (row < 0 || size_t(row) >= wps.size()) return;
    bool ok = false;
    const double v = item->text().toDouble(&ok);
    if (!ok) return;
    switch (item->column()) {
        case 0: wps[size_t(row)].time   = v; break;
        case 1: wps[size_t(row)].speed  = v; break;
        case 2: wps[size_t(row)].pos[0] = v; break;
        case 3: wps[size_t(row)].pos[1] = v; break;
        case 4: wps[size_t(row)].pos[2] = v; break;
        default: return;
    }
    syncTrajectoryToFacade(objects_[size_t(oi)]);
}

// ---------------------------------------------------------------------------
// 航迹规划参数
// ---------------------------------------------------------------------------
void SceneDesignerWindow::onRouteTypeChanged(int)
{
    // 旧折叠面板已移除, 航迹规划参数改由属性树 onPropTreeEdited 直接写回。
    if (!cmb_route_type_ || wp_syncing_) return;
    const int oi = selectedObjectIndex();
    if (oi < 0) return;
    objects_[size_t(oi)].route_type = cmb_route_type_->currentData().toInt();
}

void SceneDesignerWindow::onPlatformCatChanged(int)
{
    // 旧折叠面板已移除, 平台类别改由属性树 onPropTreeEdited 直接写回。
    if (!cmb_platform_cat_ || wp_syncing_) return;
    const int oi = selectedObjectIndex();
    if (oi < 0) return;
    objects_[size_t(oi)].platform_cat = cmb_platform_cat_->currentData().toInt();
}

void SceneDesignerWindow::onRouteLoopToggled(bool on)
{
    if (wp_syncing_) return;
    const int oi = selectedObjectIndex();
    if (oi < 0) return;
    objects_[size_t(oi)].route_loop = on;
}

void SceneDesignerWindow::onRouteDelayEdited(double v)
{
    if (wp_syncing_) return;
    const int oi = selectedObjectIndex();
    if (oi < 0) return;
    objects_[size_t(oi)].route_start_delay = v;
}

void SceneDesignerWindow::onFactionEdited(int)
{
    if (suppress_edit_signals_) return;
    const int oi = selectedObjectIndex();
    if (oi < 0) return;
    objects_[size_t(oi)].faction = Faction(cmb_faction_->currentData().toInt());
    refreshSceneTree();
    facade_->setFaction(objects_[size_t(oi)].id, toLibFaction(objects_[size_t(oi)].faction));
}

void SceneDesignerWindow::onValidateScene()
{
    if(!topic_path_.isEmpty()){
        try{
            auto* feature=viewport_->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene");if(!feature||!feature->runtime())throw std::runtime_error("体素专题尚未就绪");const auto state=feature->runtime()->snapshot();
            size_t equipmentCount=0;if(topic_document_.contains("equipment")){const auto config=earth::VoxelEquipmentConfig::parse(topic_document_.at("equipment"));equipmentCount=config.equipment.size();
                for(const auto& p:config.platforms)if(!state->objects.count(p.object))throw std::runtime_error("平台绑定的体素对象不存在："+p.object);
                for(const auto& e:config.equipment)if(!e.mountObject.empty()&&!state->objects.count(e.mountObject))throw std::runtime_error("装备安装对象不存在："+e.mountObject);
            }
            QMessageBox::information(this,tr("体素场景校验"),tr("已校验统一发布状态：%1 个对象，%2 个装备。\n内容版本 %3；占用、几何、物理和材料约束已通过事务检查。\n实际装备输出请启动正式仿真后验证。").arg(state->objects.size()).arg(equipmentCount).arg(state->contentRevision));
        }catch(const std::exception& e){QMessageBox::warning(this,tr("体素场景校验失败"),QString::fromUtf8(e.what()));}
        return;
    }
    auto result = SceneValidator::validate(objects_, facade_->terrain());
    for (const auto& object : objects_)
        if (!object.mesh_path.isEmpty() && ui::ProductWorkflow::resolveSceneAsset(object.mesh_path,
                current_scenario_path_, resolveWorkspaceRoot()).isEmpty())
            result.issues.push_back({SceneValidator::Severity::Error, tr("%1：找不到模型 %2").arg(object.id, object.mesh_path)});
    spdlog::info("SceneDesigner: scene validated (errors={}, warnings={}, passes={})",
                 result.errorCount(), result.warningCount(), result.passCount());
    SceneValidator::showReport(this, result);
}

// ---------------------------------------------------------------------------
// Scenario save / load
// ---------------------------------------------------------------------------
void SceneDesignerWindow::onSaveScenario()
{
    if (!current_scenario_path_.isEmpty() && QFileInfo::exists(current_scenario_path_)) {
        saveScenarioToPath(current_scenario_path_);
        return;
    }
    onSaveScenarioAs();
}

void SceneDesignerWindow::onSaveScenarioAs()
{
    vsgraysim::Workspace ws(resolveWorkspaceRoot().toStdString());
    ws.ensureDirs();
    auto path = QFileDialog::getSaveFileName(
        this, tr("保存场景"),
        QString::fromStdString(ws.sceneDir()) + (topic_path_.isEmpty()?"/scenario.scenario.json":"/taipei.vsimscene"),
        tr("场景文件 (*.json *.vsimscene)"));
    if (path.isEmpty()) return;
    saveScenarioToPath(path);
}

void SceneDesignerWindow::onLaunchSimulation()
{
    if(!topic_path_.isEmpty()) {
        if(topic_baseline_pending_){statusBar()->showMessage(tr("请等待体素场景准备完成"),3000);return;}
        if(!topic_document_.contains("equipment")){QMessageBox::warning(this,tr("缺少装备配置"),tr("请在专题的 equipment 字段绑定现有装备配置后交付仿真。"));return;}
        QString destination=current_scenario_path_;
        if (!earth::VoxelScenePackage::extension(destination.toStdString())) destination=QFileDialog::getSaveFileName(this,tr("保存体素专题并启动仿真"),"workspace/scenes/taipei.vsimscene",tr("体素场景包 (*.vsimscene);;兼容专题 (*.vtopic.json *.json)"));
        if(destination.isEmpty()||!saveScenarioToPath(destination))return;
        ui::ProductWorkflow::launch(this,QStringLiteral("vsgraysim_sim_control"),{QFileInfo(current_scenario_path_).absoluteFilePath()},tr("仿真主控"));return;
    }
    auto validation = SceneValidator::validate(objects_, facade_->terrain());
    for (const auto& object : objects_)
        if (!object.mesh_path.isEmpty() && ui::ProductWorkflow::resolveSceneAsset(object.mesh_path,
                current_scenario_path_, resolveWorkspaceRoot()).isEmpty())
            validation.issues.push_back({SceneValidator::Severity::Error, tr("%1：找不到模型 %2").arg(object.id, object.mesh_path)});
    if (validation.errorCount() > 0) {
        SceneValidator::showReport(this, validation);
        statusBar()->showMessage(tr("请修正场景错误后再交付仿真"), 5000);
        return;
    }
    if (validation.warningCount() > 0)
        spdlog::warn("Scene handoff retains {} validation warnings", validation.warningCount());
    QString destination = current_scenario_path_;
    if (destination.isEmpty()) {
        vsgraysim::Workspace workspace(resolveWorkspaceRoot().toStdString()); workspace.ensureDirs();
        destination = QFileDialog::getSaveFileName(this, tr("保存场景并启动仿真"),
            QString::fromStdString(workspace.sceneDir()) + "/scenario.scenario.json", tr("场景文件 (*.json)"));
    }
    if (destination.isEmpty() || !saveScenarioToPath(destination)) return;
    if (ui::ProductWorkflow::launch(this, QStringLiteral("vsgraysim_sim_control"),
            {QFileInfo(current_scenario_path_).absoluteFilePath()}, tr("仿真主控")))
        statusBar()->showMessage(tr("场景已交付仿真主控。初始化成功后可启动显控、建链并播放。"), 7000);
}

nlohmann::json SceneDesignerWindow::sceneDocument() const
{
    if(!topic_path_.isEmpty()) {
        auto document=topic_document_;document["name"]=edt_scenario_name_?edt_scenario_name_->text().toStdString():document.value("name",std::string("体素专题"));document["paused"]=true;
        if(viewport_)if(auto* feature=viewport_->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene"))if(feature->runtime()){
            const auto state=feature->runtime()->snapshot();document["checkpoint"]=feature->runtime()->checkpoint();
            if(auto* camera=viewport_->context().camera())if(auto look=camera->viewMatrix.cast<vsg::LookAt>()){
                const auto inverse=vsg::inverse(simcore::geo::makeEnuToWorldMatrix(state->anchor));const auto eye=inverse*look->eye,target=inverse*look->center;const auto up=inverse*vsg::dvec4(look->up,0.);
                document["camera"]={{"eye_m",{eye.x,eye.y,eye.z}},{"target_m",{target.x,target.y,target.z}},{"up",{up.x,up.y,up.z}}};
            }
        }
        return document;
    }
    nlohmann::json j;
    j["scenario_name"] = edt_scenario_name_->text().toStdString();
    j["description"]   = edt_scenario_desc_->text().toStdString();
    j["version"]       = "1.0";
    j["simulation"]=clock_config_.toJson();
    const std::string terrain_ref = facade_->terrainRef();
    if (!terrain_ref.empty()) j["terrain_ref"] = terrain_ref;

    // geo_anchor: 锚点单一来源 = 场景文件。当前 rocky_map 锚点回写
    // (加载场景/手动重锚后保存不丢; 无 rocky_map 时缺失 = 用预设默认)。
    if (viewport_) {
        if (auto* rocky = viewport_->featureAs<viewport3d::EarthView>(
                viewport3d::EarthView::kId)) {
            const viewport3d::GeoAnchor& ga = rocky->anchor();
            j["geo_anchor"] = {{"lat", ga.lat_deg}, {"lon", ga.lon_deg},
                               {"alt", ga.alt_m}, {"heading_deg", ga.heading_deg}};
            const auto earth=rocky->earthConfiguration();
            if(!earth.empty())j["earth"]=earth;
        }
    }
    if(const auto* terrain=terrain_facade_ ? terrain_facade_->terrain() : nullptr) {
        QCryptographicHash digest(QCryptographicHash::Sha256);
        const auto& heights=terrain->heightsData();
        digest.addData(QByteArray::fromRawData(reinterpret_cast<const char*>(heights.data()),qsizetype(heights.size()*sizeof(double))));
        digest.addData(QByteArray::number(terrain->extent(),'g',17));
        digest.addData(QByteArray::fromStdString(j.value("geo_anchor",nlohmann::json::object()).dump()));
        j["earth"]["editor_source_hash"]=digest.result().toHex().toStdString();
    }

    j["objects"] = nlohmann::json::array();
    // 类别默认值与局部编辑合并，避免只改接管许可时丢失原默认保持模式。
    const auto defaultMotionParams = [](int cat) -> nlohmann::json {
        switch (cat) {
        case 1:  return {{"max_speed", 300.0}, {"cruise_speed", 200.0},
                          {"max_load_factor", 9.0}, {"max_altitude", 15000.0},
                          {"altitude_hold", true}, {"hold_altitude", 1000.0}};
        case 2:  return {{"max_speed", 15.0}, {"cruise_speed", 10.0},
                          {"max_turn_rate", 5.0}, {"enable_wave_response", true}};
        case 3:  return {{"max_speed", 800.0}, {"boost_accel", 50.0},
                          {"max_lateral_accel", 200.0}, {"guidance_law", 1},
                          {"nav_constant", 4.0}, {"seeker_range", 20000.0},
                          {"boost_duration", 3.0}, {"max_flight_time", 60.0},
                          {"enable_terminal_maneuver", true}};
        default: return {{"max_speed_fwd", 20.0}, {"max_accel", 5.0},
                          {"max_turn_rate", 40.0}, {"max_climb_angle", 30.0},
                          {"tracked", true}};
        }
    };
    for (const auto& o : objects_) {
        nlohmann::json jo;
        jo["object_id"] = o.id.toStdString();
        jo["name"] = o.id.toStdString();   // 仿真平台命名契约 (createFromObjectJSON)
        const auto resolvedMesh = ui::ProductWorkflow::resolveSceneAsset(o.mesh_path, current_scenario_path_, resolveWorkspaceRoot());
        jo["mesh_path"] = (resolvedMesh.isEmpty() ? o.mesh_path : resolvedMesh).toStdString();
        jo["scale"]     = o.scale;
        jo["faction"]   = factionToString(o.faction);
        jo["face_count"] = facade_->entityTriangleCount(o.id);
        jo["physics"] = {
            {"mass", o.mass},
            {"initial_position", {o.pos[0], o.pos[1], o.pos[2]}},
            {"initial_rotation_deg", {0.0, o.yaw_deg, 0.0}},
            {"friction", 0.8},
            {"convex_hull", true},
        };
        const uint32_t fc = jo["face_count"].get<uint32_t>();
        nlohmann::json jfgs = nlohmann::json::array();
        auto write_group = [&](const FaceGroupDef& g) {
            const int fe = g.face_end < 0 ? int(fc > 0 ? fc - 1 : 0) : g.face_end;
            jfgs.push_back(nlohmann::json{
                {"group_id", g.group_id.toStdString()},
                {"face_range", {g.face_begin, fe}},
                {"vis_params", {{"brdf_type", g.brdf_type.toStdString()},
                                {"albedo", g.albedo}, {"roughness", g.roughness}}},
                {"lwir_params", {{"emissivity", g.emissivity}, {"base_temp", g.base_temp}}},
                {"radar_x_params", {{"rcs_lut_id", 1}, {"dielectric_real", g.dielectric_real},
                                    {"dielectric_imag", g.dielectric_imag},
                                    {"sigma0_db", g.sigma0_db}, {"surface_rough", g.surface_rough}}},
            });
        };
        if (o.face_groups.empty()) write_group(FaceGroupDef{});
        else for (const auto& g : o.face_groups) write_group(g);
        jo["face_groups"] = jfgs;

        // §14.3 thermal (整体热属性: base_temp_k / parts 热部件 / states 工况倍率;
        // SceneSource 消费: base_temp_k→fallback, parts 按 face_group 名覆盖面组)
        if (o.thermal_base_temp_k > 0.0 || !o.thermal_parts.empty() ||
            !o.thermal_states.trimmed().isEmpty()) {
            nlohmann::json jt;
            if (o.thermal_base_temp_k > 0.0) jt["base_temp_k"] = o.thermal_base_temp_k;
            if (!o.thermal_parts.empty()) {
                nlohmann::json jps = nlohmann::json::array();
                for (const auto& p : o.thermal_parts)
                    jps.push_back({{"name", p.name.toStdString()},
                                   {"temp_k", p.temp_k}, {"emissivity", p.emissivity},
                                   {"face_group", p.face_group.toStdString()}});
                jt["parts"] = jps;
            }
            if (!o.thermal_states.trimmed().isEmpty()) {
                nlohmann::json jst = nlohmann::json::object();
                const auto pairs = o.thermal_states.split(',');
                for (const auto& kv : pairs) {
                    const int c = kv.indexOf(QLatin1Char(':'));
                    if (c <= 0) continue;
                    const double v = kv.mid(c + 1).trimmed().toDouble();
                    if (v > 0.0) jst[kv.left(c).trimmed().toStdString()] = v;
                }
                if (!jst.empty()) jt["states"] = jst;
            }
            if (!jt.empty()) jo["thermal"] = jt;
        }

        // 平台类型与标签 (供仿真实体识别 / createFromObjectJSON 检测类别)
        static const char* kCatNames[4] = {"tank", "aircraft", "ship", "missile"};
        const int cat = std::max(0, std::min(3, int(o.platform_cat)));
        const std::string platformType = cat == 1 ? o.airframe_kind.toStdString() : kCatNames[cat];
        jo["type"] = platformType;
        jo["tags"] = nlohmann::json::array({"platform", platformType});

        // 运动参数 (仿真实例化平台运动模型优先读取此字段)
        jo["motion_params"] = defaultMotionParams(cat);
        if (o.motion_params.is_object()) jo["motion_params"].update(o.motion_params);
        jo["motion_params"]["motion_authority"]=o.motion_authority.toStdString();

        // 航迹规划 (route_plan, 带时间参数)
        nlohmann::json rp;
        rp["platform"]        = o.id.toStdString();
        rp["category"]        = cat;
        rp["type"]            = o.route_type;
        rp["type_name"]       = o.route_type == 3 ? "Intercept" : (o.route_type == 2 ? "Cruise" : (o.route_type == 1 ? "Patrol" : "Waypoint"));
        rp["loop"]            = o.route_loop;
        rp["start_delay_s"]   = o.route_start_delay;
        nlohmann::json rw = nlohmann::json::array();
        for (const auto& w : o.waypoints) {
            rw.push_back({
                {"time", w.time},
                {"pos", {w.pos[0], w.pos[1], w.pos[2]}},
                {"speed", w.speed}
            });
        }
        rp["waypoints"] = rw;
        jo["route_plan"] = rp;

        // 兼容旧格式 waypoints
        jo["waypoints"] = rw;

        j["objects"].push_back(jo);
    }

    j["sensors"]["sensor_list"] = nlohmann::json::array();
    j["sensors"]["cross_priority"] = nlohmann::json::object();
    int sid = 1;
    for (const auto& o : objects_) {
        for (const auto& s : o.sensors) {
            nlohmann::json js;
            js["sensor_id"] = sid++;
            js["type"]  = sensorTypeNameFromInt(s.type);
            js["band"]  = s.band.toStdString();
            js["role"]  = (s.role.isEmpty() ? QStringLiteral("search") : s.role).toStdString();
            js["platform_object"] = (s.platform.isEmpty() ? o.id : s.platform).toStdString();
            js["tx_params"] = {{"power", s.power}, {"beam_width", s.beam_width},
                {"wavelength", s.wavelength}, {"pulse_rate", s.pulse_rate}, {"duty_cycle", s.duty_cycle}};
            const double azr = vsg::radians(s.az_deg);
            const double elr = vsg::radians(s.el_deg);
            js["aperture"] = {{"position_offset", {0.0, s.offset_y, 0.0}},
                {"direction", {std::sin(azr) * std::cos(elr), std::sin(elr), -std::cos(azr) * std::cos(elr)}},
                {"fov_horizontal", s.fov_h}, {"fov_vertical", s.fov_v},
                {"azimuth_deg", s.az_deg}, {"elevation_deg", s.el_deg}, {"range_m", s.range_m}};
            // 信号处理参数: 仿真侧按装备覆盖全局默认 (距离门/多普勒门/积分时间/帧序)
            js["signal_params"] = {{"range_gates", s.range_gates},
                {"doppler_bins", s.doppler_bins},
                {"integration_time", s.integration_time},
                {"frame_interval_s", s.frame_interval}};
            // ---- §14.3 新 schema: frame/profile/stages (与旧字段并存;
            //      参数优先级 Param 消息 > 场景 JSON > 硬编码默认) ----
            const QString kind = s.frame_kind.isEmpty()
                ? deriveFrameKind(s.type, s.band) : s.frame_kind;
            if (!kind.isEmpty()) {
                nlohmann::json jfr;
                jfr["kind"] = kind.toStdString();
                jfr["lambda_min"] = s.lambda_min > 0.0
                    ? s.lambda_min : deriveLambdaMin(kind, s.wavelength);
                jfr["lambda_max"] = s.lambda_max > 0.0
                    ? s.lambda_max : deriveLambdaMax(kind, s.wavelength);
                jfr["resolution"] = {{"width", s.res_w}, {"height", s.res_h}};
                jfr["fov_deg"]    = s.fov_deg > 0.0 ? s.fov_deg : s.fov_h;
                jfr["rate_hz"]    = s.rate_hz > 0.0
                    ? s.rate_hz : deriveRateHz(kind, s.pulse_rate, s.integration_ms);
                js["frame"] = jfr;
                // profile: SensorApi.h SensorProfile 三组同名映射
                js["profile"] = {
                    {"optics", {{"f_number", s.f_number}, {"transmission", s.transmission},
                                {"psf_sigma_px", s.psf_sigma_px}, {"vignetting", s.vignetting}}},
                    {"detector", {{"pixel_pitch_um", s.pixel_pitch_um}, {"qe", s.qe},
                                  {"integration_ms", s.integration_ms}, {"netd_mk", s.netd_mk},
                                  {"well_ke", s.well_ke}, {"read_noise_e", s.read_noise_e},
                                  {"bits", s.bits}}},
                    {"electronics", {{"agc_low_pct", s.agc_low_pct},
                                     {"agc_high_pct", s.agc_high_pct}, {"gamma", s.gamma}}}};
                const auto splitStages = [](const QString& csv) {
                    nlohmann::json arr = nlohmann::json::array();
                    const auto parts = csv.split(',');
                    for (const auto& t : parts)
                        if (!t.trimmed().isEmpty()) arr.push_back(t.trimmed().toStdString());
                    return arr;
                };
                if (kind == QLatin1String("RADAR"))
                    js["radar_stages_publish"] = splitStages(s.radar_stages_publish);
                else
                    js["stages"] = splitStages(s.stages);
            }
            // §7.2 扫描机构 (B7): scan 段条件落盘 (缺省 fixed 不落, 旧场景兼容)
            if (s.scan_mode != QLatin1String("fixed") || s.scan_az_center != 0.0 ||
                s.scan_el_center != 0.0 || s.scan_az_extent != 0.0 ||
                s.scan_el_extent != 0.0 || s.scan_az_rate != 0.0 ||
                s.scan_el_rate != 0.0 || !s.scan_target_ref.isEmpty()) {
                nlohmann::json jsc;
                jsc["mode"] = s.scan_mode.toStdString();
                if (s.scan_az_center != 0.0) jsc["az_center"] = s.scan_az_center;
                if (s.scan_el_center != 0.0) jsc["el_center"] = s.scan_el_center;
                if (s.scan_az_extent != 0.0) jsc["az_extent"] = s.scan_az_extent;
                if (s.scan_el_extent != 0.0) jsc["el_extent"] = s.scan_el_extent;
                if (s.scan_az_rate != 0.0)   jsc["az_rate"]   = s.scan_az_rate;
                if (s.scan_el_rate != 0.0)   jsc["el_rate"]   = s.scan_el_rate;
                if (!s.scan_target_ref.isEmpty())
                    jsc["target_ref"] = s.scan_target_ref.toStdString();
                js["scan"] = jsc;
            }
            js["rx_cross_sensors"] = nlohmann::json::array();
            j["sensors"]["sensor_list"].push_back(js);
        }
    }

    // W2: 细粒度大气参数全部导出 (仿真软件打开时还原)
    j["atmosphere"] = {
        {"config_path", "config/atmosphere_layers.json"},
        {"weather_scenario", env_weather_.toStdString()},
        {"weather_severity", env_severity_ / 100.0},
        {"cloud_cover", env_cloud_cover_},
        {"cloud_base", env_cloud_base_},
        {"rain", env_rain_},
        {"snow", env_snow_},
        {"visibility", env_visibility_},
        {"wind_speed", env_wind_speed_},
        {"wind_az", env_wind_az_},
        {"sun_el", env_sun_el_},
        {"sun_az", env_sun_az_},
        {"wave_height", env_wave_height_}};
    if(!ocean_config_.empty())j["ocean"]=ocean_config_;

    // 绑定/导引关系 (装备挂载 EQUIPPED_ON 由 sensors.platform_object 隐式表达,
    // 此处保存显式的导引/协同关系, 供仿真实体重建 RelationshipGraph)
    j["bindings"]["guidance"] = nlohmann::json::array();
    for (const auto& b : binding_links_) {
        j["bindings"]["guidance"].push_back({
            {"from", b.from_id.toStdString()},
            {"relation", b.relation.toStdString()},
            {"to", b.to_id.toStdString()}
        });
    }
    return j;
}

bool SceneDesignerWindow::saveUsdTargetEdits(const QString& path) try
{
    auto* feature = viewport_->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene");
    if (topic_path_.isEmpty() || topic_baseline_pending_ || publishing_scene_ || !feature || !feature->runtime())
        throw std::runtime_error("请先打开并等待地球体素场景专题就绪");
    earth::saveVoxelUsdEdits(path.toStdString(), *feature->runtime()->snapshot());
    statusBar()->showMessage(tr("已保存 USD 目标编辑层"), 4000);
    return true;
}
catch (const std::exception& error) {
    statusBar()->showMessage(tr("保存 USD 目标编辑层失败　%1").arg(QString::fromUtf8(error.what())), 8000);
    qWarning() << error.what();
    return false;
}

bool SceneDesignerWindow::applyUsdTargetEdits(const QString& path) try
{
    auto* feature = viewport_->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene");
    if (topic_path_.isEmpty() || topic_baseline_pending_ || publishing_scene_ || !feature || !feature->runtime())
        throw std::runtime_error("请先打开并等待地球体素场景专题就绪");
    earth::applyVoxelUsdEdits(path.toStdString(), *feature->runtime());
    viewport_->markActivity();
    refreshSceneTree();
    statusBar()->showMessage(tr("已应用 USD 目标编辑层，可以发布体素场景包"), 5000);
    return true;
}
catch (const std::exception& error) {
    statusBar()->showMessage(tr("应用 USD 目标编辑层失败　%1").arg(QString::fromUtf8(error.what())), 8000);
    qWarning() << error.what();
    return false;
}

bool SceneDesignerWindow::saveScenarioToPath(const QString& path, bool terrainPages) try
{
    if (publishing_scene_) return false;
    // Persist live brush edits as an immutable, content-addressed Earth package
    // before handing the scene to another process. Old saved scenes retain
    // their previous terrain files even if this scene save is cancelled/fails.
    if(topic_path_.isEmpty() && terrain_facade_ && terrain_facade_->terrain()) {
        const auto pending=sceneDocument();
        const auto hash=pending.at("earth").at("editor_source_hash").get<std::string>();
        const auto directory=QFileInfo(path).absoluteDir().absoluteFilePath("earth");
        const auto package=terrain_facade_->exportGeoPackage(directory,QStringLiteral("surface_")+QString::fromStdString(hash.substr(0,20)));
        if(package.isEmpty() || !facade_->loadEarthPackage(package.toStdString()))
            throw std::runtime_error("edited terrain could not be exported and bound to the scene");
    }
    if(!topic_path_.isEmpty() && topic_baseline_pending_)throw std::runtime_error("voxel scene is still preparing; save was not committed");
    auto j = sceneDocument();
    if (terrainPages) j["terrain_paging"] = "quad-v1";
    if(!topic_path_.isEmpty()&&!j.contains("checkpoint"))throw std::runtime_error("voxel scene state is not available");
    const auto liveDocument=j.dump();
    // Packages stored alongside the scene remain movable as one directory.
    const auto portableReference=[&](nlohmann::json& reference) {
        if(!reference.is_string())return;
        const auto relative=QFileInfo(path).absoluteDir().relativeFilePath(QString::fromStdString(reference.get<std::string>()));
        if(!relative.startsWith("../")&&!QDir::isAbsolutePath(relative))reference=relative.toStdString();
    };
    if(j.contains("terrain_ref"))portableReference(j["terrain_ref"]);
    if(j.contains("earth")&&j["earth"].contains("map"))portableReference(j["earth"]["map"]);
    const bool packaged=earth::VoxelScenePackage::extension(path.toStdString());
    if (packaged) {
        if (topic_path_.isEmpty() || !j.contains("checkpoint"))
            throw std::runtime_error("scene packages require an authored voxel topic");
        QScopedValueRollback<bool> busy(publishing_scene_, true);
        statusBar()->showMessage(tr("正在后台发布体素场景包…"));
        auto task = std::async(std::launch::async, [file = path.toStdString(), &j] {
            earth::VoxelScenePackage::save(file, j, j.at("checkpoint"));
        });
        QEventLoop loop;
        QTimer poll;
        connect(&poll, &QTimer::timeout, &loop, [&] {
            if (task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) loop.quit();
        });
        poll.start(20);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
        task.get();
    }
    QSaveFile output(path);
    const QByteArray document = packaged?QByteArray():QByteArray::fromStdString(j.dump(2));
    if(!topic_path_.isEmpty()&&document.size()>256LL*1024*1024)throw std::runtime_error("voxel topic exceeds the 256 MiB document budget");
    if (!packaged&&(!output.open(QIODevice::WriteOnly) || output.write(document) != document.size() || !output.commit())) {
        QMessageBox::warning(this, tr("保存失败"), tr("无法完整保存场景：\n%1\n%2").arg(path, output.errorString()));
        return false;
    }

    const QString prev_path = QString::fromStdString(vsgraysim::Workspace::previewPathFor(path.toStdString()));
    QImage prev;
    if (viewport_) prev = viewport_->grabFrame();
    if (prev.isNull()) prev = compositeScenePreview(edt_scenario_name_->text(), int(objects_.size()),
        int(j.value("sensors",nlohmann::json::object()).value("sensor_list",nlohmann::json::array()).size()));
    if (prev.save(prev_path)) spdlog::info("SceneDesigner: preview saved '{}'", prev_path.toStdString());

    current_scenario_path_ = QFileInfo(path).absoluteFilePath();
    if(!topic_path_.isEmpty()){topic_path_=current_scenario_path_;topic_document_=j;}
    setWindowFilePath(current_scenario_path_);
    saved_document_ = liveDocument;
    spdlog::info("SceneDesigner: scenario saved '{}' ({} objects)", path.toStdString(), objects_.size());
    statusBar()->showMessage(tr("场景已保存: %1").arg(QFileInfo(path).fileName()), 4000);
    return true;
} catch (const std::exception& error) {
    QMessageBox::warning(this, tr("保存失败"), tr("场景配置无效：%1").arg(QString::fromUtf8(error.what())));
    return false;
}

void SceneDesignerWindow::onLoadScenario()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("打开场景"), "config/scenarios", tr("场景文件 (*.json *.vsimscene)"));
    if (path.isEmpty()) return;
    loadScenarioFromFile(path);
}

void SceneDesignerWindow::loadScenarioFromFile(const QString& path)
{
    std::ifstream in(path.toStdString());
    nlohmann::json j;
    try { in >> j; } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("打开失败"), tr("场景文件解析失败: %1").arg(e.what()));
        return;
    }
    if(j.value("format",std::string())=="earth-scene-topic-v1"||j.value("format",std::string())==earth::VoxelScenePackage::format) {
        try {
            const auto project=VoxelSceneProject::open(path.toStdString());
            auto config=viewport3d::ViewportConfig::fromTopicFile(project.path,"scene_designer");config.add("voxel_scene",{{"paused",true}});
            config.useWorkspaceTerrain();
            if(!confirmUnsavedChanges())return;
            topic_path_=QString::fromStdString(project.path);topic_document_=project.document;topic_baseline_pending_=true;
            current_scenario_path_=topic_path_;setWindowFilePath(current_scenario_path_);clearObjects();
            edt_scenario_name_->setText(QString::fromStdString(topic_document_.value("name",std::string("体素专题"))));
            findChild<QTabWidget*>("CentralTabs")->setCurrentWidget(viewport_);
            viewport_->setIdleSuspend(false);viewport_->applyConfig(config);viewport_->markActivity();return;
        }catch(const std::exception& error){QMessageBox::warning(this,tr("打开失败"),QString::fromUtf8(error.what()));return;}
    }
    if (!j.contains("objects")) { QMessageBox::warning(this, tr("打开失败"), tr("不是有效的场景文件")); return; }
    vsgraysim::ScenarioClockConfig importedClock;
    try {
        importedClock=vsgraysim::ScenarioClockConfig::fromJson(j.value("simulation",nlohmann::json::object()));
        if(j.contains("ocean")&&!j["ocean"].is_object())throw std::invalid_argument("ocean must be an object");
    }
    catch (const std::exception& e) { QMessageBox::warning(this,tr("打开失败"),tr("时序配置无效: %1").arg(e.what())); return; }
    QStringList missingAssets;
    for (const auto& object : j.value("objects", nlohmann::json::array())) {
        const auto mesh = QString::fromStdString(object.value("mesh_path", std::string()));
        if (!mesh.isEmpty() && ui::ProductWorkflow::resolveSceneAsset(mesh, path, resolveWorkspaceRoot()).isEmpty())
            missingAssets << mesh;
    }
    if (!missingAssets.isEmpty()) {
        QMessageBox::warning(this, tr("模型资产缺失"), tr("场景未替换。请恢复以下模型文件后重试：\n%1").arg(missingAssets.join('\n')));
        return;
    }
    try { vsgraysim::earth::EarthDataService::loadScenario(j,path.toStdString()); }
    catch(const std::exception& error) {
        QMessageBox::warning(this,tr("地表数据无效"),QString::fromStdString(error.what()));return;
    }
    if (!confirmUnsavedChanges()) return;
    if(!topic_path_.isEmpty()){
        topic_path_.clear();topic_document_=nlohmann::json();topic_baseline_pending_=false;voxelTopicUi(*this,false);
        auto config = viewport3d::ViewportConfig::fromPreset("scene_designer");
        config.useWorkspaceTerrain();
        findChild<QTabWidget*>("CentralTabs")->setCurrentWidget(viewport_);
        viewport_->applyConfig(config);
    }
    clock_config_ = std::move(importedClock);

    clearObjects();
    edt_scenario_name_->setText(QString::fromStdString(j.value("scenario_name", std::string("未命名场景"))));
    edt_scenario_desc_->setText(QString::fromStdString(j.value("description", std::string())));

    facade_->resetTerrain();
    if(viewport_) {
        if(auto* globe=viewport_->featureAs<viewport3d::EarthView>(viewport3d::EarthView::kId)) {
            std::string earthError;
            if(!globe->loadEarthScenario(j,path.toStdString(),earthError)) {
                QMessageBox::warning(this,tr("地表数据无效"),QString::fromStdString(earthError));return;
            }
            facade_->setTerrainRef(vsgraysim::earth::EarthDataService::resolveMapPath(j,path.toStdString()));
            viewport_->markActivity();
        }
    }

    for (const auto& jo : j.value("objects", nlohmann::json::array())) {
        const QString id = QString::fromStdString(jo.value("object_id", std::string("object")));
        const QString originalMesh = QString::fromStdString(jo.value("mesh_path", std::string()));
        const auto resolvedMesh = ui::ProductWorkflow::resolveSceneAsset(originalMesh, path, resolveWorkspaceRoot());
        const QString mesh = resolvedMesh.isEmpty() ? originalMesh : resolvedMesh;
        double pos[3] = {0,0,0}; double yaw = 0.0;
        const auto phys = jo.value("physics", nlohmann::json::object());
        if (phys.contains("initial_position") && phys["initial_position"].is_array() && phys["initial_position"].size() == 3)
            for (int k = 0; k < 3; ++k) pos[k] = phys["initial_position"][k].get<double>();
        if (phys.contains("initial_rotation_deg") && phys["initial_rotation_deg"].is_array() && phys["initial_rotation_deg"].size() == 3)
            yaw = phys["initial_rotation_deg"][1].get<double>();
        const double scale = jo.value("scale", 1.0);
        if (addObjectToScene(id, mesh, pos, yaw, scale)) {
            SceneObject& so = objects_.back();
            so.mass = phys.value("mass", 1000.0);
            so.faction = factionFromString(jo.value("faction", std::string("neutral")));

            // 平台类型 (route 航迹规划用)
            if (jo.contains("type")) {
                const std::string ty = jo["type"].get<std::string>();
                if (ty.find("aircraft") != std::string::npos || ty.find("uav") != std::string::npos || ty == "plane" || ty == "drone") {
                    so.platform_cat = 1;
                    so.airframe_kind = ty.find("uav") != std::string::npos || ty == "drone" ? "uav" : "aircraft";
                }
                else if (ty.find("ship") != std::string::npos || ty.find("vessel") != std::string::npos) so.platform_cat = 2;
                else if (ty.find("missile") != std::string::npos) so.platform_cat = 3;
                else so.platform_cat = 0;
            }

            // 运动参数 (缺失时保持空对象, 落盘时按类别生成默认值)
            if (jo.contains("motion_params") && jo["motion_params"].is_object())
                so.motion_params = jo["motion_params"];
            so.motion_authority=QString::fromStdString(so.motion_params.value("motion_authority",std::string("dynamic")));

            // 航迹规划参数 (route_plan)
            if (jo.contains("route_plan") && jo["route_plan"].is_object()) {
                const auto& rp = jo["route_plan"];
                so.route_type = rp.value("type", 1);
                so.route_loop = rp.value("loop", true);
                so.route_start_delay = rp.value("start_delay_s", 0.0);
                if (rp.contains("category")) so.platform_cat = rp["category"].get<int>();
            }

            if (jo.contains("waypoints") && jo["waypoints"].is_array()) {
                so.waypoints.clear();
                for (const auto& jw : jo["waypoints"]) {
                    WaypointDef w; w.time = jw.value("time", 0.0);
                    w.speed = jw.value("speed", 0.0);
                    // 兼容 pos 与 position 两种字段
                    const auto jp = jw.contains("pos") ? jw["pos"] : jw.value("position", nlohmann::json::array());
                    if (jp.is_array() && jp.size() == 3) for (int k = 0; k < 3; ++k) w.pos[k] = jp[size_t(k)].get<double>();
                    so.waypoints.push_back(w);
                }
            }
            if (so.waypoints.empty() && jo.contains("route_plan") && jo["route_plan"].contains("waypoints")) {
                for (const auto& jw : jo["route_plan"]["waypoints"]) {
                    WaypointDef w; w.time = jw.value("time", 0.0);
                    w.speed = jw.value("speed", 0.0);
                    const auto jp = jw.contains("pos") ? jw["pos"] : nlohmann::json::array();
                    if (jp.is_array() && jp.size() == 3) for (int k = 0; k < 3; ++k) w.pos[k] = jp[size_t(k)].get<double>();
                    so.waypoints.push_back(w);
                }
            }
            if (jo.contains("face_groups") && jo["face_groups"].is_array() && !jo["face_groups"].empty()) {
                so.face_groups.clear();
                for (const auto& jg : jo["face_groups"]) {
                    FaceGroupDef g;
                    g.group_id = QString::fromStdString(jg.value("group_id", std::string("group")));
                    const auto fr = jg.value("face_range", nlohmann::json::array());
                    if (fr.is_array() && fr.size() == 2) { g.face_begin = fr[0].get<int>(); g.face_end = fr[1].get<int>(); }
                    const auto vis = jg.value("vis_params", nlohmann::json::object());
                    g.brdf_type = QString::fromStdString(vis.value("brdf_type", std::string("diffuse")));
                    g.albedo = vis.value("albedo", 0.25); g.roughness = vis.value("roughness", 0.5);
                    const auto lw = jg.value("lwir_params", nlohmann::json::object());
                    g.emissivity = lw.value("emissivity", 0.9); g.base_temp = lw.value("base_temp", 300.15);
                    const auto rd = jg.value("radar_x_params", nlohmann::json::object());
                    g.dielectric_real = rd.value("dielectric_real", 18.0); g.dielectric_imag = rd.value("dielectric_imag", 5.0);
                    g.sigma0_db = rd.value("sigma0_db", -15.0); g.surface_rough = rd.value("surface_rough", 0.4);
                    so.face_groups.push_back(g);
                }
            }
            // §14.3 thermal (整体热属性; 缺省保持 0/空 = 不落盘兼容旧场景)
            if (jo.contains("thermal") && jo["thermal"].is_object()) {
                const auto& tj = jo.at("thermal");
                so.thermal_base_temp_k = tj.value("base_temp_k",
                                                   tj.value("base_temp", 0.0));
                if (tj.contains("parts") && tj["parts"].is_array()) {
                    for (const auto& jp : tj["parts"]) {
                        ThermalPartDef p;
                        p.name = QString::fromStdString(jp.value("name", std::string("part")));
                        p.temp_k = jp.value("temp_k", jp.value("base_temp_k", 450.0));
                        p.emissivity = jp.value("emissivity", 0.85);
                        p.face_group = QString::fromStdString(jp.value("face_group", std::string()));
                        so.thermal_parts.push_back(std::move(p));
                    }
                }
                if (tj.contains("states") && tj["states"].is_object()) {
                    QString csv;
                    for (auto st = tj["states"].begin(); st != tj["states"].end(); ++st) {
                        if (!csv.isEmpty()) csv += QLatin1Char(',');
                        csv += QString::fromStdString(st.key()) + QLatin1Char(':')
                             + QString::number(st.value().get<double>());
                    }
                    so.thermal_states = csv;
                }
            }
        }
    }

    const auto sensors = j.value("sensors", nlohmann::json::object());
    for (const auto& js : sensors.value("sensor_list", nlohmann::json::array())) {
        const QString plat = QString::fromStdString(js.value("platform_object", std::string()));
        SceneObject* owner = nullptr;
        for (auto& o : objects_) if (o.id == plat) { owner = &o; break; }
        if (!owner && !objects_.empty()) owner = &objects_.front();
        if (!owner) continue;
        SensorDef s;
        s.type = sensorTypeFromName(js.value("type", std::string("radar")));
        s.band = QString::fromStdString(js.value("band", std::string("X")));
        s.role = QString::fromStdString(js.value("role", std::string("search")));
        s.platform = plat;
        const auto tx = js.value("tx_params", nlohmann::json::object());
        s.power = tx.value("power", 1000.0); s.wavelength = tx.value("wavelength", 0.03);
        s.beam_width = tx.value("beam_width", 0.05); s.pulse_rate = tx.value("pulse_rate", 1000.0);
        s.duty_cycle = tx.value("duty_cycle", 0.05);
        const auto ap = js.value("aperture", nlohmann::json::object());
        s.fov_h = ap.value("fov_horizontal", 60.0); s.fov_v = ap.value("fov_vertical", 30.0);
        s.az_deg = ap.value("azimuth_deg", 0.0); s.el_deg = ap.value("elevation_deg", 0.0);
        s.range_m = ap.value("range_m", 8000.0);
        // 信号处理参数 (旧场景缺失时保持 SensorDef 默认值)
        const auto sp = js.value("signal_params", nlohmann::json::object());
        s.range_gates      = sp.value("range_gates", 1024);
        s.doppler_bins     = sp.value("doppler_bins", 256);
        s.integration_time = sp.value("integration_time", 0.01);
        s.frame_interval   = sp.value("frame_interval_s", 0.0);
        if (ap.contains("position_offset") && ap["position_offset"].is_array() && ap["position_offset"].size() == 3)
            s.offset_y = ap["position_offset"][1].get<double>();
        // 旧场景 aperture.position 数组兼容 (position_offset 缺失时取 Y 分量)
        else if (ap.contains("position") && ap["position"].is_array() && ap["position"].size() == 3)
            s.offset_y = ap["position"][1].get<double>();
        // Simulation consumes direction. Prefer it when legacy redundant angles
        // disagree, so opening and saving cannot reverse an existing aperture.
        if (ap.contains("direction") && ap["direction"].is_array() && ap["direction"].size() == 3) {
            const double dx = ap["direction"][0].get<double>(), dy = ap["direction"][1].get<double>(), dz = ap["direction"][2].get<double>();
            const double len = std::sqrt(dx*dx+dy*dy+dz*dz);
            if (len > 1e-9) { s.az_deg = vsg::degrees(std::atan2(dx, -dz)); s.el_deg = vsg::degrees(std::asin(std::max(-1.0, std::min(1.0, dy/len)))); }
        }
        // ---- §14.3 新 schema 读取 (缺失时按旧字段推导, 保证旧场景无损打开) ----
        const auto jfr = js.value("frame", nlohmann::json::object());
        s.frame_kind = QString::fromStdString(jfr.value("kind", std::string()));
        if (s.frame_kind.isEmpty()) s.frame_kind = deriveFrameKind(s.type, s.band);
        const double wl = s.wavelength;
        s.lambda_min = jfr.value("lambda_min", deriveLambdaMin(s.frame_kind, wl));
        s.lambda_max = jfr.value("lambda_max", deriveLambdaMax(s.frame_kind, wl));
        if (jfr.contains("resolution") && jfr["resolution"].is_object()) {
            s.res_w = jfr["resolution"].value("width", s.res_w);
            s.res_h = jfr["resolution"].value("height", s.res_h);
        } else if (js.contains("resolution") && js["resolution"].is_array() && js["resolution"].size() == 2) {
            s.res_w = js["resolution"][0].get<int>(); s.res_h = js["resolution"][1].get<int>();
        }
        s.fov_deg = jfr.value("fov_deg", s.fov_h);
        s.rate_hz = jfr.value("rate_hz", deriveRateHz(s.frame_kind, s.pulse_rate, s.integration_ms));
        const auto pf = js.value("profile", nlohmann::json::object());
        if (pf.is_object()) {
            const auto po = pf.value("optics", nlohmann::json::object());
            s.f_number     = po.value("f_number", s.f_number);
            s.transmission = po.value("transmission", s.transmission);
            s.psf_sigma_px = po.value("psf_sigma_px", s.psf_sigma_px);
            s.vignetting   = po.value("vignetting", s.vignetting);
            const auto pd = pf.value("detector", nlohmann::json::object());
            s.pixel_pitch_um = pd.value("pixel_pitch_um", s.pixel_pitch_um);
            s.qe             = pd.value("qe", s.qe);
            s.integration_ms = pd.value("integration_ms", s.integration_ms);
            s.netd_mk        = pd.value("netd_mk", s.netd_mk);
            s.well_ke        = pd.value("well_ke", s.well_ke);
            s.read_noise_e   = pd.value("read_noise_e", s.read_noise_e);
            s.bits           = pd.value("bits", s.bits);
            const auto pe = pf.value("electronics", nlohmann::json::object());
            s.agc_low_pct  = pe.value("agc_low_pct", s.agc_low_pct);
            s.agc_high_pct = pe.value("agc_high_pct", s.agc_high_pct);
            s.gamma        = pe.value("gamma", s.gamma);
        }
        const auto joinStages = [](const nlohmann::json& arr) {
            QString csv;
            for (const auto& e : arr) {
                if (!csv.isEmpty()) csv += QLatin1Char(',');
                csv += QString::fromStdString(e.get<std::string>());
            }
            return csv;
        };
        if (js.contains("stages") && js["stages"].is_array())
            s.stages = joinStages(js["stages"]);
        if (js.contains("radar_stages_publish") && js["radar_stages_publish"].is_array())
            s.radar_stages_publish = joinStages(js["radar_stages_publish"]);
        // §7.2 扫描机构 (B7): scan 段同名映射 (缺省 fixed)
        if (js.contains("scan")) {
            const auto& jsc = js["scan"];
            s.scan_mode = QString::fromStdString(
                jsc.value("mode", std::string("fixed"))).toLower();
            s.scan_az_center = jsc.value("az_center", 0.0);
            s.scan_el_center = jsc.value("el_center", 0.0);
            s.scan_az_extent = jsc.value("az_extent", 0.0);
            s.scan_el_extent = jsc.value("el_extent", 0.0);
            s.scan_az_rate   = jsc.value("az_rate", 0.0);
            s.scan_el_rate   = jsc.value("el_rate", 0.0);
            s.scan_target_ref = QString::fromStdString(jsc.value("target_ref", std::string()));
        }
        owner->sensors.push_back(s);
    }

    const auto atm = j.value("atmosphere", nlohmann::json::object());
    env_weather_ = QString::fromStdString(atm.value("weather_scenario", std::string("clear")));
    ocean_config_=j.value("ocean",nlohmann::json::object());
    env_severity_ = int(std::clamp(atm.value("weather_severity", 0.0), 0.0, 1.0) * 100.0);
    // Use product defaults, not the previous scene's weather.
    env_cloud_cover_  = atm.value("cloud_cover",  0.5);
    env_cloud_base_   = atm.value("cloud_base",   1200.0);
    env_rain_         = atm.value("rain",         0.0);
    env_snow_         = atm.value("snow",         0.0);
    env_visibility_   = atm.value("visibility",   10000.0);
    env_wind_speed_   = atm.value("wind_speed",   6.0);
    env_wind_az_      = atm.value("wind_az",      80.0);
    env_sun_el_       = atm.value("sun_el",       62.0);
    env_sun_az_       = atm.value("sun_az",       175.0);
    env_wave_height_  = atm.value("wave_height",  0.0);
    // 视口就绪后立即下发 (vsgReady 前 pushAtmosphereToViewport 静默跳过);
    // 此处 vsgReady 已触发 (loadScenarioFromFile 由 vsgReady 槽调用), 故立即可下发。
    pushAtmosphereToViewport();
    // 同步场景树「环境」节点标签
    if (auto* root = tree_scene_ && tree_scene_->topLevelItemCount() > 0
            ? tree_scene_->topLevelItem(0) : nullptr)
        if (root->childCount() >= 2)
            root->child(1)->setText(0, tr("环境: %1").arg(weatherLabel(env_weather_)));

    // 绑定/导引关系 (bindings.guidance)
    binding_links_.clear();
    const auto bd = j.value("bindings", nlohmann::json::object());
    for (const auto& jb : bd.value("guidance", nlohmann::json::array())) {
        BindingLink b;
        b.from_id = QString::fromStdString(jb.value("from", std::string()));
        b.relation = QString::fromStdString(jb.value("relation", std::string("guides")));
        b.to_id = QString::fromStdString(jb.value("to", std::string()));
        if (!b.from_id.isEmpty() && !b.to_id.isEmpty())
            binding_links_.push_back(std::move(b));
    }

    for (const auto& o : objects_) {
        facade_->setFaction(o.id, toLibFaction(o.faction));
        syncTrajectoryToFacade(o);
    }
    groundClampEntities();          // 沉入模型地形下方的实体抬升至表面
    refreshSceneTree();
    syncEditorsFromSelection();
    refreshFaceGroupList();
    refreshWaypointTable();
    updateStatusCounts();
    current_scenario_path_ = QFileInfo(path).absoluteFilePath();
    setWindowFilePath(current_scenario_path_);
    saved_document_ = sceneDocument().dump();
    // 场景加载完成后自适应取景 (对大模型场景, 相机 fit 到包围盒, 免手动缩放)。
    QTimer::singleShot(0, this, [this] {
        if (viewport_) {
            viewport_->resetCamera();
            viewport_->markActivity();   // 场景装载 → 需要连续若干帧渲染
        }
    });
    spdlog::info("SceneDesigner: scenario loaded '{}' ({} objects)", path.toStdString(), objects_.size());
    statusBar()->showMessage(tr("场景已加载: %1 (%2 个实体)").arg(QFileInfo(path).fileName()).arg(objects_.size()), 4000);
}

void SceneDesignerWindow::groundClampEntities()
{
    if (!facade_) return;
    // 模型地形对象 (id 含 terrain, 如 taipei 城) 的表面作为地面参考。
    QString terrain_id;
    for (const auto& o : objects_)
        if (o.id.contains(QLatin1String("terrain"), Qt::CaseInsensitive)) {
            terrain_id = o.id;
            break;
        }
    if (terrain_id.isEmpty()) return;
    // 垂直 raycast 求各实体 (x,z) 处的地形表面高度; 沉入下方者抬升至表面。
    // 只处理低空/地面实体 (y < 500), 高空平台 (飞机/导弹) 位置保持设计值。
    for (auto& o : objects_) {
        if (o.id == terrain_id || o.pos[1] > 500.0) continue;
        const double h = facade_->terrainSurfaceHeight(terrain_id, o.pos[0], o.pos[2]);
        if (h <= -1e18) continue;                       // 无命中
        if (o.pos[1] < h - 0.05) {                      // 沉入表面下方 → 贴合
            o.pos[1] = h;
            const double p[3] = {o.pos[0], o.pos[1], o.pos[2]};
            facade_->setEntityTransform(o.id, p, o.yaw_deg, o.scale);
        }
    }
}

void SceneDesignerWindow::onResetCamera()
{
    if (viewport_) {
        viewport_->resetCamera();
        viewport_->markActivity();
    }
}

void SceneDesignerWindow::onAbout()
{
    QMessageBox::information(this, tr("关于"),
        tr("VSG 场景设计器\n\n中央: VSG 3D 场景 (地面网格 + 实体模型)\n"
           "左侧: 场景树 / 地形生成 / 地形笔刷 / 水面系统\n"
           "右侧: 模型库 / 属性 (变换/传感器/电磁光学/航迹/绑定) / 地形分析 / 地形文件操作\n\n"
           "操作: 左键旋转相机 | 中键平移 | 右键/滚轮缩放\n"
           "点击放置模式: 左键在地面放置所选实体\n"
           "笔刷模式开启时: 左键拖拽编辑地形 (与放置模式互斥)\n\n"
           "VSG %1  Qt %2")
            .arg(QString::fromLatin1(VSG_VERSION_STRING), QString::fromUtf8(qVersion())));
}

// ===========================================================================
// 地形编辑能力 (自原独立地形编辑软件 MainWindow 迁入)
//   Dock: 地形生成 / 地形笔刷 / 水面系统 / 地形分析 / 地形文件操作
//   全部经 terrain_facade_ (TerrainEditFacade) 下发, vsgReady 前自动排队。
// ===========================================================================

// ---------------------------------------------------------------------------
// Dock: 地形生成 (程序化地形模板 + 参数 + 生成按钮)
// ---------------------------------------------------------------------------
void SceneDesignerWindow::buildTerrainGenDock()
{
    auto* v = createDock(tr("地形生成"), Qt::LeftDockWidgetArea);

    auto* f = new QFormLayout;
    cmb_terrain_ = new QComboBox;
    cmb_terrain_->addItems({"平坦 Flat", "平原 Plains", "丘陵 Hills",
                            "山脉 Mountains", "沙漠 Desert", "峡谷 Canyon",
                            "岛屿 Island", "戈壁 Gobi", "沼泽 Swamp",
                            "海洋 Ocean"});
    cmb_grid_ = new QComboBox;
    cmb_grid_->addItems({"128", "256", "512", "1024"});
    cmb_grid_->setCurrentIndex(1);
    spn_seed_ = new QSpinBox;
    spn_seed_->setRange(1, 999999);
    spn_seed_->setValue(20260810 % 99999);
    spn_height_ = new QDoubleSpinBox;
    spn_height_->setRange(5.0, 500.0);
    spn_height_->setValue(60.0);
    spn_height_->setSuffix(" m");
    spn_extent_ = new QDoubleSpinBox;
    spn_extent_->setRange(200.0, 100000.0);
    spn_extent_->setValue(1000.0);
    spn_extent_->setSuffix(" m");
    f->addRow(tr("地形类型:"), cmb_terrain_);
    f->addRow(tr("网格精度:"), cmb_grid_);
    f->addRow(tr("随机种子:"), spn_seed_);
    f->addRow(tr("高度幅度:"), spn_height_);
    f->addRow(tr("范围尺寸:"), spn_extent_);
    auto* btn_gen = new QPushButton(tr("生成地形"));
    btn_gen->setMinimumHeight(32);
    connect(btn_gen, &QPushButton::clicked, this, &SceneDesignerWindow::onGenerateTerrain);
    f->addRow(btn_gen);

    auto* gen_layout = ui::DockBuilder::addSection(v, tr("程序化地形"));
    gen_layout->addLayout(f);

    auto* tpl_hint = new QLabel(tr("流程: 选择地形模板 → 生成地形 → 用笔刷绘制;\n"
                                   "生成/编辑的地形随场景 JSON (terrain_ref) 供仿真装载。"));
    tpl_hint->setStyleSheet("color:#5d6675; font-size:11px;");
    tpl_hint->setWordWrap(true);
    gen_layout->addWidget(tpl_hint);

    v->addStretch(1);
}

// ---------------------------------------------------------------------------
// Dock: 地形笔刷 (8 种笔刷模式 + 参数)
// ---------------------------------------------------------------------------
void SceneDesignerWindow::buildBrushDock()
{
    auto* v = createDock(tr("地形笔刷"), Qt::LeftDockWidgetArea);

    rb_brush_.assign(size_t(BrushMode::Erode) + 1, nullptr);
    rb_brush_[int(BrushMode::None)]    = new QRadioButton(tr("关闭"));
    rb_brush_[int(BrushMode::Raise)]   = new QRadioButton(tr("抬升"));
    rb_brush_[int(BrushMode::Lower)]   = new QRadioButton(tr("下挖"));
    rb_brush_[int(BrushMode::Smooth)]  = new QRadioButton(tr("平滑"));
    rb_brush_[int(BrushMode::Flatten)] = new QRadioButton(tr("整平"));
    rb_brush_[int(BrushMode::Noise)]   = new QRadioButton(tr("噪点"));
    rb_brush_[int(BrushMode::Plateau)] = new QRadioButton(tr("台地"));
    rb_brush_[int(BrushMode::Erode)]   = new QRadioButton(tr("侵蚀"));
    rb_brush_[int(BrushMode::None)]->setChecked(true);

    auto* b_lay = ui::DockBuilder::addSection(v, tr("笔刷模式 (在场景中拖拽)"));
    auto* row_btns1 = new QHBoxLayout;
    auto* row_btns2 = new QHBoxLayout;
    int rb_i = 0;
    for (auto* rb : rb_brush_) {
        connect(rb, &QRadioButton::toggled, this,
                [this](bool) { onBrushModeChanged(); });
        (rb_i < 4 ? row_btns1 : row_btns2)->addWidget(rb);
        ++rb_i;
    }
    b_lay->addLayout(row_btns1);
    b_lay->addLayout(row_btns2);

    sld_brush_radius_ = new QSlider(Qt::Horizontal);
    sld_brush_radius_->setRange(5, 150);
    sld_brush_radius_->setValue(25);
    connect(sld_brush_radius_, &QSlider::valueChanged,
            this, &SceneDesignerWindow::onBrushParamsChanged);
    sld_brush_strength_ = new QSlider(Qt::Horizontal);
    sld_brush_strength_->setRange(1, 100);
    sld_brush_strength_->setValue(25);
    connect(sld_brush_strength_, &QSlider::valueChanged,
            this, &SceneDesignerWindow::onBrushParamsChanged);
    spn_flatten_target_ = new QDoubleSpinBox;
    spn_flatten_target_->setRange(-50.0, 500.0);
    spn_flatten_target_->setValue(8.0);
    spn_flatten_target_->setSuffix(" m");
    connect(spn_flatten_target_, &QDoubleSpinBox::valueChanged,
            this, &SceneDesignerWindow::onBrushParamsChanged);

    auto* f_brush = new QFormLayout;
    lbl_brush_radius_ = new QLabel;
    lbl_brush_radius_->setStyleSheet("color:#7fd0ff; font-family:monospace;");
    lbl_brush_strength_ = new QLabel;
    lbl_brush_strength_->setStyleSheet("color:#7fd0ff; font-family:monospace;");
    auto* row_radius = new QHBoxLayout;
    row_radius->addWidget(sld_brush_radius_, 1);
    row_radius->addWidget(lbl_brush_radius_);
    auto* row_strength = new QHBoxLayout;
    row_strength->addWidget(sld_brush_strength_, 1);
    row_strength->addWidget(lbl_brush_strength_);
    f_brush->addRow(tr("笔刷半径:"), row_radius);
    f_brush->addRow(tr("强度:"), row_strength);
    f_brush->addRow(tr("整平目标高:"), spn_flatten_target_);
    b_lay->addLayout(f_brush);
    onBrushParamsChanged();   // init value labels

    auto* hint = new QLabel(tr("提示: 按住左键在地形上拖拽即可编辑 (与放置模式互斥)\n"
                               "中键拖拽旋转 / Shift+中键平移 / 滚轮缩放\n"
                               "Ctrl+Z 撤销 / Ctrl+Shift+Z 重做"));
    hint->setStyleSheet("color:#5d6675; font-size:11px;");
    hint->setWordWrap(true);
    b_lay->addWidget(hint);

    v->addStretch(1);
}

// ---------------------------------------------------------------------------
// Dock: 水面系统 (水面类型 + 水面高度)
// ---------------------------------------------------------------------------
void SceneDesignerWindow::buildWaterDock()
{
    auto* v = createDock(tr("水面系统"), Qt::LeftDockWidgetArea);

    auto* f = new QFormLayout;
    cmb_water_ = new QComboBox;
    cmb_water_->addItems({"无水面", "海洋 Ocean", "湖泊 Lake",
                          "河流 River", "池塘 Pond"});
    cmb_water_->setCurrentIndex(int(WaterType::Lake));
    connect(cmb_water_, &QComboBox::currentIndexChanged,
            this, &SceneDesignerWindow::onApplyWater);
    sld_water_level_ = new QSlider(Qt::Horizontal);
    sld_water_level_->setRange(-50, 800);
    sld_water_level_->setValue(8);
    connect(sld_water_level_, &QSlider::valueChanged,
            this, &SceneDesignerWindow::onWaterLevelChanged);
    f->addRow(tr("水面类型:"), cmb_water_);
    f->addRow(tr("水面高度:"), sld_water_level_);

    auto* w_layout = ui::DockBuilder::addSection(v, tr("水面参数"));
    w_layout->addLayout(f);

    v->addStretch(1);
}

// ---------------------------------------------------------------------------
// Dock: 地形分析 (分析模式 + 两点工具 + 高程统计 + 剖面曲线)
// ---------------------------------------------------------------------------
void SceneDesignerWindow::buildAnalysisDock()
{
    auto* v = createDock(tr("地形分析"), Qt::RightDockWidgetArea);

    // ---- 分析模式 ----
    rb_analysis_.assign(4, nullptr);
    rb_analysis_[int(AnalysisMode::Normal)]   = new QRadioButton(tr("正常"));
    rb_analysis_[int(AnalysisMode::Slope)]    = new QRadioButton(tr("坡度"));
    rb_analysis_[int(AnalysisMode::Aspect)]   = new QRadioButton(tr("坡向"));
    rb_analysis_[int(AnalysisMode::Material)] = new QRadioButton(tr("材质"));
    rb_analysis_[int(AnalysisMode::Normal)]->setChecked(true);

    auto* a_lay = ui::DockBuilder::addSection(v, tr("分析模式"));
    auto* row_modes1 = new QHBoxLayout;
    auto* row_modes2 = new QHBoxLayout;
    for (size_t m = 0; m < rb_analysis_.size(); ++m) {
        connect(rb_analysis_[m], &QRadioButton::toggled, this,
                [this](bool) { onAnalysisModeChanged(); });
        (m < 2 ? row_modes1 : row_modes2)->addWidget(rb_analysis_[m]);
    }
    a_lay->addLayout(row_modes1);
    a_lay->addLayout(row_modes2);

    // ---- 两点分析工具 ----
    auto* t_lay = ui::DockBuilder::addSection(v, tr("两点分析工具"));
    btn_pick_los_      = new QPushButton(tr("通视分析 (两点)"));
    btn_pick_profile_  = new QPushButton(tr("剖面分析 (两点)"));
    btn_pick_distance_ = new QPushButton(tr("距离测量 (两点)"));
    for (auto* b : {btn_pick_los_, btn_pick_profile_, btn_pick_distance_}) {
        b->setCheckable(true);
        t_lay->addWidget(b);
    }
    connect(btn_pick_los_, &QPushButton::toggled, this,
            [this](bool) { onPickToolChanged(); });
    connect(btn_pick_profile_, &QPushButton::toggled, this,
            [this](bool) { onPickToolChanged(); });
    connect(btn_pick_distance_, &QPushButton::toggled, this,
            [this](bool) { onPickToolChanged(); });

    // ---- 高程统计 ----
    lbl_elev_stats_ = new QLabel(tr("最高 - | 最低 - | 平均 -"));
    lbl_elev_stats_->setStyleSheet("color:#7fd0ff; font-family:monospace;");
    v->addWidget(lbl_elev_stats_);

    // ---- 分析结果 ----
    lbl_analysis_result_ = new QLabel(tr("选择模式或激活工具后在视口中点击"));
    lbl_analysis_result_->setWordWrap(true);
    v->addWidget(lbl_analysis_result_);

    // ---- 剖面曲线小部件 ----
    profile_view_ = new TerrainProfileWidget;
    profile_view_->setMinimumHeight(170);
    profile_view_->setVisible(false);
    v->addWidget(profile_view_);

    auto* hint = new QLabel(tr("提示: 激活工具后在 3D 视口点击两次完成分析;\n"
                               "坡度/坡向/材质模式以覆盖层着色地形。"));
    hint->setStyleSheet("color:#5d6675; font-size:11px;");
    hint->setWordWrap(true);
    v->addWidget(hint);

    v->addStretch(1);
}

// ---------------------------------------------------------------------------
// Dock: 地形文件操作 (保存/加载工程 + 导出/导入高度图 + rocky 地形包)
// ---------------------------------------------------------------------------
void SceneDesignerWindow::buildTerrainIODock()
{
    auto* v = createDock(tr("地形文件"), Qt::RightDockWidgetArea);

    auto* io_lay = ui::DockBuilder::addSection(v, tr("地形工程"));
    auto* btn_save_proj = new QPushButton(tr("保存地形工程...  (Ctrl+Shift+T)"));
    auto* btn_open_proj = new QPushButton(tr("打开地形工程...  (Ctrl+Shift+O)"));
    connect(btn_save_proj, &QPushButton::clicked, this, &SceneDesignerWindow::onSaveTerrainProject);
    connect(btn_open_proj, &QPushButton::clicked, this, &SceneDesignerWindow::onLoadTerrainProject);
    io_lay->addWidget(btn_save_proj);
    io_lay->addWidget(btn_open_proj);

    auto* ter_lay = ui::DockBuilder::addSection(v, tr("高度图"));
    auto* row_ter = new QHBoxLayout;
    auto* btn_save_ter = new QPushButton(tr("导出高度图"));
    auto* btn_load_ter = new QPushButton(tr("导入高度图"));
    connect(btn_save_ter, &QPushButton::clicked, this, &SceneDesignerWindow::onSaveHeightmap);
    connect(btn_load_ter, &QPushButton::clicked, this, &SceneDesignerWindow::onLoadHeightmap);
    row_ter->addWidget(btn_save_ter);
    row_ter->addWidget(btn_load_ter);
    ter_lay->addLayout(row_ter);

    // rocky 精细地形地理包 (GeoTIFF + .map.json), 供场景/仿真加载。
    auto* geo_lay = ui::DockBuilder::addSection(v, tr("rocky 精细地形"));
    auto* btn_export_geo = new QPushButton(tr("导出 rocky 地形包..."));
    connect(btn_export_geo, &QPushButton::clicked, this, &SceneDesignerWindow::onExportGeoPackage);
    geo_lay->addWidget(btn_export_geo);

    auto* io_hint = new QLabel(tr("工程 (.vsgterrain) 含地形/水面/模型布局;\n"
                                   "高度图 (.ter) 仅含高度场;\n"
                                   "rocky 地形包导出地理配准 GeoTIFF + .map.json,\n"
                                   "可加载为粗地球之上的精细地形。"));
    io_hint->setStyleSheet("color:#5d6675; font-size:11px;");
    io_hint->setWordWrap(true);
    v->addWidget(io_hint);

    v->addStretch(1);
}

// ---------------------------------------------------------------------------
// Terrain generation / water / brush (全部经 terrain_facade_ 下发)
// ---------------------------------------------------------------------------
void SceneDesignerWindow::onGenerateTerrain()
{
    if(!topic_path_.isEmpty()){statusBar()->showMessage(tr("当前专题使用已保存的体素地形/海洋，请通过原体素编辑链修改"),4000);return;}
    const int ti = cmb_terrain_->currentIndex();
    const int wi = cmb_water_->currentIndex();
    const bool composite = (ti == int(TerrainType::Ocean) ||
                            ti == int(TerrainType::Swamp));
    const int water = composite ? -1 : (wi > 0 ? wi : -1);

    terrain_facade_->generateTemplate(ti, cmb_grid_->currentText().toUInt(),
                                      spn_extent_->value(), spn_height_->value(),
                                      uint32_t(spn_seed_->value()), water);
    if (viewport_) viewport_->markActivity();
    statusBar()->showMessage(tr("地形已生成"), 3000);
}

void SceneDesignerWindow::onApplyWater(int idx)
{
    if(!topic_path_.isEmpty()){statusBar()->showMessage(tr("当前专题使用已保存的体素地形/海洋，请通过原体素编辑链修改"),4000);return;}
    terrain_facade_->applyWater(idx);
    updateTerrainStatus();
}

void SceneDesignerWindow::onWaterLevelChanged()
{
    if(!topic_path_.isEmpty()){statusBar()->showMessage(tr("当前专题使用已保存的体素地形/海洋，请通过原体素编辑链修改"),4000);return;}
    if (!sld_water_level_) return;
    terrain_facade_->setWaterLevel(sld_water_level_->value());
    updateTerrainStatus();
}

BrushMode SceneDesignerWindow::currentBrushMode() const
{
    for (size_t m = 1; m < rb_brush_.size(); ++m)
        if (rb_brush_[m] && rb_brush_[m]->isChecked()) return BrushMode(m);
    return BrushMode::None;
}

void SceneDesignerWindow::onBrushModeChanged()
{
    const bool active = currentBrushMode() != BrushMode::None;
    pushBrushParams();
    // 与放置模式互斥: 笔刷激活时关闭点击放置 (两者都占用视口左键)。
    if (active && chk_place_ && chk_place_->isChecked())
        chk_place_->setChecked(false);
    if (active) statusBar()->showMessage(
        tr("笔刷已启用: 在 3D 视口中按住左键拖拽编辑地形 (关闭相机旋转)"), 4000);
}

void SceneDesignerWindow::onBrushParamsChanged()
{
    if (lbl_brush_radius_)
        lbl_brush_radius_->setText(QString("%1 m").arg(sld_brush_radius_->value()));
    if (lbl_brush_strength_)
        lbl_brush_strength_->setText(QString("%1 %").arg(sld_brush_strength_->value()));
    pushBrushParams();
}

void SceneDesignerWindow::pushBrushParams()
{
    if (!terrain_facade_ || !sld_brush_radius_) return;
    terrain_facade_->setBrush(int(currentBrushMode()), sld_brush_radius_->value(),
                              sld_brush_strength_->value(), spn_flatten_target_->value());
}

void SceneDesignerWindow::updateTerrainStatus()
{
    auto* t = terrain_facade_ ? terrain_facade_->terrain() : nullptr;
    if (!t) return;
    if (sb_terrain_) {
        double lo = 0, hi = 0;
        t->heightRange(lo, hi);
        sb_terrain_->setText(QString("地形 %1x%1 | %2 m | 高度 %3~%4 m")
                                 .arg(t->grid())
                                 .arg(t->extent(), 0, 'f', 0)
                                 .arg(lo, 0, 'f', 1)
                                 .arg(hi, 0, 'f', 1));
    }
    if (sb_water_ && cmb_water_) {
        sb_water_->setText(QString("水面: %1 @ %2 m")
                               .arg(cmb_water_->currentText())
                               .arg(t->waterLevel(), 0, 'f', 1));
    }
}

// ---------------------------------------------------------------------------
// Terrain project persistence (地形工程/高度图/rocky 地形包)
// ---------------------------------------------------------------------------
void SceneDesignerWindow::onSaveTerrainProject()
{
    auto* t = terrain_facade_ ? terrain_facade_->terrain() : nullptr;
    if (!t) {
        statusBar()->showMessage(tr("尚无地形可保存 (先生成或加载地形)"), 4000);
        return;
    }

    vsgraysim::Workspace ws(resolveWorkspaceRoot().toStdString());
    ws.ensureDirs();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("保存地形工程"),
        QString::fromStdString(ws.terrainDir()) + "/terrain_project.vsgterrain",
        tr("地形工程 (*.vsgterrain)"));
    if (path.isEmpty()) return;

    const nlohmann::json models = nlohmann::json::array();
    if (!terrain_facade_->saveProject(path, models)) {
        QMessageBox::warning(this, tr("保存失败"),
                             tr("工程或高度图写入失败:\n%1").arg(path));
        return;
    }

    const QFileInfo fi(path);
    const QString prev_path = QString::fromStdString(
        vsgraysim::Workspace::previewPathFor(path.toStdString()));
    const QImage prev = terrainPreviewFromHeights(t->heightsData(),
                                                  t->grid(),
                                                  t->waterLevel());
    if (prev.save(prev_path)) {
        spdlog::info("SceneDesignerWindow: terrain preview saved '{}'",
                     prev_path.toStdString());
    } else {
        spdlog::warn("SceneDesignerWindow: terrain preview save failed '{}'",
                     prev_path.toStdString());
    }

    statusBar()->showMessage(tr("地形工程已保存: %1").arg(fi.fileName()), 4000);
}

void SceneDesignerWindow::onExportGeoPackage()
{
    auto* t = terrain_facade_ ? terrain_facade_->terrain() : nullptr;
    if (!t) {
        statusBar()->showMessage(tr("尚无地形可导出 (先生成或加载地形)"), 4000);
        return;
    }

    vsgraysim::Workspace ws(resolveWorkspaceRoot().toStdString());
    ws.ensureDirs();
    const QString mapPath = QFileDialog::getSaveFileName(
        this, tr("导出 rocky 精细地形包"),
        QString::fromStdString(ws.terrainDir()) + "/detail_terrain.map.json",
        tr("rocky 地图 (*.map.json)"));
    if (mapPath.isEmpty()) return;

    const QFileInfo fi(mapPath);
    const QString outDir = fi.absolutePath();
    QString base = fi.completeBaseName();       // "detail.map.json" -> "detail.map"
    if (base.endsWith(".map", Qt::CaseInsensitive)) base.chop(4);
    if (base.isEmpty()) base = QStringLiteral("detail_terrain");

    const QString result = terrain_facade_->exportGeoPackage(outDir, base);
    if (result.isEmpty()) {
        QMessageBox::warning(this, tr("导出失败"),
            tr("rocky 地形包导出失败 (未启用 rocky, 或 GeoTIFF/GDAL 写入出错)。\n"
               "请查看日志。"));
        return;
    }
    statusBar()->showMessage(tr("rocky 地形包已导出: %1").arg(result), 6000);
    QMessageBox::information(
        this, tr("导出成功"),
        tr("精细地形包已生成:\n%1\n\n"
           "包含地理配准 GeoTIFF (高程+影像) 与 rocky .map.json,\n"
           "场景/仿真软件加载该 .map.json 即得粗地球+精细地形。")
            .arg(result));
}

void SceneDesignerWindow::onLoadTerrainProject()
{
    if(!topic_path_.isEmpty()){statusBar()->showMessage(tr("当前专题使用已保存的体素地形/海洋，请通过原体素编辑链修改"),4000);return;}
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开地形工程"), QString(), tr("地形工程 (*.vsgterrain)"));
    if (path.isEmpty()) return;
    loadTerrainProjectFromPath(path);
}

bool SceneDesignerWindow::loadTerrainProjectFromPath(const QString& path)
{
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("打开失败"),
                             tr("无法读取工程文件:\n%1").arg(path));
        return false;
    }
    nlohmann::json models;
    if (!terrain_facade_->loadProject(path, &models)) {
        QMessageBox::warning(this, tr("打开失败"),
                             tr("不是有效的地形工程文件或高度图加载失败:\n%1")
                                 .arg(path));
        return false;
    }

    const QString file_name = QFileInfo(path).fileName();
    terrain_facade_->whenReady([this, file_name] {
        syncUiFromTerrain();
        spdlog::info("SceneDesignerWindow: terrain project loaded '{}'",
                     file_name.toStdString());
        statusBar()->showMessage(tr("地形工程已加载: %1").arg(file_name), 4000);
    });
    return true;
}

// ---------------------------------------------------------------------------
// Heightmap I/O
// ---------------------------------------------------------------------------
void SceneDesignerWindow::onSaveHeightmap()
{
    auto* t = terrain_facade_ ? terrain_facade_->terrain() : nullptr;
    if (!t) { statusBar()->showMessage(tr("尚无地形"), 3000); return; }
    auto path = QFileDialog::getSaveFileName(this, tr("保存高度图"), "terrain.ter",
                                             tr("地形高度图 (*.ter)"));
    if (path.isEmpty()) return;
    if (!terrain_facade_->saveHeightmap(path))
        QMessageBox::warning(this, tr("保存失败"),
                             tr("高度图写入失败:\n%1").arg(path));
}

void SceneDesignerWindow::onLoadHeightmap()
{
    auto path = QFileDialog::getOpenFileName(this, tr("导入高度图"), QString(),
                                             tr("地形高度图 (*.ter)"));
    if (path.isEmpty()) return;
    if (!terrain_facade_->loadHeightmap(path)) {
        QMessageBox::warning(this, tr("导入失败"),
                             tr("高度图加载失败:\n%1").arg(path));
        return;
    }
    syncUiFromTerrain();
    statusBar()->showMessage(tr("高度图已加载"), 3000);
}

// ---------------------------------------------------------------------------
// Edit history
// ---------------------------------------------------------------------------
void SceneDesignerWindow::updateUndoActions()
{
    if (act_undo_) act_undo_->setEnabled(topic_path_.isEmpty() && terrain_facade_ && terrain_facade_->canUndo());
    if (act_redo_) act_redo_->setEnabled(topic_path_.isEmpty() && terrain_facade_ && terrain_facade_->canRedo());
}

void SceneDesignerWindow::onUndo()
{
    if (!topic_path_.isEmpty() || !terrain_facade_ || !terrain_facade_->canUndo()) return;
    terrain_facade_->undo();
    statusBar()->showMessage(tr("已撤销"), 2000);
}

void SceneDesignerWindow::onRedo()
{
    if (!topic_path_.isEmpty() || !terrain_facade_ || !terrain_facade_->canRedo()) return;
    terrain_facade_->redo();
    statusBar()->showMessage(tr("已重做"), 2000);
}

// ---------------------------------------------------------------------------
// Terrain analysis
// ---------------------------------------------------------------------------
SceneDesignerWindow::AnalysisMode SceneDesignerWindow::currentAnalysisMode() const
{
    for (size_t m = 1; m < rb_analysis_.size(); ++m)
        if (rb_analysis_[m] && rb_analysis_[m]->isChecked()) return AnalysisMode(m);
    return AnalysisMode::Normal;
}

void SceneDesignerWindow::onAnalysisModeChanged()
{
    if (terrain_facade_) terrain_facade_->setAnalysisMode(int(currentAnalysisMode()));
}

void SceneDesignerWindow::onPickToolChanged()
{
    auto* btn = qobject_cast<QPushButton*>(sender());
    const bool on = btn && btn->isChecked();

    auto uncheck_others = [&](QPushButton* keep) {
        for (auto* b : {btn_pick_los_, btn_pick_profile_, btn_pick_distance_}) {
            if (b && b != keep && b->isChecked()) {
                QSignalBlocker blk(b);
                b->setChecked(false);
            }
        }
    };

    if (on && btn) {
        uncheck_others(btn);
        pick_tool_ = (btn == btn_pick_los_) ? AnalysisPickTool::Los
                   : (btn == btn_pick_profile_) ? AnalysisPickTool::Profile
                   : AnalysisPickTool::Distance;
        if (terrain_facade_) terrain_facade_->setPickTool(int(pick_tool_));
        statusBar()->showMessage(
            tr("拾取模式: 在 3D 视口中依次点击两点 (中键仍可旋转视角)"), 4000);
    } else if (btn && !btn_pick_los_->isChecked() &&
               !btn_pick_profile_->isChecked() &&
               !btn_pick_distance_->isChecked()) {
        pick_tool_ = AnalysisPickTool::None;
        if (terrain_facade_) terrain_facade_->setPickTool(0);
    }
}

void SceneDesignerWindow::onProfileReady(const std::vector<double>& dists,
                                         const std::vector<double>& heights,
                                         double water_level)
{
    if (!profile_view_) return;
    std::vector<TerrainAnalyzer::ProfileSample> samples;
    samples.reserve(std::min(dists.size(), heights.size()));
    for (size_t i = 0; i < dists.size() && i < heights.size(); ++i)
        samples.push_back({dists[i], heights[i]});
    profile_view_->setProfile(samples, water_level);
    profile_view_->setVisible(true);
}

// ---------------------------------------------------------------------------
// UI <-> TerrainSystem synchronisation
// ---------------------------------------------------------------------------
void SceneDesignerWindow::syncUiFromTerrain()
{
    auto* t = terrain_facade_ ? terrain_facade_->terrain() : nullptr;
    if (!t) return;
    {
        QSignalBlocker b(cmb_terrain_);
        cmb_terrain_->setCurrentIndex(
            std::clamp(int(t->terrainType()), 0, cmb_terrain_->count() - 1));
    }
    {
        QSignalBlocker b(cmb_water_);
        cmb_water_->setCurrentIndex(
            std::clamp(int(t->waterType()), 0, cmb_water_->count() - 1));
    }
    {
        QSignalBlocker b(cmb_grid_);
        const QString g = QString::number(t->grid());
        int gi = cmb_grid_->findText(g);
        if (gi < 0) { cmb_grid_->addItem(g); gi = cmb_grid_->findText(g); }
        cmb_grid_->setCurrentIndex(gi);
    }
    { QSignalBlocker b(spn_seed_);
      spn_seed_->setValue(std::clamp(int(t->seed()),
                                     spn_seed_->minimum(), spn_seed_->maximum())); }
    { QSignalBlocker b(spn_height_); spn_height_->setValue(t->heightScale()); }
    { QSignalBlocker b(spn_extent_); spn_extent_->setValue(t->extent()); }
    {
        QSignalBlocker b(sld_water_level_);
        sld_water_level_->setValue(
            std::clamp(int(t->waterLevel()),
                       sld_water_level_->minimum(), sld_water_level_->maximum()));
    }
    updateTerrainStatus();
}

// ---------------------------------------------------------------------------
// TerrainProfileWidget (剖面曲线, 自原地形编辑软件迁入)
// ---------------------------------------------------------------------------
TerrainProfileWidget::TerrainProfileWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(260, 150);
}

void TerrainProfileWidget::setProfile(
    const std::vector<TerrainAnalyzer::ProfileSample>& samples,
    double water_level)
{
    samples_ = samples;
    water_level_ = water_level;
    update();
}

void TerrainProfileWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(16, 19, 26));

    const int ml = 46, mr = 10, mt = 12, mb = 24;
    const QRect plot(ml, mt, width() - ml - mr, height() - mt - mb);
    if (plot.width() <= 20 || plot.height() <= 20) return;

    if (samples_.size() < 2) {
        p.setPen(QColor(90, 102, 120));
        p.drawText(rect(), Qt::AlignCenter, tr("无剖面数据"));
        return;
    }

    double dmax = samples_.back().dist;
    double hmin = samples_.front().height, hmax = hmin;
    for (const auto& s : samples_) {
        hmin = std::min(hmin, s.height);
        hmax = std::max(hmax, s.height);
    }
    hmin = std::min(hmin, water_level_);
    hmax = std::max(hmax, water_level_);
    if (dmax < 1e-6) dmax = 1.0;
    if (hmax - hmin < 1e-6) { hmax = hmin + 1.0; }
    const double hspan = hmax - hmin;
    hmin -= hspan * 0.08;
    hmax += hspan * 0.08;

    auto X = [&](double d) {
        return double(plot.left()) + d / dmax * double(plot.width());
    };
    auto Y = [&](double h) {
        return double(plot.bottom()) -
               (h - hmin) / (hmax - hmin) * double(plot.height());
    };

    p.setFont(font());
    p.setPen(QColor(38, 43, 54));
    p.drawLine(plot.left(), plot.top(), plot.left(), plot.bottom());
    p.drawLine(plot.left(), plot.bottom(), plot.right(), plot.bottom());
    p.setPen(QColor(110, 122, 142));
    for (int g = 0; g <= 3; ++g) {
        const double t = double(g) / 3.0;
        const double h = hmin + t * (hmax - hmin);
        const int y = int(std::lround(Y(h)));
        if (g > 0 && g < 3) {
            p.setPen(QColor(38, 43, 54));
            p.drawLine(plot.left(), y, plot.right(), y);
        }
        p.setPen(QColor(110, 122, 142));
        p.drawText(QRect(0, y - 8, ml - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(h, 'f', 0));
    }
    p.drawText(QRect(plot.left(), plot.bottom() + 4,
                     plot.width() / 2, 18), Qt::AlignLeft | Qt::AlignVCenter,
               tr("0 m"));
    p.drawText(QRect(plot.right() - plot.width() / 2, plot.bottom() + 4,
                     plot.width() / 2, 18), Qt::AlignRight | Qt::AlignVCenter,
               QString("%1 m").arg(dmax, 0, 'f', 0));

    if (water_level_ > hmin && water_level_ < hmax) {
        const int wy = int(std::lround(Y(water_level_)));
        p.setPen(QPen(QColor(64, 130, 220, 180), 1, Qt::DashLine));
        p.drawLine(plot.left(), wy, plot.right(), wy);
    }

    QPolygonF poly;
    poly.reserve(int(samples_.size()));
    for (const auto& s : samples_)
        poly << QPointF(X(s.dist), Y(s.height));
    QPolygonF fill = poly;
    fill << QPointF(X(dmax), double(plot.bottom()));
    fill << QPointF(X(0.0), double(plot.bottom()));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(58, 106, 74, 150));
    p.drawPolygon(fill);
    p.setPen(QPen(QColor(127, 208, 255), 2));
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(poly);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 208, 84));
    p.drawEllipse(poly.front(), 3.0, 3.0);
    p.drawEllipse(poly.back(), 3.0, 3.0);

    const double dh = samples_.back().height - samples_.front().height;
    p.setPen(QColor(150, 160, 178));
    p.drawText(QRect(plot.left(), 0, plot.width(), mt),
               Qt::AlignLeft | Qt::AlignVCenter,
               tr("剖面 · 长 %1 m · 高差 %2 m")
                   .arg(dmax, 0, 'f', 0)
                   .arg(dh, 0, 'f', 1));
}

}} // namespace vsgraysim::gui
