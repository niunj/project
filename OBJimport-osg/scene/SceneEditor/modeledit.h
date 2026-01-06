#ifndef MODELEDIT_H
#define MODELEDIT_H

#include <QWidget>
#include <QMovie>
#include <QLabel>
#include <QMap>
#include <QList>
#include "../OsgRender/Mte3DService.h"
#include "../Common/readwritefile.h"
#include "../SceneEngine/sceneengine.h"
//#include "createmodel.h"
//#include "ThermalCalculate/fluentmesh.h"

namespace Ui {
class ModelEdit;
}

class ModelEdit : public QWidget
{
    Q_OBJECT

public:
    explicit ModelEdit(QWidget *parent = nullptr);
    ~ModelEdit();
    
    // 设置编辑模式
    void setEditMode(bool mode);
    
    // 设置编辑参数
    void setEditParam(const MtePlatformStru& params);
    
public:
    void loadModel();
    void loadMesh();
    void paraserModel();
    void addModel();

    void setModel();
    void setModelPos(double mLon,double mLat,double mAlt);
    void hasAdded();

    void checkStateChange(int state);
    void checkAndDisplayFileInfo(const QString& modelPath);
protected:
    void startProcessing();
private:
    bool isExhus = false;
    bool m_isEditMode; // 是否处于编辑模式
    int m_modelId; // 编辑时的模型ID
    
    QMovie *movie;
    QLabel* label;
    QLabel* m_fileInfoLabel; // 用于显示文件存在情况的提示标签

    // SceneEngine指针
    SceneEngine *m_sceneEngine;
public:
    // 设置SceneEngine指针
    void setSceneEngine(SceneEngine *engine);
private:
    Ui::ModelEdit *ui;
signals:
    void sig_modelInfo(MtePlatformStru&);
    void sig_loadedFinish();
    // 添加模型信号
    void sig_addModel(const MtePlatformStru& params);
    // 修改模型信号
    void sig_modifyModel(const MtePlatformStru& params);
};

#endif // MODELEDIT_H
