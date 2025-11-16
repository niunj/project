#include "osgQtCompositeViewer.h"
#include "Mte3DService.h"
#include <QDebug>
#include <osgQt/GraphicsWindowQt>
#include <osg/Camera>

// 调试信息相关宏定义
#define DEBUG_PRINT(msg) qDebug() << "[osgQtCompositeViewer]" << msg

osgQtCompositeViewer::osgQtCompositeViewer(QWidget *parent)
    : QWidget(parent)
    , _nextViewId(0)
    , _compositeViewer(new osgViewer::CompositeViewer)
    , _mainLayout(nullptr)
    , _timer(nullptr)
{
    DEBUG_PRINT("构造函数开始执行");
    
    // 设置窗口属性
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    
    // 初始化主布局
    _mainLayout = new QGridLayout(this);
    _mainLayout->setSpacing(6);

    _mainLayout->setContentsMargins(0, 0, 0, 0);

    setLayout(_mainLayout);
    
    _compositeViewer = new osgViewer::CompositeViewer;
    // 初始化复合视图（单线程模式）
    if (_compositeViewer) {
        DEBUG_PRINT("CompositeViewer 创建成功");
        _compositeViewer->setThreadingModel(osgViewer::CompositeViewer::SingleThreaded);
        DEBUG_PRINT("设置线程模型为SingleThreaded");
    } else {
        DEBUG_PRINT("警告: CompositeViewer 创建失败！");
    }
    
    // 恢复定时器，设置合理帧率（如16ms=60fps）
     _timer = new QTimer(this);
     connect(_timer, &QTimer::timeout, this, &osgQtCompositeViewer::updateAllViews);
     _timer->start(16);
     DEBUG_PRINT("启用定时器驱动渲染循环");

    DEBUG_PRINT("构造函数执行完成");
}

osgQtCompositeViewer::~osgQtCompositeViewer()
{
    DEBUG_PRINT("析构函数开始执行，清理资源");
    
    // 停止渲染定时器
    if (_timer && _timer->isActive()) {
        _timer->stop();
        DEBUG_PRINT("渲染定时器已停止");
    }
    
    // 清理视图窗口映射
    for (auto it = _viewWidgets.begin(); it != _viewWidgets.end(); ++it) {
        if (it.value()) {
            // 确保从布局中移除
            if (_mainLayout) {
                _mainLayout->removeWidget(it.value());
            }
            delete it.value();
            it.value() = nullptr;
        }
    }
    _viewWidgets.clear();
    
    // 清理子视图映射
    _subViews.clear();
    
    // 清理复合视图
    _compositeViewer = nullptr;
    
    // 清理定时器
    if (_timer) {
        delete _timer;
        _timer = nullptr;
    }
    
    // 清理布局
    if (_mainLayout) {
        delete _mainLayout;
        _mainLayout = nullptr;
    }
    
    DEBUG_PRINT("析构函数执行完成，所有资源已清理");
}

// 创建GraphicsWindowQt用于OSG渲染
osgQt::GraphicsWindowQt* createGraphicsWindowQt(int x, int y, int w, int h)
{
    osg::ref_ptr<osg::GraphicsContext::Traits> traits = new osg::GraphicsContext::Traits;
    traits->x = x;
    traits->y = y;
    traits->width = w;
    traits->height = h;
    traits->windowDecoration = false;
    traits->doubleBuffer = true;
    traits->sharedContext = 0;
    
    osgQt::GraphicsWindowQt* gw = new osgQt::GraphicsWindowQt(traits.get());
    return gw;
}

int osgQtCompositeViewer::addView(osg::ref_ptr<osgViewer::View> view)
{
    DEBUG_PRINT("添加视图到复合查看器");
    
    // 分配视图ID
    int viewId = _subViews.size();
    DEBUG_PRINT("为新视图分配ID: " << viewId);
    
    if (view && _compositeViewer) {
        // 为视图创建相机（如果没有）
        if (!view->getCamera()) {
            osg::ref_ptr<osg::Camera> camera = new osg::Camera;
            camera->setViewport(new osg::Viewport(0, 0, 800, 600));
            camera->setProjectionMatrixAsPerspective(30.0f, 1.3333f, 1.0f, 10000.0f);
            view->setCamera(camera.get());
        }
        
        // 创建Qt窗口用于OSG渲染
        osgQt::GraphicsWindowQt* gw = createGraphicsWindowQt(0, 0, 800, 600);
        view->getCamera()->setGraphicsContext(gw);
        
        // 获取Qt窗口组件
        QWidget* viewWidget = gw->getGLWidget();
//        viewWidget->setMinimumSize(800, 600);
        viewWidget->setFocusPolicy(Qt::StrongFocus);
        
        qDebug() << "layout:" <<viewId;
        // 添加窗口到布局 <
        if(viewId == 0){
            _mainLayout->addWidget(viewWidget, 0, 0, 1, 3);
        }
        else{
            _mainLayout->addWidget(viewWidget, 1, viewId-1, 1, 1);
        }
        
        // 将视图添加到复合视图
        _compositeViewer->addView(view.get());
        DEBUG_PRINT("视图已添加到复合视图，ID: " << viewId);
        
        // 保存映射关系
        _subViews[viewId] = view;
        _viewWidgets[viewId] = viewWidget;
        DEBUG_PRINT("已保存视图和窗口映射关系");
        
        return viewId;
    } else {
        DEBUG_PRINT("警告: 视图添加失败 - 无效的视图或复合视图");
        return -1;
    }
}

void osgQtCompositeViewer::removeView(int viewId)
{
    DEBUG_PRINT("移除视图，ID: " << viewId);
    
    // 检查视图是否存在
    auto viewIt = _subViews.find(viewId);
    
    if (viewIt != _subViews.end() && _compositeViewer) {
        // 从复合视图中移除
        _compositeViewer->removeView(viewIt.value());
        
        // 清理对应的窗口
        auto widgetIt = _viewWidgets.find(viewId);
        if (widgetIt != _viewWidgets.end()) {
            if (_mainLayout) {
                _mainLayout->removeWidget(widgetIt.value());
            }
            delete widgetIt.value();
            _viewWidgets.erase(widgetIt);
        }
        
        // 从映射中移除
        _subViews.erase(viewIt);
        
        DEBUG_PRINT("视图和窗口移除成功");
    } else {
        DEBUG_PRINT("警告: 找不到要移除的视图，ID: " << viewId);
    }
}

osg::ref_ptr<osgViewer::View> osgQtCompositeViewer::getView(int viewId)
{
    auto it = _subViews.find(viewId);
    if (it != _subViews.end()) {
        return it.value();
    }
    DEBUG_PRINT("警告: 找不到视图，ID: " << viewId);
    return nullptr;
}

// 获取指定ID的相机
osg::Camera *osgQtCompositeViewer::getCamera(int viewId)
{
    auto view = _subViews.find(viewId)->get();
    if (view && view->getCamera()) {
              return view->getCamera();
    }

    return nullptr;
}


QWidget* osgQtCompositeViewer::getViewWidget(int viewId)
{
    auto it = _viewWidgets.find(viewId);
    if (it != _viewWidgets.end()) {
        return it.value();
    }
    DEBUG_PRINT("警告: 找不到视图窗口，ID: " << viewId);
    return nullptr;
}

void osgQtCompositeViewer::updateAllViews()
{
    if (!_compositeViewer) {
        DEBUG_PRINT("警告: CompositeViewer 不存在，跳过渲染");
        return;
    }

    // 1. 仅在视图首次渲染时检查场景数据（避免重复设置）
//    static bool sceneDataInitialized = false;
//    if (!sceneDataInitialized) {
//        osg::Group* globalSceneRoot = Mte3DService::getInstance().getTopRoot();
//        if (globalSceneRoot) {
//            for (unsigned int i = 0; i < _compositeViewer->getNumViews(); ++i) {
//                osgViewer::View* view = _compositeViewer->getView(i);
//                if (view && !view->getSceneData()) {
//                    view->setSceneData(globalSceneRoot);
//                    DEBUG_PRINT("视图 " << i << " 已初始化场景根节点");
//                }
//            }
//            sceneDataInitialized = true;
//        } else {
//            DEBUG_PRINT("警告: globalSceneRoot为空，无法初始化场景数据");
//        }
//    }

    // 2. 执行OSG核心渲染（关键步骤，不可删除）
    _compositeViewer->frame();

    // 3. 同步刷新Qt窗口（确保渲染结果显示）
    for (auto& widgetPair : _viewWidgets) {
        if (widgetPair) {
            widgetPair->update();
        }
    }
}

// 实现paintEvent方法，使用事件驱动渲染
void osgQtCompositeViewer::paintEvent(QPaintEvent* event)
{
    DEBUG_PRINT("paintEvent被调用，执行单次渲染");
    // 仅在需要时手动触发一次渲染（如窗口首次显示或resize后）
    if (_compositeViewer && _compositeViewer->getNumViews() > 0) {
        _compositeViewer->frame();
    }
    QWidget::paintEvent(event); // 调用父类方法，避免Qt事件异常
}

// 重写resizeEvent以更新所有相机视口
void osgQtCompositeViewer::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    for (auto& viewPair : _subViews) {
        osg::ref_ptr<osgViewer::View> view = viewPair;
        if (!view) continue;

        osg::Camera* camera = view->getCamera();
        osgQt::GraphicsWindowQt* gw = dynamic_cast<osgQt::GraphicsWindowQt*>(camera->getGraphicsContext());
        if (!camera || !gw) continue;

        QWidget* widget = gw->getGLWidget();
        if (widget) {
            int w = widget->width();
            int h = widget->height();
            camera->setViewport(0, 0, w, h); // 直接更新视口，无需重新创建
            // 同步更新投影矩阵的宽高比（避免拉伸）
            if (h > 0) {
                float aspect = static_cast<float>(w) / h;
                camera->setProjectionMatrixAsPerspective(30.0f, aspect, 1.0f, 10000.0f);
            }
            DEBUG_PRINT("视图 " << viewPair << " 视口更新为 " << w << "x" << h);
        }
    }
}
