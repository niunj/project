#ifndef SCENEENGINE_H
#define SCENEENGINE_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QTimer>
#include <QMutex>
#include <QHash>
#include <QSharedPointer>
#include <QProcess>

#include "../Common/MteStructDef.h"  // 包含你提供的完整结构体定义
#include "../Common/readwritefile.h"  // 包含ReadWriteFile类定义



class SceneEngine : public QObject
{
    Q_OBJECT

public:
    explicit SceneEngine(QObject *parent = nullptr);
    ~SceneEngine();

    // 获取当前场景信息
    SceneInfoStru getSceneInfo() const; // 新增的getSceneInfo方法
public:

    // 从二进制文件加载场景信息
    bool loadSceneFromBin(const QString& filePath);

    // 保存场景到二进制文件
    bool saveSceneToBin(const QString& filePath);
    
    // 判断场景是否为空
    bool isSceneEmpty() const;
    


signals:
    // 传递到外部场景树的信号
    void sceneTreeDataChanged_signal(const SceneInfoStru& sceneInfo);
// 场景树操作信号
    void sceneTreeItemAdded_signal(const QString& itemType, int id, const QString& name);
    void sceneTreeItemRemoved_signal(const QString& itemType, int id);
    void sceneTreeItemModified_signal(const QString& itemType, int id, const QString& name);
    
    // 传递到三维场景的信号
    void threeDSceneTerrainAdded_signal(const MtePlatformStru& terrain);
    void threeDSceneTerrainRemoved_signal(int id);
    void threeDSceneTerrainModified_signal(const MtePlatformStru& terrain);

    void threeDSceneBackgroundAdded_signal(const MtePlatformStru& background);
    void threeDSceneBackgroundRemoved_signal(int id);
    void threeDSceneBackgroundModified_signal(const MtePlatformStru& background);

    void threeDSceneTargetAdded_signal(const MtePlatformStru& target);
    void threeDSceneTargetRemoved_signal(int id);
    void threeDSceneTargetModified_signal(const MtePlatformStru& target);

    void threeDSceneTargetPositionChanged_signal(int targetId, double x, double y, double z);
    void threeDSceneSceneCleared_signal();
    
    // 轨迹相关信号
    void threeDSceneTrackAdded_signal(int modelId, const MteTrackStru& trackData);
    void threeDSceneTrackRemoved_signal(int modelId);
    void threeDSceneTrackModified_signal(int modelId, const MteTrackStru& trackData);
    
    // 天气参数细粒度变化信号
    // void cloudParametersChanged_signal(const MteWeatherStru& weather);
    // void rainEnabledChanged_signal(bool enabled);
    // void rainParametersChanged_signal(const MteWeatherStru& weather);
    // void snowEnabledChanged_signal(bool enabled);
    // void snowParametersChanged_signal(const MteWeatherStru& weather);
    // void fogEnabledChanged_signal(bool enabled);
    // void fogParametersChanged_signal(const MteWeatherStru& weather);
    

public slots:
    // 来自外部界面的槽函数
    void onUIAddTerrain_slot(const MtePlatformStru& terrain);
    void onUIRemoveTerrain_slot(int terrainId);
    void onUIModifyTerrain_slot(const MtePlatformStru& terrain);
    

    //背景
    void onUIAddBackground_slot(const MtePlatformStru& background);
    void onUIRemoveBackground_slot(int backgroundId);
    void onUIModifyBackground_slot(const MtePlatformStru& background);

    //目标
    void onUIAddTarget_slot(const MtePlatformStru& target);
    void onUIRemoveTarget_slot(int targetId);
    void onUIModifyTarget_slot(const MtePlatformStru& target);
    // 统一平台添加槽函数，根据平台类型分发到不同的处理函数
    void onUIAddPlatform_slot(const MtePlatformStru& platform);
    
    // 三维场景发送的平台删除信号槽
    void onThreeDSceneRemovePlatform_slot(int platformId);
    

    // 轨迹操作
    // void onUIAddTrack_slot(int modelId, const MteTrackStru& trackData);
    // void onUIRemoveTrack_slot(int modelId);
    // void onUIModifyTrack_slot(int modelId, const MteTrackStru& trackData);
    // void onNetAddTrack_slot(int modelId, const MteTrackStru& trackData);
    // void onNetDelTrack_slot(int modelId);
    // void onNetModifyTrack_slot(int modelId, const MteTrackStru& trackData);


    void onUIClearScene_slot();


private:
    SceneInfoStru               m_currentScene;
    mutable             QMutex  m_sceneMutex;
    QProcess                    m_process;
    int                         modelIndex; // 用于生成模型索引的计数器

    // 辅助方法
    void updateSceneTree();
    void updateThreeDScene();

public:
    // 获取模型索引（自增）
    int getIndex();
    // 设置模型索引
    void setIndex(int index);
    
    MtePlatformStru getPlatformInfo(int modelId) const;


public:
    // 查找方法
    // 查找方法实现
    MtePlatformStru findTerrain(int id) const;

    MtePlatformStru findBackground(int id) const;

    MtePlatformStru findTarget(int id) const;


    int findTerrainIndex(int id) const;

    int findBackgroundIndex(int id) const;

    int findTargetIndex(int id) const;

    
private:
    // 检查路径是否仍在使用中
    bool isPathStillInUse(const QString& path);

    // 绑定平台信号
    void bindPlatformSignals();
    
};

#endif // SCENEENGINE_H
