#ifndef BACKGROUNDEDIT_H
#define BACKGROUNDEDIT_H

#include <QWidget>
#include "../OsgRender/Mte3DService.h"
#include "../SceneEngine/sceneengine.h"
#include <QMovie>
#include <QLabel>
#include <QFutureWatcher>

namespace Ui {
class BackGroundEdit;
}

class BackGroundEdit : public QWidget
{
    Q_OBJECT

public:
    explicit BackGroundEdit(QWidget *parent = nullptr);
    ~BackGroundEdit();
    
    // 设置编辑模式
    void setEditMode(bool mode);
    
    // 设置编辑参数
    void setEditParam(const MtePlatformStru& params);

    void addBackGround();
	void hasAdded();


private:
	QMovie* movie;
	QLabel* label;
	QLabel* m_fileInfoLabel; // 用于显示文件存在情况的提示标签
	bool m_isEditMode; // 是否处于编辑模式
	int m_backgroundId; // 编辑时的背景ID
    
    // SceneEngine指针
    SceneEngine *m_sceneEngine;
public:
    // 设置SceneEngine指针
    void setSceneEngine(SceneEngine *engine);
    // 检查并显示相关文件的存在情况
    void checkAndDisplayFileInfo(const QString& modelPath);

private slots:
    void on_Btn_model_clicked();

    void on_pushButton_OK_clicked();

    void on_pushButton_Cancel_clicked();

    void on_checkBox_select_stateChanged(int arg1);

public slots:
    void setModelPos(double mLon, double mLat, double mAlt);
private:
    Ui::BackGroundEdit *ui;
signals:
    void sig_backModel(MtePlatformStru&);
	void sig_loadedFinish();
    // 添加背景信号
    void sig_addBackground(const MtePlatformStru& params);
    // 修改背景信号
    void sig_modifyBackground(const MtePlatformStru& params);
};

#endif // BACKGROUNDEDIT_H
