#include "osgQtViewer.h"
#include <osgGA/StateSetManipulator>
#include <QPixmap>
#include <QBitmap>
#include <random>
// 包含 osgComposeViewer 相关头文件
#include <osgViewer/CompositeViewer>
osgQtViewer::osgQtViewer(QWidget* parent) : QOpenGLWidget(parent)
{
    _viewer = new osgViewer::Viewer;
    _viewer->setThreadingModel(osgViewer::Viewer::SingleThreaded);

    // 创建图形窗口
    _graphicsWindow = _viewer->setUpViewerAsEmbeddedInWindow(0, 0, width(), height());

    // MyOsgEarth相关代码已注释，因为MyOsgEarth.h文件不存在
    // _pEarthMan=new osgEarth::Util::EarthManipulator;
    // _viewer->setCameraManipulator(_pEarthMan);

}


osgQtViewer::~osgQtViewer()
{
    makeCurrent();

    doneCurrent();
}

osg::ref_ptr<osgViewer::Viewer> osgQtViewer::getViewer()
{
    return _viewer;
}


void osgQtViewer::initializeGL()
{
    initializeOpenGLFunctions();
    qDebug() << "GL Version:" << reinterpret_cast<const char*>(glGetString(GL_VERSION));
    // 初始化OSG状态
    _viewer->realize();
    
    // 启用混合以支持透明
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

}

void osgQtViewer::resizeGL(int w, int h)
{
    // 调整OSG视口
    _graphicsWindow->getEventQueue()->windowResize(0, 0, w, h);
    _graphicsWindow->resized(0, 0, w, h);
    
    // 根据 _viewer 的类型进行不同的处理
    // 普通 Viewer 的处理方式
    _viewer->getCamera()->setViewport(0, 0, w, h);

}

void osgQtViewer::paintGL()
{
    // 根据 _viewer 的类型进行不同的处理

    // 普通 Viewer 的处理方式
    _viewer->getCamera()->getGraphicsContext()->setDefaultFboId(defaultFramebufferObject());


    makeCurrent();


    // 1. 先让 OSG 画场景
    _viewer->frame();

    // 3. 如果想持续刷新浪点动画，可以调用 update()
    update();
}

void osgQtViewer::keyPressEvent(QKeyEvent* event)
{
    _graphicsWindow->getEventQueue()->keyPress((osgGA::GUIEventAdapter::KeySymbol) * (qPrintable(event->text())));
}

void osgQtViewer::keyReleaseEvent(QKeyEvent* event)
{
    _graphicsWindow->getEventQueue()->keyRelease((osgGA::GUIEventAdapter::KeySymbol) * (qPrintable(event->text())));
}

void osgQtViewer::mousePressEvent(QMouseEvent* event)
{
    int button = 0;
    switch (event->button())
    {
    case(Qt::LeftButton): button = 1; break;
    case(Qt::MidButton): button = 2; break;
    case(Qt::RightButton): button = 3; break;
    case(Qt::NoButton): button = 0; break;
    default: button = 0; break;
    }
    _graphicsWindow->getEventQueue()->mouseButtonPress(event->x(), event->y(), button);
}

void osgQtViewer::mouseReleaseEvent(QMouseEvent* event)
{
    int button = 0;
    switch (event->button())
    {
    case(Qt::LeftButton): button = 1; break;
    case(Qt::MidButton): button = 2; break;
    case(Qt::RightButton): button = 3; break;
    case(Qt::NoButton): button = 0; break;
    default: button = 0; break;
    }
    _graphicsWindow->getEventQueue()->mouseButtonRelease(event->x(), event->y(), button);
}

void osgQtViewer::mouseMoveEvent(QMouseEvent* event)
{
    _graphicsWindow->getEventQueue()->mouseMotion(event->x(), event->y());
}

void osgQtViewer::wheelEvent(QWheelEvent* event)
{
    _graphicsWindow->getEventQueue()->mouseScroll(
                event->orientation() == Qt::Vertical ?
                    (event->delta() > 0 ? osgGA::GUIEventAdapter::SCROLL_UP : osgGA::GUIEventAdapter::SCROLL_DOWN) :
                    (event->delta() > 0 ? osgGA::GUIEventAdapter::SCROLL_LEFT : osgGA::GUIEventAdapter::SCROLL_RIGHT));

}
