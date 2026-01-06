#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDebug>
#include <iostream>
#include <osgGA/TrackballManipulator>
#include <osg/Texture2D>
#include <osgDB/ReadFile>
#include <osgUtil/Optimizer>
#include <osg/ComputeBoundsVisitor>
#include <osg/Node>
#include <osgViewer/CompositeViewer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , osgLoadCount(0)
    , defaultObjPath("data/BTR-70.obj")
    , osgPositionOffset(0.0f)
    , osgCompositeViewerWidget(nullptr)
    , _mte3DService(new Mte3DServiceNew(this))
{
    ui->setupUi(this);
    setWindowTitle("OBJ Import/Export Tool");
    
    // 连接Mte3DServiceNew的信号
    connect(_mte3DService, &Mte3DServiceNew::loadCompleted, this, [this](int count) {
        osgLoadCount = count;
        updateCountLabels();
        QMessageBox::information(this, "Success", QString("OSG Read Success!\nSuccessfully Loaded: %1 models").arg(count));
    });
    
    connect(_mte3DService, &Mte3DServiceNew::loadFailed, this, [this](const QString& fileName, const QString& error) {
        QMessageBox::warning(this, "Warning", QString("Load Failed: %1\nError: %2").arg(fileName, error));
    });
    
    connect(_mte3DService, &Mte3DServiceNew::saveCompleted, this, [this](const QString& filePath, int modelCount) {
        QMessageBox::information(this, "Success", QString("OSG Write Success!\nFile: %1\nTotal Models: %2").arg(filePath, QString::number(modelCount)));
    });
    
    connect(_mte3DService, &Mte3DServiceNew::saveFailed, this, [this](const QString& filePath, const QString& error) {
        QMessageBox::critical(this, "Error", QString("Save Failed: %1\nError: %2").arg(filePath, error));
    });
    
    // 初始化OSG渲染器 - 可以通过修改此处的条件来切换
    bool useCompositeViewer = true;
    if (useCompositeViewer) {
        setupOSGCompositeViewers();
    }
    
    updateCountLabels();
}

MainWindow::~MainWindow()
{
    // 释放OSG渲染相关资源
    
    if (osgCompositeViewerWidget) {
        delete osgCompositeViewerWidget;
    }
    
    delete ui;
}

void MainWindow::updateCountLabels()
{
    QLabel* osgCountLabel = ui->osgCountLabel;
    if (osgCountLabel) {
        osgCountLabel->setText(QString("OSG Load Count: %1").arg(osgLoadCount));
    }
}


void MainWindow::setupOSGCompositeViewers()
{
    osgSceneRoot = _mte3DService->getSceneRoot();
    
    osgCompositeViewerWidget = new osgQtCompositeViewer;
    
    // 创建第一个视图
    osg::ref_ptr<osgViewer::View> view1 = new osgViewer::View;
    view1->setSceneData(osgSceneRoot);
    
    osg::ref_ptr<osgGA::CameraManipulator> manipulator1 = new osgGA::TrackballManipulator();
    view1->setCameraManipulator(manipulator1);
    
    osg::Vec3 eye1(10, 10, 10);
    osg::Vec3 center1(0, 0, 0);
    osg::Vec3 up1(0, 0, 1);
    view1->getCamera()->setViewMatrixAsLookAt(eye1, center1, up1);
    
    osg::Camera* camera1 = view1->getCamera();
    camera1->setProjectionMatrixAsPerspective(30.0f, 1.0f, 1.0f, 10000.0f);
    camera1->setClearColor(osg::Vec4(0.2f, 0.2f, 0.2f, 1.0f));
    
    osgCompositeViewerWidget->addView(view1, 0);
    
    
    osgCompositeViewerWidget->setMinimumSize(300, 200);
    osgCompositeViewerWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    QWidget* osgViewContainer = ui->osgViewContainer;
    if (osgViewContainer) {
        QVBoxLayout* layout = new QVBoxLayout(osgViewContainer);
        
        QLabel* titleLabel = new QLabel("3D Composite Viewer");
        titleLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(titleLabel);
        
        layout->addWidget(osgCompositeViewerWidget);
    }
}

void MainWindow::addDefaultGeometry(osg::ref_ptr<osg::Group> &sceneRoot)
{
    osg::ref_ptr<osg::Geode> geode = new osg::Geode;
    osg::ref_ptr<osg::Box> box = new osg::Box(osg::Vec3(0, 0, 0), 2.0f);
    osg::ref_ptr<osg::ShapeDrawable> shapeDrawable = new osg::ShapeDrawable(box);
    shapeDrawable->setColor(osg::Vec4(1.0f, 0.5f, 0.0f, 1.0f));
    geode->addDrawable(shapeDrawable);
    sceneRoot->addChild(geode);
}

void MainWindow::on_osgReadBtn_clicked()
{
    QString dirPath = QFileDialog::getExistingDirectory(this, "Select Directory", "data");
    if (dirPath.isEmpty())
        return;
    
    // 遍历目录中的所有OBJ文件
    QDir dir(dirPath);
    QStringList filters;
    filters << "*.obj";
    dir.setNameFilters(filters);
    QFileInfoList fileList = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    
    if (fileList.isEmpty())
    {
        QMessageBox::warning(this, "Warning", "No OBJ files found in directory.");
        return;
    }
    
    // 清除现有场景
    _mte3DService->clearScene();
    
    // 加载每个OBJ文件
    for (int i = 0; i < fileList.size(); ++i)
    {
        const QFileInfo& fileInfo = fileList[i];
        QString modelPath = fileInfo.absoluteFilePath();
        
        // 为每个模型添加位置偏移
        float offset = i * 100.0f;
        
        // 使用addPlatform加载模型
        int platformID = _mte3DService->addPlatform(
            modelPath,
            QVector3D(0.0f, 0.0f, 0.0f),  // 位置
            QVector3D(0.0f, 0.0f, 0.0f),     // 姿态
            1.0f                           // 缩放
        );
        
        if (platformID >= 0)
        {
            osgLoadCount++;
        }
    }
    
    // 更新计数标签
    updateCountLabels();
    QMessageBox::information(this, "Success", QString("OSG Read Success!\nSuccessfully Loaded: %1 models").arg(osgLoadCount));
}

void MainWindow::on_osgWriteBtn_clicked()
{
    QString filePath = QFileDialog::getSaveFileName(this, "Save OBJ File", "", "OBJ Files (*.obj);;All Files (*.*)");
    if (filePath.isEmpty())
        return;
    
    // 使用新的saveSceneToObj接口保存场景
    bool success = _mte3DService->saveSceneToObj(filePath);
    
    if (success)
    {
        QMessageBox::information(this, "Success", QString("OSG Write Success!\nFile: %1").arg(filePath));
    }
    else
    {
        QMessageBox::critical(this, "Error", "Failed to save OBJ file.");
    }
}
