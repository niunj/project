#include "osgwidget.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <osgDB/ReadFile>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/ShapeDrawable>
#include <osg/Shape>
#include <osg/LightSource>
#include <osg/Light>
#include <osg/PositionAttitudeTransform>
#include <osg/MatrixTransform>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osgDB/Registry>
#include <osg/Material>
#include <osgUtil/Optimizer>


// OSG Qt集成相关
#include <osgQt/GraphicsWindowQt>

OSGWidget::OSGWidget(QWidget *parent) : QOpenGLWidget(parent)
{
    // 设置窗口属性
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(640, 480);
    
    // 注意：不在构造函数中初始化OSG，因为此时OpenGL上下文尚未创建
    // 延迟到initializeGL()中进行初始化
    m_viewer = nullptr;
}

OSGWidget::~OSGWidget()
{
    // 清理OSG资源
    m_viewer->setDone(true);
    m_viewer = nullptr;
}

osgViewer::Viewer* OSGWidget::getViewer()
{
    return m_viewer.get();
}

void OSGWidget::initializeOSG()
{
    // 创建根节点
    m_root = new osg::Group;
    
    // 创建场景图形操作器
    m_manipulator = new osgGA::TrackballManipulator;
    
    // 设置默认视角
    osg::Vec3d eye(0, -10, 10);
    osg::Vec3d center(0, 0, 0);
    osg::Vec3d up(0, 0, 1);
    m_manipulator->setHomePosition(eye, center, up);
    
    // 创建OSG视图器
    m_viewer = new osgViewer::Viewer;
    m_viewer->setCameraManipulator(m_manipulator);
    
    // 配置渲染设置
    m_viewer->setThreadingModel(osgViewer::Viewer::SingleThreaded);
    
    // 注意：此时还没有设置图形上下文，所以不设置相机相关属性
}


void OSGWidget::addSampleGeometry()
{
    // 创建一个简单的盒子作为测试
    osg::ref_ptr<osg::Geode> geode = new osg::Geode;
    
    // 使用OSG内置的几何体创建函数
    osg::Box* box = new osg::Box(osg::Vec3(0, 0, 0), 2.0f);
    osg::ShapeDrawable* shapeDrawable = new osg::ShapeDrawable(box);
    shapeDrawable->setColor(osg::Vec4(1.0f, 0.5f, 0.0f, 1.0f)); // 橙色
    shapeDrawable->setUseDisplayList(false); // 确保不使用显示列表，避免潜在问题
    
    geode->addDrawable(shapeDrawable);
    
    // 创建状态集，确保正确的渲染状态
    osg::ref_ptr<osg::StateSet> stateSet = geode->getOrCreateStateSet();
    
    // 添加一个简单的材质，使对象更好地响应光照
    osg::ref_ptr<osg::Material> material = new osg::Material;
    material->setDiffuse(osg::Material::FRONT_AND_BACK, osg::Vec4(1.0f, 0.5f, 0.0f, 1.0f));
    material->setSpecular(osg::Material::FRONT_AND_BACK, osg::Vec4(1.0f, 1.0f, 1.0f, 1.0f));
    material->setShininess(osg::Material::FRONT_AND_BACK, 64.0f);
    stateSet->setAttributeAndModes(material, osg::StateAttribute::ON);
    
    // 添加到场景
    m_root->addChild(geode);
    
    // 添加一些基本光照
    osg::ref_ptr<osg::LightSource> lightSourceObj = new osg::LightSource;
    osg::ref_ptr<osg::Light> lightObj = new osg::Light;
    lightObj->setLightNum(0);
    lightObj->setPosition(osg::Vec4(10.0f, 10.0f, 10.0f, 1.0f));
    lightObj->setDiffuse(osg::Vec4(1.0f, 1.0f, 1.0f, 1.0f));
    lightObj->setSpecular(osg::Vec4(1.0f, 1.0f, 1.0f, 1.0f));
    lightObj->setAmbient(osg::Vec4(0.2f, 0.2f, 0.2f, 1.0f)); // 添加环境光
    lightSourceObj->setLight(lightObj);
    lightSourceObj->setReferenceFrame(osg::LightSource::ABSOLUTE_RF); // 使用绝对坐标系
    
    // 添加光源到场景
    m_root->addChild(lightSourceObj);
    
    // 确保场景图启用光照
    m_root->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::ON);
    m_root->getOrCreateStateSet()->setMode(GL_LIGHT0, osg::StateAttribute::ON);


}

void OSGWidget::initializeGL()
{
    // 初始化OpenGL上下文
    QOpenGLWidget::initializeGL();
    
    // 确保OSG已初始化
    if (!m_viewer) {
        initializeOSG();
    }
    
    // 设置OSG的图形上下文
    m_traits = new osg::GraphicsContext::Traits;
    m_traits->x = 0;
    m_traits->y = 0;
    m_traits->width = width();
    m_traits->height = height();
    m_traits->windowDecoration = false;
    m_traits->doubleBuffer = true;
    m_traits->sharedContext = nullptr;
    m_traits->pbuffer = false;
    m_traits->red = 8;
    m_traits->green = 8;
    m_traits->blue = 8;
    m_traits->alpha = 8;
    m_traits->depth = 24;
    m_traits->stencil = 8;
    
    // 创建Qt图形窗口
    m_graphicsWindow = new osgQt::GraphicsWindowQt(m_traits.get());
    
    // 设置相机
    osg::Camera* camera = m_viewer->getCamera();
    camera->setGraphicsContext(m_graphicsWindow);
    camera->setViewport(new osg::Viewport(0, 0, width(), height()));
    camera->setClearColor(osg::Vec4(0.1, 0.1, 0.3, 1.0)); // 深蓝色背景
    
    // 设置投影矩阵
    float aspectRatio = static_cast<float>(width()) / static_cast<float>(height());
    camera->setProjectionMatrixAsPerspective(30.0f, aspectRatio, 1.0f, 10000.0f);
    
    // 添加样本几何体和场景数据
    addSampleGeometry();
    m_viewer->setSceneData(m_root);
    
    // 开启自动渲染
    m_viewer->realize();
    
    // 初始渲染
    update();
}

void OSGWidget::resizeGL(int width, int height)
{
    // 调整视口
    if (m_viewer && m_viewer->getCamera()) {
        m_viewer->getCamera()->setViewport(new osg::Viewport(0, 0, width, height));
        
        // 更新投影矩阵
        float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        m_viewer->getCamera()->setProjectionMatrixAsPerspective(30.0f, aspectRatio, 1.0f, 10000.0f);
    }
}

void OSGWidget::paintGL()
{
    // 渲染OSG场景
    if (m_viewer && m_viewer->getCamera()->getGraphicsContext()) {
        m_viewer->frame();
    }
}

void OSGWidget::mousePressEvent(QMouseEvent *event)
{
    // 处理鼠标按下事件
    if (m_graphicsWindow) {
        osgGA::EventQueue::Events events;
        osgGA::GUIEventAdapter::MouseButtonMask mask = static_cast<osgGA::GUIEventAdapter::MouseButtonMask>(0);
        
        switch (event->button()) {
        case Qt::LeftButton:
            mask = osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON;
            break;
        case Qt::MiddleButton:
            mask = osgGA::GUIEventAdapter::MIDDLE_MOUSE_BUTTON;
            break;
        case Qt::RightButton:
            mask = osgGA::GUIEventAdapter::RIGHT_MOUSE_BUTTON;
            break;
        default:
            break;
        }
        
        m_graphicsWindow->getEventQueue()->mouseButtonPress(event->x(), event->y(), mask);
    }
    
    // 调用基类方法
    QOpenGLWidget::mousePressEvent(event);
}

void OSGWidget::mouseReleaseEvent(QMouseEvent *event)
{
    // 处理鼠标释放事件
    if (m_graphicsWindow) {
        osgGA::EventQueue::Events events;
        osgGA::GUIEventAdapter::MouseButtonMask mask = static_cast<osgGA::GUIEventAdapter::MouseButtonMask>(0);
        
        switch (event->button()) {
        case Qt::LeftButton:
            mask = osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON;
            break;
        case Qt::MiddleButton:
            mask = osgGA::GUIEventAdapter::MIDDLE_MOUSE_BUTTON;
            break;
        case Qt::RightButton:
            mask = osgGA::GUIEventAdapter::RIGHT_MOUSE_BUTTON;
            break;
        default:
            break;
        }
        
        m_graphicsWindow->getEventQueue()->mouseButtonRelease(event->x(), event->y(), mask);
    }
    
    // 调用基类方法
    QOpenGLWidget::mouseReleaseEvent(event);
}

void OSGWidget::mouseMoveEvent(QMouseEvent *event)
{
    // 处理鼠标移动事件
    if (m_graphicsWindow) {
        m_graphicsWindow->getEventQueue()->mouseMotion(event->x(), event->y());
    }
    
    // 调用基类方法
    QOpenGLWidget::mouseMoveEvent(event);
}

void OSGWidget::wheelEvent(QWheelEvent *event)
{
    // 处理鼠标滚轮事件
    if (m_graphicsWindow) {
        float delta = event->angleDelta().y() > 0 ? 1.0f : -1.0f;
        m_graphicsWindow->getEventQueue()->mouseScroll(delta > 0 ? osgGA::GUIEventAdapter::SCROLL_UP : osgGA::GUIEventAdapter::SCROLL_DOWN);
    }
    
    // 调用基类方法
    QOpenGLWidget::wheelEvent(event);
}
