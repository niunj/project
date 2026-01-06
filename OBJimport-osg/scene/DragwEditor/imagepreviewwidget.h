#ifndef IMAGEPREVIEWWIDGET_H
#define IMAGEPREVIEWWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QPixmap>
#include <QMouseEvent>
#include <QString>

#include "../OsgRender/Mte3DService.h"
#include "../OsgRender/OsgContext.h"
#include "../Common/MteStructDef.h"

#include <osgUtil/LineSegmentIntersector>
#include <osgUtil/IntersectionVisitor>

// 前向声明SceneEngine类
class SceneEngine;

class ImagePreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ImagePreviewWidget(QWidget *parent = nullptr);
    void setImage(const QPixmap &pixmap);
    void setFileName(const QString &fileName);
    void setCategoryName(const QString& cateName);
    QString getFileName() const;
    QString getCategoryName() const;
    bool isSelected() const;
    void setSelected(bool selected);


    // 设置SceneEngine
    void setSceneEngine(SceneEngine *engine);


protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;


private:
    QLabel *imageLabel;
    QLabel *fileNameLabel;
    QString currentFileName;
    QString currentCategoryName;
    bool isDragging;
    bool isSelectedState;
    QCursor originalCursor;
    SceneEngine *m_pSceneEngine; // SceneEngine指针

signals:
    void selectionChanged(bool selected);
    void doubleClicked();

signals:
    void fileDragStarted(const QString &fileName);
    void fileDragEnded();

    void notifyError(QString);

    void sigPlatformAdded(MtePlatformStru& platform);
};

#endif // IMAGEPREVIEWWIDGET_H
