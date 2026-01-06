#include "scenewindow.h"
#include "ui_scenewindow.h"
#include "./Ribbon/QRibbon.h"
#include <QFileDialog>
#include <QFile>
#include <QMessageBox>
#include <QProgressDialog>
#include "Common/MyMessageBox.h"

#include "SceneEngine/scenetreewidget.h"

// TempDirManager相关功能已移至CommonFunction类中
#include <QDir>
#include <QFileInfo>
#include <vector>
#include <string>
#include <QDockWidget>


#include "Log/log_manager.h"


#include "OsgRender/Mte3DService.h"
#include "OsgRender/OsgContext.h"

#include "Common/MteStructDef.h"
#include "Common/loginfo.h"
#include "Common/readwritefile.h"

#include "DragwEditor/drageditor.h"
#include "DragwEditor/NodePropertyWidget.h"
#include "SceneEngine/SceneTreeWidget.h"

SceneWindow::SceneWindow(QWidget* parent)
	: QMainWindow(parent)
    , ui(new Ui::SceneWindow)
{
	ui->setupUi(this);

    //场景引擎设置到场景树中
    m_pSceneEngine = new SceneEngine;

    init();

    //创建布局恢复文件
    createLayerFile();

    //尝试从布局文件恢复
    restoreLayout();

}

void SceneWindow::setWindowTitle(const QString &title)
{
    QMainWindow::setWindowTitle(title);
}

SceneWindow::~SceneWindow()
{
    shutdown_logging();

	delete ui;

}

void SceneWindow::initDock()
{
    //属性显示资源停靠
    sceneTreeWidget = new SceneTreeWidget(this);
    dockTree = new QDockWidget(tr("场景树"), this);
    dockTree->setObjectName("dockProp");
    dockTree->setWidget(sceneTreeWidget);
    addDockWidget(Qt::LeftDockWidgetArea, dockTree);
    connect(sceneTreeWidget, &SceneTreeWidget::itemSelected_signal, this, &SceneWindow::onSceneTreeItemSelected);

    sceneTreeWidget->setSceneEngine(m_pSceneEngine);

    m_dragEdit    = new DragEditor(this);
    // 设置DragEditor的SceneEngine
    m_dragEdit->setSceneEngine(m_pSceneEngine);
    m_dragEdit->setData("data/Model");

    // 绑定DragEditor的sigPlatformAdded信号到SceneEngine的onUIAddPlatform_slot槽函数
    connect(m_dragEdit, &DragEditor::sigPlatformAdded, m_pSceneEngine, &SceneEngine::onUIAddPlatform_slot);
    dockObj = new QDockWidget(tr("目标编辑"), this);
    dockObj->setObjectName("dockObj");
    dockObj->setWidget(m_dragEdit);
    addDockWidget(Qt::LeftDockWidgetArea, dockObj);


    //属性显示资源停靠
    nodePropWidget = new NodePropertyWidget(this);
    dockProp = new QDockWidget(tr("模型属性"), this);
    dockProp->setObjectName("dockProp");
    dockProp->setWidget(nodePropWidget);
    addDockWidget(Qt::RightDockWidgetArea, dockProp);


    // m_logInfo    = new LogInfo(this);
    // dockLog = new QDockWidget(tr("日志显示"), this);
    // dockLog->setObjectName("dockLog");
    // dockLog->setWidget(m_logInfo);
    // addDockWidget(Qt::BottomDockWidgetArea, dockLog);



    dockObj->setFeatures(
        QDockWidget::DockWidgetMovable |    // 允许移动
        QDockWidget::DockWidgetFloatable    // 允许浮动
        // 故意省略 DockWidgetClosable，即禁用关闭
    );

    dockProp->setFeatures(
        QDockWidget::DockWidgetMovable |    // 允许移动
        QDockWidget::DockWidgetFloatable    // 允许浮动
        // 故意省略 DockWidgetClosable，即禁用关闭
    );

    dockTree->setFeatures(
        QDockWidget::DockWidgetMovable |    // 允许移动
        QDockWidget::DockWidgetFloatable    // 允许浮动
        // 故意省略 DockWidgetClosable，即禁用关闭
    );

    // dockLog->setFeatures(
    //     QDockWidget::DockWidgetMovable |    // 允许移动
    //     QDockWidget::DockWidgetFloatable    // 允许浮动
    //     // 故意省略 DockWidgetClosable，即禁用关闭
    // );


    // 连接dock窗口的visibilityChanged信号到按钮的checked状态s
    connect(dockObj,        &QDockWidget::visibilityChanged, m_modelResourceButton, &QToolButton::setChecked);
    connect(dockProp,       &QDockWidget::visibilityChanged, m_modelPropertyButton, &QToolButton::setChecked);

    connect(dockTree,       &QDockWidget::visibilityChanged, m_projectButton, &QToolButton::setChecked);
    // connect(dockLog,       &QDockWidget::visibilityChanged, m_logButton, &QToolButton::setChecked);


}



void SceneWindow::initConectMenuAction()
{

    ui->menubar->setVisible(false);

    //场景
    // connect(ui->action_openPro, &QAction::triggered, this, &MainWindow::openProject);               //打开工程
    // connect(ui->action_savePro, &QAction::triggered, this, &MainWindow::saveProject);               //保存工程
    // connect(ui->action_saveAs, &QAction::triggered, this, &MainWindow::saveAsProject);              //另存为工程
    // connect(ui->action_closePro, &QAction::triggered, this, &MainWindow::closeProject);             //关闭工程

    // //想定编辑####
    // connect(ui->action_materialDivTool, &QAction::triggered, this, &MainWindow::startDivde);        //材质划分工具
    // connect(ui->action_terrianConfig, &QAction::triggered, this, &MainWindow::loadTerrain);         //地形配置
    // connect(ui->action_backConfig, &QAction::triggered, this, &MainWindow::loadBackGround);         //背景配置
    // connect(ui->action_objectConfig, &QAction::triggered, this, &MainWindow::loadModel);            //目标配置
    // connect(ui->action_atmosphereEffect, &QAction::triggered, this, &MainWindow::loadWeatherEffect);//气象特效配置
    // connect(ui->action_battleEffect, &QAction::triggered, this, &MainWindow::loadBattleEffect);     //战场特效

    // connect(ui->action_sensorConfig, &QAction::triggered, this, &MainWindow::loadSensor);           //添加传感器
    // connect(ui->action_trkConfig, &QAction::triggered, this, &MainWindow::loadTrackShow);           //航迹显示


    // //大气辐射设置
    // connect(ui->action_atmosphere, &QAction::triggered, this, &MainWindow::setModtranPara);
    // connect(ui->action_atomTrans, &QAction::triggered, this, &MainWindow::drawModtranTrans);
    // connect(ui->action_pathThrml, &QAction::triggered, this, &MainWindow::drawPathThrml);
    // connect(ui->action_surEmis, &QAction::triggered, this, &MainWindow::drawSurEmis);
    // connect(ui->action_solScat, &QAction::triggered, this, &MainWindow::drawSolarScat);
    // connect(ui->action_refSol, &QAction::triggered, this, &MainWindow::drawSolarRef);


    // //传感器效应
    // connect(ui->action_optical,  &QAction::triggered, this, &MainWindow::showOpticalWidget);
    // connect(ui->action_detector, &QAction::triggered, this, &MainWindow::showDetectorWidget);
    // connect(ui->action_electric, &QAction::triggered, this, &MainWindow::showElectricWidget);



    // //设置
    // connect(ui->action_systemConfig, &QAction::triggered, this, &MainWindow::SystemsetClick);//系统设置
    // connect(ui->action_logConfig, &QAction::triggered, this, &MainWindow::SystemLogClick);//设置日志
    // connect(ui->action_projectWidgetSetting, &QAction::triggered, this, &MainWindow::SetProjectTreeVisiable);//场景
    // connect(ui->action_logWidgetSetting, &QAction::triggered, this, &MainWindow::SetLogBoxVisiable);//日志
    // connect(ui->action_propertyWidgetSetting, &QAction::triggered, this, &MainWindow::SetProperttVisiable);//属性
    // connect(ui->action_fuse, &QAction::triggered, this, &MainWindow::SetFuse);//可见光红外融合

    // //帮助
    // connect(ui->action_userMannual, &QAction::triggered, this, &MainWindow::Help_userMannual);
    // connect(ui->action_theoryMannual, &QAction::triggered, this, &MainWindow::Help_theoryMannual);
    // connect(ui->action_demoExample, &QAction::triggered, this, &MainWindow::Help_demoExample);
    // connect(ui->action_demoVideo, &QAction::triggered, this, &MainWindow::Help_demoVideo);
    // connect(ui->action_about, &QAction::triggered, this, &MainWindow::SystemAboutClick);

    //此处不需要弹出提示,在closeEvent 中处理。
    connect(ui->action_exit, &QAction::triggered, [=]() {
        this->close();
    });

}


//启动初始化状态栏
void SceneWindow::initStatusBar()
{

    // 日志按钮 - 改为可check的QToolButton
    // m_logButton = new QPushButton();
    // m_logButton->setText(tr("输出日志"));
    // m_logButton->setCheckable(true);
    // m_logButton->setChecked(dockLog->isVisible());
    // ui->statusbar->addWidget(m_logButton);

    // 场景按钮 - 可check的QToolButton
    m_projectButton = new QPushButton();
    m_projectButton->setText(tr("场景树"));
    m_projectButton->setCheckable(true);
    m_projectButton->setChecked(dockTree->isVisible());
    ui->statusbar->addWidget(m_projectButton);

    // 模型资源按钮 - 可check的QToolButton
    m_modelResourceButton = new QPushButton();
    m_modelResourceButton->setText(tr("场景编辑"));
    m_modelResourceButton->setCheckable(true);
    m_modelResourceButton->setChecked(dockObj->isVisible());
    ui->statusbar->addWidget(m_modelResourceButton);


    // 模型属性按钮 - 可check的QToolButton
    m_modelPropertyButton = new QPushButton();
    m_modelPropertyButton->setText(tr("模型属性"));
    m_modelPropertyButton->setCheckable(true);
    m_modelPropertyButton->setChecked(dockProp->isVisible());
    ui->statusbar->addWidget(m_modelPropertyButton);



    //经纬度显示label
    labelPos = new QLabel;
    ui->statusbar->addPermanentWidget(labelPos);
    ui->statusbar->setContentsMargins(0, 0, 0, 0);


    // 连接按钮的clicked信号到相应的槽函数
    // connect(m_logButton, &QToolButton::clicked, this, [=](bool checked) {
    //     dockLog->setVisible(!checked);
    // });

    connect(m_projectButton, &QToolButton::clicked, this, [this](bool checked) {
        dockTree->setVisible(checked);
    });

    connect(m_modelResourceButton, &QToolButton::clicked, this, [this](bool checked) {
        dockObj->setVisible(checked);
    });

    connect(m_modelPropertyButton, &QToolButton::clicked, this, [this](bool checked) {
        dockProp->setVisible(checked);
    });


}


void SceneWindow::initLog()
{
    ///////////////////////初始化日志模块

    // 将界面实例传递给日志管理器
    // setLogInfoWidget(m_logInfo);

    // 初始化日志配置文件（确保在设置界面后初始化）
    init_logging("log_config.ini");

    LOG_INFO<< "日志模块创建成功";

}


#include <osgGA/TrackballManipulator>
#include "OsgRender/osgQtCompositeViewer.h"

void SceneWindow::initCentWindow()
{

    ////////////////////初始化中心场景界面
    // center3D    =   Mte3DService::getInstance().set3dViewer();



        osg::Group* osgSceneRoot = Mte3DService::getInstance().getTopRoot();

        osgQtCompositeViewer*  osgCompositeViewerWidget = new osgQtCompositeViewer;

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




    ui->tabWidget->addTab(osgCompositeViewerWidget, "三维视图");
    ui->tabWidget->setTabsClosable(true);       //启用标签页的关闭按钮
    connect(ui->tabWidget, &QTabWidget::tabCloseRequested, this, &SceneWindow::removeCurrentTab);

}

void SceneWindow::init()
{
    //加载QSS资源样式
    QFile qss("data/Qss/ui.qss");
    qss.open(QFile::ReadOnly);
    qApp->setStyleSheet(qss.readAll());
    qss.close();

	//创建Ribbon   
    QRibbon::install(this);

    //初始化中心窗口
    initCentWindow();

    initDock();
    // connect(&Mte3DService::getInstance(), SIGNAL(sig_mousePos(double, double, double)), this, SLOT(mouseLLH(double, double, double)));
    // connect(&Mte3DService::getInstance(), SIGNAL(sig_trkFinish()), this, SLOT(endDrawLine()));

    // 初始化状态栏
    initStatusBar();

    //初始化日志模块
    initLog();

    //初始化menu信号槽函数
    initConectMenuAction();
    
    // 连接Mte3DService的sig_platformRemoved信号到SceneEngine的onThreeDSceneRemovePlatform_slot槽函数
    // Mte3DService& mte3DService = Mte3DService::getInstance();
    // connect(&mte3DService, &Mte3DService::sig_platformRemoved,
    //         m_pSceneEngine, &SceneEngine::onThreeDSceneRemovePlatform_slot);
}


void SceneWindow::removeCurrentTab(int index)
{
	if (index > 0 && index < ui->tabWidget->count()) //防止删除主视口
	{
		// 从标签页中移除
		ui->tabWidget->removeTab(index);
	}

    LOG_DEBUG << ("删除tab页");
}


// 窗口关闭时保存布局
void SceneWindow::closeEvent(QCloseEvent *event)
{
    if( !MyMessageBox::showCustomMessageBox(tr("警告"), tr("是否关闭当前应用程序?"),tr("是"),tr("否"))) {
        event->ignore();  // 忽略关闭事件（窗口不关闭）
        return;
    }

    saveLayout();
    QMainWindow::closeEvent(event);

}

void SceneWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    // 确保首次显示时布局已正确恢复
    if (event->spontaneous()) return;
}


void SceneWindow::createLayerFile()
{
    //读取恢复 界面布局文件
    // 设置布局文件路径：运行目录/Layout/latest.layout
    QString layoutDir = QDir::currentPath() + "/Layout";
    m_layoutFilePath = layoutDir + "/latest.layout";

    // 确保Layout目录存在，不存在则创建
    QDir dir;
    if (!dir.exists(layoutDir)) {
        bool isCreated = dir.mkpath(layoutDir);
        if (!isCreated) {
            LOG_DEBUG << "无法创建Layout目录：" << layoutDir;
        }
    }
}

#include <QSettings>
// 保存布局到文件
void SceneWindow::saveLayout()
{
    if (m_layoutFilePath.isEmpty()) return;

    QSettings settings(m_layoutFilePath, QSettings::IniFormat);

    // 保存窗口几何信息（位置和大小）
    settings.setValue("geometry", saveGeometry());
    // 保存窗口状态（工具栏、停靠窗口等）
    settings.setValue("windowState", saveState());

    // 可以在这里扩展保存其他部件的状态
    // 保存三个Dock的独立状态（可见性、浮动状态、浮动窗口几何）
    // dockObj（手动创建）
    settings.setValue("DockWidget/dockObj_Visible", dockObj->isVisible());
    settings.setValue("DockWidget/dockObj_Floating", dockObj->isFloating());
    if (dockObj->isFloating()) {
        settings.setValue("DockWidget/dockObj_FloatGeometry", dockObj->geometry());
    }
    // dockProp（手动创建）
    settings.setValue("DockWidget/dockProp_Visible", dockProp->isVisible());
    settings.setValue("DockWidget/dockProp_Floating", dockProp->isFloating());
    if (dockProp->isFloating()) {
        settings.setValue("DockWidget/dockProp_FloatGeometry", dockProp->geometry());
    }

    // // dockLog（手动创建）
    // settings.setValue("DockWidget/dockLog_Visible", dockLog->isVisible());
    // settings.setValue("DockWidget/dockLog_Floating", dockLog->isFloating());
    // if (dockLog->isFloating()) {
    //     settings.setValue("DockWidget/dockLog_FloatGeometry", dockLog->geometry());
    // }

    // dockTree（手动创建）
    settings.setValue("DockWidget/dockTree_Visible", dockTree->isVisible());
    settings.setValue("DockWidget/dockTree_Floating", dockTree->isFloating());
    if (dockTree->isFloating()) {
        settings.setValue("DockWidget/dockTree_FloatGeometry", dockTree->geometry());
    }


}

// 从文件恢复布局
void SceneWindow::restoreLayout()
{
    if (!QFile::exists(m_layoutFilePath)) return;

    QSettings settings(m_layoutFilePath, QSettings::IniFormat);

    // 恢复窗口几何信息
    restoreGeometry(settings.value("geometry").toByteArray());
    // 恢复窗口状态
    restoreState(settings.value("windowState").toByteArray());


    // 可以在这里扩展恢复其他部件的状态
    // 恢复三个Dock的独立状态（可见性、浮动状态、浮动窗口几何）
    // dockObj
    bool dockObjVisible  = settings.value("DockWidget/dockObj_Visible", true).toBool();
    bool dockObjFloating = settings.value("DockWidget/dockObj_Floating", false).toBool();
    dockObj->setVisible(dockObjVisible);
    dockObj->setFloating(dockObjFloating);
    if (dockObjFloating) {
        QRect dockObjGeo = settings.value("DockWidget/dockObj_FloatGeometry").toRect();
        if (!dockObjGeo.isEmpty()) {
            dockObj->setGeometry(dockObjGeo);
        }
    }

    // dockProp
    bool dockPropVisible = settings.value("DockWidget/dockProp_Visible", true).toBool();
    bool dockPropFloating = settings.value("DockWidget/dockProp_Floating", false).toBool();
    dockProp->setVisible(dockPropVisible);
    dockProp->setFloating(dockPropFloating);
    if (dockPropFloating) {
        QRect dockPropGeo = settings.value("DockWidget/dockProp_FloatGeometry").toRect();
        if (!dockPropGeo.isEmpty()) {
            dockProp->setGeometry(dockPropGeo);
        }
    }


    // dockLog
    // bool dockLogVisible = settings.value("DockWidget/dockLog_Visible", true).toBool();
    // bool dockLogFloating = settings.value("DockWidget/dockLog_Floating", false).toBool();
    // dockLog->setVisible(dockLogVisible);
    // dockLog->setFloating(dockLogFloating);
    // if (dockLogFloating) {
    //     QRect dockLogGeo = settings.value("DockWidget/dockLog_FloatGeometry").toRect();
    //     if (!dockLogGeo.isEmpty()) {
    //         dockLog->setGeometry(dockLogGeo);
    //     }
    // }

    // dockTree
    bool dockTreeVisible = settings.value("DockWidget/dockTree_Visible", true).toBool();
    bool dockTreeFloating = settings.value("DockWidget/dockTree_Floating", false).toBool();
    dockTree->setVisible(dockTreeVisible);
    dockTree->setFloating(dockTreeFloating);
    if (dockTreeFloating) {
        QRect dockTreeGeo = settings.value("DockWidget/dockTree_FloatGeometry").toRect();
        if (!dockTreeGeo.isEmpty()) {
            dockTree->setGeometry(dockTreeGeo);
        }
    }
}


void SceneWindow::onSceneTreeItemSelected(const QString& type, int id, const QString& name)
{
    // 根据类型和ID获取平台信息
    if (type == "Terrain" || type == "Background" || type == "Target") {

        // 这里需要根据实际的获取方法进行修改
        // 临时使用空的平台信息进行测试
        // 从SceneEngine获取平台信息
        MtePlatformStru plat = m_pSceneEngine->getPlatformInfo(id);
        // 调用NodePropertyWidget的setPlatform方法显示平台信息
        nodePropWidget->setPlatform(plat);

    }
}
