#pragma once
/// @file SimMonitorPanels.h
/// @brief 仿真控制软件第二阶段增强面板: 性能监控 + 事件日志。
///
///   FpsSparkline     — QPainter 自绘 FPS 迷你曲线 (保存最近 120 个采样)
///   PerfMonitorPanel — FPS 曲线 / 平均帧耗时 / 仿真-真实时间比 / 各装备帧率
///   EventLogPanel    — 只读滚动事件日志 (时间戳 + [INFO]/[WARN]/[ERROR])
#ifndef VSGRAYSIM_GUI_SIMMONITORPANELS_H
#define VSGRAYSIM_GUI_SIMMONITORPANELS_H

#include <QLabel>
#include <QWidget>
#include <vector>

class QPlainTextEdit;

namespace vsgraysim { namespace gui {

/// FPS sparkline 迷你曲线 (最近 120 个采样, 深色底 + 网格 + 半透明填充)
class FpsSparkline : public QWidget {
public:
    explicit FpsSparkline(QWidget* parent = nullptr);

    /// 推入一次 FPS 采样 (超出 120 个时丢弃最旧)
    void pushSample(float fps);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static constexpr int kMaxSamples = 120;
    std::vector<float> samples_;
};

/// 性能监控面板: FPS 曲线 + 平均帧耗时 + 仿真/真实时间比 + 各装备数据帧率
class PerfMonitorPanel : public QWidget {
public:
    explicit PerfMonitorPanel(QWidget* parent = nullptr);

    /// 推入一次 FPS 采样 (刷新曲线与数值)
    void pushFps(double fps);
    /// 平均帧耗时 (ms)
    void setAvgFrameMs(double ms);
    /// 仿真时间 / 真实时间 比率
    void setTimeRatio(double ratio);
    /// 各装备数据生成帧率 (index: 0=雷达 1=SAR 2=红外)
    void setEquipmentFps(int index, double fps);

private:
    FpsSparkline* spark_       = nullptr;
    QLabel*       lbl_fps_     = nullptr;
    QLabel*       lbl_avg_ms_  = nullptr;
    QLabel*       lbl_ratio_   = nullptr;
    QLabel*       lbl_eq_fps_[3] = {nullptr, nullptr, nullptr};
};

/// 事件日志面板: 只读滚动日志, 每行 "[时间戳] [级别] 消息"
class EventLogPanel : public QWidget {
public:
    enum class Level { Info, Warning, Error };

    explicit EventLogPanel(QWidget* parent = nullptr);

    void log(Level level, const QString& message);
    void info(const QString& message);
    void warn(const QString& message);
    void error(const QString& message);

    /// 清空日志
    void clearLog();
    /// 日志纯文本 (供导出)
    QString plainText() const;

private:
    QPlainTextEdit* text_ = nullptr;
};

}} // namespace vsgraysim::gui
#endif // VSGRAYSIM_GUI_SIMMONITORPANELS_H
