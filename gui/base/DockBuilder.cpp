#include "DockBuilder.h"

#include <QDockWidget>
#include <QFrame>
#include <QMainWindow>
#include <QGroupBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>
#include <QCryptographicHash>

namespace vsgraysim { namespace ui {

QVBoxLayout* DockBuilder::createDock(
    QWidget* parent, const QString& title,
    Qt::DockWidgetArea area, int min_width, const QString& persistent_id)
{
    auto* dock = new QDockWidget(title, parent);
    dock->setFeatures(QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    const auto base=persistent_id.isEmpty()
        ? QStringLiteral("Product.Dock.%1").arg(QString::fromLatin1(QCryptographicHash::hash(title.toUtf8(),QCryptographicHash::Sha1).toHex().left(12)))
        : persistent_id;
    QString name=base;
    int duplicate=1;
    if(parent) while(parent->findChild<QDockWidget*>(name,Qt::FindDirectChildrenOnly))
        name=base+QStringLiteral(".%1").arg(duplicate++);
    dock->setObjectName(name);
    dock->setAccessibleName(title);
    if (min_width > 0)
        dock->setMinimumWidth(min_width);

    // 外层容器
    auto* dock_host = new QWidget(dock);
    dock_host->setObjectName("dockHost");
    auto* outer = new QVBoxLayout(dock_host);
    outer->setContentsMargins(0, 0, 0, 0);

    // 滚动区域
    auto* scroll = new QScrollArea(dock_host);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setAccessibleName(title);
    auto* host = new QWidget;
    auto* v = new QVBoxLayout(host);
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(10);
    scroll->setWidget(host);
    outer->addWidget(scroll);

    dock->setWidget(dock_host);

    // 获取父窗口的 QMainWindow 来添加 dock
    if (auto* mw = qobject_cast<QMainWindow*>(parent))
        mw->addDockWidget(area, dock);

    return v;
}

QVBoxLayout* DockBuilder::addSection(QVBoxLayout* layout, const QString& title)
{
    auto* g = new QGroupBox(title, layout->parentWidget());
    auto* v = new QVBoxLayout(g);
    v->setContentsMargins(10,10,10,10);
    v->setSpacing(8);
    layout->addWidget(g);
    return v;
}

}} // namespace vsgraysim::ui