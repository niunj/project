#include "BaseWindow.h"
#include "DockBuilder.h"
#include "ThemeManager.h"

#include <telemetry/TelemetryManager.h>

#include <QAction>
#include <QApplication>
#include <QKeySequence>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QShowEvent>
#include <QSettings>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTabWidget>
#include <cmath>

namespace vsgraysim { namespace ui {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
BaseWindow::BaseWindow(const QString& window_title, QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(window_title);
    resize(1680, 920);
    setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks |
                   QMainWindow::AnimatedDocks | QMainWindow::GroupedDragging);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

    // 应用统一暗色主题
    ThemeManager::applyDarkTheme(qApp);
}

BaseWindow::~BaseWindow() = default;

// ---------------------------------------------------------------------------
// Base status bar
// ---------------------------------------------------------------------------
void BaseWindow::buildBaseStatusBar()
{
    if(sb_frame_) return;
    auto* sb = statusBar();

    sb_frame_ = new QLabel(tr("帧 0"), this);
    sb_fps_   = new QLabel(tr("刷新率 —"), this);
    sb_frame_->setAccessibleName(tr("当前仿真帧号"));
    sb_fps_->setAccessibleName(tr("界面刷新率"));

    for (auto* lbl : {sb_frame_, sb_fps_}) {
        lbl->setStyleSheet("padding: 2px 8px; color: #9aa5b8;"
                           "font-family: monospace;");
        sb->addPermanentWidget(lbl);
    }
}

// ---------------------------------------------------------------------------
// Base menu bar
// ---------------------------------------------------------------------------
void BaseWindow::buildBaseMenuBar()
{
    if(view_menu_) return;
    auto* mb = menuBar();

    // ---- 文件 ----
    auto* mFile = mb->addMenu(tr("&文件"));
    act_home_ = mFile->addAction(tr("关闭当前窗口"));
    act_home_->setShortcut(QKeySequence::Close);
    act_home_->setStatusTip(tr("关闭当前软件窗口"));
    connect(act_home_, &QAction::triggered, this, [this]() { close(); });
    mFile->addSeparator();
    // Route through closeEvent so product-level unsaved changes can veto exit.
    mFile->addAction(tr("&退出"), this, [this]() { close(); }, QKeySequence::Quit);

    // ---- 视图 ----
    view_menu_ = mb->addMenu(tr("&视图"));
    panels_menu_ = view_menu_->addMenu(tr("面板"));
    toolbars_menu_ = view_menu_->addMenu(tr("工具栏"));
    connect(view_menu_, &QMenu::aboutToShow, this, &BaseWindow::populateViewMenu);
    view_menu_->addSeparator();
    act_reset_camera_ = view_menu_->addAction(tr("重置相机"));
    act_reset_camera_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Home));
    act_reset_camera_->setStatusTip(tr("恢复三维视口的默认观察位置"));
    view_menu_->addSeparator();
    act_save_layout_ = view_menu_->addAction(tr("保存当前布局"), this, &BaseWindow::saveWorkspaceLayout);
    act_save_layout_->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
    act_restore_layout_ = view_menu_->addAction(tr("恢复已保存布局"), this, &BaseWindow::restoreWorkspaceLayout);
    setWorkspacePersistenceEnabled(workspace_persistence_enabled_);
    auto* reset = view_menu_->addAction(tr("恢复默认布局"), this, &BaseWindow::resetWorkspaceLayout);
    reset->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_0));
    act_lock_layout_ = view_menu_->addAction(tr("锁定面板布局"));
    act_lock_layout_->setCheckable(true);
    connect(act_lock_layout_, &QAction::toggled, this, &BaseWindow::setLayoutLocked);
    act_focus_workspace_ = view_menu_->addAction(tr("专注工作区"));
    act_focus_workspace_->setCheckable(true);
    act_focus_workspace_->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F));
    connect(act_focus_workspace_, &QAction::toggled, this, &BaseWindow::setFocusWorkspace);
    act_fullscreen_ = view_menu_->addAction(tr("全屏"));
    act_fullscreen_->setCheckable(true);
    act_fullscreen_->setShortcut(QKeySequence(Qt::Key_F11));
    connect(act_fullscreen_, &QAction::toggled, this, [this](bool enabled) {
        if(enabled) showFullScreen(); else showNormal();
    });
    act_compact_ = view_menu_->addAction(tr("紧凑控件间距"));
    act_compact_->setCheckable(true);
    act_compact_->setChecked(ThemeManager::compactDensity());
    connect(act_compact_, &QAction::toggled, this, [](bool compact) {
        ThemeManager::setCompactDensity(qApp,compact);
    });

    // ---- 帮助 ----
    auto* mHelp = mb->addMenu(tr("&帮助"));
    mHelp->addAction(tr("操作快捷键"), this, [this]() {
        QMessageBox::information(this,tr("操作快捷键"),
            tr("Ctrl+O  打开场景\nCtrl+S  保存场景或工作台\n"
               "Ctrl+Home  重置相机\nCtrl+Alt+S  保存面板布局\n"
               "Ctrl+Shift+0  恢复默认布局\nCtrl+Alt+F  专注工作区\n"
               "F11  全屏\n\n关闭的面板可在“视图 → 面板”中重新打开。"));
    });
    mHelp->addAction(tr("&关于..."), this, [this]() {
        const auto version=QCoreApplication::applicationVersion();
        QMessageBox::about(this,tr("关于 %1").arg(windowTitle()),
            tr("VSG 地球仿真工作套件\n场景设计 · 仿真主控 · 显控工作台\n\n%1")
                .arg(version.isEmpty() ? tr("当前构建") : tr("版本 %1").arg(version)));
    });

    // 允许派生类添加专属菜单
    buildProductMenuItems(mb);
}

// ---------------------------------------------------------------------------
// Base tool bar
// ---------------------------------------------------------------------------
void BaseWindow::buildBaseToolBar()
{
    if(findChild<QToolBar*>(QStringLiteral("Product.PrimaryToolbar"))) return;
    auto* tb = addToolBar(tr("主工具栏"));
    tb->setObjectName(QStringLiteral("Product.PrimaryToolbar"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);

    if (act_home_) tb->addAction(act_home_);
    tb->addSeparator();

    if(!act_reset_camera_) {
        act_reset_camera_=new QAction(tr("重置相机"),this);
        act_reset_camera_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Home));
    }
    tb->addAction(act_reset_camera_);
    tb->addSeparator();

    // 允许派生类添加专属工具
    buildProductToolBarItems(tb);
}

// ---------------------------------------------------------------------------
// Dock management
// ---------------------------------------------------------------------------
QVBoxLayout* BaseWindow::createDock(const QString& title,
                                     Qt::DockWidgetArea area, int min_width,const QString& persistent_id)
{
    return DockBuilder::createDock(this, title, area, min_width,persistent_id);
}

// ---------------------------------------------------------------------------
// Status bar updates
// ---------------------------------------------------------------------------
void BaseWindow::setFrameLabel(uint64_t frame)
{
    if (sb_frame_)
        sb_frame_->setText(tr("帧 %1").arg(frame));
}

void BaseWindow::setFpsLabel(double fps)
{
    if (sb_fps_)
        sb_fps_->setText(std::isfinite(fps) && fps>=0 ? tr("刷新率 %1 Hz").arg(fps,0,'f',1) : tr("刷新率 —"));
    // 遥测上报
    if (telemetry_ && std::isfinite(fps) && fps>=0)
        telemetry_->setFps(fps);
}

QString BaseWindow::workspaceSettingsGroup() const
{
    return QStringLiteral("workspaces/%1/%2").arg(QString::fromLatin1(metaObject()->className()),workspace_id_);
}

void BaseWindow::setWorkspaceId(const QString& id)
{
    const auto value=id.trimmed().isEmpty() ? QStringLiteral("default") : id.trimmed();
    if(value==workspace_id_) return;
    if(workspace_ready_ && workspace_persistence_enabled_) saveWorkspaceLayout();
    workspace_id_=value;
    workspace_ready_=false;
    default_workspace_state_.clear();
}

void BaseWindow::setWorkspacePersistenceEnabled(bool enabled)
{
    workspace_persistence_enabled_=enabled;
    for(auto* action:{act_save_layout_,act_restore_layout_}) if(action) {
        action->setEnabled(enabled);
        action->setStatusTip(enabled ? QString() : tr("当前工作台使用自有布局文件，请使用工作台导出功能"));
        action->setToolTip(enabled ? action->text() : tr("请使用“导出工作台”保存当前面板布局"));
    }
}

void BaseWindow::normalizeWorkspaceObjects()
{
    int index=0;
    for(auto* dock:findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly)) {
        if(dock->objectName().isEmpty()) dock->setObjectName(QStringLiteral("Product.Dock.%1").arg(index));
        dock->setAccessibleName(dock->windowTitle());
        ++index;
    }
    index=0;
    for(auto* toolbar:findChildren<QToolBar*>(QString(),Qt::FindDirectChildrenOnly)) {
        if(toolbar->objectName().isEmpty()) toolbar->setObjectName(QStringLiteral("Product.Toolbar.%1").arg(index));
        ++index;
    }
}

void BaseWindow::finalizeWorkspace(bool capture_default)
{
    if(capture_default) setFocusWorkspace(false);
    normalizeWorkspaceObjects();
    if(capture_default || !workspace_ready_) {
        const bool first=!workspace_ready_;
        default_workspace_state_=saveState(1);
        workspace_ready_=true;
        if(first && workspace_persistence_enabled_) restoreWorkspaceLayout();
    }
    populateViewMenu();
}

void BaseWindow::populateViewMenu()
{
    normalizeWorkspaceObjects();
    if(panels_menu_) {
        panels_menu_->clear();
        for(auto* dock:findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly))
            panels_menu_->addAction(dock->toggleViewAction());
        panels_menu_->setEnabled(!panels_menu_->isEmpty());
    }
    if(toolbars_menu_) {
        toolbars_menu_->clear();
        for(auto* toolbar:findChildren<QToolBar*>(QString(),Qt::FindDirectChildrenOnly))
            toolbars_menu_->addAction(toolbar->toggleViewAction());
        toolbars_menu_->setEnabled(!toolbars_menu_->isEmpty());
    }
    if(act_fullscreen_) { const QSignalBlocker blocker(act_fullscreen_); act_fullscreen_->setChecked(isFullScreen()); }
    if(act_compact_) { const QSignalBlocker blocker(act_compact_); act_compact_->setChecked(ThemeManager::compactDensity()); }
    if(act_restore_layout_) {
        QSettings settings(QStringLiteral("vsgraysim"),QStringLiteral("ProductUi"));
        act_restore_layout_->setEnabled(workspace_persistence_enabled_ && settings.contains(workspaceSettingsGroup()+QStringLiteral("/state")));
    }
    if(layout_locked_) setLayoutLocked(true); // also covers newly assembled docks
}

void BaseWindow::setLayoutLocked(bool locked)
{
    layout_locked_=locked;
    for(auto* dock:findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly)) {
        if(!dock->property("productUnlockedFeatures").isValid())
            dock->setProperty("productUnlockedFeatures",int(dock->features()));
        auto features=QDockWidget::DockWidgetFeatures::fromInt(dock->property("productUnlockedFeatures").toInt());
        if(locked) features&=~(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        dock->setFeatures(features);
    }
}

void BaseWindow::setFocusWorkspace(bool focus)
{
    if(workspace_focused_==focus) return;
    workspace_focused_=focus;
    if(focus) {
        focus_hidden_docks_.clear();
        for(auto* dock:findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly)) if(dock->isVisible() && !dock->property("workspacePrimary").toBool()) {
            focus_hidden_docks_.push_back(dock); dock->hide();
        }
    } else {
        for(auto dock:focus_hidden_docks_) if(dock) dock->show();
        focus_hidden_docks_.clear();
    }
    if(act_focus_workspace_) { const QSignalBlocker blocker(act_focus_workspace_); act_focus_workspace_->setChecked(focus); }
}

void BaseWindow::saveWorkspaceLayout()
{
    if(!workspace_persistence_enabled_ || !workspace_ready_) return;
    const bool wasFocused=workspace_focused_;
    setFocusWorkspace(false);
    normalizeWorkspaceObjects();
    QSettings settings(QStringLiteral("vsgraysim"),QStringLiteral("ProductUi"));
    settings.beginGroup(workspaceSettingsGroup());
    settings.setValue(QStringLiteral("geometry"),saveGeometry());
    settings.setValue(QStringLiteral("state"),saveState(1));
    settings.setValue(QStringLiteral("locked"),layout_locked_);
    settings.sync();
    setFocusWorkspace(wasFocused);
    statusBar()->showMessage(settings.status()==QSettings::NoError ? tr("工作区布局已保存") : tr("无法写入工作区布局"),2500);
}

void BaseWindow::restoreWorkspaceLayout()
{
    if(!workspace_persistence_enabled_) return;
    setFocusWorkspace(false);
    QSettings settings(QStringLiteral("vsgraysim"),QStringLiteral("ProductUi"));
    settings.beginGroup(workspaceSettingsGroup());
    const auto geometry=settings.value(QStringLiteral("geometry")).toByteArray();
    const auto state=settings.value(QStringLiteral("state")).toByteArray();
    if(!geometry.isEmpty()) restoreGeometry(geometry);
    if(!state.isEmpty() && !restoreState(state,1)) {
        if(!default_workspace_state_.isEmpty()) restoreState(default_workspace_state_,1);
        statusBar()->showMessage(tr("保存的布局已失效，已恢复默认面板"),3500);
    }
    setLayoutLocked(settings.value(QStringLiteral("locked"),false).toBool());
    if(act_lock_layout_) { const QSignalBlocker blocker(act_lock_layout_); act_lock_layout_->setChecked(layout_locked_); }
}

void BaseWindow::resetWorkspaceLayout()
{
    setFocusWorkspace(false);
    if(!default_workspace_state_.isEmpty()) restoreState(default_workspace_state_,1);
    setLayoutLocked(false);
    if(act_lock_layout_) act_lock_layout_->setChecked(false);
    statusBar()->showMessage(tr("已恢复默认布局"),2500);
}

void BaseWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    finalizeWorkspace();
}

void BaseWindow::closeEvent(QCloseEvent* event)
{
    QMainWindow::closeEvent(event);
    if(event->isAccepted()) saveWorkspaceLayout();
}

}} // namespace vsgraysim::ui
