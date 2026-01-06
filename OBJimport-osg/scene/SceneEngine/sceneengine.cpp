#include "SceneEngine.h"
#include <QDebug>

#include <QDateTime>
#include <QTimer>
#include <QFile>
#include <QDir>

#include "../OsgRender/Mte3DService.h"
#include "../OsgRender/OsgContext.h"
#include "../Log/log_manager.h"




bool copyFileToDir(const QString& sourceFilePath, const QString& targetFolderPath, bool overwrite)
{
    // 检查源文件是否存在
    QFile sourceFile(sourceFilePath);
    if (!sourceFile.exists()) {
        LOG_DEBUG << "错误: 源文件不存在 -" << sourceFilePath;
        return false;
    }

    // 确保目标文件夹存在
    QDir targetDir(targetFolderPath);
    if (!targetDir.exists()) {
        if (!targetDir.mkpath(".")) {
            LOG_DEBUG << "错误: 无法创建目标文件夹 -" << targetFolderPath;
            return false;
        }
    }

    // 构建目标文件的完整路径
    QFileInfo sourceFileInfo(sourceFilePath);
    QString targetFilePath = targetFolderPath + QDir::separator() + sourceFileInfo.fileName();

    // 如果目标文件已存在，根据overwrite参数决定是否覆盖
    QFile targetFile(targetFilePath);
    if (targetFile.exists()) {
        if (overwrite) {
            if (!targetFile.remove()) {
                LOG_DEBUG << "错误: 无法删除已存在的目标文件 -" << targetFilePath;
                return false;
            }
        }
        else {
            LOG_DEBUG << "错误: 目标文件已存在 -" << targetFilePath;
            return false;
        }
    }

    // 尝试复制文件
    if (sourceFile.copy(targetFilePath)) {
        LOG_DEBUG << "文件复制成功:" << sourceFilePath << "->" << targetFilePath;
        return true;
    }
    else {
        LOG_DEBUG << "错误: 文件复制失败 -" << sourceFile.errorString();
        return false;
    }

}

SceneEngine::SceneEngine(QObject *parent)
    : QObject(parent)
{
    // 初始化场景
    m_currentScene.sceneName = "DefaultScene";
    m_currentScene.scenePath = "";
    m_currentScene.strDateTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    // 初始化模型索引计数器
    modelIndex = 0;

    qRegisterMetaType<MtePlatformStru>("MtePlatformStru");
    qRegisterMetaType<QVector<int>>("QVector<int>");


    // // 绑定平台信号
    bindPlatformSignals();
    
}

// 获取模型索引（自增）
int SceneEngine::getIndex()
{
    return ++modelIndex;
}

// 设置模型索引
void SceneEngine::setIndex(int index)
{
    modelIndex = index;
}

SceneEngine::~SceneEngine()
{
}

SceneInfoStru SceneEngine::getSceneInfo() const
{
    // 与getCurrentScene方法实现相同，返回当前场景信息
    QMutexLocker locker(&m_sceneMutex);
    return m_currentScene;
}

bool SceneEngine::isSceneEmpty() const
{
    QMutexLocker locker(&m_sceneMutex);
    
    // 判断目标、背景、地形是否为空
    if (m_currentScene.modelVec.isEmpty() && 
        m_currentScene.backVec.isEmpty() && 
        m_currentScene.terrainVec.isEmpty()) {
        return true;
    }
    
    return false;
}


// 来自外部界面的槽函数实现
void SceneEngine::onUIAddTerrain_slot(const MtePlatformStru& terrain)
{
    QMutexLocker locker(&m_sceneMutex);

    m_currentScene.terrainVec.append(terrain);

    emit sceneTreeItemAdded_signal("Terrain", terrain.m_id, terrain.m_name);
    emit threeDSceneTerrainAdded_signal(terrain);
}

void SceneEngine::onUIRemoveTerrain_slot(int terrainId)
{
    QMutexLocker locker(&m_sceneMutex);
    int index = findTerrainIndex(terrainId);
    if (index >= 0) {
        MtePlatformStru removedTerrain = m_currentScene.terrainVec[index];
        QString path = removedTerrain.m_path;
        m_currentScene.terrainVec.remove(index);

        emit sceneTreeItemRemoved_signal("Terrain", terrainId);
        emit threeDSceneTerrainRemoved_signal(terrainId);
        

    }
}

void SceneEngine::onUIModifyTerrain_slot(const MtePlatformStru& terrain)
{
    QMutexLocker locker(&m_sceneMutex);
    int index = findTerrainIndex(terrain.m_id);
    if (index >= 0) {
        // 保存旧的平台信息
        MtePlatformStru oldTerrain = m_currentScene.terrainVec[index];
        QString oldPath = oldTerrain.m_path;
        

        m_currentScene.terrainVec[index] = terrain;

        emit sceneTreeItemModified_signal("Terrain", terrain.m_id, terrain.m_name);
        emit threeDSceneTerrainModified_signal(terrain);
        

    }
}

void SceneEngine::onUIAddBackground_slot(const MtePlatformStru& background)
{
    QMutexLocker locker(&m_sceneMutex);

    m_currentScene.backVec.append(background);

    emit sceneTreeItemAdded_signal("Background", background.m_id, background.m_name);
    emit threeDSceneBackgroundAdded_signal(background);
}

void SceneEngine::onUIRemoveBackground_slot(int backgroundId)
{
    QMutexLocker locker(&m_sceneMutex);
    int index = findBackgroundIndex(backgroundId);
    if (index >= 0) {
        MtePlatformStru removedBackground = m_currentScene.backVec[index];
        QString path = removedBackground.m_path;
        m_currentScene.backVec.remove(index);

        emit sceneTreeItemRemoved_signal("Background", backgroundId);
        emit threeDSceneBackgroundRemoved_signal(backgroundId);
        

    }
}

void SceneEngine::onUIModifyBackground_slot(const MtePlatformStru& background)
{
    QMutexLocker locker(&m_sceneMutex);
    int index = findBackgroundIndex(background.m_id);
    if (index >= 0) {
        // 保存旧的平台信息
        MtePlatformStru oldBackground = m_currentScene.backVec[index];
        QString oldPath = oldBackground.m_path;
        
        // 修改平台信息并处理材质

        m_currentScene.backVec[index] = background;

        emit sceneTreeItemModified_signal("Background", background.m_id, background.m_name);
        emit threeDSceneBackgroundModified_signal(background);
        
    }
}

void SceneEngine::onUIAddTarget_slot(const MtePlatformStru& target)
{
    QMutexLocker locker(&m_sceneMutex);

    m_currentScene.modelVec.append(target);

    emit sceneTreeItemAdded_signal("Target", target.m_id, target.m_name);
    emit threeDSceneTargetAdded_signal(target);
}

// 统一平台添加槽函数实现，根据平台类型分发到不同的处理函数
void SceneEngine::onUIAddPlatform_slot(const MtePlatformStru& platform)
{
    // 根据平台类型调用对应的添加函数
    switch (platform.modelType)
    {
    case ModelType::GEO: // 地形
        onUIAddTerrain_slot(platform);
        break;
    case ModelType::BACKGROUND: // 背景
        onUIAddBackground_slot(platform);
        break;
    case ModelType::OBJECT: // 目标
        onUIAddTarget_slot(platform);
        break;
    default:
        LOG_DEBUG << "未知平台类型: " << static_cast<int>(platform.modelType);
        break;
    }
}


void  SceneEngine::onUIClearScene_slot()
{

}


// 检查路径是否仍在使用中
bool SceneEngine::isPathStillInUse(const QString& path)
{
    if (path.isEmpty()) {
        return false;
    }
    
    // 获取文件夹路径作为材质名
    QFileInfo fileInfo(path);
    QString materialName = fileInfo.absolutePath();
    
    // 检查地形
    for (const auto& terrain : m_currentScene.terrainVec) {
        if (terrain.m_path == path) {
            return true;
        }
    }
    
    // 检查背景
    for (const auto& background : m_currentScene.backVec) {
        if (background.m_path == path) {
            return true;
        }
    }
    
    // 检查目标
    for (const auto& target : m_currentScene.modelVec) {
        if (target.m_path == path) {
            return true;
        }
    }
    
    return false;
}



void SceneEngine::onUIRemoveTarget_slot(int targetId)
{
    QMutexLocker locker(&m_sceneMutex);
    int index = findTargetIndex(targetId);
    if (index >= 0) {
        MtePlatformStru removedTarget = m_currentScene.modelVec[index];
        QString path = removedTarget.m_path;
        m_currentScene.modelVec.remove(index);

        emit sceneTreeItemRemoved_signal("Target", targetId);
        emit threeDSceneTargetRemoved_signal(targetId);


    }
}

// 三维场景发送的平台删除信号槽实现
void SceneEngine::onThreeDSceneRemovePlatform_slot(int platformId)
{
    QMutexLocker locker(&m_sceneMutex);
    
    // 先检查是否是目标平台
    int targetIndex = findTargetIndex(platformId);
    if (targetIndex >= 0) {
        MtePlatformStru removedTarget = m_currentScene.modelVec[targetIndex];
        QString path = removedTarget.m_path;
        m_currentScene.modelVec.remove(targetIndex);

        emit sceneTreeItemRemoved_signal("Target", platformId);
        emit threeDSceneTargetRemoved_signal(platformId);
        return;
    }
    
    // 检查是否是背景平台
    int backgroundIndex = findBackgroundIndex(platformId);
    if (backgroundIndex >= 0) {
        MtePlatformStru removedBackground = m_currentScene.backVec[backgroundIndex];
        QString path = removedBackground.m_path;
        m_currentScene.backVec.remove(backgroundIndex);

        emit sceneTreeItemRemoved_signal("Background", platformId);
        emit threeDSceneBackgroundRemoved_signal(platformId);
        return;
    }
    
    // 检查是否是地形平台
    int terrainIndex = findTerrainIndex(platformId);
    if (terrainIndex >= 0) {
        MtePlatformStru removedTerrain = m_currentScene.terrainVec[terrainIndex];
        QString path = removedTerrain.m_path;
        m_currentScene.terrainVec.remove(terrainIndex);

        emit sceneTreeItemRemoved_signal("Terrain", platformId);
        emit threeDSceneTerrainRemoved_signal(platformId);
        return;
    }
    
    // 如果没有找到，记录日志
    LOG_DEBUG << "未找到要删除的平台，ID: " << platformId;
}

void SceneEngine::onUIModifyTarget_slot(const MtePlatformStru& target)
{
    QMutexLocker locker(&m_sceneMutex);
    int index = findTargetIndex(target.m_id);
    if (index >= 0) {
        // 保存旧的平台信息
        MtePlatformStru oldTarget = m_currentScene.modelVec[index];
        QString oldPath = oldTarget.m_path;
        
        m_currentScene.modelVec[index] = target;

        emit sceneTreeItemModified_signal("Target", target.m_id, target.m_name);
        emit threeDSceneTargetModified_signal(target);
        
    }
}


#include <QThread>

// 辅助方法实现
void SceneEngine::updateSceneTree()
{
    LOG_INFO<< QThread::currentThreadId();

    emit sceneTreeDataChanged_signal(m_currentScene);
}

void SceneEngine::updateThreeDScene()
{
    // 发送所有场景元素到三维场景
    for (const auto& terrain : m_currentScene.terrainVec) {
        emit threeDSceneTerrainAdded_signal(terrain);
    }

    for (const auto& background : m_currentScene.backVec) {
        emit threeDSceneBackgroundAdded_signal(background);
    }

    for (const auto& target : m_currentScene.modelVec) {
        emit threeDSceneTargetAdded_signal(target);
    }

}

// 查找方法实现
MtePlatformStru SceneEngine::findTerrain(int id) const
{
    for (int i = 0; i < m_currentScene.terrainVec.size(); ++i) {
        if (m_currentScene.terrainVec[i].m_id == id) {
            return m_currentScene.terrainVec[i];
        }
    }
    return MtePlatformStru(); // 返回空对象
}

MtePlatformStru SceneEngine::findBackground(int id) const
{
    for (int i = 0; i < m_currentScene.backVec.size(); ++i) {
        if (m_currentScene.backVec[i].m_id == id) {
            return m_currentScene.backVec[i];
        }
    }
    return MtePlatformStru(); // 返回空对象
}

MtePlatformStru SceneEngine::findTarget(int id) const
{
    for (int i = 0; i < m_currentScene.modelVec.size(); ++i) {
        if (m_currentScene.modelVec[i].m_id == id) {
            return m_currentScene.modelVec[i];
        }
    }
    return MtePlatformStru(); // 返回空对象
}



int SceneEngine::findTerrainIndex(int id) const
{
    for (int i = 0; i < m_currentScene.terrainVec.size(); ++i) {
        if (m_currentScene.terrainVec[i].m_id == id) {
            return i;
        }
    }
    return -1;
}

int SceneEngine::findBackgroundIndex(int id) const
{
    for (int i = 0; i < m_currentScene.backVec.size(); ++i) {
        if (m_currentScene.backVec[i].m_id == id) {
            return i;
        }
    }
    return -1;
}

int SceneEngine::findTargetIndex(int id) const
{
    for (int i = 0; i < m_currentScene.modelVec.size(); ++i) {
        if (m_currentScene.modelVec[i].m_id == id) {
            return i;
        }
    }
    return -1;
}

MtePlatformStru SceneEngine::getPlatformInfo(int modelId) const
{
    QMutexLocker locker(&m_sceneMutex);

    // 查找指定ID的模型
    for (const auto& model : m_currentScene.modelVec) {
        if (model.m_id == modelId) {
            return model;
        }
    }

    // 如果没找到，返回默认值
    return MtePlatformStru();
}



void SceneEngine::bindPlatformSignals()
{
    // 获取Mte3DService实例
    Mte3DService& mte3DService = Mte3DService::getInstance();

    // 绑定地形相关信号
    connect(this, &SceneEngine::threeDSceneTerrainAdded_signal, [&mte3DService,this](const MtePlatformStru& terrain) {
        mte3DService.addPlatform(terrain);
    });

    connect(this, &SceneEngine::threeDSceneTerrainRemoved_signal, [&mte3DService](int id) {
        // mte3DService.deletePlatform(id);
    });

    connect(this, &SceneEngine::threeDSceneTerrainModified_signal, [&mte3DService](const MtePlatformStru& terrain) {
        // 先删除旧的平台，再添加新的平台来实现修改
        // mte3DService.deletePlatform(terrain.m_id);
        // mte3DService.addPlatform(terrain, getMaterialTextureByPlatformId(terrain.m_id));
    });

    // 绑定背景相关信号
    connect(this, &SceneEngine::threeDSceneBackgroundAdded_signal, [&mte3DService,this](const MtePlatformStru& background) {
        mte3DService.addPlatform(background);
    });

    connect(this, &SceneEngine::threeDSceneBackgroundRemoved_signal, [&mte3DService](int id) {
        // mte3DService.deletePlatform(id);
    });

    connect(this, &SceneEngine::threeDSceneBackgroundModified_signal, [&mte3DService](const MtePlatformStru& background) {
        // 先删除旧的平台，再添加新的平台来实现修改
        // mte3DService.deletePlatform(background.m_id);
        // mte3DService.addPlatform(background, getMaterialTextureByPlatformId());
    });

    // 绑定目标相关信号
    connect(this, &SceneEngine::threeDSceneTargetAdded_signal, [&mte3DService,this](const MtePlatformStru& target) {
        mte3DService.addPlatform(target);
    });
    
    connect(this, &SceneEngine::threeDSceneTargetRemoved_signal, [&mte3DService](int id) {
        // mte3DService.deletePlatform(id);
    });
    
    connect(this, &SceneEngine::threeDSceneTargetModified_signal, [&mte3DService](const MtePlatformStru& target) {
        // 先删除旧的平台，再添加新的平台来实现修改
        // mte3DService.deletePlatform(target.m_id);
        // mte3DService.addPlatform(target);
    });
}



// 保存场景信息到二进制文件
bool SceneEngine::saveSceneToBin(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        LOG_DEBUG << "无法打开文件进行写入:" << filePath;
        return false;
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_5_0); // 设置数据流版本

    // 写入场景信息
    out << m_currentScene;


    file.close();

    //保存场景时,不需要刷新场景树和三维

    return true;
}


// 从二进制文件加载场景信息
bool SceneEngine::loadSceneFromBin(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        LOG_DEBUG << "无法打开文件进行读取:" << filePath;
        return false;
    }

    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_5_0); // 设置数据流版本

    // 读取场景信息
    in >> m_currentScene;


    file.close();

    // 更新场景路径并发送信号
    QMutexLocker locker(&m_sceneMutex);
    m_currentScene.scenePath = QFileInfo(filePath).absolutePath();

    // 辅助方法实现
    updateSceneTree();
    updateThreeDScene();

    return true;
}


