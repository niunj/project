#ifndef SCENEWINDOW_H
#define SCENEWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QLabel>
#include <QTimeEdit>
#include <QComboBox>
#include <QPair>
#include <QToolButton>
#include <QTextCodec>
#include <QPointer>
#include <qprocess.h>


QT_BEGIN_NAMESPACE
namespace Ui {
class SceneWindow;
}
QT_END_NAMESPACE


class SceneEngine;
class SceneTreeWidget;
class LogInfo;
class DragEditor;
class NodePropertyWidget;


class SceneWindow : public QMainWindow
{
    Q_OBJECT

public:
    SceneWindow(QWidget *parent = nullptr);
    ~SceneWindow();
    
    // 设置窗口标题
    void setWindowTitle(const QString &title);

private:
    //初始化menu信号槽
    void initConectMenuAction();

    //启动初始化状态栏
    void initStatusBar();
    //初始化状态栏
    void initLog();

    void initCentWindow();

    void initDock();

    //初始化
    void init();


    //主视口Tab页面
    void removeCurrentTab(int index);

    // 场景树操作
    void onSceneTreeItemSelected(const QString& type, int id, const QString& name);


private:
    // 状态栏按钮
    QPushButton* m_logButton = nullptr;
    QPushButton* m_projectButton = nullptr;
    QPushButton* m_modelResourceButton = nullptr;
    QPushButton* m_modelPropertyButton = nullptr;
    
private:

    //中心3d
    QWidget* center3D = nullptr;


//时间数据相关
private:

    //状态栏时间设置
    QLabel*     labelPos            = nullptr;

private:
    //场景数据引擎
    //负责场景数据的增删改查，同时联动场景树和三维场景
    SceneEngine* m_pSceneEngine = nullptr;

private:
    Ui::SceneWindow *ui;

signals:
    void sig_time(double,double,double);
    void sig_slideEnd();



private:
    DragEditor*             m_dragEdit      = nullptr;
    NodePropertyWidget*     nodePropWidget  = nullptr;
    // LogInfo*                m_logInfo       = nullptr;
    SceneTreeWidget*        sceneTreeWidget = nullptr;

private:

    QDockWidget* dockObj                    = nullptr;

    QDockWidget* dockProp                   = nullptr;

    QDockWidget* dockTree                   = nullptr;

    QDockWidget* dockLog                    = nullptr;

private:

    // 布局文件路径
    QString m_layoutFilePath;

    // 创建布局文件
    void createLayerFile();

    // 保存布局到文件
    void saveLayout();

    // 从文件恢复布局
    void restoreLayout();

    //外部接口与协议测试相关代码
protected:
    // 重写关闭事件，用于保存布局
    void closeEvent(QCloseEvent *event) override;

    // 重写显示事件，确保首次显示时恢复布局
    void showEvent(QShowEvent *event) override;

};
#endif // MAINWINDOW_H
