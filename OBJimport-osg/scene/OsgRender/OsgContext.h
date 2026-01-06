#pragma once
#include "MyOsgEarth.h"
#include <osgGA/GUIEventHandler>
#include <fstream>
#include <QMap>
#include <QWidget>


#include "../OsgRender/MouseIntersectionHandler.h"
#include "../Common/MteStructDef.h"

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>
#include <osg/Image>
#include <osgDB/WriteFile>
#include <osg/Geode>
#include <osg/Geometry>
#include <iostream>
#include <vector>
#include <thread>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers> // 包含 ScreenCaptureHandler
#include <osg/Image>
#include <osgDB/WriteFile>
#include <osg/Geode>
#include <osg/NodeCallback>

#include <iostream>
#include <vector>
#include <thread>


class osgQtCompositeViewer;
class MouseIntersectionHandler;


class osgContext :public QObject, public osg::Referenced
{
    Q_OBJECT

public:
    osgContext();
    ~osgContext();


    static osgContext* getInstance();

    void init();


public://********************************************测量****************************************************
    void measureDistance(bool is_m);

    void switchMouseLocation(bool is_on);

public:


    // 获取红外视图
    osg::ref_ptr<osgViewer::View> getInfraredView(int viewId);

    // 为视图分配唯一的显示掩码
    unsigned int allocateDisplayMask(int viewId);


public:
    void initMouseIntersectionHandler();

    void removeMouseIntersectionHandler();


    // 多视口创建（创建 CompositeViewer 管理的 widget）
    // 返回创建的 composite widget（外部可把它放入 UI 布局）
    osgQtCompositeViewer* createCompositeViewer(QWidget* parent = nullptr);

    // 多视口管理（兼容接口）
    // addViewToComposite/ removeViewFromComposite 为便捷转发接口（对现有调用兼容）
    // 注意：addViewToComposite 忽略传入 view 指针并使用内部 composite 的 addView()
    int addViewToComposite(int viewId = -1);
    void removeViewFromComposite(int viewId);

    // 为指定 ID 的 View 安装屏幕捕获处理器和对应的 ImageSender
    // 每个 View 会监听不同的端口，从 basePort 开始
    void installScreenCapture(int viewId, int basePort = 12345);


    // 获取指定 view 的 camera（成功返回非空指针）
    osg::Camera* getCamera(int viewId = 0);

    // 获取指定 view 的实例（成功返回非空指针）
    osgViewer::View* getView(int viewId = 0);

    // 为指定 view 添加事件处理器
    void addEventHandlerToView(int viewId, osgGA::GUIEventHandler* handler);

    // 判断 viewId 是否有效
    bool isViewValid(int viewId) const;

    // 获取3D视图管理器
    QWidget* get3DViewer();
    

    // 为指定视图设置自定义漫游器
    void setManipulatorForView(int viewId, osgGA::CameraManipulator* manipulator);
    
    // 为指定视图设置视口
    void setViewportForView(int viewId, int x, int y, int width, int height);
    
public:
    // 为指定视图设置经纬度姿态
    void setViewPosition(int viewId, double latitude, double longitude, double altitude, double heading, double pitch, double roll);
    
    // void prepareSceneRoots();
    

private:
    // 多视口容器（使用 osgQtCompositeViewer 管理）
    osgQtCompositeViewer*                           osg3DCompositeViewer = nullptr;

    // 新增：MouseIntersectionHandler 管理
    MouseIntersectionHandler*                       mouseInterHandler = nullptr;


};

