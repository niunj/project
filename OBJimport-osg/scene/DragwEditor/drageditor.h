#ifndef DRAGEDITOR_H
#define DRAGEDITOR_H

#include <QWidget>
#include <QDir>
#include <QFileInfoList>
#include <QGridLayout>
#include <QMap>

// 假设我们有一个用于显示OBJ文件的自定义部件
#include "imagepreviewwidget.h"


class QLineEdit;
class QPushButton;
class QScrollArea;
class QWidget;
class QToolBox;
class QLabel;

// 创建自定义的点击处理部件
class ClickablePreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit ClickablePreviewWidget(const QString &cat, QWidget *parent = nullptr)
        : QWidget(parent), category(cat) {
        setAttribute(Qt::WA_StyledBackground, true);
    }

    QString getCategory() const { return category; }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            emit clicked();
        }
        QWidget::mousePressEvent(event);
    }

private:
    QString category;
};

class SceneEngine;

class DragEditor : public QWidget
{
    Q_OBJECT

public:
    DragEditor(QWidget *parent = nullptr);
    ~DragEditor();

    void initializeToolBox();

    // 添加工具箱页面
    void addToolBoxPage(const QString &category, const QStringList &files);

    // 获取分类的显示名称
    QString getCategoryDisplayName(const QString &category);
    
    // 追加新模型到指定分类
    void addModelToCategory(const QString &category, const QString &modelName);

    void setData(const QString &data);
    
    // 设置SceneEngine
    void setSceneEngine(SceneEngine *engine);

public slots:
    void slotNotofyLog(QString log);
signals:

    void sigPlatformAdded( MtePlatformStru& platform);

private:
    QGridLayout *fileGridLayout;
    QLineEdit *directoryLineEdit;
    QPushButton *scanBtn;
    QPushButton *browseBtn;
    QScrollArea *scrollArea;
    QWidget *filesWidget;


private:
    QToolBox *toolBox;
    QLabel   *logLabel;
    QMap<QString, QStringList>  dataStructure;
    QMap<QString, QGridLayout*> categoryLayouts; // 存储每个分类的gridLayout
    ImagePreviewWidget *selectedPreviewWidget; // 当前选中的预览widget
    SceneEngine *m_pSceneEngine; // SceneEngine指针
    QList<ImagePreviewWidget*> m_previewWidgets;
    
    // 辅助函数：获取指定分类的ClickablePreviewWidget位置
    QPair<int, int> getClickablePreviewPosition(const QString &category);
    
    // 辅助函数：重新排列网格布局中的widget
    void rearrangeGridLayout(const QString &category);
    
    // 从指定分类中删除模型
    void removeModelFromCategory(const QString &category, const QString &modelName);
    
    // 删除模型对应的目录
    void deleteModelDirectory(const QString &modelPath);
    
protected:
    void keyPressEvent(QKeyEvent *event) override;

};
#endif // MAINWINDOW_H
