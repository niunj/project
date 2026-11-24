/// @file PropertyTreeWidget.cpp
/// @brief 见 PropertyTreeWidget.h。
#include "PropertyTreeWidget.h"

#include <QHeaderView>
#include <QSignalBlocker>

namespace vsgraysim { namespace gui { namespace base {

PropertyTreeWidget::PropertyTreeWidget(QWidget* parent)
    : QTreeWidget(parent)
{
    setHeaderLabels({QStringLiteral("属性"), QStringLiteral("值")});
    setRootIsDecorated(true);
    setAlternatingRowColors(false);
    setFrameShape(QFrame::NoFrame);
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(1, QHeaderView::Stretch);
    setDefaultDropAction(Qt::IgnoreAction);
    setDragDropMode(QTreeWidget::NoDragDrop);

    setStyleSheet(
        "QTreeWidget { background:#151b23; border:1px solid #262e3b;"
        "  color:#c9d4e3; }"
        "QTreeWidget::item { padding:3px 2px; }"
        "QTreeWidget::item:selected { background:#1d4f8a; color:#eaf2fc; }"
        "QHeaderView::section { background:#1a2130; color:#9aa5b8;"
        "  border:none; padding:5px; }");

    connect(this, &QTreeWidget::itemChanged,
            this, &PropertyTreeWidget::onItemChanged);
}

void PropertyTreeWidget::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (!editable_ || !item || column != 1) return;
    const QString key = item->data(1, int(RoleKey)).toString();
    if (key.isEmpty()) return;
    emit valueEdited(item->data(0, int(RoleGroupId)).toString(),
                     key, item->text(1));
}

void PropertyTreeWidget::setGroups(
    const QVector<prop::Group>& groups,
    const std::function<QString(const prop::Item&)>& valueOf)
{
    const QSignalBlocker blocker(this);
    clear();

    for (const auto& g : groups) {
        if (g.items.isEmpty()) continue;
        auto* gParent = new QTreeWidgetItem(this);
        gParent->setText(0, g.title);
        gParent->setData(0, int(RoleGroupId), g.id);
        QFont f = gParent->font(0);
        f.setBold(true);
        gParent->setFont(0, f);
        gParent->setForeground(0, QColor(0x7f, 0xc8, 0xff));
        gParent->setFlags(Qt::ItemIsEnabled);

        for (const auto& it : g.items) {
            auto* leaf = new QTreeWidgetItem(gParent);
            leaf->setText(0, it.label);
            const QString val = it.key.isEmpty() ? QString() : valueOf(it);
            leaf->setText(1, val);
            leaf->setData(0, int(RoleGroupId), g.id);
            leaf->setData(1, int(RoleKey), it.key);

            // Runtime/Derived 以强调色区分静态配置
            if (it.source != int(prop::Source::Config))
                leaf->setForeground(1, QColor(0x4f, 0xd0, 0x7a));

            Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
            if (editable_ && it.editableOnDesign)
                flags |= Qt::ItemIsEditable;
            leaf->setFlags(flags);
        }
        gParent->setExpanded(true);
    }

    if (!groups.isEmpty() && topLevelItemCount() > 0)
        setCurrentItem(topLevelItem(0));
}

void PropertyTreeWidget::setItemValue(const QString& key, const QString& text)
{
    if (key.isEmpty()) return;
    for (int gi = 0; gi < topLevelItemCount(); ++gi) {
        QTreeWidgetItem* g = topLevelItem(gi);
        for (int ri = 0; ri < g->childCount(); ++ri) {
            QTreeWidgetItem* leaf = g->child(ri);
            if (leaf->data(1, int(RoleKey)).toString() == key) {
                const QSignalBlocker b(this);
                leaf->setText(1, text);
                return;
            }
        }
    }
}

QString PropertyTreeWidget::itemValue(const QString& key) const
{
    for (int gi = 0; gi < topLevelItemCount(); ++gi) {
        QTreeWidgetItem* g = topLevelItem(gi);
        for (int ri = 0; ri < g->childCount(); ++ri) {
            QTreeWidgetItem* leaf = g->child(ri);
            if (leaf->data(1, int(RoleKey)).toString() == key)
                return leaf->text(1);
        }
    }
    return QString();
}

void PropertyTreeWidget::setEditable(bool on)
{
    editable_ = on;
    const QSignalBlocker b(this);
    for (int gi = 0; gi < topLevelItemCount(); ++gi) {
        QTreeWidgetItem* g = topLevelItem(gi);
        for (int ri = 0; ri < g->childCount(); ++ri) {
            QTreeWidgetItem* leaf = g->child(ri);
            Qt::ItemFlags fl = leaf->flags();
            if (on) fl |= Qt::ItemIsEditable; else fl &= ~Qt::ItemIsEditable;
            leaf->setFlags(fl);
        }
    }
}

}}} // namespace vsgraysim::gui::base