#ifndef OBJVIEWERWIDGET_H
#define OBJVIEWERWIDGET_H

#include <QWidget>
#include <QString>
#include <QPushButton>

// OSG forward declarations
#include <osg/ref_ptr>

namespace osg {
class Node;
class Group;
class Camera;
class GraphicsContext;
}

namespace osgViewer {
class Viewer;
}

namespace osgQt {
class GraphicsWindowQt;
}

class ObjViewerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ObjViewerWidget(QWidget *parent = nullptr);
    ~ObjViewerWidget() override;

    // 加载OBJ文件
    bool loadObjFile(const QString &filePath);

private slots:
    void onFrame();

private:
    osg::ref_ptr<osg::Group> root;
    osg::ref_ptr<osgViewer::Viewer> viewer;
    osg::ref_ptr<osgQt::GraphicsWindowQt> graphicsWindow = nullptr;
    QWidget *glWidget = nullptr; // widget coming from GraphicsWindowQt
    int widthHint = 200;
    int heightHint = 200;

    QPushButton *objOpenBtn = nullptr;

    // 重置视图
    void resetView();
};

#endif // OBJVIEWERWIDGET_H
