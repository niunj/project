#pragma once
/// @file HealthPanel.h
/// @brief Launcher 全局健康面板 — 显示所有进程遥测状态。
///
/// 功能:
///   - 自动发现进程并连接其遥测 UDS 服务器
///   - 每个进程一张健康卡片 (状态/FPS/CPU/内存/运行时长)
///   - 进程日志查看器 (颜色区分级别)
///   - 故障告警显示
///   - 颜色编码状态指示: 绿色(运行中) / 黄色(警告) / 红色(故障/离线)

#include <QMap>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QTextEdit;
class QVBoxLayout;
class QButtonGroup;
class QFrame;

namespace vsgraysim::telemetry {
struct ProcessHealth;
}

namespace vsgraysim { namespace gui {

/// 单进程健康卡片
class ProcessHealthCard : public QWidget {
    Q_OBJECT
public:
    explicit ProcessHealthCard(const QString& process_name, QWidget* parent = nullptr);

    /// 更新健康数据
    void updateHealth(const QString& status, double fps, double cpu,
                      size_t mem_bytes, double uptime_sec, int64_t ts);

    /// 追加日志
    void appendLog(const QString& level, const QString& module,
                   const QString& message, int64_t ts);

    /// 设置状态指示颜色
    void setStatusColor(const QColor& color);

    QString processName() const { return name_; }

    /// 当前状态 (running/warning/error/offline/unknown)
    QString status() const { return last_status_; }

    /// 折线图历史 (CPU / FPS)
    const std::vector<double>& cpuHistory() const { return cpu_history_; }
    const std::vector<double>& fpsHistory() const { return fps_history_; }
    const std::vector<double>& memHistory() const { return mem_history_; }

private:
    QString name_;
    QString last_status_ = "unknown";
    QLabel* icon_;         ///< 状态指示圆点
    QLabel* title_;        ///< 进程名称
    QLabel* pill_;         ///< 状态胶囊
    QLabel* fps_label_;    ///< FPS
    QLabel* cpu_label_;    ///< CPU %
    QLabel* mem_label_;    ///< 内存
    QLabel* uptime_label_; ///< 运行时长
    QProgressBar* fps_bar_ = nullptr;  ///< FPS 迷你进度条
    QProgressBar* cpu_bar_ = nullptr;  ///< CPU 迷你进度条
    QProgressBar* mem_bar_ = nullptr;  ///< 内存迷你进度条
    QPushButton* btn_toggle_log_ = nullptr; ///< 展开/折叠日志
    QTextEdit*   log_view_ = nullptr;  ///< 日志视图
    bool         log_visible_ = false;

    std::vector<double> cpu_history_;  ///< CPU 历史 (用于迷你趋势)
    std::vector<double> fps_history_;  ///< FPS 历史
    std::vector<double> mem_history_;  ///< 内存历史 (MB)
};

/// 全局健康面板
class HealthPanel : public QWidget {
    Q_OBJECT
public:
    enum Filter { FilterAll = 0, FilterOk, FilterWarn, FilterFault, FilterOffline };

    explicit HealthPanel(QWidget* parent = nullptr);
    ~HealthPanel() override;

    /// 启动遥测监听 (定时扫描发现进程并连接)
    void startMonitoring();

    /// 停止遥测监听
    void stopMonitoring();

    /// 手动添加进程 (用于测试或调试)
    void addProcess(const QString& name, const std::string& socket_path);

    /// 移除进程
    void removeProcess(const QString& name);

    /// 清空所有
    void clearAll();

private slots:
    void onDiscoveryTick();
    void onReconnectTick();

private:
    // 发现新进程, 建立遥测连接
    void discoverProcesses();
    bool connectToProcess(const QString& name, const std::string& socket_path);
    void disconnectFromProcess(const QString& name);

    // 解析接收到的遥测数据
    void handleHealthData(const QString& name, const std::string& json);
    void handleLogData(const QString& name, const std::string& json);
    void handleFaultData(const QString& name, const std::string& json);

    // 遥测客户端读取线程
    void clientReaderThread(const QString& name, int fd);

    // UI 构建
    QWidget* buildSummaryStrip();
    QWidget* buildFilterBar();
    void     applyFilter();
    void     updateSummary(const QString& status);

    // UI
    QVBoxLayout* card_layout_ = nullptr;
    QScrollArea* scroll_area_ = nullptr;
    QWidget*     card_container_ = nullptr;
    QPushButton* btn_clear_ = nullptr;
    QPushButton* btn_export_ = nullptr;
    QPushButton* btn_pause_ = nullptr;
    QButtonGroup* filter_group_ = nullptr;
    Filter        current_filter_ = FilterAll;

    // 汇总标签
    QLabel* lbl_total_ = nullptr;
    QLabel* lbl_ok_ = nullptr;
    QLabel* lbl_warn_ = nullptr;
    QLabel* lbl_fault_ = nullptr;
    QLabel* lbl_offline_ = nullptr;
    QLabel* lbl_cpu_ = nullptr;
    QLabel* lbl_mem_ = nullptr;

    // 状态计数 (用于汇总)
    int count_ok_ = 0, count_warn_ = 0, count_fault_ = 0, count_offline_ = 0;

    // 进程卡片
    QMap<QString, ProcessHealthCard*> cards_;

    // 遥测连接
    struct TelemetryConnection {
        int fd = -1;
        std::thread reader_thread;
        std::atomic<bool> running{false};
    };
    QMap<QString, TelemetryConnection*> connections_;

    // 定时器
    QTimer* discovery_timer_ = nullptr;
    QTimer* reconnect_timer_ = nullptr;

    // 发现路径
    std::string discovery_path_ = "/tmp/vsgraysim/discovery.sock";
    int discovery_fd_ = -1;

    // 互斥锁保护连接
    std::mutex connections_mutex_;
};

}} // namespace vsgraysim::gui