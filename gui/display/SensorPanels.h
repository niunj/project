#pragma once
/// @file SensorPanels.h
/// @brief 显控"可复用传感器面板" — 合流显控的核心可装配显示单元。
///
/// 目标: 把原 radar/sar/ir 三个显控窗口的成熟"处理 + 显示"收敛为按传感器类型
/// 可复用的 DisplayPanel, 由统一工作台(ControlWorkbench)按 WorkModeProfile 装配。
///
///   RadarDisplayPanel -- 复用 MtiMtdProcessor/CfarProcessor/TrackManager + PpiDisplay
///   ImageDisplayPanel  -- IR/SAR 大幅面图像 (辐射亮度/回波幅度 -> 灰度图)
///
/// 每个面板由外部(工作台) 定时 tick(session): 从 EquipmentSession 拉最新帧 -> 处理 -> 刷新显示。
#ifndef VSGRAYSIM_GUI_DISPLAY_SENSOR_PANELS_H
#define VSGRAYSIM_GUI_DISPLAY_SENSOR_PANELS_H

#include <QWidget>
#include <memory>

#include "vsgraysim/core/SensorTypes.h"
#include "PpiDisplay.h"

namespace vsgraysim { namespace gui { namespace display {

class EquipmentSession;

/// 可复用的传感器显示面板基类
class DisplayPanel : public QWidget {
public:
    explicit DisplayPanel(QWidget* parent = nullptr) : QWidget(parent) {}
    ~DisplayPanel() override = default;

    /// 拉一帧并刷新显示 (由工作台每秒多次调用)
    virtual void tick(EquipmentSession* session) = 0;
    virtual void setEarthMap(const std::string&) {}
};

/// 雷达面板: MTI/MTD -> CFAR -> 航迹 -> PPI + RD 图
class RadarDisplayPanel : public DisplayPanel {
    Q_OBJECT
public:
    explicit RadarDisplayPanel(QWidget* parent = nullptr);
    ~RadarDisplayPanel() override;
    void tick(EquipmentSession* session) override;
    void setEarthMap(const std::string& path) override;

private:

    class Impl;
    std::unique_ptr<Impl> d_;
};

/// Image display preserves physical pixel readings; contrast/palette affect
/// presentation only. SAR raw echo and RD products are explicitly unfocused.
class ImageDisplayPanel : public DisplayPanel {
    Q_OBJECT
public:
    explicit ImageDisplayPanel(SensorType type, QWidget* parent = nullptr, bool compact = false);
    ~ImageDisplayPanel() override;
    void tick(EquipmentSession* session) override;

private:
    class Impl;
    std::unique_ptr<Impl> d_;
};

/// 按传感器类型创建对应面板 (原显控窗口"处理+显示"的合流产物)
std::unique_ptr<DisplayPanel> createPanelFor(SensorType type, QWidget* parent = nullptr);

}}} // namespace vsgraysim::gui::display

#endif // VSGRAYSIM_GUI_DISPLAY_SENSOR_PANELS_H
