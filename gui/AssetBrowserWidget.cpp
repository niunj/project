#include "AssetBrowserWidget.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QListView>
#include <QMimeData>
#include <QPainter>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QVBoxLayout>

namespace vsgraysim { namespace gui {

QString resolveWorkspaceRoot()
{
    const QString exe_dir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(exe_dir + "/../workspace").absolutePath(),   // build/gui/../..
        QDir("workspace").absolutePath(),                 // cwd 下的 workspace
        exe_dir + "/workspace",
    };
    for (const QString& c : candidates) {
        if (QDir(c).exists()) return c;
    }
    // 都不存在: 优先 cwd 下的 workspace (首次运行自动创建)
    return QDir("workspace").absolutePath();
}

namespace {

constexpr int kSubStripH = 18;  ///< 底部副标题条高度 (卡片/列表统一分格)

/// 在图标/文本下方绘制副标题的 delegate。
/// 统一分格: 主区 (图标+标题) 在上 + 底部副标题条在下, 卡片/列表两视图一致。
class AssetItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override
    {
        const bool icon_mode =
            qobject_cast<const QListView*>(opt.widget) &&
            qobject_cast<const QListView*>(opt.widget)->viewMode() ==
                QListView::IconMode;
        QStyleOptionViewItem o(opt);
        const QString sub = idx.data(Qt::UserRole + 1).toString();
        if (!sub.isEmpty()) {
            // 预留底部副标题条, 交给基类绘制图标与主标题
            o.rect.adjust(0, 0, 0, -kSubStripH);
        }
        QStyledItemDelegate::paint(p, o, idx);

        if (sub.isEmpty()) return;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        QFont f = p->font();
        f.setPointSizeF(f.pointSizeF() * 0.9);
        p->setFont(f);
        const QRect r = opt.rect;
        // 卡片: 副标题居左下; 列表: 与图标右侧标题对齐。
        const int x  = icon_mode ? 6 : 72;
        const int rm = icon_mode ? 12 : 4;
        const QRect sub_r(r.left() + x, r.bottom() - kSubStripH + 2,
                          r.width() - rm - x, kSubStripH - 4);
        p->setPen(QColor("#8b95a6"));
        p->drawText(sub_r, Qt::AlignLeft | Qt::AlignVCenter,
                    p->fontMetrics().elidedText(sub, Qt::ElideRight, sub_r.width()));
        p->restore();
    }
};

/// 拖拽数据源模型: 在标准 mimeData 基础上追加携带模型路径的自定义
/// MIME 类型 application/x-vsgray-model, 供视口拖放创建实体使用。
class BrowserItemModel : public QStandardItemModel {
public:
    using QStandardItemModel::QStandardItemModel;
    QMimeData* mimeData(const QModelIndexList& indexes) const override
    {
        auto* md = QStandardItemModel::mimeData(indexes);
        if (!md || indexes.isEmpty()) return md;
        if (auto* item = itemFromIndex(indexes.first())) {
            const QString path = item->data(Qt::UserRole).toString();
            if (!path.isEmpty())
                md->setData("application/x-vsgray-model", path.toUtf8());
        }
        return md;
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// AssetBrowserWidget
// ---------------------------------------------------------------------------
AssetBrowserWidget::AssetBrowserWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

void AssetBrowserWidget::buildUi()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(6);

    auto* bar = new QHBoxLayout;
    bar->addStretch(1);
    btn_cards_ = new QToolButton(this);
    btn_cards_->setText(tr("卡片"));
    btn_cards_->setCheckable(true);
    btn_cards_->setChecked(true);
    btn_cards_->setToolTip(tr("卡片视图"));
    btn_list_ = new QToolButton(this);
    btn_list_->setText(tr("列表"));
    btn_list_->setCheckable(true);
    btn_list_->setToolTip(tr("列表视图"));
    bar->addWidget(btn_cards_);
    bar->addWidget(btn_list_);
    outer->addLayout(bar);

    view_ = new QListView(this);
    view_->setViewMode(QListView::IconMode);
    view_->setResizeMode(QListView::Adjust);
    view_->setMovement(QListView::Static);
    view_->setUniformItemSizes(false);
    view_->setSpacing(6);
    view_->setIconSize(QSize(200, 150));
    view_->setGridSize(QSize(232, 226));
    view_->setWordWrap(true);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setItemDelegate(new AssetItemDelegate(view_));
    view_->setMinimumHeight(260);
    view_->setDragEnabled(true);
    view_->setDragDropMode(QAbstractItemView::DragOnly);
    view_->setDropIndicatorShown(false);
    outer->addWidget(view_, 1);

    model_ = new BrowserItemModel(this);
    view_->setModel(model_);

    connect(btn_cards_, &QToolButton::clicked, this, [this]() {
        btn_cards_->setChecked(true);
        btn_list_->setChecked(false);
        setMode(Mode::Cards);
    });
    connect(btn_list_, &QToolButton::clicked, this, [this]() {
        btn_cards_->setChecked(false);
        btn_list_->setChecked(true);
        setMode(Mode::List);
    });
    connect(view_, &QListView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (idx.isValid() && idx.row() >= 0 && size_t(idx.row()) < entries_.size())
            emit activated(entries_[size_t(idx.row())]);
    });
    if (view_->selectionModel()) {
        connect(view_->selectionModel(), &QItemSelectionModel::currentChanged,
                this, [this](const QModelIndex& idx) {
                    if (idx.isValid() && idx.row() >= 0 &&
                        size_t(idx.row()) < entries_.size())
                        emit selectionChanged(entries_[size_t(idx.row())]);
                });
    }
}

QIcon AssetBrowserWidget::iconFor(const vsgraysim::WorkspaceEntry& e) const
{
    if (!e.preview.empty()) {
        const QPixmap pm(e.preview.c_str());
        if (!pm.isNull()) return QIcon(pm);
    }
    // 占位图标: 灰底 + 首字符
    QPixmap pm(64, 64);
    pm.fill(QColor("#1c2330"));
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#4a90d9"), 1.5));
    p.drawRoundedRect(1, 1, 62, 62, 8, 8);
    if (!e.name.empty()) {
        QFont f = p.font();
        f.setPointSize(18);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor("#e8ecf3"));
        p.drawText(pm.rect(), Qt::AlignCenter, QString::fromStdString(e.name).left(1));
    }
    return QIcon(pm);
}

QString AssetBrowserWidget::subtitleFor(const vsgraysim::WorkspaceEntry& e) const
{
    if (e.meta.is_object()) {
        const auto& m = e.meta;
        if (m.contains("scenario_name")) {
            const int objs = int(m.value("objects", nlohmann::json::array()).size());
            int sensors = 0;
            const auto& sens = m.value("sensors", nlohmann::json::object());
            sensors = int(sens.value("sensor_list", nlohmann::json::array()).size());
            QString t = tr("%1 实体 · %2 传感器").arg(objs).arg(sensors);
            if (m.contains("terrain_ref"))
                t += tr(" · 地形:%1").arg(QString::fromStdString(m["terrain_ref"].get<std::string>()));
            return t;
        }
        if (m.contains("format")) {
            QString t;
            if (m.contains("grid")) t += QString("%1×%1").arg(m.value("grid", 0));
            if (m.contains("extent")) t += tr(" · 范围%1m").arg(m.value("extent", 0.0));
            if (m.contains("water_type")) {
                const int wt = m.value("water_type", 0);
                static const char* water_names[] = {"无水", "海洋", "湖泊", "河流", "池塘"};
                if (wt >= 0 && wt <= 4) t += tr(" · 水面:%1").arg(tr(water_names[wt]));
            }
            return t;
        }
    }
    return QString();
}

void AssetBrowserWidget::setEntries(const std::vector<vsgraysim::WorkspaceEntry>& entries)
{
    entries_ = entries;
    refreshModel();
    selectFirst();
    // 初始选中同步通知详情面板
    if (!entries_.empty() && view_->currentIndex().isValid())
        emit selectionChanged(entries_[size_t(view_->currentIndex().row())]);
}

void AssetBrowserWidget::refreshModel()
{
    model_->clear();
    for (const auto& e : entries_) {
        auto* item = new QStandardItem;
        item->setText(QString::fromStdString(e.name));
        item->setIcon(iconFor(e));
        item->setToolTip(QString::fromStdString(e.path));
        item->setData(QString::fromStdString(e.path), Qt::UserRole);
        const QString sub = subtitleFor(e);
        if (!sub.isEmpty())
            item->setData(sub, Qt::UserRole + 1);
        item->setSizeHint(QSize(224, mode_ == Mode::Cards ? 218 : 64));
        model_->appendRow(item);
    }
}

void AssetBrowserWidget::setMode(Mode m)
{
    mode_ = m;
    btn_cards_->setChecked(m == Mode::Cards);
    btn_list_->setChecked(m == Mode::List);
    view_->setViewMode(m == Mode::Cards ? QListView::IconMode : QListView::ListMode);
    if (m == Mode::Cards) {
        view_->setIconSize(QSize(200, 150));
        view_->setGridSize(QSize(232, 226));
    } else {
        // 列表: 统一行高 (64 = 主区 46 + 副标题条 18) 与间距, 分格与卡片一致。
        view_->setIconSize(QSize(64, 44));
        view_->setGridSize(QSize());
    }
    refreshModel();
}

vsgraysim::WorkspaceEntry AssetBrowserWidget::selected() const
{
    vsgraysim::WorkspaceEntry e;
    const QModelIndex idx = view_->currentIndex();
    if (!idx.isValid() || idx.row() < 0 || size_t(idx.row()) >= entries_.size())
        return e;
    return entries_[size_t(idx.row())];
}

void AssetBrowserWidget::selectFirst()
{
    if (model_->rowCount() > 0) {
        view_->setCurrentIndex(model_->index(0, 0));
        view_->scrollTo(model_->index(0, 0));
    }
}

}} // namespace vsgraysim::gui