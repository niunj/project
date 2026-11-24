#pragma once
/// @file LauncherWindow.h
/// @brief 进程管理器导航页 — 通过 QProcess 启动各子进程。
///
/// 导航页常驻, 卡片徽标实时显示进程运行状态 (运行中/未运行/已崩溃)。
/// 子进程退出后自动更新状态, 支持手动重启。
/// 集成全局健康面板 (HealthPanel) 显示所有进程遥测。

#include <QColor>
#include <QFrame>
#include <QMap>
#include <QProcess>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace vsgraysim { namespace gui {

class HealthPanel;

/// 圆角渐变图标徽章
class IconBadge : public QWidget {
    Q_OBJECT
public:
    enum class Glyph { Scene, Simulation, Infrared, Radar, SAR };

    explicit IconBadge(QWidget* parent = nullptr);
    QSize sizeHint() const override { return {84, 84}; }

    void setGlyph(Glyph g)          { glyph_ = g; update(); }
    void setAccent(const QColor& c) { accent_ = c; update(); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Glyph  glyph_ = Glyph::Scene;
    QColor accent_ = QColor("#4a90d9");
};

/// 功能入口卡片
class ModuleCard : public QFrame {
    Q_OBJECT
public:
    ModuleCard(const QString& title, const QString& desc,
               const QString& badge, const QColor& accent,
               QWidget* parent = nullptr);

    void setIconGlyph(IconBadge::Glyph g);
    void setBadge(const QString& text);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    IconBadge* icon_   = nullptr;
    QLabel*    badge_  = nullptr;
    QColor     accent_;
    bool       hovered_ = false;
};

/// 进程管理器导航页 — 三软件进程 + 全局健康面板
class LauncherWindow : public QWidget {
    Q_OBJECT
public:
    explicit LauncherWindow(QWidget* parent = nullptr);

    /// 设置子进程可执行文件目录
    void setProcessDir(const QString& dir) { process_dir_ = dir; }

    /// 终止所有子进程
    void terminateAllProcesses();

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void closeEvent(QCloseEvent*) override;

private:
    void buildUi();

    /// 启动/激活进程
    void launchProcess(const QString& process_name, const QString& exec_name,
                       ModuleCard* card, const QStringList& args = {});

    /// 更新卡片状态
    void updateCardStatus(ModuleCard* card, const QString& status);

    // 进程管理
    QString process_dir_;  ///< 可执行文件目录
    QMap<QString, QProcess*> processes_;  ///< process_name -> QProcess*

    // 卡片
    ModuleCard* card_scene_   = nullptr;
    ModuleCard* card_sim_     = nullptr;
    ModuleCard* card_ctrl_    = nullptr;  ///< 显控装配软件 (雷达/SAR/红外统一入口)

    // 健康面板
    HealthPanel*   health_panel_ = nullptr;
    QPushButton*   btn_health_   = nullptr;
    QVBoxLayout*   root_layout_  = nullptr;
    bool           health_visible_ = false;
};

}} // namespace vsgraysim::gui