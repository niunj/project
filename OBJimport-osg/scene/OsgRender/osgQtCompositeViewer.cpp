#include "osgQtCompositeViewer.h"
#include "Mte3DService.h"
#include <osgQt/GraphicsWindowQt>
#include <osg/Camera>
#include <QScreen>
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLayoutItem>
#include <QKeyEvent>
#include <QFileDialog>

#include <QGuiApplication>
#include "../Log/log_manager.h"



osgQtCompositeViewer::osgQtCompositeViewer(QWidget *parent)
    : QWidget(parent)
    , _compositeViewer(new osgViewer::CompositeViewer)
    , _mainLayout(nullptr)
    , _timer(nullptr)
{
    LOG_DEBUG <<"构造函数开始执行";
    
    // 设置窗口属性
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    
    // 初始化主布局
    _mainLayout = new QGridLayout(this);
    _mainLayout->setSpacing(6);

    _mainLayout->setContentsMargins(0, 0, 0, 0);

    setLayout(_mainLayout);
    

    // 初始化复合视图（单线程模式）
    if (_compositeViewer) {
        LOG_DEBUG <<"CompositeViewer 创建成功";
        _compositeViewer->setThreadingModel(osgViewer::CompositeViewer::SingleThreaded);
        LOG_DEBUG <<"设置线程模型为SingleThreaded";
    } else {
        LOG_DEBUG <<"警告: CompositeViewer 创建失败！";
    }
    

    // 恢复定时器，设置合理帧率（如16ms=60fps）
     _timer = new QTimer(this);
     connect(_timer, &QTimer::timeout, this, &osgQtCompositeViewer::updateAllViews);
     _timer->start(16);
     LOG_DEBUG <<"启用定时器驱动渲染循环";

    LOG_DEBUG <<"构造函数执行完成";
}

osgQtCompositeViewer::~osgQtCompositeViewer()
{
    LOG_DEBUG <<"析构函数开始执行，清理资源";
    
    // 停止渲染定时器
    if (_timer && _timer->isActive()) {
        _timer->stop();
        LOG_DEBUG <<"渲染定时器已停止";
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
    
    LOG_DEBUG <<"析构函数执行完成，所有资源已清理";
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

int osgQtCompositeViewer::addView(osg::ref_ptr<osgViewer::View> view, int viewId)
{
    LOG_DEBUG <<"添加视图到复合查看器";
    
    // 分配视图ID
    if (viewId < 0) {
        // 如果未指定或无效，使用_subViews.size()作为viewId
        viewId = _subViews.size();
        LOG_DEBUG << "自动为新视图分配ID: " << viewId;
    } else {
        // 检查指定的viewId是否已存在
        if (_subViews.contains(viewId)) {
            LOG_DEBUG << "警告: 指定的视图ID " << viewId << " 已存在，将自动分配新ID";
            viewId = _subViews.size();
            LOG_DEBUG << "自动为新视图分配ID: " << viewId;
        } else {
            LOG_DEBUG << "使用指定的视图ID: " << viewId;
        }
    }
    
    if (view && _compositeViewer) {
        // 为视图创建相机（如果没有）
        if (!view->getCamera()) {
            osg::ref_ptr<osg::Camera> camera = new osg::Camera;
            camera->setViewport(new osg::Viewport(0, 0, 800, 600));
            camera->setProjectionMatrixAsPerspective(30.0f, 1.3333f, 1.0f, 10000.0f);

            // 注意：这个设置需要在相机初始化之前，或者在窗口创建之前生效
            view->setCamera(camera.get());
        }
        
        // 创建Qt窗口用于OSG渲染
        osgQt::GraphicsWindowQt* gw = createGraphicsWindowQt(0, 0, 800, 600);
        view->getCamera()->setGraphicsContext(gw);
        
        // 获取Qt窗口组件
        QWidget* viewWidget = gw->getGLWidget();
        viewWidget->setFocusPolicy(Qt::StrongFocus);
        
        // 为GLWidget安装事件过滤器，以便捕获键盘事件
        viewWidget->installEventFilter(this);

        // 将GLWidget添加到布局
        if(viewId == 0){
            // 第0个视图直接占主区域
            _mainLayout->addWidget(viewWidget, 0, 0, 1, 3);
            _viewWidgets[viewId] = viewWidget;
        }
        else{
            // 其余视图布局在第二行
            _mainLayout->addWidget(viewWidget, 1, viewId-1, 1, 1);
            _viewWidgets[viewId] = viewWidget;
        }

        // 将视图添加到复合视图
        _compositeViewer->addView(view.get());
        LOG_DEBUG <<"视图已添加到复合视图，ID: " << viewId;
        
        // 保存视图映射
        _subViews[viewId] = view;
        LOG_DEBUG <<"已保存视图和窗口映射关系";
        
        return viewId;
    } else {
        LOG_DEBUG <<"警告: 视图添加失败 - 无效的视图或复合视图";
        return -1;
    }
}

void osgQtCompositeViewer::removeView(int viewId)
{
    LOG_DEBUG <<"移除视图，ID: " << viewId;
    
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
        
        LOG_DEBUG <<"视图和窗口移除成功";
    } else {
        LOG_DEBUG <<"警告: 找不到要移除的视图，ID: " << viewId;
    }
}

osg::ref_ptr<osgViewer::View> osgQtCompositeViewer::getView(int viewId)
{
    auto it = _subViews.find(viewId);
    if (it != _subViews.end()) {
        return it.value();
    }
    LOG_DEBUG <<"警告: 找不到视图，ID: " << viewId;
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


void osgQtCompositeViewer::updateAllViews()
{
    if (!_compositeViewer) {
        LOG_DEBUG <<"警告: CompositeViewer 不存在，跳过渲染";
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
//                    LOG_DEBUG <<"视图 " << i << " 已初始化场景根节点");
//                }
//            }
//            sceneDataInitialized = true;
//        } else {
//            LOG_DEBUG <<"警告: globalSceneRoot为空，无法初始化场景数据");
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
    LOG_DEBUG <<"paintEvent被调用，执行单次渲染";
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
            LOG_INFO <<"视图 " << viewPair << " 视口更新为 " << w << "x" << h;
        }
    }
}

// 实现eventFilter方法，捕获子控件的键盘事件
bool osgQtCompositeViewer::eventFilter(QObject* obj, QEvent* event) {
    // 只处理键盘事件
    if (event->type() == QEvent::KeyPress) {
        // 将事件转换为键盘事件
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        
        // 获取Mte3DService实例
        Mte3DService& mte3DService = Mte3DService::getInstance();

        // 处理Ctrl+S保存场景到Obj文件
        if (keyEvent->modifiers() & Qt::ControlModifier && keyEvent->key() == Qt::Key_S) {
            // 打开文件保存对话框
            QString filePath = QFileDialog::getSaveFileName(this, tr("保存场景到Obj文件"), ".", tr("Obj文件 (*.obj)"));
            if (!filePath.isEmpty()) {
                // 确保文件后缀是.obj
                if (!filePath.endsWith(".obj", Qt::CaseInsensitive)) {
                    filePath += ".obj";
                }
                // 保存场景到Obj文件
                mte3DService.saveSceneToObj(filePath);
            }
            // 返回true表示事件已处理
            return true;
        }
        // 处理Delete键删除选中平台
        else if (keyEvent->key() == Qt::Key_Delete) {
            int selectedId = -1;//mte3DService.getSelectedPlatformId();
            if (selectedId != -1) {
                // 发送信号通知Mte3DService删除平台
                emit threeDSceneRemovePlatform_signal(selectedId);
            }
            // 返回true表示事件已处理
            return true;
        }
        
        // 其他键盘事件不处理，让事件继续传递给GLWidget，由OSG的原生事件处理器处理
        // 返回false表示事件未处理，继续传递给GLWidget的事件处理器
        return false;
    }
    
    // 其他事件类型传递给父类处理
    return QWidget::eventFilter(obj, event);
}

// 重写keyPressEvent方法处理键盘事件
void osgQtCompositeViewer::keyPressEvent(QKeyEvent* event) {
    // 不再在这里处理Ctrl+S和Delete事件，避免重复处理
    // 事件已经在eventFilter中处理了
    QWidget::keyPressEvent(event);
}
