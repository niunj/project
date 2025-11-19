// UTF-8 BOM
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QMenuBar>
#include <QAction>

#include "osgwidget.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_osgWidget(nullptr)
    , m_layout(nullptr)
{
    // 先调用setupUi获取基本UI结构
    ui->setupUi(this);
    
    // 设置窗口标题和大小
    setWindowTitle("OSG Viewer in Qt");
    resize(800, 600);
    
    // 初始化UI和OSG集成
    setupUI();
    
    // 创建菜单
    createMenus();
}

MainWindow::~MainWindow()
{
    // 清理OSGWidget资源
    if (m_osgWidget)
    {
        delete m_osgWidget;
        m_osgWidget = nullptr;
    }
    
    // 清理布局资源
    if (m_layout)
    {
        delete m_layout;
        m_layout = nullptr;
    }
    
    delete ui;
}

void MainWindow::setupUI()
{
    // 创建主布局
    m_layout = new QVBoxLayout;
    
    // 创建OSG渲染窗口
    m_osgWidget = new OSGWidget(this);
    m_osgWidget->setMinimumSize(640, 480);
    
    // 将OSG窗口添加到布局
    m_layout->addWidget(m_osgWidget, 1); // 1表示拉伸因子
    m_layout->setContentsMargins(0, 0, 0, 0); // 无边距
    
    // 设置中心部件
    QWidget *centralWidget = new QWidget(this);
    centralWidget->setLayout(m_layout);
    setCentralWidget(centralWidget);
}

void MainWindow::createMenus()
{
    // 创建视图菜单
    QMenu *viewMenu = menuBar()->addMenu("视图(&V)");
    
    // 重置视图动作
    QAction *resetViewAction = new QAction("重置视图(&R)", this);
    resetViewAction->setShortcut(QKeySequence::Refresh);
    connect(resetViewAction, &QAction::triggered, this, &MainWindow::on_actionReset_View_triggered);
    viewMenu->addAction(resetViewAction);
    
    // 创建帮助菜单
    QMenu *helpMenu = menuBar()->addMenu("帮助(&H)");
    
    // 关于动作
    QAction *aboutAction = new QAction("关于(&A)", this);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::on_actionAbout_triggered);
    helpMenu->addAction(aboutAction);
}

void MainWindow::on_actionReset_View_triggered()
{
    if (m_osgWidget)
    {
        // 直接更新视图，避免依赖getViewer方法
        m_osgWidget->update();
    }
}

void MainWindow::on_actionAbout_triggered()
{
    QMessageBox::about(this, "关于", 
                       "OSG Viewer in Qt 示例程序\n" 
                       "演示如何在Qt应用程序中集成OpenSceneGraph渲染窗口\n" 
                       "版本 1.0");
}
