#include "SimStartDialog.h"

#include "AssetBrowserWidget.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include <filesystem>

#include <spdlog/spdlog.h>

namespace vsgraysim { namespace gui {

namespace {

/// 启动对话框专用深色样式 (与场景设计启动页一致)。
const char* kStartDialogQss = R"QSS(
QDialog { background: #161c27; color: #d7dbe2; }
QLabel { background: transparent; color: #c6ccd8; }
QListView { background: #10131a; border: 1px solid #2a3040; border-radius: 6px; }
QListView::item:selected { background: #2d6fb8; color: white; }
QToolButton { background: #243044; color: #dbe4f0; border: 1px solid #34425c;
  border-radius: 4px; padding: 3px 10px; }
QToolButton:hover { background: #2e3d57; }
QToolButton:checked { background: #2d6fb8; color: white; }
QPushButton { background: #243044; color: #dbe4f0; border: 1px solid #34425c;
  border-radius: 5px; padding: 7px 20px; font-size: 13px; }
QPushButton:hover { background: #2e3d57; }
QPushButton:pressed { background: #3a4c6e; }
QPushButton#primary { background: #1d4f8a; border-color: #2d6fb8; font-weight: bold; }
QPushButton#primary:hover { background: #2d6fb8; }
QFrame#detailsPanel { background: #1a2130; border: 1px solid #2a3040; border-radius: 8px; }
)QSS";

} // anonymous namespace

SimStartDialog::SimStartDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("仿真控制 · 选择场景"));
    resize(1000, 600);
    setMinimumSize(840, 500);
    setStyleSheet(QString::fromUtf8(kStartDialogQss));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(14);

    // ---- 顶部横幅 ----------------------------------------------------------
    auto* lbl_title = new QLabel(tr("仿真控制台"), this);
    lbl_title->setStyleSheet("font-size: 20px; font-weight: 600; color: #e8ecf3;");
    auto* lbl_sub = new QLabel(
        tr("装载已创建好的场景文件 (场景设计器保存至 workspace/scenes/) · 依据目标电磁/光学属性仿真雷达与红外特性"), this);
    lbl_sub->setStyleSheet("font-size: 12px; color: #8b95a6;");
    root->addWidget(lbl_title);
    root->addWidget(lbl_sub);

    // 流程提示条
    auto* flow = new QLabel(
        tr("流程: ① 选择场景 → ② 装载并初始化管线 → ③ 播放观测 (RD 图 / 红外等四波段 / 3D 预览)"),
        this);
    flow->setStyleSheet("color:#7fd0ff; font-size:12px; background:#1a2130;"
                        "border:1px solid #2a3040; border-radius:6px; padding:7px 10px;");
    root->addWidget(flow);

    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #2a3040;");
    root->addWidget(sep);

    // ---- 中部: 左侧场景列表 + 右侧详情预览 ----------------------------------
    auto* body = new QHBoxLayout;
    body->setSpacing(14);

    scenes_ = new AssetBrowserWidget(this);
    body->addWidget(scenes_, 3);

    auto* details_panel = new QFrame(this);
    details_panel->setObjectName("detailsPanel");
    details_panel->setFixedWidth(270);
    auto* vd = new QVBoxLayout(details_panel);
    vd->setContentsMargins(14, 14, 14, 14);
    auto* lbl_dh = new QLabel(tr("场景详情"), details_panel);
    lbl_dh->setStyleSheet("font-size: 14px; font-weight: bold; color: #7fd0ff;");
    vd->addWidget(lbl_dh);
    auto* sep2 = new QFrame(details_panel);
    sep2->setFrameShape(QFrame::HLine);
    sep2->setStyleSheet("color: #2a3040;");
    vd->addWidget(sep2);
    details_ = new QLabel(tr("未选择"), details_panel);
    details_->setWordWrap(true);
    details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    details_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    details_->setStyleSheet("font-size: 12px; line-height: 1.6; color: #c6ccd8;");
    vd->addWidget(details_, 1);
    body->addWidget(details_panel);
    root->addLayout(body, 1);

    // ---- 底部按钮 ----------------------------------------------------------
    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btns->button(QDialogButtonBox::Ok)->setText(tr("装载并进入仿真"));
    btns->button(QDialogButtonBox::Ok)->setObjectName("primary");
    root->addWidget(btns);

    vsgraysim::Workspace ws(resolveWorkspaceRoot().toStdString());
    scenes_->setEntries(ws.listScenes());

    connect(btns, &QDialogButtonBox::accepted, this, &SimStartDialog::onAccept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(scenes_, &AssetBrowserWidget::activated,
            this, &SimStartDialog::onActivated);
    connect(scenes_, &AssetBrowserWidget::selectionChanged,
            this, &SimStartDialog::updateDetails);
    updateDetails(scenes_->selected());
}

void SimStartDialog::updateDetails(const vsgraysim::WorkspaceEntry& e)
{
    if (!details_) return;
    if (e.path.empty()) {
        details_->setText(tr("未选择"));
        return;
    }
    QString html;
    html += QString("<b style=\"color:#e8ecf3; font-size:13px;\">%1</b><br/>")
                .arg(QString::fromStdString(e.name));
    html += QString("<span style=\"color:#8b95a6;\">%1</span><br/><br/>")
                .arg(QFileInfo(QString::fromStdString(e.path)).fileName());

    if (e.meta.is_object()) {
        const auto& m = e.meta;
        if (m.contains("scenario_name")) {
            const int objs = int(m.value("objects", nlohmann::json::array()).size());
            const auto& sens = m.value("sensors", nlohmann::json::object());
            const int sensors = int(sens.value("sensor_list", nlohmann::json::array()).size());
            html += tr("<b>场景:</b> %1<br/>")
                        .arg(QString::fromStdString(m.value("scenario_name", std::string())));
            const std::string desc = m.value("description", std::string());
            if (!desc.empty())
                html += tr("<b>描述:</b> %1<br/>").arg(QString::fromStdString(desc));
            html += tr("<b>目标实体:</b> %1 个<br/>").arg(objs);
            html += tr("<b>传感器:</b> %1 个<br/>").arg(sensors);
            // 传感器类型概要
            QStringList types;
            for (const auto& js : sens.value("sensor_list", nlohmann::json::array()))
                types << QString::fromStdString(js.value("type", std::string("?")));
            if (!types.isEmpty())
                html += tr("<b>类型:</b> %1<br/>").arg(types.join(", "));
            if (m.contains("terrain_ref"))
                html += tr("<b>地形:</b> %1<br/>")
                            .arg(QString::fromStdString(
                                std::filesystem::path(m["terrain_ref"].get<std::string>())
                                    .stem().string()));
            const auto& atm = m.value("atmosphere", nlohmann::json::object());
            if (atm.is_object() && atm.contains("weather_scenario"))
                html += tr("<b>天气:</b> %1<br/>")
                            .arg(QString::fromStdString(
                                atm.value("weather_scenario", std::string("clear"))));
        }
    } else {
        html += tr("<span style=\"color:#8b95a6;\">无元数据</span><br/>");
    }
    details_->setText(html);
}

void SimStartDialog::onActivated(const vsgraysim::WorkspaceEntry& e)
{
    (void)e;
    onAccept();
}

void SimStartDialog::onAccept()
{
    const auto s = scenes_->selected();
    if (s.path.empty()) return;
    scenario_path_ = QString::fromStdString(s.path);
    spdlog::info("SimStartDialog: scenario '{}'", scenario_path_.toStdString());
    accept();
}

}} // namespace vsgraysim::gui
