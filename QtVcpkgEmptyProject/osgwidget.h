#ifndef OSGWIDGET_H
#define OSGWIDGET_H

#include <QOpenGLWidget>
#include <osgViewer/Viewer>
#include <osgGA/TrackballManipulator>
#include <osgQt/GraphicsWindowQt>

class OSGWidget : public QOpenGLWidget
{
    Q_OBJECT

public:
    explicit OSGWidget(QWidget *parent = nullptr);
    ~OSGWidget();

    // 获取OSG视图器
    osgViewer::Viewer* getViewer();
    
    // 初始化OSG场景
    void initializeOSG();
    
    // 添加基本几何体到场景
    void addSampleGeometry();

protected:
    // Qt OpenGL事件处理
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    
    // 鼠标事件处理
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    // OSG相关成员
    osg::ref_ptr<osgViewer::Viewer> m_viewer;
    osg::ref_ptr<osg::GraphicsContext::Traits> m_traits;
    osg::ref_ptr<osgQt::GraphicsWindowQt> m_graphicsWindow;
    osg::ref_ptr<osgGA::TrackballManipulator> m_manipulator;
    osg::ref_ptr<osg::Group> m_root;
};

#endif // OSGWIDGET_H
