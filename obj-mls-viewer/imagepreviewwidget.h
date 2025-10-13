#ifndef IMAGEPREVIEWWIDGET_H
#define IMAGEPREVIEWWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QPixmap>
#include <QMouseEvent>
#include <QString>

class ImagePreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ImagePreviewWidget(QWidget *parent = nullptr);
    void setImage(const QPixmap &pixmap);
    void setFileName(const QString &fileName);
    QString getFileName() const;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QLabel *imageLabel;
    QString currentFileName;
    bool isDragging;
    QCursor originalCursor;

signals:
    void fileDragStarted(const QString &fileName);
    void fileDragEnded();
};

#endif // IMAGEPREVIEWWIDGET_H
