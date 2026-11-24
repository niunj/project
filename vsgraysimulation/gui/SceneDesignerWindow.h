#pragma once
/// @file SceneDesignerWindow.h
/// @brief Qt6 场景设计器主窗口 —— 多分类 Dock 设计 (含地形编辑能力)。
///
/// Layout
/// ------
///   +---------------------------------------------------------------+
///   | MenuBar   File | Edit | View | Tools | Help                   |
///   | ToolBar   [保存场景] [打开场景] [Undo] [Redo] [⌂ Camera]       |
///   +------+------+------------------------------+------------------+
///   |场景树 |地形  |                              | 模型库卡片       |
///   |Dock   |生成  |      Viewport3DView          | Dock (拖动添加)  |
///   |(默认) |地形  |  (scene_designer preset +     |------------------|
///   |地形笔 |笔刷  |   terrain + brush + trajectory| 属性 Dock        |
///   |刷水面 |水面  |   + fov + analysis)          | (变换/传感器/    |
///   |       |      |                              |  电磁/航迹/绑定) |
///   |       |      |                              | 地形分析/文件IO  |
///   +------+------+------------------------------+------------------+
///   | StatusBar: 实体 | 传感器 | 地形 | 水面 | 帧 | FPS               |
///   +---------------------------------------------------------------+
///
/// Dock 分布:
///   左: 场景树(默认) / 地形生成 / 地形笔刷 / 水面系统
///   右: 模型库卡片(拖动→场景添加) / 属性 / 地形分析 / 地形文件操作
///
/// 实体添加通过模型库卡片「拖动到视口」完成; 选中实体后, 右侧「属性」
/// Dock 以 Tab 分组展示并编辑 变换/传感器/电磁光学/航迹/绑定配置。
///
/// 共用基础 UI 模块: gui/base/ (BaseWindow / DockBuilder / WidgetFactory)
#ifndef VSGRAYSIM_GUI_SCENE_DESIGNER_WINDOW_H
#define VSGRAYSIM_GUI_SCENE_DESIGNER_WINDOW_H

#include "base/BaseWindow.h"
#include "TerrainAnalyzer.h"   // TerrainProfileWidget (剖面曲线) 数据类型
#include "vsgraysim/layer1_scene/TerrainSystem.h"   // BrushMode / TerrainType / WaterType

#include <nlohmann/json.hpp>
#include "vsgraysim/core/ScenarioClockConfig.h"

#include <QLabel>
#include <string>
#include <vector>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QAction;
class QLineEdit;
class QListWidget;
class QPoint;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace vsgraysim { namespace viewport3d {
class Viewport3DView;
class SceneDesignFacade;
class TerrainEditFacade;
}}

namespace vsgraysim { namespace gui {

namespace display { class RelationshipGraphView; }
namespace base { class PropertyTreeWidget; }

enum class Faction : int { Red = 0, Blue = 1, Neutral = 2 };

struct WaypointDef {
    double time = 0.0;
    double pos[3] = {0.0, 0.0, 0.0};
    double speed = 0.0;  // 途经速度 (m/s), 0=沿用上一段
};

struct SensorDef {
    int     type = 3;
    QString band = "X";
    double  power = 1000.0;
    double  wavelength = 0.03;
    double  beam_width = 0.05;
    double  pulse_rate = 1000.0;
    double  duty_cycle = 0.05;
    double  fov_h = 60.0;
    double  fov_v = 30.0;
    double  offset_y = 2.0;
    double  az_deg = 0.0;
    double  el_deg = 0.0;
    double  range_m = 8000.0;
    QString platform;
    QString role = "search";   ///< 传感器角色: search(搜索) / guidance(制导)
    // 信号处理参数 (落盘为 signal_params 段, 仿真侧逐装备覆盖全局默认值)
    int    range_gates = 1024;      ///< 雷达距离门数
    int    doppler_bins = 256;      ///< 雷达多普勒门数
    double integration_time = 0.01; ///< 红外积分时间 (s)
    double frame_interval = 0.0;    ///< 独立帧周期 (s), 0 = 跟随主时钟 (红外帧序用)

    // ---- §14.3 新 schema (frame/profile/stages, 与 SensorApi.h SensorProfile 同名映射):
    //      旧字段 (type/band/tx_params/aperture/frame_interval_s) 读取时推导补齐,
    //      写回时新旧并存; 参数优先级 Param 消息 > 场景 JSON > 硬编码默认。 ----
    QString frame_kind;             ///< frame.kind: VIS|SWIR|MWIR|LWIR|RADAR (空=未映射, 如 laser)
    double  lambda_min = 0.0;       ///< frame.lambda_min (m), 0 = 按 kind 推导
    double  lambda_max = 0.0;       ///< frame.lambda_max (m), 0 = 按 kind 推导
    int     res_w = 640;            ///< frame.resolution.width (px)
    int     res_h = 512;            ///< frame.resolution.height (px)
    double  fov_deg = 0.0;          ///< frame.fov_deg (°, 水平视场), 0 = 沿用 aperture.fov_horizontal
    double  rate_hz = 0.0;          ///< frame.rate_hz (Hz), 0 = 按 kind 推导 (雷达=PRF, 成像=1/integration)
    // profile.optics (P1→P2)
    double  f_number      = 4.0;    ///< F# (与 well_ke 标定一致, F/4 下 LWIR 地面 raw≈0.55)
    double  transmission  = 0.85;   ///< 光学透过率 [0,1]
    double  psf_sigma_px  = 0.9;    ///< 高斯 PSF σ (px)
    double  vignetting    = 0.15;   ///< 渐晕 cos⁴ 系数 [0,1]
    // profile.detector (P2→P3)
    double  pixel_pitch_um = 15.0;  ///< 像元间距 (μm)
    double  qe             = 0.7;   ///< 量子效率 [0,1]
    double  integration_ms = 8.0;   ///< 积分时间 (ms)
    double  netd_mk        = 50.0;  ///< NETD (mK)
    double  well_ke        = 20000.0; ///< 满阱 (ke-)
    double  read_noise_e   = 80.0;  ///< 读出噪声 (e-)
    int     bits           = 14;    ///< ADC 位数
    // profile.electronics (P3→P4)
    double  agc_low_pct  = 0.002;   ///< AGC 下限分位
    double  agc_high_pct = 0.998;   ///< AGC 上限分位
    double  gamma        = 1.0;     ///< Gamma (1.0=线性)
    // 产品发布级 (逗号分隔编辑; stages: scene|pupil|focal|raw|image,
    //             radar_stages_publish: echo|range_comp|rd_map|complex_image|product)
    QString stages = QStringLiteral("scene,pupil,focal,raw,image");
    QString radar_stages_publish = QStringLiteral("echo,range_comp,rd_map,complex_image,product");
    // ---- §7.2 扫描机构 (B7, scan 段): GimbalModel 消费; 缺省 fixed=现状
    //      (aperture.direction 固定指向)。仿真控制侧状态机 SEARCH→TRACK 迁移由
    //      Param 消息驱动 (场景 JSON 只给初始值)。 ----
    QString scan_mode = QStringLiteral("fixed"); ///< fixed|raster|sector|circular|rosette|conical|spiral|slaved
    double  scan_az_center = 0.0;   ///< 扫描中心方位 (°, 机体系)
    double  scan_el_center = 0.0;   ///< 扫描中心俯仰 (°)
    double  scan_az_extent = 0.0;   ///< 方位扫描范围 (°; conical 为锥角, rosette/spiral 为幅度)
    double  scan_el_extent = 0.0;   ///< 俯仰扫描范围 (°; raster 行铺满)
    double  scan_az_rate   = 0.0;   ///< 方位速率 (°/s; circular 为 rpm; conical/rosette 为 Hz)
    double  scan_el_rate   = 0.0;   ///< 俯仰速率 (°/s)
    QString scan_target_ref;        ///< slaved 指向目标 (objects[].object_id)
};

struct FaceGroupDef {
    QString group_id = "hull";
    int     face_begin = 0;
    int     face_end   = -1;
    QString brdf_type = "diffuse";
    double  albedo    = 0.25;
    double  roughness = 0.5;
    double  emissivity = 0.9;
    double  base_temp  = 300.15;
    double  dielectric_real = 18.0;
    double  dielectric_imag = 5.0;
    double  sigma0_db       = -15.0;
    double  surface_rough   = 0.4;
};

/// 热部件 (§14.3 thermal.parts: 引擎/尾焰等高温区, 经 face_group 落到 RT 面组)。
struct ThermalPartDef {
    QString name = "engine";     ///< 部件名 (engine/exhaust/...)
    double  temp_k = 450.0;      ///< 部件温度 (K)
    double  emissivity = 0.85;   ///< 发射率 [0,1]
    QString face_group;          ///< 关联 FaceGroupDef.group_id (SceneSource 按名匹配覆盖)
};

struct SceneObject {
    QString id;
    QString mesh_path;
    double  pos[3]   = {0.0, 0.0, 0.0};
    double  yaw_deg  = 0.0;
    double  scale    = 1.0;
    double  mass     = 1000.0;
    Faction faction = Faction::Neutral;
    std::vector<SensorDef> sensors;
    std::vector<FaceGroupDef> face_groups;
    // §14.3 thermal (整体热属性; SceneSource: base_temp_k→fallback, parts 按名覆盖面组)
    double thermal_base_temp_k = 0.0;   ///< 0 = 不落盘 (沿用面组 lwir_params/默认 300K)
    std::vector<ThermalPartDef> thermal_parts;
    QString thermal_states;             ///< 工况温度倍率 CSV "idle:1.0,cruise:1.3,max:1.8" (空=不落盘)
    std::vector<WaypointDef> waypoints;
    // 航迹规划参数 (route_plan)
    int     route_type   = 1;   // platform::RouteType 枚举值 (默认 PATROL)
    bool    route_loop   = true;
    double  route_start_delay = 0.0;
    int     platform_cat = 0;   // platform::PlatformCategory (地面=0/空中=1/水面=2/导弹=3)
    QString airframe_kind = QStringLiteral("aircraft"); ///< aircraft | uav
    nlohmann::json motion_params=nlohmann::json::object(); ///< 空对象 = 按类别生成默认值
    QString motion_authority = QStringLiteral("dynamic");
};

/// 导引/协同绑定关系 (由场景设计器编辑; 与 platform::RelationshipGraph 的关系类型对应)
struct BindingLink {
    QString from_id;    ///< 源 (制导雷达/平台)
    QString relation;   ///< "guides" / "fire_control_of" / "target_of" / "fusion_peer"
    QString to_id;      ///< 目标 (导弹/目标)
};

/// 剖面曲线小部件: 用 QPainter 绘制两点间的地形高程序列 (距离-高程图)。
/// (自原地形编辑软件 MainWindow 迁入)
class TerrainProfileWidget : public QWidget {
public:
    explicit TerrainProfileWidget(QWidget* parent = nullptr);
    void setProfile(const std::vector<TerrainAnalyzer::ProfileSample>& samples,
                    double water_level);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    std::vector<TerrainAnalyzer::ProfileSample> samples_;
    double water_level_ = 0.0;
};

class SceneDesignerWindow : public ui::BaseWindow {
    Q_OBJECT
public:
    explicit SceneDesignerWindow(const QString& terrain_template = {},
                                 const QString& open_scenario = {},
                                 QWidget* parent = nullptr, const QString& topic = {});
    ~SceneDesignerWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onResetCamera();

    // ---- entities ----
    void onRemoveObject();
    void onSceneTreeSelectionChanged();
    void onTransformEdited();
    void onPlaceToggled(bool on);
    /// 模型库卡片拖动到视口: 在拖放点地面位置创建实体。
    void onModelDropped(const QString& path, const QPoint& local_pos);
    /// 视口内点击实体拾取: 同步选中场景树对应实体, 属性 Dock 随之刷新。
    void onEntityPicked(const QString& id);

    // ---- face groups ----
    void onFaceGroupSelected(int idx);
    void onAddFaceGroup();
    void onFaceGroupEdited();

    // ---- thermal parts (§14.3 热部件 + 面组选择器) ----
    void onAddThermalPart();
    void onRemoveThermalPart();
    void onThermalPartSelected(int row);
    void onThermalPartEdited();

    // ---- sensors ----
    void onAddSensor();
    void onSensorSelected(int row);
    void onSensorEdited();

    // ---- waypoints ----
    void onWaypointAdd();
    void onWaypointItemChanged(QTableWidgetItem* item);

    // ---- route planning ----
    void onRouteTypeChanged(int index);
    void onRouteLoopToggled(bool on);
    void onRouteDelayEdited(double v);
    void onPlatformCatChanged(int index);

    // ---- bindings (导引/协同关系) ----
    void onBindingAdd();
    void onBindingRemove();
    void onBindingAddGraph(const QString& from_id, const QString& to_id);
    void onBindingRemoveGraph(const QString& from_id, const QString& to_id);

    // ---- faction ----
    void onFactionEdited(int index);

    // ---- property tree 编辑回写 (集中处理属性树编辑) ----
    void onPropTreeEdited(const QString& groupId, const QString& key,
                          const QString& text);

    // ---- validation ----
    void onValidateScene();

    // ---- scenario io ----
    void onSaveScenario();
    void onSaveScenarioAs();
    void onLaunchSimulation();
    void onLoadScenario();
    void loadScenarioFromFile(const QString& path);
    bool saveScenarioFile(const QString& path) { return saveScenarioToPath(path); }

    // ---- terrain editing (自原地形编辑软件迁入) ----
    void onGenerateTerrain();
    void onApplyWater(int idx);
    void onWaterLevelChanged();
    void onBrushModeChanged();
    void onBrushParamsChanged();
    void onSaveHeightmap();
    void onLoadHeightmap();
    void onSaveTerrainProject();
    void onLoadTerrainProject();
    bool loadTerrainProjectFromPath(const QString& path);
    /// 导出 rocky 精细地形地理包 (GeoTIFF 高程/影像 + .map.json)。
    void onExportGeoPackage();
    void onUndo();
    void onRedo();
    void onAnalysisModeChanged();
    void onPickToolChanged();
    void onProfileReady(const std::vector<double>& dists,
                        const std::vector<double>& heights, double water_level);

    void onAbout();

private:
    // ---- 构建 (Dock 分布: 左侧场景/环境, 右侧模型库/属性) ----
    QString topic_path_;
    nlohmann::json topic_document_;
    bool topic_baseline_pending_=false;
    void buildCentralWidget();
    void buildScenarioInfoDock();     ///< 左: 场景信息 (已并入右侧属性变量面板)
    void buildSceneTreeDock();        ///< 左: 场景树 (默认, 空显示空/非空显示树)
    void buildTerrainGenDock();       ///< 左: 地形生成 (程序化模板+参数+生成)
    void buildBrushDock();            ///< 左: 地形笔刷 (8 种模式+参数)
    void buildWaterDock();            ///< 左: 水面系统 (类型+高度)
    void buildModelLibraryDock();     ///< 右: 模型库卡片 (拖动→场景添加)
    void buildPropertiesDock();       ///< 右: 属性 (统一由 PropertyTreeWidget 显示)
    void buildAnalysisDock();         ///< 右: 地形分析 (模式+两点工具+剖面)
    void buildTerrainIODock();        ///< 右: 地形文件操作 (工程/高度图/地形包)
    // 属性 Dock 内的各分组 Tab (选中实体后可查看+编辑)
    void buildTransformDock();        ///< 属性·变换
    void buildSensorDock();           ///< 属性·传感器
    void buildFaceGroupDock();        ///< 属性·电磁光学
    void buildWaypointDock();         ///< 属性·航迹
    void buildBindingsDock();         ///< 属性·绑定/导引关系
    /// 新建「属性」Tab 页并返回其布局 (供各分组构建器写入控件)。
    QVBoxLayout* beginPropsTab(const QString& title);

    // 覆写 BaseWindow 扩展点
    void buildProductMenuItems(QMenuBar* mb) override;
    void buildProductToolBarItems(QToolBar* tb) override;
    void buildProductDocks() override;

    bool addObjectToScene(const QString& id, const QString& path,
                          const double pos[3], double yaw_deg, double scale);
    void clearObjects();
    void applyTransform(SceneObject& obj);
    void syncTrajectoryToFacade(const SceneObject& obj);
    /// 模型地形场景: 把沉入地形表面下方的实体抬升至表面 (垂直 raycast)。
    void groundClampEntities();
    void refreshSceneTree();
    void selectObjectInTree(const QString& object_id);
    void refreshFaceGroupList();
    void syncFaceGroupEditors();
    /// 刷新电磁光学 Tab 的热部件列表 + 面组选择器 (§14.3)。
    void refreshThermalParts();
    void syncThermalPartEditors();
    void refreshSensorList();
    void refreshWaypointTable();
    void syncEditorsFromSelection();
    void refreshBindingsPanel();
    /// 刷新右侧统一「属性变量」表格 (按选中的节点类型显示不同信息)。
    void refreshPropsTable();
    int  selectedObjectIndex() const;
    int  selectedSensorIndex() const;
    int  selectedFaceGroupIndex() const;
    void updateStatusCounts();
    bool saveScenarioToPath(const QString& path);
    nlohmann::json sceneDocument() const;
    bool confirmUnsavedChanges();
    std::string saved_document_;
    /// W2: 把细粒度大气参数打包成 JSON 实时下发到视口 rocky_env Control 接口。
    void pushAtmosphereToViewport();

    // ---- terrain editing 辅助 (自原地形编辑软件迁入) ----
    enum class AnalysisMode { Normal = 0, Slope, Aspect, Material };
    enum class AnalysisPickTool { None = 0, Los, Profile, Distance };
    AnalysisMode currentAnalysisMode() const;
    BrushMode currentBrushMode() const;
    void pushBrushParams();
    void updateTerrainStatus();
    void syncUiFromTerrain();
    void updateUndoActions();

    // -- 共用 3D 视口 --
    viewport3d::Viewport3DView*   viewport_ = nullptr;
    viewport3d::SceneDesignFacade* facade_  = nullptr;
    viewport3d::TerrainEditFacade* terrain_facade_ = nullptr; ///< 地形编辑门面 (笔刷/生成/分析)

    // -- status bar (扩展) --
    QLabel* sb_objects_ = nullptr;
    QLabel* sb_sensors_ = nullptr;
    QLabel* sb_terrain_ = nullptr;   ///< 地形状态 (网格/尺寸/高度范围)
    QLabel* sb_water_   = nullptr;   ///< 水面状态 (类型/水位)

    // -- terrain edit actions (撤销/重做) --
    QAction* act_undo_ = nullptr;
    QAction* act_redo_ = nullptr;

    // -- terrain generation / water / brush / analysis controls --
    QComboBox*  cmb_terrain_ = nullptr;
    QComboBox*  cmb_grid_    = nullptr;
    QSpinBox*   spn_seed_    = nullptr;
    QDoubleSpinBox* spn_height_ = nullptr;
    QDoubleSpinBox* spn_extent_ = nullptr;
    QComboBox*  cmb_water_   = nullptr;
    QSlider*    sld_water_level_ = nullptr;
    std::vector<QRadioButton*> rb_brush_;
    QSlider*    sld_brush_radius_ = nullptr;
    QSlider*    sld_brush_strength_ = nullptr;
    QLabel*     lbl_brush_radius_ = nullptr;
    QLabel*     lbl_brush_strength_ = nullptr;
    QDoubleSpinBox* spn_flatten_target_ = nullptr;
    std::vector<QRadioButton*> rb_analysis_;
    QPushButton* btn_pick_los_      = nullptr;
    QPushButton* btn_pick_profile_  = nullptr;
    QPushButton* btn_pick_distance_ = nullptr;
    QLabel*      lbl_elev_stats_    = nullptr;
    QLabel*      lbl_analysis_result_ = nullptr;
    TerrainProfileWidget* profile_view_ = nullptr;
    AnalysisPickTool pick_tool_ = AnalysisPickTool::None;

    // -- scenario info --
    QLineEdit* edt_scenario_name_ = nullptr;
    QLineEdit* edt_scenario_desc_ = nullptr;

    // -- entities --
    QTreeWidget* tree_scene_       = nullptr;
    QVBoxLayout*  props_container_layout_ = nullptr; ///< 统一「属性变量」分组面板容器
    QTableWidget* table_props_     = nullptr;   ///< 属性变量概览表格 (只读)
    base::PropertyTreeWidget* tree_props_ = nullptr; ///< 属性参数分组树 (树驱动, 公共控件)

    // -- transform --
    QCheckBox* chk_place_ = nullptr;
    QComboBox* cmb_faction_ = nullptr;
    QDoubleSpinBox* spn_pos_x_ = nullptr;
    QDoubleSpinBox* spn_pos_y_ = nullptr;
    QDoubleSpinBox* spn_pos_z_ = nullptr;
    QDoubleSpinBox* spn_yaw_   = nullptr;
    QDoubleSpinBox* spn_scale_ = nullptr;
    QDoubleSpinBox* spn_mass_  = nullptr;

    // -- waypoints --
    QTableWidget* tbl_waypoints_ = nullptr;
    bool wp_syncing_ = false;

    // -- route planning --
    QComboBox* cmb_route_type_  = nullptr;
    QComboBox* cmb_platform_cat_ = nullptr;
    QCheckBox* chk_route_loop_  = nullptr;
    QDoubleSpinBox* spn_route_delay_ = nullptr;

    // -- sensors --
    QListWidget* lst_sensors_ = nullptr;
    QComboBox* cmb_sensor_type_ = nullptr;
    QComboBox* cmb_sensor_band_ = nullptr;
    QComboBox* cmb_sensor_role_ = nullptr;
    QDoubleSpinBox* spn_s_power_ = nullptr;
    QDoubleSpinBox* spn_s_wavelen_ = nullptr;
    QDoubleSpinBox* spn_s_beam_ = nullptr;
    QDoubleSpinBox* spn_s_prf_ = nullptr;
    QDoubleSpinBox* spn_s_duty_ = nullptr;
    QDoubleSpinBox* spn_s_fovh_ = nullptr;
    QDoubleSpinBox* spn_s_fovv_ = nullptr;
    QDoubleSpinBox* spn_s_offset_y_ = nullptr;
    QDoubleSpinBox* spn_s_az_    = nullptr;
    QDoubleSpinBox* spn_s_el_    = nullptr;
    QDoubleSpinBox* spn_s_range_ = nullptr;

    // -- face groups --
    QComboBox* cmb_face_group_  = nullptr;
    QComboBox* cmb_brdf_        = nullptr;
    QDoubleSpinBox* spn_fg_albedo_    = nullptr;
    QDoubleSpinBox* spn_fg_rough_     = nullptr;
    QDoubleSpinBox* spn_fg_emiss_     = nullptr;
    QDoubleSpinBox* spn_fg_temp_      = nullptr;
    QDoubleSpinBox* spn_fg_dielec_r_  = nullptr;
    QDoubleSpinBox* spn_fg_dielec_i_  = nullptr;
    QDoubleSpinBox* spn_fg_sigma0_    = nullptr;
    QDoubleSpinBox* spn_fg_surf_rough_ = nullptr;
    QSpinBox* spn_fg_face_begin_ = nullptr;
    QSpinBox* spn_fg_face_end_   = nullptr;

    // -- thermal parts (§14.3 热部件 + 面组选择器, 电磁光学 Tab 内) --
    QListWidget* lst_thermal_parts_ = nullptr;
    QComboBox*  cmb_tp_face_group_  = nullptr;   ///< 面组选择器 (选项 = 当前实体 face_groups)
    QLineEdit*  edt_tp_name_        = nullptr;
    QDoubleSpinBox* spn_tp_temp_    = nullptr;
    QDoubleSpinBox* spn_tp_emiss_   = nullptr;
    QPushButton* btn_tp_del_        = nullptr;

    // -- environment (数据成员; 界面并入属性面板「环境」节点, 不再单独设置 Dock) --
    QString env_weather_ = QStringLiteral("clear");   ///< 天气场景代码 (clear/rain/fog/snow)
    vsgraysim::ScenarioClockConfig clock_config_;
    nlohmann::json ocean_config_=nlohmann::json::object();
    int     env_severity_ = 0;                        ///< 天气严重度 0-100
    // W2: 细粒度大气参数 (实时下发到 rocky_env; 导出到 atmosphere JSON)
    double  env_cloud_cover_  = 0.5;     ///< 云量 0..1
    double  env_cloud_base_   = 1200.0;  ///< 云底高 (m)
    double  env_rain_         = 0.0;     ///< 降雨强度 0..1
    double  env_snow_         = 0.0;     ///< 降雪强度 0..1
    double  env_visibility_   = 10000.0; ///< 能见度 (m)
    double  env_wind_speed_   = 6.0;     ///< 风速 (m/s)
    double  env_wind_az_      = 80.0;    ///< 风向 (°, 北=0 顺时针)
    double  env_sun_el_       = 62.0;    ///< 太阳高度 (°)
    double  env_sun_az_       = 175.0;   ///< 太阳方位 (°)
    double  env_wave_height_  = 0.0;     ///< 浪高 (m, 0=自动按风速)

    // -- bindings (绑定/导引关系) --
    QTreeWidget* tree_bindings_ = nullptr;
    QComboBox* cmb_b_from_ = nullptr;
    QComboBox* cmb_b_relation_ = nullptr;
    QComboBox* cmb_b_to_ = nullptr;
    QListWidget* lst_b_links_ = nullptr;
    QPushButton* btn_b_del_ = nullptr;
    /// 中央 Tab「关系图」: Qt QGraphicsView 协作/交互关系可视化+编辑。
    display::RelationshipGraphView* graph_bindings_ = nullptr;

    // -- scene data --
    std::vector<SceneObject> objects_;
    // 导引/协同绑定关系 (场景文件 bindings 段)
    std::vector<BindingLink> binding_links_;

    // -- startup injection / scenario io state --
    std::string terrain_template_;
    QString open_scenario_on_start_;
    QString current_scenario_path_;

    int next_sensor_id_ = 1;
    bool suppress_edit_signals_ = false;
};

}} // namespace vsgraysim::gui
#endif // VSGRAYSIM_GUI_SCENE_DESIGNER_WINDOW_H
