#ifndef SCENETREEWIDGET_H
#define SCENETREEWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent>
#include "../Common/MteStructDef.h"
#include "SceneEngine.h"
// #include "SceneEditor/modeltrackedit.h"

class SceneTreeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SceneTreeWidget(QWidget *parent = nullptr);
    ~SceneTreeWidget();

    void setSceneEngine(SceneEngine* engine);

signals:
    void itemSelected_signal(const QString& type, int id, const QString& name);
    void itemDoubleClicked_signal(const QString& type, int id, const QString& name);
    void positionItem_signal(int id);

public slots:
    void onSceneTreeDataChanged_slot(const SceneInfoStru& sceneInfo);
    void onSceneTreeItemAdded_slot(const QString& type, int id, const QString& name);
    void onSceneTreeItemRemoved_slot(const QString& type, int id);
    void onSceneTreeItemModified_slot(const QString& type, int id, const QString& name);

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onCustomContextMenuRequested(const QPoint& pos);

private:
    void createTreeStructure();
    void setupConnections();
    void createContextMenu();

    void addTerrainItem(const MtePlatformStru& terrain);
    void addBackgroundItem(const MtePlatformStru& background);
    void addTargetItem(const MtePlatformStru& target);


    void removeItem(const QString& type, int id);
    void modifyItem(const QString& type, int id, const QString& name);

    QTreeWidgetItem* findItem(const QString& type, int id);
    void updateTreeWidgetItem(QTreeWidgetItem* item, const QString& name);

    void clearTerrainItems();
    void clearBackgroundItems();
    void clearTargetItems();


    QTreeWidget* m_treeWidget;
    SceneEngine* m_sceneEngine;

    QTreeWidgetItem* m_terrainRoot;
    QTreeWidgetItem* m_backgroundRoot;
    QTreeWidgetItem* m_targetRoot;


    QMenu* m_contextMenu;
    QAction* m_addTerrainAction;
    QAction* m_addBackgroundAction;
    QAction* m_addTargetAction;

    QAction* m_clearTerrainAction;
    QAction* m_clearBackgroundAction;
    QAction* m_clearTargetAction;

    QAction* m_deleteAction;
    QAction* m_modifyAction;
    QAction* m_trackAction;
    QAction* m_saveSceneAction;

    QString m_currentRightClickedType;
    int m_currentRightClickedId;
    QString m_currentRightClickedName;
};

#endif // SCENETREEWIDGET_H
