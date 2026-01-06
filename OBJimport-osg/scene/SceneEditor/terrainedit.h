#ifndef TERRAINEDIT_H
#define TERRAINEDIT_H

#include <QWidget>
#include <QFileDialog>
#include <QMovie>
#include <QLabel>
#include "../OsgRender/Mte3DService.h"
#include "../SceneEngine/sceneengine.h"


namespace Ui {
class TerrainEdit;
}

class TerrainEdit : public QWidget
{
    Q_OBJECT

public:
    explicit TerrainEdit(QWidget *parent = nullptr);
    ~TerrainEdit();
    
    // 设置编辑模式
    void setEditMode(bool mode);
    
    // 设置编辑参数
    void setEditParam(const MtePlatformStru& params);
    
private slots:
    void on_Btn_model_clicked();

    void on_pushButton_OK_clicked();

    void on_pushButton_Cancel_clicked();

    void addTerrain();
    void setModelPos(double mLon,double mLat,double mAlt);
    void hasAdded();

    // 检查并显示相关文件的存在情况
    void checkAndDisplayFileInfo(const QString& modelPath);
private:
    //编辑地形参数还是添加地形参数
    bool        m_bEdit =   false;
    int         m_terrainId = -1;  // 编辑时的地形ID

	QMovie* movie;
	QLabel* label;
	QLabel* m_fileInfoLabel; // 用于显示文件存在情况的提示标签
    Ui::TerrainEdit *ui;
    
    // SceneEngine指针
    SceneEngine *m_sceneEngine;
public:
    // 设置SceneEngine指针
    void setSceneEngine(SceneEngine *engine);
signals:
    void sig_terrainModel(MtePlatformStru&);
	void sig_loadedFinish();
    void sig_LLH(double,double);
    // 添加地形信号
    void sig_addTerrain(const MtePlatformStru& params);
    // 修改地形信号
    void sig_modifyTerrain(const MtePlatformStru& params);
};

#endif // TERRAINEDIT_H
