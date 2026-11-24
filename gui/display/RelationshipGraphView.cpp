/// @file RelationshipGraphView.cpp
/// @brief 见 RelationshipGraphView.h。
#include "RelationshipGraphView.h"

#include <QFrame>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QHash>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QStyleOptionGraphicsItem>

#include <cmath>
#include <map>

namespace vsgraysim { namespace gui { namespace display {

namespace {
constexpr qreal kNodeW = 150.0;
constexpr qreal kNodeH = 40.0;
const QRectF kSceneRect(0.0, 0.0, 1400.0, 900.0);

/// 阵营配色 (与 PropertySchema::faction 对齐: 0=中立,1=蓝,2=红)。
QColor zoneColor(int faction)
{
    switch (faction) {
    case 1:  return QColor(0x2f, 0x6f, 0xd8, 60);   // 蓝方半透明底
    case 2:  return QColor(0xd8, 0x4f, 0x4f, 60);   // 红方半透明底
    default: return QColor(0x50, 0x5a, 0x66, 40);
    }
}

qreal factionBandX(int faction)
{
    switch (faction) {
    case 1:  return kSceneRect.width() * 0.25;      // 左: 蓝方
    case 2:  return kSceneRect.width() * 0.75;      // 右: 红方
    default: return kSceneRect.width() * 0.5;       // 中: 中立
    }
}

/// 节点图形项: 阵营色圆角框 + 类型徽标 + 名称。
class GraphNodeItem : public QGraphicsItem {
public:
    GraphNodeItem(const GraphNode& n, QGraphicsItem* parent = nullptr)
        : QGraphicsItem(parent), node_(n)
    { setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable); }

    QRectF boundingRect() const override { return QRectF(0, 0, kNodeW, kNodeH); }

    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        const QColor fc = RelationshipGraphView::factionColor(node_.faction);
        const QColor bg = QColor(fc.red(), fc.green(), fc.blue(), 40);
        const QRectF r = boundingRect().adjusted(1, 1, -1, -1);
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setPen(QPen(fc, 2));
        p->setBrush(bg);
        if (node_.isMissile)
            p->drawPolygon(QPolygonF() << r.topLeft() << r.bottomLeft()
                    << QPointF(r.right(), r.center().y()));
        else
            p->drawRoundedRect(r, 8, 8);

        // 左上角选中有光圈
        if (isSelected()) {
            p->setPen(QPen(QColor(0x7f, 0xc8, 0xff), 1));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(r.adjusted(-3, -3, 3, 3), 10, 10);
        }

        // 名称 + 类型徽标
        p->setPen(QColor(0xeaf2fc));
        QFont f = p->font();
        f.setPointSize(9);
        p->setFont(f);
        const QString glyph = node_.isMissile ? u8"▷ " : u8"▣ ";
        p->drawText(r.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                    glyph + node_.label);
    }

    const QString& id() const { return node_.id; }
private:
    GraphNode node_;
};
} // namespace

// ===========================================================================
// RelationshipGraphView
// ===========================================================================
RelationshipGraphView::RelationshipGraphView(QWidget* parent)
    : QGraphicsView(parent)
{
    scene_ = new QGraphicsScene(this);
    scene_->setSceneRect(kSceneRect);
    setScene(scene_);
    setRenderHint(QPainter::Antialiasing, true);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setBackgroundBrush(QColor(0x0d, 0x11, 0x17));
    setFrameShape(QFrame::NoFrame);
}

RelationshipGraphView::~RelationshipGraphView() = default;

QColor RelationshipGraphView::factionColor(int faction)
{
    switch (faction) {
    case 2:  return QColor(0xe0, 0x5f, 0x5a);
    case 1:  return QColor(0x5f, 0x9e, 0xe0);
    default: return QColor(0x8a, 0x94, 0xa3);
    }
}

QString RelationshipGraphView::relationLayer(const QString& relation)
{
    if (relation == QStringLiteral("equipped_on"))  return QStringLiteral("assemble");
    if (relation == QStringLiteral("guides") ||
        relation == QStringLiteral("fire_control_of")) return QStringLiteral("control");
    if (relation == QStringLiteral("seeker_locks") ||
        relation == QStringLiteral("target_of"))     return QStringLiteral("engage");
    if (relation == QStringLiteral("fusion_peer"))   return QStringLiteral("cooperate");
    return QStringLiteral("comm");                    // 通信/其它
}

QColor RelationshipGraphView::relationColor(const QString& relation)
{
    const QString layer = relationLayer(relation);
    if (layer == QStringLiteral("assemble"))  return QColor(0x6b, 0x76, 0x88);
    if (layer == QStringLiteral("control"))   return QColor(0xe0, 0x8f, 0x3a);
    if (layer == QStringLiteral("engage"))    return QColor(0xe0, 0x4f, 0x4f);
    if (layer == QStringLiteral("cooperate")) return QColor(0x4f, 0xd0, 0x7a);
    return QColor(0x4f, 0xc0, 0xd8);           // comm
}

Qt::PenStyle RelationshipGraphView::relationStyle(const QString& relation)
{
    const QString layer = relationLayer(relation);
    if (layer == QStringLiteral("engage"))    return Qt::DashLine;
    if (layer == QStringLiteral("cooperate")) return Qt::DashDotLine;
    if (layer == QStringLiteral("comm"))      return Qt::DotLine;
    return Qt::SolidLine;
}

float RelationshipGraphView::relationWidth(const QString& relation)
{
    const QString layer = relationLayer(relation);
    if (layer == QStringLiteral("control")) return 3.0f;
    return 2.0f;
}

QString RelationshipGraphView::relationLabel(const QString& relation)
{
    return relationLayer(relation) == QStringLiteral("assemble")
        ? QString() : relation;
}

void RelationshipGraphView::setEditable(bool on)
{
    editable_ = on;
    setDragMode(on ? QGraphicsView::NoDrag : QGraphicsView::ScrollHandDrag);
}

void RelationshipGraphView::clear()
{
    nodes_.clear();
    edges_.clear();
    scene_->clear();       // 会清掉 drag_line_ 指向的项
    drag_line_ = nullptr;
    drag_from_.clear();
}

void RelationshipGraphView::setGraph(const QVector<GraphNode>& nodes,
                                     const QVector<GraphEdge>& edges)
{
    clear();
    nodes_ = nodes;
    edges_ = edges;
    relayout();
    rebuildEdges();
}

void RelationshipGraphView::relayout()
{
    // 按阵营分区布点: 固定横带 x, 同阵营纵向错开。
    std::map<int, int> counts;
    for (const auto& n : nodes_) counts[n.faction]++;
    std::map<int, int> used;

    for (const auto& n : nodes_) {
        const int cnt = counts[n.faction];
        const int u = used[n.faction]++;
        const qreal y = kSceneRect.height() * 0.5
            + (cnt <= 1 ? 0.0 : (u - (cnt - 1) * 0.5) * 120.0);
        auto* gi = new GraphNodeItem(n);
        gi->setPos(factionBandX(n.faction) - kNodeW * 0.5, y - kNodeH * 0.5);
        gi->setData(0, n.id);
        scene_->addItem(gi);
    }
}

void RelationshipGraphView::rebuildEdges()
{
    // 收集节点中心位置
    QHash<QString, QPointF> centers;
    for (auto* item : scene_->items()) {
        auto* gi = dynamic_cast<GraphNodeItem*>(item);
        if (!gi) continue;
        centers.insert(gi->id(), gi->scenePos() + QPointF(kNodeW, kNodeH) * 0.5);
    }

    for (const auto& e : edges_) {
        if (!centers.contains(e.fromId) || !centers.contains(e.toId)) continue;
        const QPointF a = centers.value(e.fromId);
        const QPointF b = centers.value(e.toId);

        // 连线 + 关系类型标签
        auto* line = scene_->addLine(QLineF(a, b),
            QPen(relationColor(e.relation), relationWidth(e.relation),
                 relationStyle(e.relation)));
        line->setZValue(-1);
        line->setFlag(QGraphicsItem::ItemIsSelectable, true);

        const QString lr = relationLabel(e.relation);
        if (!lr.isEmpty()) {
            auto* t = scene_->addSimpleText(lr);
            t->setBrush(relationColor(e.relation));
            t->setPos((a + b) * 0.5);
            t->setZValue(-0.5);
        }
    }
}

void RelationshipGraphView::drawBackground(QPainter* painter, const QRectF& rect)
{
    painter->fillRect(rect, QColor(0x0d, 0x11, 0x17));
    const qreal w = kSceneRect.width() * 0.5;
    // 左侧蓝方区
    painter->fillRect(QRectF(kSceneRect.left(), kSceneRect.top(), w, kSceneRect.height()),
                      zoneColor(1));
    // 右侧红方区
    painter->fillRect(QRectF(kSceneRect.left() + w, kSceneRect.top(),
                             w, kSceneRect.height()),
                      zoneColor(2));
    // 顶部阵营标签
    painter->setPen(QColor(0x9a, 0xa5, 0xb8));
    QFont f = painter->font();
    f.setPointSize(12);
    f.setBold(true);
    painter->setFont(f);
    painter->drawText(QRectF(kSceneRect.left(), 10, w, 30),
                      Qt::AlignCenter, u8"蓝方阵营");
    painter->drawText(QRectF(kSceneRect.left() + w, 10, w, 30),
                      Qt::AlignCenter, u8"红方阵营");
    painter->setPen(QPen(QColor(0x2a, 0x33, 0x42), 1));
    painter->drawLine(kSceneRect.left() + w, kSceneRect.top() + 40,
                      kSceneRect.left() + w, kSceneRect.bottom());
}

static GraphNodeItem* nodeAt(QGraphicsScene* sc, const QPointF& scenePos)
{
    if (!sc) return nullptr;
    const QList<QGraphicsItem*> items = sc->items(scenePos);
    for (auto* it : items) {
        if (auto* gi = dynamic_cast<GraphNodeItem*>(it)) return gi;
    }
    return nullptr;
}

void RelationshipGraphView::mousePressEvent(QMouseEvent* e)
{
    if (editable_) {
        if (auto* gi = nodeAt(scene_, mapToScene(e->pos()))) {
            drag_from_ = gi->id();
            drag_line_ = scene_->addLine(
                QLineF(gi->scenePos() + QPointF(kNodeW, kNodeH) * 0.5,
                       gi->scenePos() + QPointF(kNodeW, kNodeH) * 0.5),
                QPen(QColor(0x7f, 0xc8, 0xff), 1, Qt::DashLine));
        } else {
            drag_from_.clear();
            QGraphicsView::mousePressEvent(e);
        }
        return;
    }
    // 点击节点 → 联动信号
    if (auto* gi = nodeAt(scene_, mapToScene(e->pos()))) {
        scene_->clearSelection();
        gi->setSelected(true);
        emit nodeClicked(gi->id());
        e->accept();
        return;
    }
    QGraphicsView::mousePressEvent(e);
}

void RelationshipGraphView::mouseMoveEvent(QMouseEvent* e)
{
    if (editable_ && drag_line_ && !drag_from_.isEmpty()) {
        drag_line_->setLine(QLineF(drag_line_->line().p1(), mapToScene(e->pos())));
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void RelationshipGraphView::mouseReleaseEvent(QMouseEvent* e)
{
    if (editable_ && drag_line_) {
        if (!drag_from_.isEmpty()) {
            if (auto* gi = nodeAt(scene_, mapToScene(e->pos()))) {
                const QString to = gi->id();
                if (to != drag_from_) emit edgeRequested(drag_from_, to);
            }
        }
        if (drag_line_) { scene_->removeItem(drag_line_); delete drag_line_; }
        drag_line_ = nullptr;
        drag_from_.clear();
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

void RelationshipGraphView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (!editable_) { QGraphicsView::mouseDoubleClickEvent(e); return; }
    const QPointF sp = mapToScene(e->pos());
    // 识别被双击的边 (边是 z<0 的 QGraphicsLineItem, 其端点即两端节点中心)。
    QGraphicsLineItem* hit = nullptr;
    for (auto* it : scene_->items(sp))
        if (auto* line = dynamic_cast<QGraphicsLineItem*>(it)) { hit = line; break; }
    if (!hit) return;

    // 由线端点反查两端节点 id。
    auto nodeIdAt = [this](const QPointF& p) -> QString {
        QString best; double bestd = 1e18;
        for (auto* it : scene_->items())
            if (auto* gi = dynamic_cast<GraphNodeItem*>(it)) {
                const QPointF c = gi->scenePos() +
                    QPointF(kNodeW, kNodeH) * 0.5;
                const double d = (c - p).manhattanLength();
                if (d < bestd) { bestd = d; best = gi->id(); }
            }
        return best;
    };
    const QLineF ln = hit->line();
    const QString from = nodeIdAt(ln.p1());
    const QString to   = nodeIdAt(ln.p2());
    if (!from.isEmpty() && !to.isEmpty() && from != to)
        emit edgeRemoved(from, to);
}

void RelationshipGraphView::resizeEvent(QResizeEvent* e)
{
    QGraphicsView::resizeEvent(e);
    fitInView(kSceneRect, Qt::KeepAspectRatio);
}

}}} // namespace vsgraysim::gui::display