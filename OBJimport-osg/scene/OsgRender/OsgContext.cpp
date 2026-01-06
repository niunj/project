#include "OsgContext.h"
#include "../Common/MteStructDef.h"
#include "Mte3DService.h"
#include "osgQtCompositeViewer.h"

#include "../Log/log_manager.h"

#include <osg/GraphicsContext>

osgContext::osgContext()
{
    LOG_DEBUG << "[osgContext] 构造函数开始执行";
	init();
    LOG_DEBUG << "[osgContext] 构造函数执行完成";
}

osgContext::~osgContext()
{

}

osgContext* osgContext::getInstance()
{
    LOG_DEBUG << "[osgContext] getInstance() 被调用，返回单例实例";
	static osgContext minstance;
	return &minstance;
}

void osgContext::switchMouseLocation(bool is_on)
{
    LOG_DEBUG << "[osgContext] switchMouseLocation() 被调用，状态设置为: " << (is_on ? "开启" : "关闭");
    if(mouseInterHandler != nullptr) {
        LOG_DEBUG << "[osgContext] 鼠标交互处理器存在，设置状态为: " << (is_on ? "启用" : "禁用");
        mouseInterHandler->setEnabled(is_on);
    } else {
        LOG_DEBUG << "[osgContext] 鼠标交互处理器不存在，无法设置状态";
    }
    LOG_DEBUG << "[osgContext] switchMouseLocation() 执行完成";
}

void osgContext::initMouseIntersectionHandler()
{
    LOG_DEBUG << "[osgContext] initMouseIntersectionHandler() 开始初始化鼠标交互处理器";
    if (mouseInterHandler) {
        LOG_DEBUG << "[osgContext] 鼠标交互处理器已存在，跳过初始化";
        return; 
    }

    LOG_DEBUG << "[osgContext] 创建新的 MouseIntersectionHandler 实例";
    mouseInterHandler = new MouseIntersectionHandler;

    LOG_DEBUG << "[osgContext] 设置信号连接：mouseInterHandler->sig_mousePos -> Mte3DService::sig_mousePos";
    // QObject::connect(mouseInterHandler, &MouseIntersectionHandler::sig_mousePos,
    //                  &Mte3DService::getInstance(), &Mte3DService::sig_mousePos);
    
    // 将鼠标处理器添加到所有现有视图
    if (osg3DCompositeViewer) {
        int viewId = 0;
        while (true) {
            osg::ref_ptr<osgViewer::View> view = osg3DCompositeViewer->getView(viewId);
            if (!view) break;
            
            view->addEventHandler(mouseInterHandler);
            LOG_DEBUG << "[osgContext] 已将鼠标处理器添加到视图" << viewId;
            viewId++;
        }
    }
    
    LOG_DEBUG << "[osgContext] initMouseIntersectionHandler() 初始化完成";
}



void osgContext::removeMouseIntersectionHandler()
{
    LOG_DEBUG << "[osgContext] removeMouseIntersectionHandler() 开始移除鼠标交互处理器";
    if (!mouseInterHandler) {
        LOG_DEBUG << "[osgContext] 鼠标交互处理器不存在，无需移除";
        return;
    }

    // 从所有视图中移除鼠标处理器
    if (osg3DCompositeViewer) {
        int viewId = 0;
        while (true) {
            osg::ref_ptr<osgViewer::View> view = osg3DCompositeViewer->getView(viewId);
            if (!view) break;
            
            view->removeEventHandler(mouseInterHandler);
            LOG_DEBUG << "[osgContext] 已从视图" << viewId << "移除鼠标处理器";
            viewId++;
        }
    }

    LOG_DEBUG << "[osgContext] 断开 QT 连接：mouseInterHandler 与 Mte3DService 之间的所有连接";
    QObject::disconnect(mouseInterHandler, nullptr, &Mte3DService::getInstance(), nullptr);
    
    LOG_DEBUG << "[osgContext] 将鼠标交互处理器指针设置为 nullptr";
    mouseInterHandler = nullptr;
    
    LOG_DEBUG << "[osgContext] removeMouseIntersectionHandler() 移除完成";
}

void osgContext::init()
{
    LOG_DEBUG << "[osgContext] init() 开始初始化上下文";
	// 初始化多视口相关成员
    LOG_DEBUG << "[osgContext] 初始化多视口相关成员";
    osg3DCompositeViewer = nullptr;
    mouseInterHandler = nullptr;
    
    LOG_DEBUG << "[osgContext] init() 初始化完成";
}


void osgContext::measureDistance(bool is_m)
{
    LOG_DEBUG << "[osgContext] measureDistance() 被调用，状态设置为: " << (is_m ? "开启" : "关闭");
    
    // 当前函数实现被注释掉
    LOG_DEBUG << "[osgContext] measureDistance() 当前为未实现状态（函数体被注释）";
    
//    if (is_m)
//    {
//        LOG_DEBUG << "[osgContext] 创建距离测量处理器并添加到视图";
//        measureDis = new DistanceMeasureHandler(_pMapNode, _3Droot);
//        osgContext::getInstance()->get3DViewer()->getViewer()->addEventHandler(measureDis);
//    }
//    else
//    {
//        LOG_DEBUG << "[osgContext] 从视图中移除距离测量处理器";
//        osgContext::getInstance()->get3DViewer()->getViewer()->removeEventHandler(measureDis);
//    }
    
    LOG_DEBUG << "[osgContext] measureDistance() 执行完成";
}

// 多视口创建：创建并返回 CompositeViewer Widget
osgQtCompositeViewer* osgContext::createCompositeViewer(QWidget* parent)
{
    LOG_DEBUG << "[osgContext] createCompositeViewer() 开始创建复合视图组件";
    
    // 若已存在 composite viewer，返回现有实例
    if (osg3DCompositeViewer) {
        LOG_DEBUG << "[osgContext] 复合视图组件已存在，直接返回现有实例";
        return osg3DCompositeViewer;
    }

    LOG_DEBUG << "[osgContext] 创建新的 osgQtCompositeViewer 实例";
    // 创建 composite viewer widget（内部会创建 CompositeViewer 并若干子 view）
    osg3DCompositeViewer = new osgQtCompositeViewer(parent);
    
    return osg3DCompositeViewer;
}


void osgContext::installScreenCapture(int viewId, int basePort) {

  osgViewer::View *view = getView(viewId);
  if(view) {
      // 2. 如果线程还没启动，现在启动
      // if (!_io_thread_started) {
      //     _io_thread = std::thread([this]() {
      //         _io_context.run();
      //     });
      //     _io_thread_started = true;
      // }


      // 为每个 View 分配一个唯一的端口
      // int port = basePort + viewId;

      // 如果已存在，先移除旧的
//     if (_capture_handlers.contains(viewId)) {
//         view->removeEventHandler(_capture_handlers[viewId]);
//         _capture_handlers.remove(viewId);
// //     }
//      if (_image_senders.contains(viewId)) {
//          _image_senders.remove(viewId);
//      }

//       // 2. 创建新的 ImageSender 和 ScreenCaptureHandler
//      // 使用 std::move 将临时的 unique_ptr 的所有权转移给 QMap
//      _image_senders[viewId] = new ImageSender(_io_context, port);


//      osg::ref_ptr<ContinuousSendDrawCallback> sendCallback = new ContinuousSendDrawCallback(_io_context,
//                                                                                  _image_senders[viewId], 0.033);


//      view->getCamera()->setFinalDrawCallback(sendCallback);

  }

}

#include <osgGA/TrackballManipulator>

// 向 composite 添加 view（兼容旧签名）
int osgContext::addViewToComposite(int viewId)
{ 
    if (!osg3DCompositeViewer)
    {
        LOG_DEBUG << "[osgContext] addViewToComposite() 失败：复合查看器未创建";
        return -1;
    }
    
    // 创建新视图
    LOG_DEBUG << "[osgContext] 创建新的 osgViewer::View 实例";
    osg::ref_ptr<osgViewer::View> view = new osgViewer::View;
    

    // 配置视图参数
    view->setName("MainView");
    
    // 获取 Mte3DService 的场景根节点并设置到视图
    osg::Group* sceneRoot = Mte3DService::getInstance().getTopRoot();
    if (sceneRoot) {
        view->setSceneData(sceneRoot);
        LOG_DEBUG << "[osgContext] 已将场景数据设置到视图";
    }

    // 为视图设置默认相机操纵器
    osg::ref_ptr<osgGA::TrackballManipulator> manipulator = new osgGA::TrackballManipulator;
    manipulator->setHomePosition(osg::Vec3d(0, -10, 5), osg::Vec3d(0, 0, 0), osg::Vec3d(0, 0, 1));
    view->setCameraManipulator(manipulator);
    
    // 添加状态集控制器
    view->addEventHandler(new osgGA::StateSetManipulator(view->getCamera()->getOrCreateStateSet()));
    view->addEventHandler(new osgViewer::StatsHandler);

    // 添加视图到复合查看器（注意：osgQtCompositeViewer::addView 内部会处理相机和GraphicsContext的创建）
    int resultId = osg3DCompositeViewer->addView(view, viewId);
    

    // 给Camera设置ViewID Uniform（供回调读取）
    // 每个View 创建时都会自带这个参数。
    osg::StateSet* ss = view->getCamera()->getOrCreateStateSet();
    ss->addUniform(new osg::Uniform("ViewID", resultId));

    // 如果鼠标处理器已存在，将其添加到新创建的视图
    if (mouseInterHandler) {
        view->addEventHandler(mouseInterHandler);
        LOG_DEBUG << "[osgContext] 已将鼠标处理器添加到新视图" << resultId;
    }


    return resultId;
}

void osgContext::removeViewFromComposite(int viewId)
{
    if (!osg3DCompositeViewer)
    {
        return;
    }
    
    // 1. 移除并销毁 ImageSender
    // _image_senders.remove(viewId);

    // 2. 从 View 中移除并销毁 ScreenCaptureHandler
    // if (_capture_handlers.contains(viewId)) {
    //    getView(viewId)->removeEventHandler(_capture_handlers[viewId]);
    //    _capture_handlers.remove(viewId);
    // }

    osg3DCompositeViewer->removeView(viewId);
}


// 获取指定 view 的摄像机
osg::Camera* osgContext::getCamera(int viewId)
{
    // 多视口情况，委托给 composite widget
    if (osg3DCompositeViewer)
    {
        return osg3DCompositeViewer->getCamera(viewId);
    }

    return nullptr;
}

// 获取指定 view 的实例
osgViewer::View *osgContext::getView(int viewId)
{
    // 多视口情况，委托给 composite widget
    if (osg3DCompositeViewer)
    {
        return osg3DCompositeViewer->getView(viewId).get();
    }

    return nullptr;
}

// 为指定 view 添加事件处理器
void osgContext::addEventHandlerToView(int viewId, osgGA::GUIEventHandler* handler)
{
    if (!handler)
        return;

    // 多视口情况，通过 composite widget 获取 view 并添加处理器
    if (osg3DCompositeViewer)
    {
        osg::ref_ptr<osgViewer::View> view = osg3DCompositeViewer->getView(viewId);
        if (view)
        {
            view->addEventHandler(handler);
        }
    }
}


QWidget* osgContext::get3DViewer()
{
    return osg3DCompositeViewer;
}


// 判断 view 是否有效
bool osgContext::isViewValid(int viewId) const
{
    // 多视口情况
    if (osg3DCompositeViewer)
    {
        return osg3DCompositeViewer->getView(viewId).valid();
    }

    return false;
}



// osg::ref_ptr<osgEarth::Util::EarthManipulator> osgContext::getEarthManipulator(int viewId)
// {
//     LOG_DEBUG << "[osgContext] getEarthManipulator() 开始获取地球漫游器";

//     // 多视口场景，尝试从第一个 composite 子 view 获取 manipulator
//     if (osg3DCompositeViewer)
//     {
//         LOG_DEBUG << "[osgContext] 复合查看器存在，继续获取地球漫游器";
//         // 从 view id 0 获取主要操控器
//         if (isViewValid(0))
//         {
//             LOG_DEBUG << "[osgContext] 视图ID 0 有效，获取该视图的漫游器";
//             osg::ref_ptr<osgViewer::View> v = osg3DCompositeViewer->getView(viewId);
//             if (v)
//             {
//                 LOG_DEBUG << "[osgContext] 成功获取视图对象，获取相机漫游器";
//                 osgGA::CameraManipulator* cm = v->getCameraManipulator();
//                 osgEarth::Util::EarthManipulator* em = dynamic_cast<osgEarth::Util::EarthManipulator*>(cm);
//                 if (em) {
//                     LOG_DEBUG << "[osgContext] 成功获取并转换为地球漫游器";
//                     return em;
//                 } else {
//                     LOG_DEBUG << "[osgContext] 警告: 相机漫游器不是 EarthManipulator 类型";
//                 }
//             } else {
//                 LOG_DEBUG << "[osgContext] 警告: 无法获取视图ID 0 的视图对象";
//             }
//         } else {
//             LOG_DEBUG << "[osgContext] 视图ID 0 无效";
//         }
//     } else {
//         LOG_DEBUG << "[osgContext] 复合查看器不存在";
//     }

//     LOG_DEBUG << "[osgContext] getEarthManipulator() 无法获取地球漫游器，返回 nullptr";
//     return nullptr;
// }

// osg::ref_ptr<osgEarth::Util::EarthManipulator> osgContext::getEarthManipulator()
// {
//     LOG_DEBUG << "[osgContext] getEarthManipulator() 开始获取地球漫游器";
    
//     // 多视口场景，尝试从第一个 composite 子 view 获取 manipulator
//     if (osg3DCompositeViewer)
//     {
//         LOG_DEBUG << "[osgContext] 复合查看器存在，继续获取地球漫游器";
//         // 从 view id 0 获取主要操控器
//         if (isViewValid(0))
//         {
//             LOG_DEBUG << "[osgContext] 视图ID 0 有效，获取该视图的漫游器";
//             osg::ref_ptr<osgViewer::View> v = osg3DCompositeViewer->getView(0);
//             if (v)
//             {
//                 LOG_DEBUG << "[osgContext] 成功获取视图对象，获取相机漫游器";
//                 osgGA::CameraManipulator* cm = v->getCameraManipulator();
//                 osgEarth::Util::EarthManipulator* em = dynamic_cast<osgEarth::Util::EarthManipulator*>(cm);
//                 if (em) {
//                     LOG_DEBUG << "[osgContext] 成功获取并转换为地球漫游器";
//                     return em;
//                 } else {
//                     LOG_DEBUG << "[osgContext] 警告: 相机漫游器不是 EarthManipulator 类型";
//                 }
//             } else {
//                 LOG_DEBUG << "[osgContext] 警告: 无法获取视图ID 0 的视图对象";
//             }
//         } else {
//             LOG_DEBUG << "[osgContext] 视图ID 0 无效";
//         }
//     } else {
//         LOG_DEBUG << "[osgContext] 复合查看器不存在";
//     }

//     LOG_DEBUG << "[osgContext] getEarthManipulator() 无法获取地球漫游器，返回 nullptr";
//     return nullptr;
// }

// 为视图分配唯一的显示掩码
unsigned int osgContext::allocateDisplayMask(int viewId)
{

    // 使用不同的位掩码确保各个视图的图层只在对应视图中可见
    // 保留低8位给默认图层，红外视图使用高位
    unsigned int mask = 1 << (viewId + 8);
    

    return mask;
}



// 为指定视图设置自定义漫游器
void osgContext::setManipulatorForView(int viewId, osgGA::CameraManipulator* manipulator)
{
    LOG_DEBUG << "[osgContext] setManipulatorForView() 开始为视图设置自定义漫游器，viewId: " << viewId;
    
    // 检查composite viewer是否存在
    if (!osg3DCompositeViewer) {
        return;
    }
    
    // 检查漫游器指针是否有效
    if (!manipulator) {
        return;
    }
    
    // 检查视口是否有效
    if (!isViewValid(viewId)) {
        return;
    }
    
    LOG_DEBUG << "[osgContext] 视图ID有效，获取视图对象";
    // 获取视口
    osg::ref_ptr<osgViewer::View> view = osg3DCompositeViewer->getView(viewId);
    if (view.valid()) {
        // 设置漫游器
        view->setCameraManipulator(manipulator);
    }
    
    LOG_DEBUG << "[osgContext] setManipulatorForView() 执行完成";
}

// 为指定视图设置视口
void osgContext::setViewportForView(int viewId, int x, int y, int width, int height)
{
    LOG_DEBUG << "[osgContext] setViewportForView() 开始为视图设置视口，viewId: " << viewId
             << "，视口参数: (" << x << "," << y << "," << width << "," << height << ")";
    
    // 检查composite viewer是否存在
    if (!osg3DCompositeViewer) {
        return;
    }
    
    // 检查视口参数是否有效
    if (width <= 0 || height <= 0) {
        return;
    }
    
    // 检查视口是否有效
    if (!isViewValid(viewId)) {
        return;
    }
    
    // 获取视口
    osg::ref_ptr<osgViewer::View> view = osg3DCompositeViewer->getView(viewId);
    if (view.valid()) {

        // 获取相机
        osg::ref_ptr<osg::Camera> camera = view->getCamera();
        if (camera.valid()) {

            // 设置视口
            camera->setViewport(new osg::Viewport(x, y, width, height));

        }
    }
    
    LOG_DEBUG << "[osgContext] setViewportForView() 执行完成";
}

// 为指定视图设置经纬度姿态
void osgContext::setViewPosition(int viewId, double latitude, double longitude, double altitude, double heading, double pitch, double roll)
{
    LOG_DEBUG << "[osgContext] setViewPosition() 开始设置视图位置姿态，viewId: " << viewId
             << "，经度=" << longitude << "，纬度=" << latitude << "，高度=" << altitude
             << "，航向=" << heading << "，俯仰=" << pitch << "，横滚=" << roll;

    // 检查composite viewer是否存在
    if (!osg3DCompositeViewer) {
        LOG_DEBUG << "[osgContext] setViewPosition() 失败：复合查看器未创建";
        return;
    }

    // 检查视口是否有效
    if (!isViewValid(viewId)) {
        LOG_DEBUG << "[osgContext] setViewPosition() 失败：视图ID无效";
        return;
    }

    LOG_DEBUG << "[osgContext] 视图ID有效，获取视图对象";
    // 获取视图
    osg::ref_ptr<osgViewer::View> view = osg3DCompositeViewer->getView(viewId);
    if (view.valid()) {
        LOG_DEBUG << "[osgContext] 成功获取视图对象，获取漫游器";
        
        // 获取漫游器
        osgGA::CameraManipulator* manipulator = view->getCameraManipulator();
        if (!manipulator) {
            LOG_DEBUG << "[osgContext] 警告: 视图没有漫游器，viewId: " << viewId;
            return;
        }

        // 尝试转换为地球漫游器
//        osgEarth::Util::EarthManipulator* earthManipulator = dynamic_cast<osgEarth::Util::EarthManipulator*>(manipulator);

    } else {
        LOG_DEBUG << "[osgContext] 警告: 无法获取视图对象，viewId: " << viewId;
    }
    
    LOG_DEBUG << "[osgContext] setViewPosition() 执行完成";
}
