#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <vector>
#include <string>

// OSG相关头文件
#include <osg/Node>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgViewer/Viewer>
#include <osg/MatrixTransform>
#include <osg/PositionAttitudeTransform>
#include <osg/Geode>
#include <osg/ShapeDrawable>
#include <osg/Vec3>

// 引入osgQtViewer类

// 引入osgQtCompositeViewer类
#include "scene/OsgRender/osgQtCompositeViewer.h"

// 引入Mte3DServiceNew类
#include "Mte3DServiceNew.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_osgReadBtn_clicked();
    void on_osgWriteBtn_clicked();

private:
    Ui::MainWindow *ui;
    int osgLoadCount;
    QString defaultObjPath;
    float osgPositionOffset;
    
    // 使用Mte3DServiceNew类处理3D模型
    Mte3DServiceNew* _mte3DService;
    
    // OSG渲染相关
    osg::ref_ptr<osg::Group> osgSceneRoot;
    

    // 使用osgQtCompositeViewer类
    osgQtCompositeViewer *osgCompositeViewerWidget;
    
    void updateCountLabels();
    void setupOSGCompositeViewers();
    void addDefaultGeometry(osg::ref_ptr<osg::Group> &sceneRoot);
};
#endif // MAINWINDOW_H
