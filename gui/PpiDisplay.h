#pragma once
/// @file PpiDisplay.h
/// @brief 雷达 PPI(平面位置显示器) 显示控件。
///
/// 以极坐标方式显示雷达回波: 天线扫描线旋转、距离环、方位刻度、
/// 目标亮点与航迹点。数据通过 addSweep() / setTargets() 从主线程推入。
#ifndef VSGRAYSIM_GUI_PPI_DISPLAY_H
#define VSGRAYSIM_GUI_PPI_DISPLAY_H

#include <QWidget>
#include <QImage>
#include <QPointF>
#include <QTimer>
#include <QElapsedTimer>
#include <array>
#include <vector>
#include <vsgraysim/broker/SensorFrames.h>

namespace vsgraysim { namespace gui {

/// PPI 上的一个目标亮点 (方位/距离为极坐标)
struct PpiTarget {
    double bearing_deg = 0.0;   ///< 方位角 (度, 0=正北, 顺时针)
    double range_km    = 0.0;   ///< 距离 (km)
    double snr_db      = 0.0;   ///< 信噪比 (dB), 决定亮点强度
    bool   tracked     = false; ///< 是否已被跟踪
};

class PpiDisplay : public QWidget {
    Q_OBJECT
public:
    explicit PpiDisplay(QWidget* parent = nullptr);

    /// 设置显示量程 (km), 决定最大显示距离
    void setRangeScale(double km);
    double rangeScale() const { return range_scale_km_; }

    /// 设置天线扫描速度 (deg/s)
    void setSweepSpeed(double deg_per_s) { sweep_speed_ = deg_per_s; }

    /// 天线当前方位角 (度)
    double sweepBearing() const { return sweep_bearing_; }

    /// B7 §7.2: 帧驱动扫描线 —— 仿真端帧头 CPI 中心波束方位 (罗盘角 0=北
    /// 顺时针) 直接驱动天线指向; 持续 >1.5s 无帧回落本地匀速扫描 (待机兜底)。
    void notifyFrameSweep(double bearing_deg);

    /// 清空回波余辉
    void clearEcho();
    /// Add one observed bearing. Range bins outside this radial are left empty.
    void setEchoRadial(double bearingDeg,double firstRangeKm,double stepKm,std::vector<float> db,double simTime);
    size_t echoBearingCount() const;
    void setGroundLayer(std::vector<RadarFrame::GroundSample> ground);
    void setGroundVisible(bool enabled);

    /// 推入一批目标点
    void setTargets(const std::vector<PpiTarget>& targets);

    /// 开机/待机 (待机时停止扫描)
    void setPowered(bool on) { if(powered_!=on){powered_=on;update();} }
    bool powered() const { return powered_; }

signals:
    /// 天线方位角变化 (度), 供数据处理模块同步
    void sweepBearingChanged(double bearing_deg);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void onSweepTick();

private:
    void drawBackground(QPainter& p, double cx, double cy, double R);
    void drawEcho(QPainter& p, double cx, double cy, double R);
    void drawSweep(QPainter& p, double cx, double cy, double R);
    void drawTargets(QPainter& p, double cx, double cy, double R);
    void rebuildEcho();
    void rebuildGround();

    struct Radial {
        std::vector<float> db;
        double firstKm=0,stepKm=0,time=0;
        float peak=-240.f;
    };

    bool    powered_ = true;
    double  range_scale_km_ = 50.0;
    double  sweep_speed_ = 24.0;      ///< deg/s
    double  sweep_bearing_ = 0.0;     ///< 当前天线方位角
    double  sweep_integration_ = 0.0; ///< 累计角度用于余辉
    bool    frame_driven_ = false;    ///< B7: 帧头驱动模式 (帧数据源优先)
    QElapsedTimer frame_timer_;       ///< 最后帧到达计时 (超时回落本地扫描)

    std::vector<PpiTarget> targets_;
    std::array<Radial,360> radials_;
    QImage  echo_layer_;
    QImage ground_layer_;
    std::vector<RadarFrame::GroundSample> ground_;
    bool ground_visible_=true,ground_dirty_=false;
    double latest_echo_time_=0;
    bool echo_dirty_=false;
    QTimer  sweep_timer_;
};

}} // namespace vsgraysim::gui
#endif // VSGRAYSIM_GUI_PPI_DISPLAY_H
