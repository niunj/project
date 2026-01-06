#ifndef MODELTRACKEDIT_H
#define MODELTRACKEDIT_H

#include <QWidget>
#include <QCloseEvent>
#include <tuple>
#include "../OsgRender/Mte3DService.h"
#include "../Common/readwritefile.h"
#include "../Common/MteStructDef.h"

using namespace std;

class SceneEngine;  // Forward declaration of SceneEngine

namespace Ui {
class ModelTrackEdit;
}

class ModelTrackEdit : public QWidget
{
    Q_OBJECT

public:
    explicit ModelTrackEdit(QWidget *parent = nullptr);
    ~ModelTrackEdit();
    
    // 设置 SceneEngine 指针
    void setSceneEngine(SceneEngine *engine);
public slots:
    void setModelTrack();
    void cancelTrack();

    void setModelID(int modelID);
    void endTrackClicked();
    void setModelPos(double mLon, double mLat, double mAlt);
protected:
    void closeEvent(QCloseEvent* event);
    void showEvent(QShowEvent *event);

private:
    SceneEngine *m_sceneEngine = nullptr;  // SceneEngine 指针成员变量
    int model_ID = 0;

    int tableID = 0;
    QVector <QVector3D>trackData;
private:
    Ui::ModelTrackEdit *ui;
signals:
    void sig_getModelID();
    // void sig_trackInfo(ModelTrackStru);
    void sig_addTrack(int modelId, const MteTrackStru& trackData);
    void sig_removeTrack(int modelId);
    void sig_modifyTrack(int modelId, const MteTrackStru& trackData);
private slots:
    void on_pushButton_clicked();
    void on_radioButton_map_clicked();
    void on_radioButton_file_clicked();
    void on_radioButton_self_clicked();
    void on_pushButton_file_clicked();
    void on_lineEdit_keyNum_editingFinished();
};

#endif // MODELTRACKEDIT_H
