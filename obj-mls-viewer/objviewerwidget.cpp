#include "objviewerwidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QFileDialog>
#include <QDebug>

// OSG includes
#include <osg/Group>
#include <osg/Node>
#include <osg/BoundingSphere>
#include <osg/Vec3>
#include <osgDB/ReadFile>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>
#include <osgQt/GraphicsWindowQt>
#include <osg/GraphicsContext>
#include <osg/Viewport>
#include <osgGA/TrackballManipulator>


// 用于处理MLS文件的假设实现
#include "mlsreader.h"

// Helper to create GraphicsWindowQt (conventional setup)
static osg::ref_ptr<osgQt::GraphicsWindowQt> createGraphicsWindow(int x, int y, int w, int h)
{
    osg::ref_ptr<osg::GraphicsContext::Traits> traits = new osg::GraphicsContext::Traits();
    traits->windowName = "OSGQtWindow";
    traits->x = x;
    traits->y = y;
    traits->width = w;
    traits->height = h;
    // For embedding, allow window decorations (so GL widget behaves correctly)
    traits->windowDecoration = true;
    traits->doubleBuffer = true;
    traits->alpha = 8;
    traits->stencil = 8;
    traits->sampleBuffers = 1;
    traits->samples = 4;

    osg::ref_ptr<osgQt::GraphicsWindowQt> gw = new osgQt::GraphicsWindowQt(traits.get());
    return gw;
}

ObjViewerWidget::ObjViewerWidget(QWidget *parent)
    : QWidget(parent)
{
    root = new osg::Group();

    // fix the widget size to 200x200
    setFixedSize(widthHint, heightHint);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // layout: GL widget above, status bar below
    QVBoxLayout *main = new QVBoxLayout(this);
    main->setContentsMargins(0,0,0,0);
    main->setSpacing(2);

    // create and embed graphics window
    graphicsWindow = createGraphicsWindow(0,0,widthHint,heightHint);
    if (graphicsWindow) {
        QWidget *maybeWidget = dynamic_cast<QWidget*>(graphicsWindow.get());
        if (maybeWidget) {
            maybeWidget->setParent(this);
            maybeWidget->setFixedSize(widthHint, heightHint - 24); // leave space for status bar
            main->addWidget(maybeWidget, 1);
        } else {
            // fallback: use the GL widget provided by GraphicsWindowQt
            glWidget = graphicsWindow->getGLWidget();
            if (glWidget) {
                glWidget->setParent(this);
                glWidget->setFixedSize(widthHint, heightHint - 24);
                main->addWidget(glWidget, 1);
            }
        }
    }

    // status bar: only contains open buttons for convenience
    QWidget *statusBar = new QWidget(this);
    statusBar->setFixedHeight(24);
    QHBoxLayout *sLayout = new QHBoxLayout(statusBar);
    sLayout->setContentsMargins(4,4,4,4);
    sLayout->setSpacing(6);

    objOpenBtn = new QPushButton(tr("打开OBJ"), statusBar);
    QPushButton *dummy = new QPushButton(tr(""), statusBar);
    dummy->setVisible(false);
    sLayout->addStretch();
    sLayout->addWidget(dummy);
    sLayout->addWidget(objOpenBtn);
    main->addWidget(statusBar, 0);

    // setup viewer
    viewer = new osgViewer::Viewer();
    viewer->setThreadingModel(osgViewer::Viewer::SingleThreaded);
    viewer->addEventHandler(new osgViewer::StatsHandler());

    // set a camera manipulator so home() centers on scene
    viewer->setCameraManipulator(new osgGA::TrackballManipulator());

    osg::Camera* camera = viewer->getCamera();

    // set GraphicsContext and a proper viewport object
    if (graphicsWindow) {
        camera->setGraphicsContext(graphicsWindow.get());
        camera->setViewport(new osg::Viewport(0, 0, widthHint, heightHint - 24));
        camera->setDrawBuffer(GL_BACK);
        double aspect = static_cast<double>(widthHint) / static_cast<double>(heightHint - 24);
        camera->setProjectionMatrixAsPerspective(30.0, aspect, 1.0, 10000.0);
    }

    viewer->setSceneData(root.get());

    // timer to drive frames
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &ObjViewerWidget::onFrame);
    timer->start(16);

    // connect buttons
    connect(objOpenBtn, &QPushButton::clicked, this, [this](){
        QString f = QFileDialog::getOpenFileName(this, tr("打开OBJ文件"), QString(), tr("OBJ Files (*.obj *.OBJ)"));
        if (!f.isEmpty()) loadObjFile(f);
    });
}

ObjViewerWidget::~ObjViewerWidget()
{
    if (viewer) {
        viewer->setDone(true);
        viewer = nullptr;
    }
}

void ObjViewerWidget::onFrame()
{
    if (viewer) viewer->frame();
}

bool ObjViewerWidget::loadObjFile(const QString &filePath)
{
    if (filePath.isEmpty()) return false;

    osg::ref_ptr<osg::Node> node = osgDB::readNodeFile(filePath.toStdString());
    if (!node) {
        qWarning() << "无法读取OBJ文件:" << filePath;
        return false;
    }

    // remove previous geometry children
    root->removeChildren(0, root->getNumChildren());
    root->addChild(node.get());

    // adjust camera to fit the newly loaded model
    osg::BoundingSphere bs = root->getBound();
    qDebug() << "Model bound radius:" << bs.radius();
    if (bs.radius() > 0.0) {
        osg::Vec3d center = bs.center();
        double distance = bs.radius() * 3.0;
        osg::Vec3d eye(center.x(), center.y() - distance, center.z() + distance);
        osg::Vec3d up(0.0, 0.0, 1.0);
        if (viewer && viewer->getCamera()) {
            viewer->getCamera()->setViewMatrixAsLookAt(eye, center, up);
        }
    }

    // reset view by letting viewer home (keeps manipulator state in sync)
    if (viewer) viewer->home();

    return true;
}

bool ObjViewerWidget::loadMlsFile(const QString &filePath)
{
    if (filePath.isEmpty()) return false;
    MlsReader reader;
    if (!reader.read(filePath)) {
        qWarning() << "无法读取MLS文件:" << filePath;
        return false;
    }

    // 示例：仅更改状态指示，真实实现应解析MLS并应用到场景
    return true;
}
