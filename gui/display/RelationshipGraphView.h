#pragma once
/// @file RelationshipGraphView.h
/// @brief Qt QGraphicsView 关系图控件: 展示/编辑场景平台-装备的协作交互关系。
///
/// 设计要点 (与 PropertySchema 一致, 三软件可复用):
///   - 阵营分区: 背景左右分区 (红/蓝) + 节点阵营色本体 + 光圈;
///   - 多层线型: 按关系类型区分线型/颜色/箭头 (自动化, 各自独立 Layer);
///   - 可编辑态: 从节点拖拽到节点建立边 (edgeRequested), 双击边删除 (edgeRemoved);
///   - 树 ↔ 图联动: nodeClicked 信号回传给宿主, 高亮同名节点。
#ifndef VSGRAYSIM_GUI_DISPLAY_RELATIONSHIPGRAPHVIEW_H
#define VSGRAYSIM_GUI_DISPLAY_RELATIONSHIPGRAPHVIEW_H

#include <QGraphicsView>
#include <QSet>
#include <QVector>

class QGraphicsScene;
class QGraphicsLineItem;
class QGraphicsSimpleTextItem;

namespace vsgraysim { namespace gui { namespace display {

/// 图节点数据 (无 Qt 依赖, 便于跨层 JSON 快照喂入)。
struct GraphNode {
    QString id;      ///< 与场景树/实体一致的 id
    int     kind = 2;///< prop::NodeKind (Entity/Sensor...)
    QString label;   ///< 显示名
    int     faction = 0;  ///< 0=中立, 1=蓝方, 2=红方 (对齐 Faction)
    bool    isMissile = false;
};

/// 图边数据。
struct GraphEdge {
    QString fromId;
    QString toId;
    QString relation;   ///< 关系类型串: equipped_on/guides/fire_control_of/target_of/fusion_peer/comm...
};

class RelationshipGraphView : public QGraphicsView {
    Q_OBJECT
public:
    explicit RelationshipGraphView(QWidget* parent = nullptr);
    ~RelationshipGraphView() override;

    /// 全量刷新拓扑 (节点自动按阵营分区布局, 边按关系分层着样式)。
    void setGraph(const QVector<GraphNode>& nodes, const QVector<GraphEdge>& edges);
    /// 切换是否可编辑 (编辑态支持拖拽建边/双击删边)。
    void setEditable(bool on);
    bool editable() const { return editable_; }
    void clear();

    static QColor factionColor(int faction);
    static QColor relationColor(const QString& relation);
    static Qt::PenStyle relationStyle(const QString& relation);
    static float    relationWidth(const QString& relation);
    static QString  relationLayer(const QString& relation);
    static QString  relationLabel(const QString& relation);

signals:
    /// 点击节点 (用于与场景树/属性联动)。
    void nodeClicked(const QString& id);
    /// 编辑态: 从 from 拖拽到 to 建立边 (宿主负责写回数据并 setGraph)。
    void edgeRequested(const QString& fromId, const QString& toId);
    /// 双击一条边请求删除。
    void edgeRemoved(const QString& fromId, const QString& toId);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;

private:
    void relayout();
    void rebuildEdges();

    QGraphicsScene* scene_ = nullptr;
    QVector<GraphNode> nodes_;
    QVector<GraphEdge> edges_;
    bool editable_ = false;

    // 拖拽建边交互状态
    QString drag_from_;
    QGraphicsLineItem* drag_line_ = nullptr;
};

}}} // namespace vsgraysim::gui::display
#endif // VSGRAYSIM_GUI_DISPLAY_RELATIONSHIPGRAPHVIEW_H