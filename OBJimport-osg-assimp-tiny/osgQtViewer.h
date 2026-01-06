#pragma once
// #include "MyOsgEarth.h" // 该文件不存在，暂时注释掉
#include <QWidget>
#include <QTimer>
#include <QGridLayout>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QOpenGLWidget>

// 添加必要的OSG头文件
#include <osg/Node>
#include <osgViewer/Viewer>
#include <osgViewer/GraphicsWindow>
#include <osg/MatrixTransform>
#include <osg/PositionAttitudeTransform>
#include <osgGA/StateSetManipulator>

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLTexture>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QImage>

class CameraManipulatorWidget;

class  osgQtViewer : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
	Q_OBJECT


public:
    osgQtViewer(QWidget* parent = 0);
	~osgQtViewer();


public:
    osg::ref_ptr<osgViewer::Viewer> getViewer();


public slots:
//	void setSaltNoise(bool noise);
//	void setGaussianNoise(bool noise);

//    void setNoiseDentisy(double dentisy);
//	void setBrightNess(double breight);
//	void setRate(double rate);
//    void setDetectorTotal(float rate);


protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;


    virtual void keyPressEvent(QKeyEvent* event);
    virtual void keyReleaseEvent(QKeyEvent* event);
    virtual void mousePressEvent(QMouseEvent* event);
    virtual void mouseReleaseEvent(QMouseEvent* event);
    virtual void mouseMoveEvent(QMouseEvent* event);
    virtual void wheelEvent(QWheelEvent* event);


private:
    osg::ref_ptr<osgViewer::Viewer>                     _viewer;
    osg::ref_ptr<osgViewer::GraphicsWindowEmbedded>     _graphicsWindow;


};


