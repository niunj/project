#pragma once
/// @file SimulationControlWindow.h
/// @brief Qt6 仿真控制主窗口 —— 多分类 Dock 设计。
///
/// Layout
/// ------
///   +---------------------------------------------------------------+
///   | MenuBar  文件 | 仿真 | 视图 | 帮助                              |
///   | ToolBar  [← 导航页] [⌂ 重置相机] [● 录制] [▶ 回放] [日志]    |
///   +------+------+------+------+------+------+---------------------+
///   |场景   |播放  |显示   |传感器 |装备   |波段   | QSplitter       |
///   |装载   |控制  |选项   |监视   |数据   |预览   |  Viewport3DView |
///   |Dock   |Dock  |Dock   |Dock   |分发   |Dock   |  + RadarDisplay |
///   |       |      |       |       |Dock   |       |                 |
///   +------+------+------+------+------+------+---------------------+
///   | 性能监控 (Right Dock)                                           |
///   +---------------------------------------------------------------+
///   | 装备状态与事件日志 (Bottom Dock)                                 |
///   +---------------------------------------------------------------+
///   | StatusBar: 仿真状态 | 仿真时间 | 帧 | FPS                       |
///   +---------------------------------------------------------------+
///
/// 8 个分类 Dock:
///   左侧 (标签化): 场景装载 / 播放控制 / 显示选项 / 传感器监视
///                  / 装备数据分发 / 波段预览
///   右侧: 性能监控
///   底部: 装备状态与事件日志
///
/// 共用基础 UI 模块: gui/base/ (BaseWindow / DockBuilder / WidgetFactory)
#ifndef VSGRAYSIM_GUI_SIMULATION_CONTROL_WINDOW_H
#define VSGRAYSIM_GUI_SIMULATION_CONTROL_WINDOW_H

#include "base/BaseWindow.h"

#include <QLabel>
#include <QTimer>
#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <map>
#include <string>
#include <thread>

#include "vsgraysim/Simulation.h"
#include "vsgraysim/kernel/TimingBus.h"
#include "vsgraysim/broker/EquipmentDataSource.h"
#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include "vsgraysim/broker/FrameRecorder.h"

#include "platform/PlatformManager.h"
#include "platform/PlatformMotionFactory.h"

#include <nlohmann/json.hpp>

class QAction;
class QComboBox;
class QGroupBox;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTreeWidget;

namespace vsgraysim { namespace viewport3d {
class Viewport3DView;
class SimulationDisplayFacade;
}}

namespace vsgraysim { namespace gui { namespace base { class PropertyTreeWidget; } }}

namespace vsgraysim { namespace gui {

class PerfMonitorPanel;
class EventLogPanel;
class SimulationWorkerClient;

class SimulationControlWindow : public ui::BaseWindow {
    Q_OBJECT
public:
    explicit SimulationControlWindow(QWidget* parent = nullptr);
    ~SimulationControlWindow() override;

    /// 装载一个场景文件 (workspace/scenes/*.json): 展示摘要+3D预览, 并直接初始化。
    void loadScenarioFile(const QString& path);
    /// W2: 把场景 JSON 里的细粒度大气参数还原下发到 preview_viewport_ 的 rocky_env 模块。
    void applyAtmosphereFromScenario();
    nlohmann::json runtimeStatus() const;

private slots:
    // ---- scenario load ----
    void onBrowseScenario();

    // ---- configuration ----
    void onInitSimulation();
    void onLaunchDisplay();
    void onOpenDesigner();

    // ---- playback ----
    void onPlayPause();
    void onStepOnce();
    void onStop();
    void onRestartWorker();
    void onTick();

    // ---- 第二阶段增强: 录制 / 回放 / 事件日志 ----
    void onToggleRecord(bool checked);
    void onReplayData();
    void onClearLog();
    void onExportLog();

    void onAbout();

private:
    // ---- 覆写 BaseWindow 扩展点 ----
    void buildProductMenuItems(QMenuBar* mb) override;
    void buildProductToolBarItems(QToolBar* tb) override;
    void buildProductDocks() override;

    // ---- Dock 构建 ----
    void buildPlaybackOverlay();             ///< 3D 视口顶部播放控制条 (半透明浮层 + 动画)
    void buildEquipmentMonitorDock();        ///< 装备在线状态工作监控 (右侧, 状态+分发+日志合并)
    void buildPerfMonitorDock();             ///< 性能监控 (右侧)

    // ---- 场景实体树 / 实体属性 (只读: 复用场景设计软件的查看能力, 不编辑/不修改) ----
    void buildEntityTreeDock();       ///< 场景实体树 (只读)
    void buildScenePropertiesDock();  ///< 实体属性 (只读)
    void refreshEntityTree(const nlohmann::json& scenario); ///< 从场景 JSON 重建实体树
    void updateSceneProperties();     ///< 刷新当前选中实体的属性 (静态+运行时)
    void updateSim3DVisuals();        ///< 动态更新 3D 探测特效 + 平台中文标注 (随平台快照刷新)
    nlohmann::json findScenarioObject(const QString& object_id) const;

    std::string resolveConfigPath(const QString& p) const;
    void advanceOneFrame();         ///< 定步长单帧推进 (调用方须持 sim_mtx_)
    void startSimWorker();          ///< 启动仿真工作线程 (重计算与 UI/渲染分离)
    void stopSimWorker();           ///< 停止并汇合仿真工作线程
    void updateStatusLabels();
    void setPlaybackEnabled(bool running);
    void buildTimingBus();          ///< 创建时序子系统并注册装备数据源
    void registerEquipment();       ///< 把仿真装备注册到注册表 (供显控软件扫描链接)
    void registerEquipmentFromScenario(); ///< 发现面: 场景驱动装备注册 (装载即注册, 不依赖首帧产帧)
    void updateDistMonitor();       ///< 刷新装备数据分发面板
    void setServiceOnline(bool online);   ///< 数据服务上下线 (+事件日志)

    // ---- 第二阶段增强: 录制 / 回放 ----
    bool startRecording();          ///< 打开三个录制文件并挂接到数据分发处
    void stopRecording();           ///< 结束录制并落盘
    void startReplay();             ///< 选择录制文件并开始回放
    void stopReplay(bool finished); ///< 结束/中止回放
    void replayTick();              ///< 回放推进一帧 (重新发布到 broker)
    void pollControlCommandsWorker(); ///< 仿真线程: 装备控制与飞控装订、命令回报
    void updateFlightControlMonitor();
    void updateEngagementLoopWorker(); ///< 仿真线程: 交战闭环 (日志入队列)
    void drainWorkerLogs();           ///< UI 线程: 排空仿真线程日志队列
    void updateChannelRates();      ///< 各装备帧率/流量统计 (周期刷新)
    void updateEquipmentTable();    ///< 装备状态总览表格
    void updatePerfStats();         ///< 性能监控面板
    void pushFrameMs(double ms);    ///< 帧耗时滑窗 (≤120)

    // ---- scenario summary / 3D preview / flow indicator ----
    void buildScenarioSummary(const QString& path);
    void setFlowStage(int stage);   ///< 0=装载 1=初始化 2=运行

    // ---- backend ----
    Simulation sim_;
    std::shared_ptr<VoxelEquipmentSource> voxel_source_;
    std::unique_ptr<SimulationWorkerClient> engine_worker_;
    QTableWidget* task_pool_table_=nullptr;
    AsyncVoxelSceneReadView remote_voxel_view_;
    std::string worker_view_generation_;
    void startVoxelWorker(const QString&);
    void acceptWorkerStatus(const nlohmann::json&);
    earth::VoxelSceneSnapshotPtr pending_voxel_view_;
    bool pending_voxel_initialization_=false;
    std::unique_ptr<TimingBus> bus_;                    ///< 时序子系统 (主循环)
    std::shared_ptr<EquipmentDataSource> data_source_;  ///< 装备数据源 (发布 GPU 句柄)

    // ---- 仿真线程与 UI/渲染线程分离 (#6 性能优化) ----
    // 重计算 (bus_->tick / 平台演化 / 交战闭环) 在专用线程墙钟 pacing 执行,
    // UI 线程仅做显示刷新; 定步长序列不变, 仿真时钟 = 帧数 × dt 保持确定性。
    mutable std::mutex sim_mtx_;         ///< 保护 bus_/platform_mgr_/sim_ 状态访问
    std::thread sim_worker_;             ///< 仿真工作线程
    std::atomic<bool> sim_running_{false};
    std::atomic<bool> pending_register_{false};  ///< 首帧成功 → UI 线程注册装备

    // ---- 平台运动模型 ----
    std::shared_ptr<platform::PlatformManager> platform_mgr_;  ///< 平台管理器
    uint32_t next_equip_id_ = 1000;                            ///< 装备 ID 计数器
    std::atomic<bool> sim_inited_{false};
    std::atomic<bool> playing_{false};
    bool   equipment_registered_ = false;   ///< 装备已注册到注册表
    std::atomic<double> sim_time_{0.0};
    std::atomic<std::size_t> frames_{0};
    int    target_fps_ = 60;
    std::atomic<double> simulation_step_{1.0/60.0};
    QTimer tick_timer_;
    QString scenario_path_;      ///< 当前装载的场景文件

    // ---- 装备数据分发监视 ----
    QLabel* lbl_dist_[3] = {nullptr, nullptr, nullptr};  ///< 雷达/SAR/红外 分发状态行
    QLabel* lbl_dist_service_ = nullptr;                 ///< 数据服务在线状态
    bool service_online_ = false;                        ///< 数据服务状态 (日志去重)

    // ---- 第二阶段: 数据录制 / 回放 ----
    std::array<FrameRecorder, 3> recorders_;   ///< 雷达/SAR/IR 帧录制器
    std::array<FramePlayer, 3>   players_;     ///< 雷达/SAR/IR 帧回放器
    bool     recording_      = false;
    std::atomic<bool> replay_active_{false};  ///< 回放会话进行中 (含暂停)
    bool     replay_playing_ = false;          ///< 回放推进中 (未暂停)
    uint64_t replay_frames_  = 0;              ///< 已回放帧数

    // ---- 第二阶段: 工具栏 / 监控面板 ----
    QAction*          act_record_     = nullptr;
    QAction*          act_replay_     = nullptr;
    QAction*          act_clear_log_  = nullptr;
    QAction*          act_export_log_ = nullptr;
    PerfMonitorPanel* perf_panel_     = nullptr;
    EventLogPanel*    log_panel_      = nullptr;
    QTableWidget*     table_equip_    = nullptr;
    QTableWidget*     table_flight_   = nullptr;

    // ---- 第二阶段: 统计 ----
    struct ChannelRate {
        uint64_t last_frames   = 0;    ///< 上次统计时累计发布帧数
        uint64_t last_frame_id = 0;    ///< 最近帧号
        int64_t  last_ms       = 0;    ///< 上次统计时刻 (epoch ms)
        double   fps  = 0.0;           ///< 数据帧率 (Hz)
        double   kbps = 0.0;           ///< 数据流量 (KB/s)
    };
    ChannelRate ch_rate_[3];
    std::mutex powered_mtx_;                       ///< 保护 equip_powered_ (仿真线程写)
    bool equip_powered_[3] = {true, true, true};  ///< 开关机状态 (控制命令驱动)
    std::mutex frame_mtx_;                         ///< 保护 frame_ms_history_
    std::deque<double> frame_ms_history_;         ///< 最近帧耗时滑窗 (≤120)
    // 仿真线程 → UI 线程日志队列 (EventLogPanel 是 QWidget, 不可跨线程操作)
    struct LogEntry { int level; QString msg; };   ///< 0=info 1=warn 2=error
    std::mutex log_mtx_;
    std::deque<LogEntry> log_queue_;
    bool powered_dirty_ = false;         ///< 有开关机命令待刷新表格 (log_mtx_ 保护)
    int64_t ratio_last_ms_  = 0;                  ///< 仿真/真实时间比采样点
    double  ratio_last_sim_ = 0.0;

    // ---- flow indicator ----
    QLabel* lbl_flow_[3] = {nullptr, nullptr, nullptr};

    // ---- 场景树 (只读, 已并入场景装载) ----
    QTreeWidget*   tree_entities_       = nullptr;
    base::PropertyTreeWidget* tree_props_ = nullptr;
    QString        sel_object_id_;        ///< 当前选中的实体对应对象 id
    std::mutex earth_view_mutex_;
    earth::EarthSurfaceSnapshotPtr pending_earth_view_;
    double pending_earth_time_=0;
    nlohmann::json scenario_data_;        ///< 当前装载的场景 JSON

    // ---- 3D preview (共用 viewport3d 库: simulation_display 预设) ----
    viewport3d::Viewport3DView*         preview_viewport_ = nullptr;
    viewport3d::SimulationDisplayFacade* sim_facade_      = nullptr;
    std::map<uint32_t,bool> aircraft_fx_alive_; // previous authoritative state for a one-shot visual burst
    std::set<std::string> impact_handled_;   ///< 已播过命中爆炸的交战 (幂等避免重复)
    std::set<std::string> launch_handled_;   ///< 已播过发射特效的拦截弹 (幂等)
    std::set<std::string> miss_handled_;     ///< 已打过脱靶标记的拦截弹 (幂等)
    QWidget*       playback_host_ = nullptr;   ///< 视口承载容器 (浮层与视口分层叠加)

    // ---- config widgets (nullable: 传统配置路径, 未创建时为 null) ----
    QLineEdit*   edt_object_ = nullptr;
    QLineEdit*   edt_sensor_ = nullptr;
    QLineEdit*   edt_atm_    = nullptr;

    // ---- 3D 视口顶部播放控制条 widgets (半透明浮层) ----
    QWidget*     playback_bar_ = nullptr;
    QPushButton* btn_play_ = nullptr;   ///< 播放/暂停切换
    QPushButton* btn_step_ = nullptr;   ///< 单步方正
    QPushButton* btn_cont_ = nullptr;   ///< 连续仿真
    QPushButton* btn_pause_= nullptr;   ///< 暂停
    QPushButton* btn_stop_ = nullptr;   ///< 结束
    // cmb_speed_ (倍速) 已移除: 定步长时序, 避免步长缩放引入时序复杂性

    // ---- status bar (扩展) ----
    QLabel* sb_state_  = nullptr;
    QLabel* sb_time_   = nullptr;
    QLabel* sb_backend_ = nullptr;
    QLabel* sb_earth_ = nullptr;
    QAction* act_launch_display_ = nullptr;
    QString backend_description_;

    struct { int64_t last_ms = 0; int frames = 0; double fps = 0; } fps_state_;
};

}} // namespace vsgraysim::gui
#endif // VSGRAYSIM_GUI_SIMULATION_CONTROL_WINDOW_H
