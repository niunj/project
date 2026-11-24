/// @file SimMonitorPanels.cpp
/// @brief 性能监控 / 事件日志面板实现 (仿真控制软件第二阶段增强)。
#include "SimMonitorPanels.h"

#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPolygonF>
#include <QVBoxLayout>

#include <algorithm>

namespace vsgraysim { namespace gui {

namespace {
const char* kValueStyle   = "font-family: monospace; color:#c6ccd8;";
const char* kCaptionStyle = "color:#9aa5b8;";
} // anonymous namespace

// ---------------------------------------------------------------------------
// FpsSparkline
// ---------------------------------------------------------------------------
FpsSparkline::FpsSparkline(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(90);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void FpsSparkline::pushSample(float fps)
{
    if (samples_.size() >= size_t(kMaxSamples))
        samples_.erase(samples_.begin());
    samples_.push_back(fps);
    update();
}

void FpsSparkline::clear()
{
    samples_.clear();
    update();
}

void FpsSparkline::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0x0a, 0x0d, 0x12));

    // 横向网格 (1/4, 2/4, 3/4)
    p.setPen(QPen(QColor(0x2a, 0x30, 0x40), 1));
    for (int i = 1; i < 4; ++i) {
        const int y = height() * i / 4;
        p.drawLine(0, y, width(), y);
    }

    if (samples_.empty()) {
        p.setPen(QColor(0x5d, 0x66, 0x75));
        p.drawText(rect().adjusted(8, 6, -8, -6),
                   Qt::AlignLeft | Qt::AlignTop, tr("等待 FPS 采样..."));
        return;
    }

    float peak = 1.0f, current = 0.0f;
    for (float s : samples_) {
        peak = std::max(peak, s);
        current = s;
    }
    const float scale = peak * 1.15f;
    constexpr int pad = 5;
    const float step = samples_.size() > 1
        ? float(width() - 2 * pad) / float(samples_.size() - 1) : 0.0f;

    QPolygonF line;
    for (size_t i = 0; i < samples_.size(); ++i) {
        const float x = float(pad) + step * float(i);
        const float y = float(height() - pad) -
                        samples_[i] / scale * float(height() - 2 * pad);
        line.append(QPointF(x, y));
    }

    // 曲线下半透明填充
    QPolygonF fill = line;
    fill.append(QPointF(line.last().x(), float(height() - pad)));
    fill.append(QPointF(line.first().x(), float(height() - pad)));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(84, 196, 106, 36));
    p.drawPolygon(fill);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0x54, 0xc4, 0x6a), 1));
    p.drawPolyline(line);

    p.setPen(QColor(0xd7, 0xdb, 0xe2));
    p.drawText(rect().adjusted(8, 4, -8, -4), Qt::AlignRight | Qt::AlignTop,
               QString("%1  (峰值 %2 · %3 帧)")
                   .arg(current, 0, 'f', 1)
                   .arg(peak, 0, 'f', 1)
                   .arg(samples_.size()));
    p.setPen(QColor(0x5d, 0x66, 0x75));
    p.drawText(rect().adjusted(8, 4, -8, -4), Qt::AlignLeft | Qt::AlignTop,
               tr("FPS 曲线"));
}

// ---------------------------------------------------------------------------
// PerfMonitorPanel
// ---------------------------------------------------------------------------
PerfMonitorPanel::PerfMonitorPanel(QWidget* parent) : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(8);

    spark_ = new FpsSparkline(this);
    v->addWidget(spark_);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(6);
    auto caption = [this](const QString& text) {
        auto* l = new QLabel(text, this);
        l->setStyleSheet(kCaptionStyle);
        return l;
    };
    auto value = [this]() {
        auto* l = new QLabel(tr("—"), this);
        l->setStyleSheet(kValueStyle);
        return l;
    };
    grid->addWidget(caption(tr("实时 FPS")), 0, 0);
    lbl_fps_ = value();
    grid->addWidget(lbl_fps_, 0, 1);
    grid->addWidget(caption(tr("平均帧耗时")), 0, 2);
    lbl_avg_ms_ = value();
    grid->addWidget(lbl_avg_ms_, 0, 3);
    grid->addWidget(caption(tr("仿真/真实时间比")), 1, 0);
    lbl_ratio_ = value();
    grid->addWidget(lbl_ratio_, 1, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    v->addLayout(grid);

    auto* g_eq = new QGroupBox(tr("各装备数据生成帧率"), this);
    auto* h = new QHBoxLayout(g_eq);
    const char* names[3] = {"雷达", "SAR", "红外"};
    for (int i = 0; i < 3; ++i) {
        auto* cap = new QLabel(tr(names[i]), g_eq);
        cap->setStyleSheet(kCaptionStyle);
        lbl_eq_fps_[i] = new QLabel(tr("—"), g_eq);
        lbl_eq_fps_[i]->setStyleSheet(kValueStyle);
        h->addWidget(cap);
        h->addWidget(lbl_eq_fps_[i], 1);
    }
    v->addWidget(g_eq);
    v->addStretch(1);
}

void PerfMonitorPanel::pushFps(double fps)
{
    if (spark_) spark_->pushSample(float(fps));
    if (lbl_fps_) lbl_fps_->setText(QString::number(fps, 'f', 1));
}

void PerfMonitorPanel::setAvgFrameMs(double ms)
{
    if (lbl_avg_ms_)
        lbl_avg_ms_->setText(QString::number(ms, 'f', 2) + QStringLiteral(" ms"));
}

void PerfMonitorPanel::setTimeRatio(double ratio)
{
    if (lbl_ratio_)
        lbl_ratio_->setText(QStringLiteral("×") + QString::number(ratio, 'f', 2));
}

void PerfMonitorPanel::setEquipmentFps(int index, double fps)
{
    if (index < 0 || index > 2 || !lbl_eq_fps_[index]) return;
    lbl_eq_fps_[index]->setText(QString::number(fps, 'f', 1) + QStringLiteral(" Hz"));
}

// ---------------------------------------------------------------------------
// EventLogPanel
// ---------------------------------------------------------------------------
EventLogPanel::EventLogPanel(QWidget* parent) : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    text_ = new QPlainTextEdit(this);
    text_->setReadOnly(true);
    text_->setMaximumBlockCount(4000);          // 环形缓冲, 防止无限增长
    text_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont f = text_->font();
    f.setFamily(QStringLiteral("monospace"));
    f.setPointSize(9);
    text_->setFont(f);
    v->addWidget(text_);
}

void EventLogPanel::log(Level level, const QString& message)
{
    if (!text_) return;
    static const char* tags[3]   = {"[INFO]", "[WARN]", "[ERROR]"};
    static const char* colors[3] = {"#54c46a", "#e9c062", "#e06c6c"};
    const int idx = int(level);
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    text_->appendHtml(QString("%1 <span style=\"color:%2;\">%3</span> %4")
                          .arg(stamp, QString::fromUtf8(colors[idx]),
                               QString::fromUtf8(tags[idx]),
                               message.toHtmlEscaped()));
}

void EventLogPanel::info(const QString& message)  { log(Level::Info, message); }
void EventLogPanel::warn(const QString& message)  { log(Level::Warning, message); }
void EventLogPanel::error(const QString& message) { log(Level::Error, message); }

void EventLogPanel::clearLog()
{
    if (text_) text_->clear();
}

QString EventLogPanel::plainText() const
{
    return text_ ? text_->toPlainText() : QString();
}

}} // namespace vsgraysim::gui
