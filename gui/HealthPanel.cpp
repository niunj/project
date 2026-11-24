/// @file HealthPanel.cpp
/// @brief Launcher 全局健康面板实现。

#include "HealthPanel.h"

#include <QDateTime>
#include <QButtonGroup>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace vsgraysim { namespace gui {

// 遥测消息类型枚举 (与 TelemetryManager 一致)
enum class TelemetryMsgType : uint8_t {
    Health = 0x01,
    Log    = 0x02,
    Fault  = 0x03,
    Metric = 0x04,
};

// ===========================================================================
// ProcessHealthCard
// ===========================================================================
ProcessHealthCard::ProcessHealthCard(const QString& process_name,
                                     QWidget* parent)
    : QWidget(parent), name_(process_name)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(8);

    // ===== 头部: 状态点 + 名称 + 状态胶囊 + 日志开关 =====
    auto* header = new QHBoxLayout();
    header->setSpacing(8);

    icon_ = new QLabel(this);
    icon_->setFixedSize(12, 12);
    icon_->setStyleSheet(
        "background-color: #2ecc71; border-radius: 6px;"
        "box-shadow: 0 0 6px #2ecc71;");

    title_ = new QLabel(process_name, this);
    title_->setStyleSheet(
        "font-weight: 700; font-size: 14px; color: #eef2f7; letter-spacing: 0.3px;");
    header->addWidget(icon_);
    header->addWidget(title_);
    header->addSpacing(4);

    pill_ = new QLabel("running", this);
    pill_->setStyleSheet(
        "background-color: #1b3a2e; color: #4ade80;"
        "border: 1px solid #2ecc71; border-radius: 10px;"
        "padding: 2px 10px; font-size: 11px;");
    header->addWidget(pill_);
    header->addStretch();

    btn_toggle_log_ = new QPushButton("日志 ▾", this);
    btn_toggle_log_->setStyleSheet(
        "QPushButton { background-color: #1c232e; color: #8aa0b8;"
        "  border: 1px solid #2c3650; border-radius: 6px; padding: 3px 10px;"
        "  font-size: 11px; }"
        "QPushButton:hover { background-color: #252f40; }");
    btn_toggle_log_->setCursor(Qt::PointingHandCursor);
    header->addWidget(btn_toggle_log_);
    layout->addLayout(header);

    // ===== 指标区: FPS / CPU / 内存 迷你条 + 运行时长 =====
    auto makeBar = [&](const QString& caption, QLabel*& val, QProgressBar*& bar,
                       const QString& bar_color) {
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* cap = new QLabel(caption, this);
        cap->setFixedWidth(44);
        cap->setStyleSheet("color: #6b789a; font-size: 11px;");
        row->addWidget(cap);
        bar = new QProgressBar(this);
        bar->setTextVisible(false);
        bar->setFixedHeight(6);
        bar->setStyleSheet(
            "QProgressBar { background-color: #161b26; border: none;"
            "  border-radius: 3px; }"
            "QProgressBar::chunk { background-color: " + bar_color + ";"
            "  border-radius: 3px; }");
        row->addWidget(bar, 1);
        val = new QLabel("--", this);
        val->setFixedWidth(64);
        val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        val->setStyleSheet("color: #d7dee8; font-family: monospace; font-size: 11px;");
        row->addWidget(val);
        layout->addLayout(row);
    };

    makeBar("FPS", fps_label_, fps_bar_, "#3b9dff");
    makeBar("CPU", cpu_label_, cpu_bar_, "#39d98a");
    makeBar("MEM", mem_label_, mem_bar_, "#f5a623");

    // 运行时长
    auto* up = new QHBoxLayout();
    auto* cap = new QLabel("UPTIME", this);
    cap->setStyleSheet("color:#6b789a; font-size:11px;");
    up->addWidget(cap);
    uptime_label_ = new QLabel("00:00:00", this);
    uptime_label_->setStyleSheet(
        "color: #9fb1c8; font-family: monospace; font-size: 11px;");
    up->addWidget(uptime_label_);
    up->addStretch();
    layout->addLayout(up);

    // ===== 日志视图 (默认折叠) =====
    log_view_ = new QTextEdit(this);
    log_view_->setReadOnly(true);
    log_view_->setMaximumHeight(120);
    log_view_->setStyleSheet(
        "QTextEdit {"
        "  background-color: #0a0e14;"
        "  color: #c6ccd8;"
        "  font-family: monospace;"
        "  font-size: 10.5px;"
        "  border: 1px solid #222a38;"
        "  border-radius: 6px;"
        "  padding: 4px;"
        "}");
    log_view_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    log_view_->setVisible(false);
    layout->addWidget(log_view_);

    connect(btn_toggle_log_, &QPushButton::clicked, this, [this]() {
        log_visible_ = !log_visible_;
        log_view_->setVisible(log_visible_);
        btn_toggle_log_->setText(log_visible_ ? "日志 ▴" : "日志 ▾");
        setMinimumHeight(log_visible_ ? 230 : 150);
        if (log_visible_) {
            auto* sb = log_view_->verticalScrollBar();
            sb->setValue(sb->maximum());
        }
    });

    // 卡片整体样式
    setStyleSheet(
        "ProcessHealthCard {"
        "  background-color: #141b27;"
        "  border: 1px solid #253047;"
        "  border-radius: 10px;"
        "}");
    setMinimumHeight(150);
}

void ProcessHealthCard::updateHealth(const QString& status, double fps,
                                     double cpu, size_t mem_bytes,
                                     double uptime_sec, int64_t /*ts*/)
{
    last_status_ = status;

    // 状态点 + 状态胶囊颜色
    QString dot, bg, fg, border;
    if (status == "running" || status == "ok") {
        dot  = "#2ecc71"; bg = "#153227"; fg = "#4ade80"; border = "#2ecc71";
    } else if (status == "warning" || status == "degraded") {
        dot  = "#f5a623"; bg = "#2f2610"; fg = "#f7b731"; border = "#f5a623";
    } else if (status == "error" || status == "fault" || status == "stopped") {
        dot  = "#ef5350"; bg = "#2e1518"; fg = "#ff6b6b"; border = "#ef5350";
    } else if (status == "offline") {
        dot  = "#7f8c9a"; bg = "#20242c"; fg = "#8aa0b8"; border = "#43506a";
    } else {
        dot  = "#9aa0a8"; bg = "#1c2028"; fg = "#b0b7c2"; border = "#47506a";
    }
    icon_->setStyleSheet(
        QString("background-color: %1; border-radius: 6px; box-shadow: 0 0 6px %1;")
            .arg(dot));
    pill_->setText(status);
    pill_->setStyleSheet(
        QString("background-color: %1; color: %2; border: 1px solid %3;"
                "border-radius: 10px; padding: 2px 10px; font-size: 11px;")
            .arg(bg, fg, border));

    // 迷你进度条 + 数值
    fps_label_->setText(QString::number(fps, 'f', 1));
    fps_bar_->setRange(0, 60);
    fps_bar_->setValue(int(fps < 0 ? 0 : std::min(fps, 60.0)));

    cpu_label_->setText(QString::number(cpu, 'f', 1) + "%");
    cpu_bar_->setRange(0, 100);
    cpu_bar_->setValue(int(std::min(cpu, 100.0)));

    const double mem_mb = double(mem_bytes) / (1024.0 * 1024);
    mem_bar_->setRange(0, 16384);   // 以 16 GB 为满刻度
    mem_bar_->setValue(int(std::min(mem_mb, 16384.0)));
    if (mem_bytes >= 1024ull * 1024 * 1024)
        mem_label_->setText(QString::number(mem_mb / 1024.0, 'f', 1) + "G");
    else if (mem_bytes >= 1024ull * 1024)
        mem_label_->setText(QString::number(mem_mb, 'f', 0) + "M");
    else
        mem_label_->setText(QString::number(double(mem_bytes) / 1024.0, 'f', 0) + "K");

    int hours = int(uptime_sec) / 3600;
    int mins = (int(uptime_sec) % 3600) / 60;
    int secs = int(uptime_sec) % 60;
    uptime_label_->setText(QString("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(mins, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0')));

    // 记录趋势历史 (限长)
    const size_t kHistory = 48;
    cpu_history_.push_back(cpu);
    fps_history_.push_back(fps);
    mem_history_.push_back(mem_mb);
    if (cpu_history_.size() > kHistory) cpu_history_.erase(cpu_history_.begin());
    if (fps_history_.size() > kHistory) fps_history_.erase(fps_history_.begin());
    if (mem_history_.size() > kHistory) mem_history_.erase(mem_history_.begin());
}

void ProcessHealthCard::appendLog(const QString& level, const QString& module,
                                  const QString& message, int64_t ts)
{
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(ts);
    QString time_str = dt.toString("hh:mm:ss.zzz");

    // 颜色编码
    QString color;
    if (level == "ERROR" || level == "FATAL")
        color = "#f44336";
    else if (level == "WARN")
        color = "#ff9800";
    else if (level == "INFO")
        color = "#4caf50";
    else
        color = "#9e9e9e";

    QString html = QString("<span style='color:%1;'>[%2] [%3] %4</span><br>")
        .arg(color).arg(time_str, level, message);

    log_view_->insertHtml(html);

    // 限制日志行数
    const int maxLines = 50;
    auto* doc = log_view_->document();
    if (doc->blockCount() > maxLines) {
        QTextCursor cursor(doc);
        cursor.movePosition(QTextCursor::Start);
        cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor,
                           doc->blockCount() - maxLines);
        cursor.removeSelectedText();
    }

    // 自动滚动到底部
    auto* sb = log_view_->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void ProcessHealthCard::setStatusColor(const QColor& color)
{
    icon_->setStyleSheet(
        QString("background-color: %1; border-radius: 6px;")
            .arg(color.name()));
}

// ===========================================================================
// HealthPanel
// ===========================================================================
HealthPanel::HealthPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("healthRoot");
    // 统一面板与所有子控件背景为深色, 避免任何 QWidget 默认白色区域外露。
    setStyleSheet(
        "HealthPanel, QWidget { background-color: #0d1117; color: #e8edf4; }"
        "QWidget { background-color: #0d1117; }"
        "QLabel { background-color: transparent; }"
        "QScrollArea { background-color: transparent; border: none; }"
        "QScrollArea > QWidget > QWidget { background-color: transparent; }"
        "QFrame { background-color: transparent; }");

    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(12, 12, 12, 12);
    main_layout->setSpacing(10);

    // 1) 汇总条: 总进程/运行/告警/故障/离线 + 总 CPU/内存
    main_layout->addWidget(buildSummaryStrip());

    // 2) 工具栏 + 过滤器
    main_layout->addWidget(buildFilterBar());

    auto* tool_row = new QHBoxLayout();
    auto* title = new QLabel(tr("进程健康监控"), this);
    title->setStyleSheet(
        "font-size: 15px; font-weight: 700; color: #e8edf4; letter-spacing: 0.5px;");
    tool_row->addWidget(title);
    tool_row->addStretch();

    btn_pause_ = new QPushButton("⏸ 暂停刷新", this);
    btn_pause_->setCheckable(true);
    btn_pause_->setStyleSheet(
        "QPushButton { background-color: #1c2430; color: #9fb1c8;"
        "  border: 1px solid #2c3650; border-radius: 6px; padding: 4px 12px;"
        "  font-size: 12px; }"
        "QPushButton:hover { background-color: #243043; }"
        "QPushButton:checked { background-color: #b8860b; color: #ffe7a1;"
        "  border-color: #d4a017; }");
    tool_row->addWidget(btn_pause_);

    btn_clear_ = new QPushButton("清空日志", this);
    btn_clear_->setStyleSheet(
        "QPushButton { background-color: #1c2430; color: #b0b7c2;"
        "  border: 1px solid #2c3650; border-radius: 6px; padding: 4px 12px;"
        "  font-size: 12px; }"
        "QPushButton:hover { background-color: #243043; }");
    tool_row->addWidget(btn_clear_);

    btn_export_ = new QPushButton("导出日志", this);
    btn_export_->setStyleSheet(
        "QPushButton { background-color: #1c2430; color: #b0b7c2;"
        "  border: 1px solid #2c3650; border-radius: 6px; padding: 4px 12px;"
        "  font-size: 12px; }"
        "QPushButton:hover { background-color: #243043; }");
    tool_row->addWidget(btn_export_);
    main_layout->addLayout(tool_row);

    // 3) 卡片容器 (滚动区域)
    scroll_area_ = new QScrollArea(this);
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setFrameShape(QFrame::NoFrame);
    scroll_area_->setStyleSheet(
        "QScrollArea { border: none; background-color: transparent; }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 0;}"
        "QScrollBar::handle:vertical { background: #2a3650; border-radius: 5px; min-height: 30px;}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");

    card_container_ = new QWidget(scroll_area_);
    card_container_->setObjectName("cardContainer");
    // 显式透明: 防止滚动视口默认白色背景在卡片未填满区域外露。
    card_container_->setAttribute(Qt::WA_TranslucentBackground, true);
    card_container_->setStyleSheet(
        "QWidget#cardContainer { background-color: transparent; }");
    card_layout_ = new QVBoxLayout(card_container_);
    card_layout_->setContentsMargins(0, 0, 4, 0);
    card_layout_->setSpacing(10);
    card_layout_->addStretch();

    scroll_area_->setWidget(card_container_);
    if (auto* vp = scroll_area_->viewport())
        vp->setAutoFillBackground(false);
    main_layout->addWidget(scroll_area_, 1);

    // 定时器
    discovery_timer_ = new QTimer(this);
    connect(discovery_timer_, &QTimer::timeout, this, &HealthPanel::onDiscoveryTick);
    reconnect_timer_ = new QTimer(this);
    connect(reconnect_timer_, &QTimer::timeout, this, &HealthPanel::onReconnectTick);

    // 暂停刷新
    connect(btn_pause_, &QPushButton::toggled, this, [this](bool paused) {
        if (paused) {
            discovery_timer_->stop();
            reconnect_timer_->stop();
            btn_pause_->setText("▶ 继续刷新");
        } else {
            discovery_timer_->start(3000);
            reconnect_timer_->start(5000);
            btn_pause_->setText("⏸ 暂停刷新");
        }
    });

    // 清空按钮
    connect(btn_clear_, &QPushButton::clicked, this, [this]() {
        for (auto* card : cards_)
            card->findChild<QTextEdit*>()->clear();
    });

    // 导出按钮
    connect(btn_export_, &QPushButton::clicked, this, [this]() {
        for (auto it = cards_.begin(); it != cards_.end(); ++it) {
            qDebug() << "[HealthPanel] " << it.key()
                     << "log:" << it.value()->findChild<QTextEdit*>()->toPlainText();
        }
    });
}

QWidget* HealthPanel::buildSummaryStrip()
{
    auto* strip = new QFrame(this);
    strip->setObjectName("summaryStrip");
    strip->setStyleSheet(
        "QFrame#summaryStrip { background-color: #141b27;"
        "  border: 1px solid #233046; border-radius: 10px; }");

    auto* lay = new QHBoxLayout(strip);
    lay->setContentsMargins(16, 12, 16, 12);
    lay->setSpacing(8);

    auto addTile = [&](const QString& caption, QLabel*& value,
                       const QString& accent) {
        auto* tile = new QWidget(strip);
        auto* v = new QVBoxLayout(tile);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);
        value = new QLabel("0", strip);
        value->setAlignment(Qt::AlignCenter);
        value->setStyleSheet(
            QString("font-size: 22px; font-weight: 700; color: %1;").arg(accent));
        auto* cap = new QLabel(caption, strip);
        cap->setAlignment(Qt::AlignCenter);
        cap->setStyleSheet("font-size: 11px; color: #6b789a;");
        v->addWidget(value);
        v->addWidget(cap);
        lay->addWidget(tile, 1);
    };

    addTile("总进程", lbl_total_, "#e8edf4");
    addTile("运行",   lbl_ok_,    "#4ade80");
    addTile("告警",   lbl_warn_,  "#f7b731");
    addTile("故障",   lbl_fault_, "#ff6b6b");
    addTile("离线",   lbl_offline_, "#8aa0b8");

    // 分隔
    auto* sep = new QFrame(strip);
    sep->setFrameShape(QFrame::VLine);
    sep->setStyleSheet("color: #2a3650;");
    lay->addSpacing(8);
    lay->addWidget(sep);
    lay->addSpacing(8);

    auto* cpu_block = new QWidget(strip);
    auto* cv = new QVBoxLayout(cpu_block);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(2);
    lbl_cpu_ = new QLabel("0.0%", strip);
    lbl_cpu_->setAlignment(Qt::AlignCenter);
    lbl_cpu_->setStyleSheet("font-size: 18px; font-weight: 700; color: #3b9dff;");
    auto* cc = new QLabel("总 CPU", strip);
    cc->setAlignment(Qt::AlignCenter);
    cc->setStyleSheet("font-size: 11px; color: #6b789a;");
    cv->addWidget(lbl_cpu_);
    cv->addWidget(cc);
    lay->addWidget(cpu_block, 1);

    auto* mem_block = new QWidget(strip);
    auto* mv = new QVBoxLayout(mem_block);
    mv->setContentsMargins(0, 0, 0, 0);
    mv->setSpacing(2);
    lbl_mem_ = new QLabel("0.0M", strip);
    lbl_mem_->setAlignment(Qt::AlignCenter);
    lbl_mem_->setStyleSheet("font-size: 18px; font-weight: 700; color: #f5a623;");
    auto* mc = new QLabel("总内存", strip);
    mc->setAlignment(Qt::AlignCenter);
    mc->setStyleSheet("font-size: 11px; color: #6b789a;");
    mv->addWidget(lbl_mem_);
    mv->addWidget(mc);
    lay->addWidget(mem_block, 1);

    return strip;
}

QWidget* HealthPanel::buildFilterBar()
{
    auto* bar = new QWidget(this);
    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    auto* cap = new QLabel("筛选:", bar);
    cap->setStyleSheet("color: #6b789a; font-size: 12px;");
    lay->addWidget(cap);

    filter_group_ = new QButtonGroup(this);
    filter_group_->setExclusive(true);

    const Filter kinds[5] = { FilterAll, FilterOk, FilterWarn, FilterFault, FilterOffline };
    const char* labels[5] = { "全部", "运行", "告警", "故障", "离线" };
    for (int i = 0; i < 5; ++i) {
        auto* b = new QPushButton(labels[i], bar);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(
            "QPushButton { background-color: #1c2430; color: #9fb1c8;"
            "  border: 1px solid #2c3650; border-radius: 14px; padding: 3px 14px;"
            "  font-size: 12px; }"
            "QPushButton:hover { background-color: #243043; }"
            "QPushButton:checked { background-color: #2f5c8f; color: #eaf2ff;"
            "  border-color: #3b9dff; }");
        filter_group_->addButton(b, int(kinds[i]));
        lay->addWidget(b);
        if (i == 0) b->setChecked(true);
    }
    lay->addStretch();

    auto* legend = new QLabel(
        "● 运行   ● 告警   ● 故障   ● 离线", bar);
    legend->setStyleSheet("color: #6b789a; font-size: 11px;");
    lay->addWidget(legend);

    connect(filter_group_, QOverload<int>::of(&QButtonGroup::idClicked),
            this, [this](int id) {
        current_filter_ = Filter(id);
        applyFilter();
    });

    return bar;
}

void HealthPanel::applyFilter()
{
    for (auto it = cards_.begin(); it != cards_.end(); ++it) {
        const QString s = it.value()->status();
        bool show = true;
        switch (current_filter_) {
        case FilterAll:     show = true; break;
        case FilterOk:      show = (s == "running" || s == "ok"); break;
        case FilterWarn:    show = (s == "warning" || s == "degraded"); break;
        case FilterFault:   show = (s == "error" || s == "fault" || s == "stopped"); break;
        case FilterOffline: show = (s == "offline"); break;
        }
        it.value()->setVisible(show);
    }
}

void HealthPanel::updateSummary(const QString& status)
{
    count_ok_ = count_warn_ = count_fault_ = count_offline_ = 0;
    double total_cpu = 0.0, total_mem = 0.0;
    for (auto it = cards_.begin(); it != cards_.end(); ++it) {
        const QString s = it.value()->status();
        if (s == "running" || s == "ok") ++count_ok_;
        else if (s == "warning" || s == "degraded") ++count_warn_;
        else if (s == "error" || s == "fault" || s == "stopped") ++count_fault_;
        else ++count_offline_;

        const auto& ch = it.value()->cpuHistory();
        const auto& mh = it.value()->memHistory();
        if (!ch.empty()) total_cpu += ch.back();
        if (!mh.empty()) total_mem += mh.back();
    }
    (void)status;

    if (lbl_total_)   lbl_total_->setText(QString::number(cards_.size()));
    if (lbl_ok_)      lbl_ok_->setText(QString::number(count_ok_));
    if (lbl_warn_)    lbl_warn_->setText(QString::number(count_warn_));
    if (lbl_fault_)   lbl_fault_->setText(QString::number(count_fault_));
    if (lbl_offline_) lbl_offline_->setText(QString::number(count_offline_));
    if (lbl_cpu_)
        lbl_cpu_->setText(QString::number(total_cpu, 'f', 1) + "%");
    if (lbl_total_ && lbl_total_->text().toInt() > 0 && lbl_mem_)
        lbl_mem_->setText(QString::number(total_mem, 'f', 0) + "M");
}

HealthPanel::~HealthPanel()
{
    stopMonitoring();
}

void HealthPanel::startMonitoring()
{
    // 连接 Launcher 的 ServiceDiscovery 以发现进程
    // 尝试连接 discovery socket
    discovery_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (discovery_fd_ >= 0) {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, discovery_path_.c_str(), sizeof(addr.sun_path) - 1);

        if (::connect(discovery_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            ::close(discovery_fd_);
            discovery_fd_ = -1;
        }
    }

    // 启动发现定时器 (每 3 秒扫描一次)
    discovery_timer_->start(3000);
    reconnect_timer_->start(5000);
}

void HealthPanel::stopMonitoring()
{
    discovery_timer_->stop();
    reconnect_timer_->stop();

    // 关闭所有连接
    for (auto it = connections_.begin(); it != connections_.end(); ++it) {
        disconnectFromProcess(it.key());
    }
    connections_.clear();

    if (discovery_fd_ >= 0) {
        ::close(discovery_fd_);
        discovery_fd_ = -1;
    }
}

void HealthPanel::addProcess(const QString& name, const std::string& socket_path)
{
    if (cards_.contains(name)) return;

    // 添加卡片
    auto* card = new ProcessHealthCard(name, card_container_);
    int idx = card_layout_->count() - 1; // 在 stretch 之前
    card_layout_->insertWidget(idx, card);
    cards_[name] = card;
    applyFilter();
    updateSummary("unknown");

    // 连接遥测 UDS
    connectToProcess(name, socket_path);
}

void HealthPanel::removeProcess(const QString& name)
{
    disconnectFromProcess(name);

    if (auto* card = cards_.take(name)) {
        card_layout_->removeWidget(card);
        card->deleteLater();
    }
    updateSummary("unknown");
}

void HealthPanel::clearAll()
{
    // 断开所有连接
    for (auto it = connections_.begin(); it != connections_.end(); ++it)
        disconnectFromProcess(it.key());
    connections_.clear();

    // 移除所有卡片
    for (auto* card : cards_) {
        card_layout_->removeWidget(card);
        card->deleteLater();
    }
    cards_.clear();
    updateSummary("unknown");
}

// ---- 定时器槽 ----
void HealthPanel::onDiscoveryTick()
{
    discoverProcesses();
}

void HealthPanel::onReconnectTick()
{
    // 重连已断开连接的进程
    // 通过 ServiceDiscovery 重新发现
    discoverProcesses();
}

// ---- 进程发现 ----
void HealthPanel::discoverProcesses()
{
    if (discovery_fd_ < 0) {
        // 尝试重新连接 discovery socket
        discovery_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (discovery_fd_ >= 0) {
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, discovery_path_.c_str(), sizeof(addr.sun_path) - 1);
            if (::connect(discovery_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                ::close(discovery_fd_);
                discovery_fd_ = -1;
                return;
            }
        } else {
            return;
        }
    }

    // 发送请求: flag=2 (查询所有进程)
    uint8_t query_flag = 2;
    if (::send(discovery_fd_, &query_flag, 1, 0) < 0) {
        ::close(discovery_fd_);
        discovery_fd_ = -1;
        return;
    }

    // 读取响应: count[4] + 多个 [name_size[4] + name + socket_path_size[4] + socket_path]
    uint32_t count = 0;
    ssize_t n = ::read(discovery_fd_, &count, 4);
    if (n < 4) {
        // 可能没有数据, 先关闭等下次
        return;
    }
    count = ntohl(count);

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t name_sz = 0, path_sz = 0;
        if (::read(discovery_fd_, &name_sz, 4) < 4) break;
        name_sz = ntohl(name_sz);
        std::string name(name_sz, '\0');
        if (::read(discovery_fd_, &name[0], name_sz) < int(name_sz)) break;

        if (::read(discovery_fd_, &path_sz, 4) < 4) break;
        path_sz = ntohl(path_sz);
        std::string socket_path(path_sz, '\0');
        if (::read(discovery_fd_, &socket_path[0], path_sz) < int(path_sz)) break;

        // 跳过 Launcher 自身
        if (name == "launcher") continue;

        // 检查是否已有连接
        QString qname = QString::fromStdString(name);
        if (!connections_.contains(qname) && !cards_.contains(qname)) {
            addProcess(qname, socket_path);
        }
    }
}

bool HealthPanel::connectToProcess(const QString& name,
                                   const std::string& socket_path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    // 设置连接超时 (非阻塞 + poll)
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int ret = ::connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        ::close(fd);
        return false;
    }

    // 等待连接完成
    struct pollfd pfd = {fd, POLLOUT, 0};
    ret = poll(&pfd, 1, 2000); // 2 秒超时
    if (ret <= 0) {
        ::close(fd);
        return false;
    }

    // 恢复阻塞模式
    fcntl(fd, F_SETFL, flags);

    // 创建连接对象
    auto* conn = new TelemetryConnection();
    conn->fd = fd;
    conn->running = true;

    // 启动读取线程
    conn->reader_thread = std::thread([this, name, fd]() {
        clientReaderThread(name, fd);
    });

    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_[name] = conn;
    }

    return true;
}

void HealthPanel::disconnectFromProcess(const QString& name)
{
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = connections_.find(name);
    if (it == connections_.end()) return;

    auto* conn = it.value();
    conn->running = false;
    ::shutdown(conn->fd, SHUT_RDWR);
    ::close(conn->fd);
    if (conn->reader_thread.joinable())
        conn->reader_thread.join();

    delete conn;
    connections_.erase(it);
}

void HealthPanel::clientReaderThread(const QString& name, int fd)
{
    // 读取循环: type[1] + payload_size[4] + payload[payload_size]
    while (true) {
        // 检查是否还在运行
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            auto it = connections_.find(name);
            if (it == connections_.end() || !it.value()->running) break;
        }

        uint8_t type_byte = 0;
        uint32_t payload_size = 0;

        // 读取 type
        ssize_t n = ::read(fd, &type_byte, 1);
        if (n <= 0) break;

        // 读取 payload_size
        n = ::read(fd, &payload_size, 4);
        if (n < 4) break;

        payload_size = ntohl(payload_size);
        if (payload_size > 1024 * 1024) break; // 安全限制

        // 读取 payload
        std::string payload(payload_size, '\0');
        ssize_t total_read = 0;
        while (total_read < int(payload_size)) {
            n = ::read(fd, &payload[total_read], payload_size - total_read);
            if (n <= 0) { total_read = -1; break; }
            total_read += n;
        }
        if (total_read < 0) break;

        // 根据类型分发
        switch (TelemetryMsgType(type_byte)) {
        case TelemetryMsgType::Health:
            handleHealthData(name, payload);
            break;
        case TelemetryMsgType::Log:
            handleLogData(name, payload);
            break;
        case TelemetryMsgType::Fault:
            handleFaultData(name, payload);
            break;
        default:
            break;
        }
    }

    // 连接断开, 标记进程离线
    QMetaObject::invokeMethod(this, [this, name]() {
        if (auto* card = cards_.value(name)) {
            card->updateHealth("offline", 0, 0, 0, 0, 0);
            card->appendLog("WARN", "telemetry", "连接已断开",
                QDateTime::currentMSecsSinceEpoch());
            updateSummary("offline");
        }
    }, Qt::QueuedConnection);

    ::close(fd);
}

void HealthPanel::handleHealthData(const QString& name, const std::string& json)
{
    // 简单 JSON 解析 (不依赖 nlohmann/json, 手写简易解析)
    auto getStr = [&](const std::string& key) -> std::string {
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return {};
        pos = json.find(':', pos);
        if (pos == std::string::npos) return {};
        pos = json.find_first_of("\"0123456789-", pos + 1);
        if (pos == std::string::npos) return {};
        if (json[pos] == '\"') {
            auto end = json.find('"', pos + 1);
            if (end == std::string::npos) return {};
            return json.substr(pos + 1, end - pos - 1);
        }
        auto end = json.find_first_of(",}", pos + 1);
        if (end == std::string::npos) return json.substr(pos);
        return json.substr(pos, end - pos);
    };
    auto getNum = [&](const std::string& key) -> double {
        auto s = getStr(key);
        if (s.empty()) return 0.0;
        try { return std::stod(s); } catch (...) { return 0.0; }
    };
    auto getInt = [&](const std::string& key) -> int64_t {
        auto s = getStr(key);
        if (s.empty()) return 0;
        try { return std::stoll(s); } catch (...) { return 0; }
    };

    std::string status = getStr("status");
    double fps = getNum("fps");
    double cpu = getNum("cpu");
    int64_t mem = getInt("mem");
    double uptime = getNum("uptime");
    int64_t ts = getInt("ts");

    QMetaObject::invokeMethod(this, [this, name, status, fps, cpu, mem, uptime, ts]() {
        if (auto* card = cards_.value(name)) {
            card->updateHealth(QString::fromStdString(status), fps, cpu,
                               size_t(mem), uptime, ts);
            updateSummary(QString::fromStdString(status));
        }
    }, Qt::QueuedConnection);
}

void HealthPanel::handleLogData(const QString& name, const std::string& json)
{
    auto getStr = [&](const std::string& key) -> std::string {
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return {};
        pos = json.find(':', pos);
        if (pos == std::string::npos) return {};
        auto start = json.find('"', pos + 1);
        if (start == std::string::npos) return {};
        auto end = json.find('"', start + 1);
        if (end == std::string::npos) return {};
        return json.substr(start + 1, end - start - 1);
    };
    auto getInt = [&](const std::string& key) -> int64_t {
        auto s = getStr(key);
        if (s.empty()) return 0;
        try { return std::stoll(s); } catch (...) { return 0; }
    };

    std::string level = getStr("level");
    std::string module = getStr("module");
    std::string msg = getStr("msg");
    int64_t ts = getInt("ts");

    QMetaObject::invokeMethod(this, [this, name, level, module, msg, ts]() {
        if (auto* card = cards_.value(name)) {
            card->appendLog(QString::fromStdString(level),
                           QString::fromStdString(module),
                           QString::fromStdString(msg), ts);
        }
    }, Qt::QueuedConnection);
}

void HealthPanel::handleFaultData(const QString& name, const std::string& json)
{
    // 故障作为特殊日志显示
    handleLogData(name, "{\"level\":\"ERROR\",\"module\":\"fault\",\"msg\":\""
                       + json + "\",\"ts\":0}");
}

}} // namespace vsgraysim::gui