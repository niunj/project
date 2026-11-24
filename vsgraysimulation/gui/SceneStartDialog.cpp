#include "SceneStartDialog.h"

#include "AssetBrowserWidget.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include <filesystem>

#include <spdlog/spdlog.h>

namespace vsgraysim { namespace gui {

namespace {

/// 启动对话框专用深色样式 (与导航页品牌风格一致)。
const char* kStartDialogQss = R"QSS(
QDialog { background: #161c27; color: #d7dbe2; }
QLabel { background: transparent; color: #c6ccd8; }
QTabWidget::pane { border: 1px solid #2a3040; border-radius: 6px; background: #1a2130; }
QTabBar::tab { background: #1c2330; color: #8b95a6; padding: 7px 18px;
  border: 1px solid #2a3040; border-bottom: none;
  border-top-left-radius: 6px; border-top-right-radius: 6px; margin-right: 3px; }
QTabBar::tab:selected { background: #1a2130; color: #7fd0ff; font-weight: bold; }
QTabBar::tab:hover { color: #d7dbe2; }
QListView { background: #10131a; border: none; }
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

SceneStartDialog::SceneStartDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("场景设计 · 开始"));
    resize(1060, 640);
    setMinimumSize(880, 540);
    setStyleSheet(QString::fromUtf8(kStartDialogQss));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(14);

    // ---- 顶部横幅 ----------------------------------------------------------
    auto* banner = new QHBoxLayout;
    banner->setSpacing(12);
    auto* lbl_title = new QLabel(tr("场景设计器"), this);
    lbl_title->setStyleSheet("font-size: 20px; font-weight: 600; color: #e8ecf3;");
    auto* lbl_sub = new QLabel(
        tr("打开已有场景编辑修改, 或从场景模板 (地形 + 目标场景树) 快速创建新场景"), this);
    lbl_sub->setStyleSheet("font-size: 12px; color: #8b95a6;");
    auto* title_col = new QVBoxLayout;
    title_col->setSpacing(2);
    title_col->addWidget(lbl_title);
    title_col->addWidget(lbl_sub);
    banner->addLayout(title_col, 1);
    root->addLayout(banner);

    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #2a3040;");
    root->addWidget(sep);

    // ---- 中部: 左侧资产浏览 + 右侧详情预览 ---------------------------------
    auto* body = new QHBoxLayout;
    body->setSpacing(14);

    tabs_ = new QTabWidget(this);
    {
        auto* page_s = new QWidget(tabs_);
        auto* vs = new QVBoxLayout(page_s);
        vs->setContentsMargins(8, 8, 8, 8);
        auto* lbl_s = new QLabel(tr("已有场景 (workspace/scenes/) — 双击直接打开编辑修改"), page_s);
        lbl_s->setStyleSheet("color:#7fd0ff; font-size:12px;");
        vs->addWidget(lbl_s);
        scenes_ = new AssetBrowserWidget(page_s);
        vs->addWidget(scenes_, 1);
        page_s->setLayout(vs);
        tabs_->addTab(page_s, tr("已有场景文件 (编辑修改)"));

        auto* page_t = new QWidget(tabs_);
        auto* vt = new QVBoxLayout(page_t);
        vt->setContentsMargins(8, 8, 8, 8);
        auto* lbl_t = new QLabel(tr("场景模板 (地形编辑器导出的地形工程) — 选中后以地形+场景树快速创建新场景"), page_t);
        lbl_t->setStyleSheet("color:#7fd0ff; font-size:12px;");
        lbl_t->setWordWrap(true);
        vt->addWidget(lbl_t);
        terrains_ = new AssetBrowserWidget(page_t);
        vt->addWidget(terrains_, 1);
        page_t->setLayout(vt);
        tabs_->addTab(page_t, tr("场景模板 (新建场景)"));
    }
    body->addWidget(tabs_, 3);

    // 详情预览面板
    auto* details_panel = new QFrame(this);
    details_panel->setObjectName("detailsPanel");
    details_panel->setFixedWidth(270);
    auto* vd = new QVBoxLayout(details_panel);
    vd->setContentsMargins(14, 14, 14, 14);
    auto* lbl_dh = new QLabel(tr("详情预览"), details_panel);
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
    btns->button(QDialogButtonBox::Ok)->setText(tr("打开"));
    btns->button(QDialogButtonBox::Ok)->setObjectName("primary");
    root->addWidget(btns);

    // 工作区数据
    vsgraysim::Workspace ws(resolveWorkspaceRoot().toStdString());
    terrains_->setEntries(ws.listTerrains());
    scenes_->setEntries(ws.listScenes());

    connect(btns, &QDialogButtonBox::accepted, this, &SceneStartDialog::onAccept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(tabs_, &QTabWidget::currentChanged, this, &SceneStartDialog::onTabChanged);
    connect(terrains_, &AssetBrowserWidget::activated,
            this, &SceneStartDialog::onActivated);
    connect(scenes_, &AssetBrowserWidget::activated,
            this, &SceneStartDialog::onActivated);
    connect(terrains_, &AssetBrowserWidget::selectionChanged,
            this, &SceneStartDialog::updateDetails);
    connect(scenes_, &AssetBrowserWidget::selectionChanged,
            this, &SceneStartDialog::updateDetails);
    onTabChanged(0);
}

void SceneStartDialog::updateDetails(const vsgraysim::WorkspaceEntry& e)
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
            // 场景条目
            const int objs = int(m.value("objects", nlohmann::json::array()).size());
            const auto& sens = m.value("sensors", nlohmann::json::object());
            const int sensors = int(sens.value("sensor_list", nlohmann::json::array()).size());
            html += tr("<b>场景:</b> %1<br/>")
                        .arg(QString::fromStdString(m.value("scenario_name", std::string())));
            const std::string desc = m.value("description", std::string());
            if (!desc.empty())
                html += tr("<b>描述:</b> %1<br/>").arg(QString::fromStdString(desc));
            html += tr("<b>实体:</b> %1 个<br/>").arg(objs);
            html += tr("<b>传感器:</b> %1 个<br/>").arg(sensors);
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
        } else if (m.contains("format")) {
            // 地形条目
            if (m.contains("grid"))
                html += tr("<b>网格:</b> %1×%1<br/>").arg(m.value("grid", 0));
            if (m.contains("extent"))
                html += tr("<b>范围:</b> %1 m<br/>").arg(m.value("extent", 0.0), 0, 'f', 0);
            if (m.contains("max_height"))
                html += tr("<b>最大高程:</b> %1 m<br/>").arg(m.value("max_height", 0.0), 0, 'f', 1);
            if (m.contains("water_type")) {
                const int wt = m.value("water_type", 0);
                static const char* water_names[] = {"无水", "海洋", "湖泊", "河流", "池塘"};
                if (wt >= 0 && wt <= 4)
                    html += tr("<b>水面:</b> %1<br/>").arg(tr(water_names[wt]));
            }
        }
    } else {
        html += tr("<span style=\"color:#8b95a6;\">无元数据</span><br/>");
    }
    details_->setText(html);
}

void SceneStartDialog::onTabChanged(int idx)
{
    // 根据当前标签切换确定按钮文案 (0=已有场景打开, 1=场景模板创建)
    auto* btns = findChild<QDialogButtonBox*>();
    if (auto* ok = btns ? btns->button(QDialogButtonBox::Ok) : nullptr)
        ok->setText(idx == 0 ? tr("打开") : tr("创建"));
    if (idx == 0) {
        scenes_->setFocus();
        updateDetails(scenes_->selected());
    } else {
        terrains_->setFocus();
        updateDetails(terrains_->selected());
    }
}

void SceneStartDialog::onActivated(const vsgraysim::WorkspaceEntry& e)
{
    (void)e;
    // 双击即确定
    onAccept();
}

void SceneStartDialog::onAccept()
{
    if (tabs_) {
        if (tabs_->currentIndex() == 0) {
            const auto s = scenes_->selected();
            if (s.path.empty()) return;
            scenario_path_ = QString::fromStdString(s.path);
            choice_ = Choice::OpenScenario;
        } else {
            const auto t = terrains_->selected();
            if (t.path.empty()) return;
            terrain_project_ = QString::fromStdString(t.path);
            choice_ = Choice::NewFromTerrain;
        }
    }
    spdlog::info("SceneStartDialog: choice={}, terrain='{}', scenario='{}'",
                 int(choice_), terrain_project_.toStdString(),
                 scenario_path_.toStdString());
    accept();
}

}} // namespace vsgraysim::gui
