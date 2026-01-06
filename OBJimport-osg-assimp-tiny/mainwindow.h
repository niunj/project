#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <assimp/scene.h>
#include <vector>
#include <string>
#include "TinyObj/tiny_obj_loader.h"

// OSG相关头文件
#include <osg/Node>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgViewer/Viewer>
#include <osg/MatrixTransform>
#include <osg/PositionAttitudeTransform>
#include <osg/Geode>
#include <osg/ShapeDrawable>

// 引入osgQtViewer类
#include "osgQtViewer.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

// 存储Assimp加载的模型数据
struct AssimpModelData {
    aiScene* scene;
    aiVector3D position;
};

// 存储TinyObj加载的模型数据
struct TinyObjModelData {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    aiVector3D position;
};

// 存储OSG加载的模型数据
struct OSGModelData {
    osg::ref_ptr<osg::Node> node;
    aiVector3D position;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_assimpReadBtn_clicked();
    void on_assimpWriteBtn_clicked();
    void on_tinyobjReadBtn_clicked();
    void on_tinyobjWriteBtn_clicked();
    void on_osgReadBtn_clicked();
    void on_osgWriteBtn_clicked();

private:
    Ui::MainWindow *ui;
    int assimpLoadCount;
    int tinyobjLoadCount;
    int osgLoadCount;
    QString defaultObjPath;
    float assimpPositionOffset;
    float tinyobjPositionOffset;
    float osgPositionOffset;
    
    // 存储多个模型
    std::vector<AssimpModelData> assimpModels;
    std::vector<TinyObjModelData> tinyobjModels;
    std::vector<OSGModelData> osgModels;
    
    // OSG渲染相关
    osg::ref_ptr<osg::Group> assimpSceneRoot;
    osg::ref_ptr<osg::Group> tinyobjSceneRoot;
    osg::ref_ptr<osg::Group> osgSceneRoot;
    
    // 使用osgQtViewer类
    osgQtViewer *assimpViewerWidget;
    osgQtViewer *tinyobjViewerWidget;
    osgQtViewer *osgViewerWidget;
    
    void updateCountLabels();
    void setupOSGViewers();
    void addDefaultGeometry(osg::ref_ptr<osg::Group> &sceneRoot);
};
#endif // MAINWINDOW_H