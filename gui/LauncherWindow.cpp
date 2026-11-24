/// @file LauncherWindow.cpp
/// @brief 进程管理器导航页实现 — 通过 QProcess 启动子进程。

#include "LauncherWindow.h"
#include "HealthPanel.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cmath>

#include <spdlog/spdlog.h>

namespace vsgraysim { namespace gui {

namespace {

const char* kLauncherQss = R"QSS(
QWidget#LauncherRoot { background: transparent; }
QLabel { background: transparent; }
QPushButton {
  background: #243044; color: #dbe4f0; border: 1px solid #34425c;
  border-radius: 5px; padding: 6px 16px; font-size: 13px;
}
QPushButton:hover { background: #2e3d57; }
QPushButton:pressed { background: #3a4c6e; }
)QSS";

void paintCardBase(QPainter& p, const QRect& r, bool hovered)
{
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, r.top(), 0, r.bottom());
    if (hovered) {
        g.setColorAt(0.0, QColor("#232b3b"));
        g.setColorAt(1.0, QColor("#1b2230"));
    } else {
        g.setColorAt(0.0, QColor("#1c2330"));
        g.setColorAt(1.0, QColor("#161c27"));
    }
    p.setBrush(g);
    p.setPen(QPen(hovered ? QColor("#4a90d9") : QColor("#2a3040"), hovered ? 2.0 : 1.0));
    p.drawRoundedRect(QRectF(r).adjusted(1, 1, -1, -1), 14, 14);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// IconBadge
// ---------------------------------------------------------------------------
IconBadge::IconBadge(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(84, 84);
    setMaximumSize(84, 84);
}

void IconBadge::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect().adjusted(2, 2, -2, -2);

    QLinearGradient g(r.topLeft(), r.bottomRight());
    g.setColorAt(0.0, accent_.lighter(125));
    g.setColorAt(1.0, accent_.darker(135));
    p.setBrush(g);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(r, 18, 18);

    QRadialGradient hl(r.center().x(), r.top() + r.height() * 0.2, r.width() * 0.8);
    hl.setColorAt(0.0, QColor(255, 255, 255, 60));
    hl.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setBrush(hl);
    p.drawRoundedRect(r, 18, 18);

    p.setPen(QPen(QColor(255, 255, 255, 235), 3.0, Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    const double cx = r.center().x(), cy = r.center().y();
    const double s  = r.width();

    switch (glyph_) {
    case Glyph::Scene: {
        const double w = s * 0.28, h = s * 0.16, d = s * 0.30;
        QPainterPath cube;
        cube.moveTo(cx, cy - h - d * 0.5);
        cube.lineTo(cx + w, cy - d * 0.5);
        cube.lineTo(cx, cy + h - d * 0.5);
        cube.lineTo(cx - w, cy - d * 0.5);
        cube.closeSubpath();
        cube.moveTo(cx - w, cy - d * 0.5);
        cube.lineTo(cx - w, cy + d * 0.5);
        cube.lineTo(cx, cy + h + d * 0.5);
        cube.lineTo(cx, cy + h - d * 0.5);
        cube.moveTo(cx + w, cy - d * 0.5);
        cube.lineTo(cx + w, cy + d * 0.5);
        cube.lineTo(cx, cy + h + d * 0.5);
        p.drawPath(cube);
        break;
    }
    case Glyph::Infrared: {
        p.setBrush(QColor(255, 100, 0, 200));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(cx, cy), s * 0.28, s * 0.28);
        p.setBrush(QColor(255, 150, 0, 180));
        p.drawRect(QRectF(cx - s * 0.15, cy + s * 0.32, s * 0.3, s * 0.08));
        const double r = s * 0.4;
        for (int i = 0; i < 8; ++i) {
            double a = i * M_PI / 4;
            p.drawLine(QPointF(cx, cy),
                       QPointF(cx + r * std::cos(a), cy - r * std::sin(a)));
        }
        break;
    }
    case Glyph::Radar: {
        const double R = s * 0.34;
        p.drawArc(QRectF(cx - R, cy - R, 2 * R, 2 * R), 10 * 16, 340 * 16);
        const double r2 = R * 0.5;
        p.drawArc(QRectF(cx - r2, cy - r2, 2 * r2, 2 * r2), 10 * 16, 340 * 16);
        for (int i = 0; i < 4; ++i) {
            double a0 = i * M_PI / 2;
            double a1 = a0 + M_PI / 8;
            p.drawLine(QPointF(cx, cy),
                       QPointF(cx + R * std::cos(a0), cy - R * std::sin(a0)));
            p.drawLine(QPointF(cx, cy),
                       QPointF(cx + R * std::cos(a1), cy - R * std::sin(a1)));
        }
        p.setBrush(QColor(255, 255, 255, 235));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(cx, cy), 4, 4);
        break;
    }
    case Glyph::SAR: {
        for (int i = 1; i <= 4; ++i) {
            double ri = s * 0.1 * i;
            p.drawEllipse(QPointF(cx, cy), ri, ri);
        }
        p.setBrush(QColor(0, 150, 255, 200));
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(cx - s * 0.3, cy - s * 0.04, s * 0.6, s * 0.08));
        p.drawRect(QRectF(cx - s * 0.15, cy - s * 0.04, s * 0.3, s * 0.08));
        p.setPen(QPen(QColor(0, 150, 255), 1.5, Qt::DashLine));
        p.drawLine(cx - s * 0.3, cy, cx + s * 0.3, cy);
        p.drawLine(cx, cy - s * 0.3, cx, cy + s * 0.3);
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// ModuleCard
// ---------------------------------------------------------------------------
ModuleCard::ModuleCard(const QString& title, const QString& desc,
                       const QString& badge, const QColor& accent,
                       QWidget* parent)
    : QFrame(parent), accent_(accent)
{
    setMinimumSize(260, 210);
    setMaximumHeight(320);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(24, 28, 24, 22);
    v->setSpacing(12);

    icon_ = new IconBadge(this);
    icon_->setAccent(accent);
    v->addWidget(icon_, 0, Qt::AlignLeft);

    auto* lbl_title = new QLabel(title, this);
    lbl_title->setStyleSheet("font-size: 19px; font-weight: 600; color: #e8ecf3;");
    v->addWidget(lbl_title);

    auto* lbl_desc = new QLabel(desc, this);
    lbl_desc->setWordWrap(true);
    lbl_desc->setStyleSheet("font-size: 12.5px; color: #8b95a6; line-height: 1.5;");
    v->addWidget(lbl_desc);

    v->addStretch(1);

    badge_ = new QLabel(badge, this);
    badge_->setStyleSheet(QString(
        "font-size: 11px; color: %1; background: rgba(%2,%3,%4,0.12); "
        "border: 1px solid rgba(%2,%3,%4,0.35); border-radius: 4px; "
        "padding: 3px 10px;")
        .arg(accent.lighter(140).name())
        .arg(accent.red()).arg(accent.green()).arg(accent.blue()));
    v->addWidget(badge_, 0, Qt::AlignLeft);

    for (QObject* c : children())
        if (c->isWidgetType()) c->installEventFilter(this);
}

void ModuleCard::setIconGlyph(IconBadge::Glyph g) { icon_->setGlyph(g); }
void ModuleCard::setBadge(const QString& text) { if (badge_) badge_->setText(text); }

void ModuleCard::paintEvent(QPaintEvent*) {
    QPainter p(this);
    paintCardBase(p, rect(), hovered_);
}

void ModuleCard::enterEvent(QEnterEvent*) { hovered_ = true;  update(); }
void ModuleCard::leaveEvent(QEvent*)      { hovered_ = false; update(); }

void ModuleCard::mouseReleaseEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::LeftButton && rect().contains(ev->position().toPoint()))
        emit clicked();
    QFrame::mouseReleaseEvent(ev);
}

bool ModuleCard::eventFilter(QObject* obj, QEvent* ev) {
    if (obj != this && ev->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::LeftButton) {
            emit clicked();
            return true;
        }
    }
    return QFrame::eventFilter(obj, ev);
}

// ---------------------------------------------------------------------------
// LauncherWindow
// ---------------------------------------------------------------------------
LauncherWindow::LauncherWindow(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("LauncherRoot");
    setWindowTitle(tr("vsgraysim — 进程管理器"));
    resize(1000, 600);
    setMinimumSize(900, 720);

    qApp->setStyle("Fusion");
    qApp->setStyleSheet(QString::fromUtf8(kLauncherQss));

    buildUi();
    spdlog::info("LauncherWindow (process manager): built");
}

void LauncherWindow::buildUi()
{
    root_layout_ = new QVBoxLayout(this);
    root_layout_->setContentsMargins(56, 34, 56, 20);
    root_layout_->setSpacing(0);

    // 顶栏: 品牌 + 健康监视
    auto* topbar = new QHBoxLayout;
    auto* lbl_brand = new QLabel(tr("vsgraysim"), this);
    lbl_brand->setStyleSheet(
        "font-size: 30px; font-weight: 700; color: #f0f3f8; letter-spacing: 1px;");
    topbar->addWidget(lbl_brand);
    auto* lbl_sub = new QLabel(
        tr("多光谱装备仿真工作台  ·  三软件入口 · 显控装配统一入口"), this);
    lbl_sub->setStyleSheet("font-size: 12.5px; color: #7f8a9c; margin-left: 12px;");
    topbar->addWidget(lbl_sub);
    topbar->addStretch();
    btn_health_ = new QPushButton(tr("📊 进程监视"), this);
    btn_health_->setStyleSheet(
        "QPushButton { background-color: #1a2744; color: #7ab7ff;"
        "  border: 1px solid #2a4570; border-radius: 5px; padding: 6px 16px;"
        "  font-size: 13px; }"
        "QPushButton:hover { background-color: #243d5e; }");
    topbar->addWidget(btn_health_);
    root_layout_->addLayout(topbar);
    root_layout_->addSpacing(22);

    auto* grid = new QGridLayout;
    grid->setSpacing(22);

    // 第一行: 场景设计 (含地形编辑能力) / 仿真控制
    card_scene_ = new ModuleCard(
        tr("场景设计软件"),
        tr("程序化地形生成、笔刷雕刻、水面系统、地形分析与工程存取;\n"
           "实体部署、变换摆放、传感器孔径/波段发射参数编辑与场景 JSON 存取。"),
        tr("未运行"), QColor("#3fb68b"), this);
    card_scene_->setIconGlyph(IconBadge::Glyph::Scene);

    card_sim_ = new ModuleCard(
        tr("仿真控制软件"),
        tr("装载场景 · 时序子系统驱动 · RT 射线追踪数据生成 ·\n"
           "回波/辐射亮度经 GPU 句柄分发给各显控软件。"),
        tr("未运行"), QColor("#4a90d9"), this);
    card_sim_->setIconGlyph(IconBadge::Glyph::Simulation);

    grid->addWidget(card_scene_, 0, 0);
    grid->addWidget(card_sim_,   0, 1);

    // 第二行: 显控装配软件 (雷达/SAR/红外统一入口)
    card_ctrl_ = new ModuleCard(
        tr("大显控软件"),
        tr("连接场景仿真，统一选择飞机、雷达、红外、光学显控；\n"
           "独立设备界面与综合任务台共享数据连接。"),
        tr("未运行"), QColor("#6c5ce7"), this);
    card_ctrl_->setIconGlyph(IconBadge::Glyph::Radar);
    grid->addWidget(card_ctrl_, 1, 0, 1, 2);

    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    root_layout_->addLayout(grid);
    root_layout_->addSpacing(16);

    // 健康面板 (默认隐藏) — 显示时占满 "显控装配软件" 卡下方全部布局区域,
    // 隐藏时自动收起空间 (隐藏控件在布局中不占位), 其余控件随之适配大小。
    health_panel_ = new HealthPanel(this);
    health_panel_->setVisible(false);
    health_panel_->setMinimumHeight(220);
    root_layout_->addWidget(health_panel_, 1);

    // 底部版本信息
    auto* bottom_bar = new QHBoxLayout();
    auto* lbl_ver = new QLabel(
        tr("多进程独立部署  ·  Unix Domain Socket + POSIX 共享内存 IPC  ·  QProcess 生命周期管理"), this);
    lbl_ver->setStyleSheet("font-size: 11px; color: #5d6675;");
    bottom_bar->addStretch();
    bottom_bar->addWidget(lbl_ver, 0, Qt::AlignRight);
    root_layout_->addLayout(bottom_bar);

    connect(card_scene_, &ModuleCard::clicked, this, [this]() {
        launchProcess("scene_designer", "vsgraysim_scene_designer", card_scene_);
    });
    connect(card_sim_, &ModuleCard::clicked, this, [this]() {
        launchProcess("sim_control", "vsgraysim_sim_control", card_sim_);
    });
    // "显控装配软件" -> 统一显控, 启动即进入"装备/平台链接装配"界面
    connect(card_ctrl_, &ModuleCard::clicked, this, [this]() {
        launchProcess("ctrl_control", "vsgraysim_control", card_ctrl_);
    });

    // 健康面板切换
    connect(btn_health_, &QPushButton::clicked, this, [this]() {
        health_visible_ = !health_visible_;
        health_panel_->setVisible(health_visible_);
        btn_health_->setText(health_visible_ ? tr("🔴 隐藏监视") : tr("📊 进程监视"));
        if (health_visible_) {
            health_panel_->startMonitoring();
            // 展开以容纳健康面板
            if (height() < 760) resize(width(), 760);
        } else {
            health_panel_->stopMonitoring();
            // 收起时窗口高度收缩, 布局区域随之适配
            if (height() > 700) resize(width(), 700);
        }
    });
}

// ---------------------------------------------------------------------------
// 注: 三个独立显控(雷达/SAR/红外)已合并为统一的"显控装配软件"入口,
//     由 buildUi() 中的 card_ctrl_ 负责, 不再为每个传感器单独建卡。
// ---------------------------------------------------------------------------

void LauncherWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient bg(0, 0, 0, height());
    bg.setColorAt(0.0, QColor("#12161f"));
    bg.setColorAt(0.55, QColor("#161b26"));
    bg.setColorAt(1.0, QColor("#0e1219"));
    p.fillRect(rect(), bg);

    QRadialGradient glow(width() * 0.85, height() * 0.1, width() * 0.5);
    glow.setColorAt(0.0, QColor(74, 144, 217, 34));
    glow.setColorAt(1.0, QColor(74, 144, 217, 0));
    p.fillRect(rect(), glow);

    QRadialGradient glow2(width() * 0.1, height() * 0.95, width() * 0.4);
    glow2.setColorAt(0.0, QColor(63, 182, 139, 22));
    glow2.setColorAt(1.0, QColor(63, 182, 139, 0));
    p.fillRect(rect(), glow2);
}

void LauncherWindow::showEvent(QShowEvent* e)
{
    qApp->setStyle("Fusion");
    qApp->setStyleSheet(QString::fromUtf8(kLauncherQss));
    QWidget::showEvent(e);
}

void LauncherWindow::closeEvent(QCloseEvent* e)
{
    terminateAllProcesses();
    qApp->quit();
    QWidget::closeEvent(e);
}

// ---------------------------------------------------------------------------
// 进程管理
// ---------------------------------------------------------------------------
void LauncherWindow::launchProcess(const QString& process_name,
                                    const QString& exec_name,
                                    ModuleCard* card,
                                    const QStringList& args)
{
    // 检查是否已在运行
    if (processes_.contains(process_name) && processes_[process_name]) {
        auto* proc = processes_[process_name];
        if (proc->state() == QProcess::Running) {
            // 已运行 → 激活窗口 (通过 IPC 发送激活命令, 简化跳过)
            spdlog::info("Launcher: {} already running (PID {})",
                         process_name.toStdString(), proc->processId());
            return;
        }
        // 已退出 → 清理并重新启动
        processes_.remove(process_name);
    }

    QString exec_path;
    if (!process_dir_.isEmpty())
        exec_path = process_dir_ + "/" + exec_name;
    else
        exec_path = exec_name;

    auto* proc = new QProcess(this);

    // 进程退出时更新状态
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process_name, card](int exit_code, QProcess::ExitStatus status) {
        if (status == QProcess::CrashExit) {
            updateCardStatus(card, tr("已崩溃"));
            spdlog::warn("Launcher: {} crashed (exit code {})",
                         process_name.toStdString(), exit_code);
        } else {
            updateCardStatus(card, tr("已退出"));
            spdlog::info("Launcher: {} exited (code {})",
                         process_name.toStdString(), exit_code);
        }
        processes_.remove(process_name);
    });

    // 启动进程
    proc->start(exec_path, args);

    if (proc->waitForStarted(3000)) {
        processes_[process_name] = proc;
        updateCardStatus(card, tr("运行中"));
        spdlog::info("Launcher: started {} (PID {})",
                     process_name.toStdString(), proc->processId());
    } else {
        updateCardStatus(card, tr("启动失败"));
        spdlog::error("Launcher: failed to start {}: {}",
                      process_name.toStdString(), proc->errorString().toStdString());
        delete proc;
    }
}

void LauncherWindow::updateCardStatus(ModuleCard* card, const QString& status)
{
    if (card)
        card->setBadge(status);
}

void LauncherWindow::terminateAllProcesses()
{
    for (auto it = processes_.begin(); it != processes_.end(); ++it) {
        auto* proc = it.value();
        if (proc && proc->state() == QProcess::Running) {
            spdlog::info("Launcher: terminating {} (PID {})",
                         it.key().toStdString(), proc->processId());
            proc->terminate();
            if (!proc->waitForFinished(3000)) {
                proc->kill();
                proc->waitForFinished(2000);
            }
        }
    }
    processes_.clear();
}

}} // namespace vsgraysim::gui