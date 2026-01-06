#include "SceneTreeWidget.h"
#include <QDebug>

#include "../SceneEditor/terrainedit.h"            //地形添加编辑
#include "../SceneEditor/backgroundedit.h"         //背景添加编辑
#include "../SceneEditor/modeledit.h"              //目标添加编辑

#include <QApplication>

#include "../Log/log_manager.h"
#include "../OsgRender/Mte3DService.h"

SceneTreeWidget::SceneTreeWidget(QWidget *parent)
    : QWidget(parent)
    , m_treeWidget(nullptr)
    , m_sceneEngine(nullptr)
    , m_terrainRoot(nullptr)
    , m_backgroundRoot(nullptr)
    , m_targetRoot(nullptr)
    , m_contextMenu(nullptr)
    , m_addTerrainAction(nullptr)
    , m_addBackgroundAction(nullptr)
    , m_addTargetAction(nullptr)
    , m_clearTerrainAction(nullptr)
    , m_clearBackgroundAction(nullptr)
    , m_clearTargetAction(nullptr)
    , m_deleteAction(nullptr)
    , m_modifyAction(nullptr)
    , m_trackAction(nullptr)
    , m_saveSceneAction(nullptr)
    , m_currentRightClickedType("")
    , m_currentRightClickedId(-1)
    , m_currentRightClickedName("")
{
    // 创建主布局
    QVBoxLayout* layout = new QVBoxLayout(this);

    // 创建树形控件
    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setHeaderLabel("场景树");
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);

    layout->addWidget(m_treeWidget);

    // 创建树结构
    createTreeStructure();

    // 创建上下文菜单
    createContextMenu();

    // 设置连接
    setupConnections();
}

SceneTreeWidget::~SceneTreeWidget()
{
}

void SceneTreeWidget::setSceneEngine(SceneEngine* engine)
{
    m_sceneEngine = engine;
    if (m_sceneEngine) {
        connect(m_sceneEngine, &SceneEngine::sceneTreeDataChanged_signal,
                this, &SceneTreeWidget::onSceneTreeDataChanged_slot,Qt::DirectConnection);

        connect(m_sceneEngine, &SceneEngine::sceneTreeItemAdded_signal,
                this, &SceneTreeWidget::onSceneTreeItemAdded_slot,Qt::DirectConnection);

        connect(m_sceneEngine, &SceneEngine::sceneTreeItemRemoved_signal,
                this, &SceneTreeWidget::onSceneTreeItemRemoved_slot,Qt::DirectConnection);

        connect(m_sceneEngine, &SceneEngine::sceneTreeItemModified_signal,
                this, &SceneTreeWidget::onSceneTreeItemModified_slot,Qt::DirectConnection);
    }
}

void SceneTreeWidget::createTreeStructure()
{
    // 添加根节点
    m_terrainRoot = new QTreeWidgetItem(m_treeWidget);
    m_terrainRoot->setText(0, "地形");
    m_terrainRoot->setIcon(0, QIcon("data/icons/mainGuiIcon/mainGui/leftWork/mesh.png")); // 假设有图标
    m_terrainRoot->setData(0, Qt::UserRole, "TerrainRoot");

    m_backgroundRoot = new QTreeWidgetItem(m_treeWidget);
    m_backgroundRoot->setText(0, "背景");
    m_backgroundRoot->setIcon(0, QIcon("data/icons/mainGuiIcon/mainGui/leftWork/environment.png"));
    m_backgroundRoot->setData(0, Qt::UserRole, "BackgroundRoot");

    m_targetRoot = new QTreeWidgetItem(m_treeWidget);
    m_targetRoot->setText(0, "目标");
    m_targetRoot->setIcon(0, QIcon("data/icons/mainGuiIcon/mainGui/leftWork/obj.png"));
    m_targetRoot->setData(0, Qt::UserRole, "TargetRoot");

    // 展开所有根节点
    m_treeWidget->expandAll();
}

void SceneTreeWidget::createContextMenu()
{
    m_contextMenu = new QMenu(this);

    // 为根节点创建添加操作
    m_addTerrainAction      = new QAction("添加地形", this);
    m_addBackgroundAction   = new QAction("添加背景", this);
    m_addTargetAction       = new QAction("添加目标", this);


    // 为根节点创建清除操作
    m_clearTerrainAction    = new QAction("清除地形", this);
    m_clearBackgroundAction = new QAction("清除背景", this);
    m_clearTargetAction     = new QAction("清除目标", this);


    // 为子节点创建操作
    m_deleteAction = new QAction("删除", this);
    m_modifyAction = new QAction("修改", this);
    m_trackAction  = new QAction("航迹", this);

    // 保存场景操作
    m_saveSceneAction = new QAction("保存场景", this);

    // 添加到菜单
    m_contextMenu->addAction(m_addTerrainAction);
    m_contextMenu->addAction(m_clearTerrainAction);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_addBackgroundAction);
    m_contextMenu->addAction(m_clearBackgroundAction);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_addTargetAction);
    m_contextMenu->addAction(m_clearTargetAction);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_deleteAction);
    m_contextMenu->addAction(m_modifyAction);
    m_contextMenu->addAction(m_trackAction);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_saveSceneAction);

    // 初始隐藏所有操作
    m_addTerrainAction->setVisible(false);
    m_addBackgroundAction->setVisible(false);
    m_addTargetAction->setVisible(false);

    m_clearTerrainAction->setVisible(false);
    m_clearBackgroundAction->setVisible(false);
    m_clearTargetAction->setVisible(false);

    m_deleteAction->setVisible(false);
    m_modifyAction->setVisible(false);

    connect(m_addTerrainAction, &QAction::triggered, [this]() {
        TerrainEdit m_terrainEdit;
        connect(&m_terrainEdit, &TerrainEdit::sig_addTerrain, m_sceneEngine, &SceneEngine::onUIAddTerrain_slot);

        m_terrainEdit.setWindowModality(Qt::WindowModal);
        m_terrainEdit.setEditMode(false); // 设置为添加模式
        m_terrainEdit.setSceneEngine(m_sceneEngine);
        m_terrainEdit.show();
        
        while (m_terrainEdit.isVisible()) {
            QApplication::processEvents(QEventLoop::AllEvents, 100); // 处理UI事件，不阻塞
        }
    });

    connect(m_addBackgroundAction, &QAction::triggered, [this]() {
        BackGroundEdit m_backGroundEdit;
        connect(&m_backGroundEdit, &BackGroundEdit::sig_addBackground, m_sceneEngine, &SceneEngine::onUIAddBackground_slot);

        m_backGroundEdit.setWindowModality(Qt::WindowModal);
        m_backGroundEdit.setEditMode(false); // 设置为添加模式
        m_backGroundEdit.setSceneEngine(m_sceneEngine);
        m_backGroundEdit.show();
        
        while (m_backGroundEdit.isVisible()) {
            QApplication::processEvents(QEventLoop::AllEvents, 100); // 处理UI事件，不阻塞
        }
    });

    connect(m_addTargetAction, &QAction::triggered, [this]() {
        ModelEdit m_modelEdit;
        connect(&m_modelEdit, &ModelEdit::sig_addModel, m_sceneEngine, &SceneEngine::onUIAddTarget_slot);

        m_modelEdit.setWindowModality(Qt::WindowModal);
        m_modelEdit.setEditMode(false); // 设置为添加模式
        m_modelEdit.setSceneEngine(m_sceneEngine);
        m_modelEdit.show();
        
        while (m_modelEdit.isVisible()) {
            QApplication::processEvents(QEventLoop::AllEvents, 100); // 处理UI事件，不阻塞
        }
    });




    connect(m_clearTerrainAction, &QAction::triggered, [this]() {
        clearTerrainItems();
    });

    connect(m_clearBackgroundAction, &QAction::triggered, [this]() {
        clearBackgroundItems();
    });

    connect(m_clearTargetAction, &QAction::triggered, [this]() {
        clearTargetItems();
    });



    connect(m_deleteAction, &QAction::triggered, [this]() {
        if (!m_currentRightClickedType.isEmpty() && m_currentRightClickedId != -1) {
            if (m_sceneEngine) {
                if (m_currentRightClickedType == "Terrain") {
                    m_sceneEngine->onUIRemoveTerrain_slot(m_currentRightClickedId);
                } else if (m_currentRightClickedType == "Background") {
                    m_sceneEngine->onUIRemoveBackground_slot(m_currentRightClickedId);
                } else if (m_currentRightClickedType == "Target") {
                    m_sceneEngine->onUIRemoveTarget_slot(m_currentRightClickedId);
                }
            }
        }
    });
    


    connect(m_modifyAction, &QAction::triggered, [this]() {
        if (!m_currentRightClickedType.isEmpty() && m_currentRightClickedId != -1) {


            LOG_DEBUG << "编辑" << m_currentRightClickedType << m_currentRightClickedId <<m_currentRightClickedName;
            if (m_currentRightClickedType == "Terrain") {
                if(m_sceneEngine) {
                    MtePlatformStru terrain = m_sceneEngine->findTerrain(m_currentRightClickedId);

                    // 返回-1,表示非法
                    if(terrain.m_id != -1) {
                        TerrainEdit m_terrainEdit;
                        connect(&m_terrainEdit, &TerrainEdit::sig_modifyTerrain, m_sceneEngine, &SceneEngine::onUIModifyTerrain_slot);

                        m_terrainEdit.setWindowModality(Qt::WindowModal);
                        m_terrainEdit.setEditParam(terrain);
                        m_terrainEdit.setSceneEngine(m_sceneEngine);
                        m_terrainEdit.show();

                        // 核心：通过 Qt 事件循环让作用域持续（不退出）
                        // 仅当主动关闭窗口时，才退出该逻辑（但栈变量仍在类中，不销毁）
                        while (m_terrainEdit.isVisible()) {
                            QApplication::processEvents(QEventLoop::AllEvents, 100); // 处理UI事件，不阻塞
                        }
                    }
                }

            } else if (m_currentRightClickedType == "Background") {
                if(m_sceneEngine) {
                    MtePlatformStru background = m_sceneEngine->findBackground(m_currentRightClickedId);

                    // 返回-1,表示非法
                    if(background.m_id != -1) {
                        BackGroundEdit m_backGroundEdit;
                        connect(&m_backGroundEdit, &BackGroundEdit::sig_modifyBackground, m_sceneEngine, &SceneEngine::onUIModifyBackground_slot);

                        m_backGroundEdit.setWindowModality(Qt::WindowModal);
                        m_backGroundEdit.setEditParam(background);
                        m_backGroundEdit.setSceneEngine(m_sceneEngine);
                        m_backGroundEdit.show();

                        // 核心：通过 Qt 事件循环让作用域持续（不退出）
                        // 仅当主动关闭窗口时，才退出该逻辑（但栈变量仍在类中，不销毁）
                        while (m_backGroundEdit.isVisible()) {
                            QApplication::processEvents(QEventLoop::AllEvents, 100); // 处理UI事件，不阻塞
                        }
                    }
                }

            } else if (m_currentRightClickedType == "Target") {
                if(m_sceneEngine) {
                    MtePlatformStru target = m_sceneEngine->findTarget(m_currentRightClickedId);

                    // 返回-1,表示非法
                    if(target.m_id != -1) {
                        ModelEdit m_modelEdit;
                        connect(&m_modelEdit, &ModelEdit::sig_modifyModel, m_sceneEngine, &SceneEngine::onUIModifyTarget_slot);

                        m_modelEdit.setWindowModality(Qt::WindowModal);
                        m_modelEdit.setEditParam(target);
                        m_modelEdit.setSceneEngine(m_sceneEngine);
                        m_modelEdit.show();

                        // 核心：通过 Qt 事件循环让作用域持续（不退出）
                        // 仅当主动关闭窗口时，才退出该逻辑（但栈变量仍在类中，不销毁）
                        while (m_modelEdit.isVisible()) {
                            QApplication::processEvents(QEventLoop::AllEvents, 100); // 处理UI事件，不阻塞
                        }
                    }
                }

            }

        }
    });

    connect(m_trackAction, &QAction::triggered, [this]() {
        if (m_sceneEngine && m_currentRightClickedId != -1) {
            LOG_DEBUG << "航迹" << m_currentRightClickedType << m_currentRightClickedId << m_currentRightClickedName;
        }
    });

    connect(m_saveSceneAction, &QAction::triggered, [this]() {
        LOG_DEBUG << "保存场景";

        QString filePath = QFileDialog::getSaveFileName(this, tr("保存场景到Obj文件"), ".", tr("Obj文件 (*.obj)"));
        if (!filePath.isEmpty()) {
            // 确保文件后缀是.obj
            if (!filePath.endsWith(".obj", Qt::CaseInsensitive)) {
                filePath += ".obj";
            }
            // 保存场景到Obj文件
            Mte3DService::getInstance().saveSceneToObj(filePath);
        }
    });
}

void SceneTreeWidget::setupConnections()
{
    connect(m_treeWidget, &QTreeWidget::itemClicked,
            this, &SceneTreeWidget::onItemClicked);
    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked,
            this, &SceneTreeWidget::onItemDoubleClicked);
    connect(m_treeWidget, &QTreeWidget::customContextMenuRequested,
            this, &SceneTreeWidget::onCustomContextMenuRequested);
}

void SceneTreeWidget::onItemClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)

    // 获取项目类型和ID
    QString type = item->data(0, Qt::UserRole).toString();
    int id = item->data(0, Qt::UserRole + 1).toInt();
    QString name = item->text(0);

    // 检查是否是子节点（非根节点）
    if (type != "TerrainRoot" && type != "BackgroundRoot" &&
        type != "TargetRoot"  && !type.isEmpty()) {
        // 这是子节点，发送选中信号
        emit itemSelected_signal(type, id, name);
    }
}

void SceneTreeWidget::onItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)

    // 获取项目类型和ID
    QString type  = item->data(0, Qt::UserRole).toString();
    int id        = item->data(0, Qt::UserRole + 1).toInt();
    QString name  = item->text(0);

    // 检查是否是子节点（非根节点）
    if (type != "TerrainRoot" && type != "BackgroundRoot" &&
        type != "TargetRoot" && !type.isEmpty()) {
        // 这是子节点，发送双击信号
        emit itemDoubleClicked_signal(type, id, name);
        
        // 如果是TerrainRoot、BackgroundRoot或TargetRoot下的元素，发送定位信号
        QTreeWidgetItem* parentItem = item->parent();
        if (parentItem) {
            if (parentItem == m_terrainRoot || parentItem == m_backgroundRoot || parentItem == m_targetRoot) {
                // Mte3DService::getInstance().positionPlatform(id);
                emit positionItem_signal(id);
            }
        }
    }
}

void SceneTreeWidget::onCustomContextMenuRequested(const QPoint& pos)
{
    QTreeWidgetItem* item = m_treeWidget->itemAt(pos);

    // 重置所有菜单项隐藏
    m_addTerrainAction->setVisible(false);
    m_addBackgroundAction->setVisible(false);
    m_addTargetAction->setVisible(false);

    m_clearTerrainAction->setVisible(false);
    m_clearBackgroundAction->setVisible(false);
    m_clearTargetAction->setVisible(false);

    m_deleteAction->setVisible(false);
    m_modifyAction->setVisible(false);
    m_trackAction->setVisible(false);
    m_saveSceneAction->setVisible(false);

    // 如果点击的是空白处，显示保存场景菜单
    if (!item) {
        m_saveSceneAction->setVisible(true);
        m_contextMenu->exec(m_treeWidget->mapToGlobal(pos));
        return;
    }

    // 获取项目类型
    QString type = item->data(0, Qt::UserRole).toString();

    if (type == "TerrainRoot") {
        // 根节点显示对应的添加和清除操作
        m_addTerrainAction->setVisible(true);
        m_clearTerrainAction->setVisible(true);
    } else if (type == "BackgroundRoot") {
        m_addBackgroundAction->setVisible(true);
        m_clearBackgroundAction->setVisible(true);
    } else if (type == "TargetRoot") {
        m_addTargetAction->setVisible(true);
        m_clearTargetAction->setVisible(true);
    } else if (!type.isEmpty()) {
        // 子节点显示删除和修改操作
        m_deleteAction->setVisible(true);
        m_modifyAction->setVisible(true);
        
        // 只有Target类型的节点显示航迹操作
        if (type == "Target") {
            m_trackAction->setVisible(true);
        }

        // 保存当前右键点击的项目信息
        m_currentRightClickedType = type;
        m_currentRightClickedId = item->data(0, Qt::UserRole + 1).toInt();
        m_currentRightClickedName = item->text(0);
    }

    // 只有当有可见的操作时才显示菜单
    bool hasVisibleActions = m_addTerrainAction->isVisible() ||
                            m_addBackgroundAction->isVisible() ||
                            m_addTargetAction->isVisible() ||
                            m_clearTerrainAction->isVisible() ||
                            m_clearBackgroundAction->isVisible() ||
                            m_clearTargetAction->isVisible() ||
                            m_deleteAction->isVisible() ||
                            m_modifyAction->isVisible() ||
                            m_trackAction->isVisible() ||
                            m_saveSceneAction->isVisible();

    if (hasVisibleActions) {
        m_contextMenu->exec(m_treeWidget->mapToGlobal(pos));
    }
}

void SceneTreeWidget::onSceneTreeDataChanged_slot(const SceneInfoStru& sceneInfo)
{
    // 清空所有子节点
    m_terrainRoot->takeChildren();
    m_backgroundRoot->takeChildren();
    m_targetRoot->takeChildren();

    // 重新添加所有项目
    for (const auto& terrain : sceneInfo.terrainVec) {
        addTerrainItem(terrain);
    }

    for (const auto& background : sceneInfo.backVec) {
        addBackgroundItem(background);
    }

    for (const auto& target : sceneInfo.modelVec) {
        addTargetItem(target);
    }

}

void SceneTreeWidget::onSceneTreeItemAdded_slot(const QString& type, int id, const QString& name)
{
    if (type == "Terrain") {
        MtePlatformStru terrain;
        terrain.m_id = id;
        terrain.m_name = name;
        addTerrainItem(terrain);
    } else if (type == "Background") {
        MtePlatformStru background;
        background.m_id = id;
        background.m_name = name;
        addBackgroundItem(background);
    } else if (type == "Target") {
        MtePlatformStru target;
        target.m_id = id;
        target.m_name = name;
        addTargetItem(target);
    } else if (type == "Track") {
        // 找到对应的平台节点
        QTreeWidgetItem* targetItem = findItem("Target", id);
        if (targetItem) {
            // 检查是否已经存在航迹节点
            for (int i = 0; i < targetItem->childCount(); i++) {
                QTreeWidgetItem* child = targetItem->child(i);
                if (child->data(0, Qt::UserRole).toString() == "Track") {
                    // 更新现有航迹节点
                    child->setText(0, name);
                    return;
                }
            }
            
            // 添加新的航迹节点
            QTreeWidgetItem* trackItem = new QTreeWidgetItem(targetItem);
            trackItem->setText(0, name);
            trackItem->setData(0, Qt::UserRole, "Track");
            trackItem->setData(0, Qt::UserRole + 1, id);
            targetItem->addChild(trackItem);
        }
    }
}

void SceneTreeWidget::onSceneTreeItemRemoved_slot(const QString& type, int id)
{
    removeItem(type, id);
}

void SceneTreeWidget::onSceneTreeItemModified_slot(const QString& type, int id, const QString& name)
{
    modifyItem(type, id, name);
}

void SceneTreeWidget::addTerrainItem(const MtePlatformStru& terrain)
{
    QTreeWidgetItem* item = new QTreeWidgetItem(m_terrainRoot);
    item->setText(0, terrain.m_name);
    item->setData(0, Qt::UserRole, "Terrain");
    item->setData(0, Qt::UserRole + 1, terrain.m_id);
    m_terrainRoot->addChild(item);
}

void SceneTreeWidget::addBackgroundItem(const MtePlatformStru& background)
{
    QTreeWidgetItem* item = new QTreeWidgetItem(m_backgroundRoot);
    item->setText(0, background.m_name);
    item->setData(0, Qt::UserRole, "Background");
    item->setData(0, Qt::UserRole + 1, background.m_id);
    m_backgroundRoot->addChild(item);
}

void SceneTreeWidget::addTargetItem(const MtePlatformStru& target)
{
    QTreeWidgetItem* item = new QTreeWidgetItem(m_targetRoot);
    item->setText(0, target.m_name);
    item->setData(0, Qt::UserRole, "Target");
    item->setData(0, Qt::UserRole + 1, target.m_id);
    m_targetRoot->addChild(item);
    
}


void SceneTreeWidget::removeItem(const QString& type, int id)
{
    if (type == "Track") {
        // Track是Target的子节点，需要先找到对应的Target节点
        for (int i = 0; i < m_targetRoot->childCount(); ++i) {
            QTreeWidgetItem* targetItem = m_targetRoot->child(i);
            for (int j = 0; j < targetItem->childCount(); ++j) {
                QTreeWidgetItem* child = targetItem->child(j);
                if (child->data(0, Qt::UserRole).toString() == "Track" && 
                    child->data(0, Qt::UserRole + 1).toInt() == id) {
                    delete targetItem->takeChild(j);
                    return;
                }
            }
        }
    } else {
        QTreeWidgetItem* parent = nullptr;
        if (type == "Terrain") {
            parent = m_terrainRoot;
        } else if (type == "Background") {
            parent = m_backgroundRoot;
        } else if (type == "Target") {
            parent = m_targetRoot;
        }

        if (parent) {
            for (int i = 0; i < parent->childCount(); ++i) {
                QTreeWidgetItem* child = parent->child(i);
                if (child->data(0, Qt::UserRole + 1).toInt() == id) {
                    delete parent->takeChild(i);
                    break;
                }
            }
        }
    }
}

void SceneTreeWidget::modifyItem(const QString& type, int id, const QString& name)
{
    if (type == "Track") {
        // Track是Target的子节点，需要先找到对应的Target节点
        for (int i = 0; i < m_targetRoot->childCount(); ++i) {
            QTreeWidgetItem* targetItem = m_targetRoot->child(i);
            for (int j = 0; j < targetItem->childCount(); ++j) {
                QTreeWidgetItem* child = targetItem->child(j);
                if (child->data(0, Qt::UserRole).toString() == "Track" && 
                    child->data(0, Qt::UserRole + 1).toInt() == id) {
                    child->setText(0, name);
                    return;
                }
            }
        }
    } else {
        QTreeWidgetItem* parent = nullptr;
        if (type == "Terrain") {
            parent = m_terrainRoot;
        } else if (type == "Background") {
            parent = m_backgroundRoot;
        } else if (type == "Target") {
            parent = m_targetRoot;
        }

        if (parent) {
            for (int i = 0; i < parent->childCount(); ++i) {
                QTreeWidgetItem* child = parent->child(i);
                if (child->data(0, Qt::UserRole + 1).toInt() == id) {
                    child->setText(0, name);
                    break;
                }
            }
        }
    }
}

QTreeWidgetItem* SceneTreeWidget::findItem(const QString& type, int id)
{
    if (type == "Track") {
        // Track是Target的子节点，需要先找到对应的Target节点
        for (int i = 0; i < m_targetRoot->childCount(); ++i) {
            QTreeWidgetItem* targetItem = m_targetRoot->child(i);
            for (int j = 0; j < targetItem->childCount(); ++j) {
                QTreeWidgetItem* child = targetItem->child(j);
                if (child->data(0, Qt::UserRole).toString() == "Track" && 
                    child->data(0, Qt::UserRole + 1).toInt() == id) {
                    return child;
                }
            }
        }
    } else {
        QTreeWidgetItem* parent = nullptr;
        if (type == "Terrain") {
            parent = m_terrainRoot;
        } else if (type == "Background") {
            parent = m_backgroundRoot;
        } else if (type == "Target") {
            parent = m_targetRoot;
        }

        if (parent) {
            for (int i = 0; i < parent->childCount(); ++i) {
                QTreeWidgetItem* child = parent->child(i);
                if (child->data(0, Qt::UserRole + 1).toInt() == id) {
                    return child;
                }
            }
        }
    }
    return nullptr;
}

void SceneTreeWidget::updateTreeWidgetItem(QTreeWidgetItem* item, const QString& name)
{
    if (item) {
        item->setText(0, name);
    }
}

void SceneTreeWidget::clearTerrainItems()
{
    if (m_sceneEngine) {
        // 获取所有地形ID
        QVector<int> terrainIds;
        for (int i = 0; i < m_terrainRoot->childCount(); ++i) {
            QTreeWidgetItem* child = m_terrainRoot->child(i);
            int id = child->data(0, Qt::UserRole + 1).toInt();
            terrainIds.append(id);
        }

        // 按逆序删除（从后往前删除，避免索引变化问题）
        for (int i = terrainIds.size() - 1; i >= 0; --i) {
            m_sceneEngine->onUIRemoveTerrain_slot(terrainIds[i]);
        }
    }
}

void SceneTreeWidget::clearBackgroundItems()
{
    if (m_sceneEngine) {
        // 获取所有背景ID
        QVector<int> backgroundIds;
        for (int i = 0; i < m_backgroundRoot->childCount(); ++i) {
            QTreeWidgetItem* child = m_backgroundRoot->child(i);
            int id = child->data(0, Qt::UserRole + 1).toInt();
            backgroundIds.append(id);
        }

        // 按逆序删除（从后往前删除，避免索引变化问题）
        for (int i = backgroundIds.size() - 1; i >= 0; --i) {
            m_sceneEngine->onUIRemoveBackground_slot(backgroundIds[i]);
        }
    }
}

void SceneTreeWidget::clearTargetItems()
{
    if (m_sceneEngine) {
        // 获取所有目标ID
        QVector<int> targetIds;
        for (int i = 0; i < m_targetRoot->childCount(); ++i) {
            QTreeWidgetItem* child = m_targetRoot->child(i);
            int id = child->data(0, Qt::UserRole + 1).toInt();
            targetIds.append(id);
        }

        // 按逆序删除（从后往前删除，避免索引变化问题）
        for (int i = targetIds.size() - 1; i >= 0; --i) {
            m_sceneEngine->onUIRemoveTarget_slot(targetIds[i]);
        }
    }
}

