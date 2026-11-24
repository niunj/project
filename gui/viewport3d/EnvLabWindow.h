#pragma once
/// @file EnvLabWindow.h
/// @brief rocky_env 环境模块验证程序 (env_lab) 主窗口。
///
/// 目的: 提供一个**独立**的 VSG+Qt 测试台, 可分别或组合加载
/// rocky_sky / rocky_cloud / rocky_ocean 三个模块, 通过控制面板驱动其全部
/// Control/Query 接口, 直观验证功能与显示效果。
///
/// 结构:
///   +---------------------------+----------------------------+
///   |                           |  模块开关 (sky/cloud/ocean)  |
///   |      Viewport3DView       |  ---- 天空面板 ----          |
///   |      (VSG Vulkan 渲染)     |  ---- 云面板 ----            |
///   |                           |  ---- 海洋面板 ----          |
///   |                           |  状态读数 (实时查询)          |
///   +---------------------------+----------------------------+
///
/// 面板不直接依赖 rocky_env 类型以外的渲染细节: 通过 EnvFeatures 暴露的
/// ISkyControl / ICloudControl / ICloudQuery / IOceanControl / IOceanSampler
/// 访问器操作模块, 与宿主程序使用同一套公开 API。
#ifndef VSGRAYSIM_VIEWPORT3D_ENVLABWINDOW_H
#define VSGRAYSIM_VIEWPORT3D_ENVLABWINDOW_H

#include <QMainWindow>
#include <QString>

#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QSlider;
class QSpinBox;
class QTimer;

namespace vsgraysim { namespace viewport3d {

class Viewport3DView;
class SkyFeature;
class CloudFeature;
class OceanFeature;

/// 环境模块验证台主窗口。
class EnvLabWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit EnvLabWindow(QWidget* parent = nullptr);
    ~EnvLabWindow() override;

private:
    // ---- UI 构建 ----
    void buildUi();
    QWidget* buildSkyPanel();
    QWidget* buildCloudPanel();
    QWidget* buildOceanPanel();
    QWidget* buildStatusPanel();

    // ---- 模块挂载 / 卸载 (分别加载三个模块) ----
    /// 按三个复选框的当前状态同步视口特性; 返回是否发生变更。
    bool syncModules();
    void refreshFeaturePointers();

    // ---- 面板 → 模块 推送 ----
    void pushSkyParams();
    void pushCloudParams();
    void pushOceanParams();

    // ---- 模块 → 面板 回读 (每帧/定时刷新) ----
    void refreshReadouts();

    /// 统一日志输出到窗口内日志区 + spdlog。
    void log(const QString& msg);

private:
    // ---- 视口 ----
    Viewport3DView* view_ = nullptr;

    // ---- 特性指针 (模块挂载后刷新; 卸载/重载时置空) ----
    SkyFeature*   sky_   = nullptr;
    CloudFeature* cloud_ = nullptr;
    OceanFeature* ocean_ = nullptr;

    // ---- 模块开关 ----
    QCheckBox* cb_sky_   = nullptr;
    QCheckBox* cb_cloud_ = nullptr;
    QCheckBox* cb_ocean_ = nullptr;

    // ---- 天空面板控件 ----
    QDoubleSpinBox* sky_hours_     = nullptr;   ///< UTC 小时 (时间基准)
    QDoubleSpinBox* sky_time_scale_= nullptr;   ///< 仿真时间倍率
    QSlider*        sky_exposure_  = nullptr;   ///< 曝光 ×100
    QSlider*        sky_turbidity_ = nullptr;   ///< 浊度 ×100
    QCheckBox*      sky_show_sun_  = nullptr;
    QCheckBox*      sky_show_moon_ = nullptr;
    QCheckBox*      sky_show_atm_  = nullptr;
    QLabel*         sky_readout_   = nullptr;

    // ---- 云面板控件 ----
    QSlider*   cloud_cover_     = nullptr;      ///< 全局云量 ×100
    QSpinBox*  cloud_layer_count_ = nullptr;    ///< 应用的分层数 (0=单层兼容)
    QComboBox* cloud_layer_type_[4] = {};       ///< 各层类型
    QSlider*   cloud_layer_cover_[4] = {};      ///< 各层覆盖 ×100
    QCheckBox* cloud_layer_on_[4]  = {};        ///< 各层开关
    QDoubleSpinBox* cloud_layer_alt_[4] = {};   ///< 各层高度 (m)
    QLabel*    cloud_readout_   = nullptr;

    // ---- 海洋面板控件 ----
    QSlider*        ocean_wind_      = nullptr; ///< 风速 ×10 (0.1 m/s 精度)
    QDoubleSpinBox* ocean_wind_az_   = nullptr; ///< 风向方位角 (度)
    QSlider*        ocean_wave_hs_   = nullptr; ///< 手动有效波高 ×100
    QCheckBox*      ocean_auto_hs_   = nullptr; ///< 自动海况 (跟随风)
    QDoubleSpinBox* ocean_swell_h_   = nullptr; ///< 涌浪波高 (m)
    QDoubleSpinBox* ocean_swell_len_ = nullptr; ///< 涌浪波长 (m)
    QDoubleSpinBox* ocean_swell_dir_ = nullptr; ///< 涌浪偏角 (度)
    QComboBox*      ocean_reflect_   = nullptr; ///< 反射档位 0/1
    QCheckBox*      ocean_curvature_ = nullptr; ///< 球面曲率
    QSpinBox*       ocean_harmonics_ = nullptr; ///< 谐波数 4..16
    QLabel*         ocean_readout_   = nullptr;

    // ---- 状态 / 日志 ----
    QPlainTextEdit* log_view_ = nullptr;
    QLabel*         fps_label_ = nullptr;

    bool pushing_ = false;   ///< 面板→模块推送期间抑制回读覆盖
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_ENVLABWINDOW_H
