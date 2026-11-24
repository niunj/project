#pragma once
/// @file BaseWindow.h
/// @brief Qt6 产品窗口基类 —— 统一菜单栏/工具栏/状态栏/Dock 管理。
///
/// 所有产品软件窗口 (地形编辑/场景设计/仿真控制) 继承本类，获得：
///   - 统一暗色主题
///   - 公共菜单项 (返回导航页、视图Dock切换、关于)
///   - 公共工具栏 (导航页、重置相机)
///   - 公共状态栏 (帧号、FPS)
///   - 标准化 Dock 创建
///   - 可选的 TelemetryManager 集成 (遥测上报)
///
/// 使用方式 (在派生类构造函数中):
///   1. 构造函数先调用 BaseWindow(title, parent)
///   2. 创建中央控件 (Viewport3DView 等)
///   3. 依次调用 buildBaseStatusBar(), buildBaseMenuBar(), buildBaseToolBar()
///   4. 调用 buildProductDocks() 创建产品专属的多个 Dock

#include <QMainWindow>
#include <QLabel>
#include <QString>
#include <QByteArray>
#include <QPointer>
#include <QList>

#include <memory>

class QAction;
class QMenu;
class QMenuBar;
class QStatusBar;
class QToolBar;
class QVBoxLayout;
class QCloseEvent;
class QShowEvent;
class QDockWidget;

namespace vsgraysim::telemetry {
class TelemetryManager;
}

namespace vsgraysim { namespace ui {

class BaseWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit BaseWindow(const QString& window_title,
                        QWidget* parent = nullptr);
    ~BaseWindow() override;

    // ---- TelemetryManager 集成 ----

    /// 设置遥测管理器 (可选, 用于进程健康上报)
    void setTelemetryManager(telemetry::TelemetryManager* mgr) { telemetry_ = mgr; }

    /// 获取遥测管理器
    telemetry::TelemetryManager* telemetryManager() const { return telemetry_; }

    // ---- 公共构建块 (在派生类构造函数中按顺序调用) ----

    /// 构建公共状态栏: 帧号 + FPS 标签。
    void buildBaseStatusBar();

    /// 构建公共菜单栏: 文件(返回导航页) + 视图(Dock切换+重置相机) + 帮助(关于)。
    /// 派生类可在 buildProductMenuItems() 中扩展。
    void buildBaseMenuBar();

    /// 构建公共工具栏: 导航页 + 重置相机按钮。
    /// 派生类可在 buildProductToolBarItems() 中扩展。
    void buildBaseToolBar();

    // ---- Dock 管理 ----

    /// 创建一个标准分类 Dock (含滚动区域), 返回内容布局。
    QVBoxLayout* createDock(const QString& title,
                            Qt::DockWidgetArea area = Qt::LeftDockWidgetArea,
                            int min_width = 0,
                            const QString& persistent_id = {});

    // ---- 状态栏更新 ----

    void setFrameLabel(uint64_t frame);
    void setFpsLabel(double fps);

    // ---- 公共 Action 访问 ----

    QAction* homeAction() const { return act_home_; }
    QAction* resetCameraAction() const { return act_reset_camera_; }
    QMenu* viewMenu() const { return view_menu_; }

    /// Capture the assembled default layout, assign stable dock names and
    /// restore this product's saved workspace. Call again after rebuilding docks.
    void finalizeWorkspace(bool capture_default = false);
    void setWorkspaceId(const QString& id);
    void setWorkspacePersistenceEnabled(bool enabled);

public slots:
    void saveWorkspaceLayout();
    void restoreWorkspaceLayout();
    void resetWorkspaceLayout();

public:
    // ---- 扩展点 (派生类覆写) ----

    /// 在产品专属菜单栏中添加菜单项 (在文件/视图/帮助菜单之后)。
    virtual void buildProductMenuItems(QMenuBar* mb) {}

    /// 在产品专属工具栏中添加工具按钮 (在导航页/重置相机按钮之后)。
    virtual void buildProductToolBarItems(QToolBar* tb) {}

    /// 构建产品专属的 Dock 面板。
    virtual void buildProductDocks() {}

    // ---- 公共状态栏标签 (派生类可直接访问) ----

    QLabel* sb_frame_ = nullptr;
    QLabel* sb_fps_   = nullptr;

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    QString workspaceSettingsGroup() const;
    void normalizeWorkspaceObjects();
    void populateViewMenu();
    void setLayoutLocked(bool locked);
    void setFocusWorkspace(bool focus);
    QAction* act_home_          = nullptr;
    QAction* act_reset_camera_  = nullptr;
    QAction* act_save_layout_ = nullptr;
    QAction* act_restore_layout_ = nullptr;
    QAction* act_lock_layout_ = nullptr;
    QAction* act_focus_workspace_ = nullptr;
    QAction* act_fullscreen_ = nullptr;
    QAction* act_compact_ = nullptr;
    QMenu* view_menu_ = nullptr;
    QMenu* panels_menu_ = nullptr;
    QMenu* toolbars_menu_ = nullptr;
    QByteArray default_workspace_state_;
    QString workspace_id_ = QStringLiteral("default");
    QList<QPointer<QDockWidget>> focus_hidden_docks_;
    bool workspace_ready_ = false;
    bool workspace_persistence_enabled_ = true;
    bool layout_locked_ = false;
    bool workspace_focused_ = false;
    telemetry::TelemetryManager* telemetry_ = nullptr;
};

}} // namespace vsgraysim::ui
