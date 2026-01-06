#ifndef OSGQTCOMPOSEVIEWER_H
#define OSGQTCOMPOSEVIEWER_H

#include <osgViewer/CompositeViewer>
#include <osgViewer/View>
#include <osg/GraphicsContext>
#include <QMap>
#include <QWidget>
#include <QVBoxLayout>
#include <QTimer>
#include <QFrame>
#include <QVBoxLayout>


// 复合视图管理器，继承自QWidget，为每个view创建对应的widget
class osgQtCompositeViewer : public QWidget
{
    Q_OBJECT
public:
    osgQtCompositeViewer(QWidget *parent = nullptr);
    ~osgQtCompositeViewer();

    // 获取指定ID的子视图（ID从0开始）
    osg::ref_ptr<osgViewer::View> getView(int viewId = 0);
    
    // 获取指定ID的相机
    osg::Camera *getCamera(int viewId = 0);
    

    // 添加一个外部创建的View到CompositeView
    int addView(osg::ref_ptr<osgViewer::View> view, int viewId = -1);

    // 移除指定ID的视图
    void removeView(int viewId);
    
    // 更新所有视图并执行渲染
    void updateAllViews();
    
    // 获取复合视图实例
    osg::ref_ptr<osgViewer::CompositeViewer> getCompositeViewer() { return _compositeViewer; }
    
signals:
    // 发送三维场景平台删除信号
    void threeDSceneRemovePlatform_signal(int platformId);

protected:
    // 重写paintEvent方法用于渲染
    virtual void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event);
    // 重写keyPressEvent方法处理键盘事件
    virtual void keyPressEvent(QKeyEvent* event) override;
    // 重写eventFilter方法捕获子控件的键盘事件
    virtual bool eventFilter(QObject* obj, QEvent* event) override;

private:
    osg::ref_ptr<osgViewer::CompositeViewer>            _compositeViewer; // 复合视图实例
    QMap<int, osg::ref_ptr<osgViewer::View>>            _subViews; // 子视图映射（ID -> 视图）
    QMap<int, QWidget*>                                 _viewWidgets; // 视图窗口映射（ID -> Widget）
    QGridLayout*                                        _mainLayout; // 主布局管理器
    QTimer*                                             _timer; // 定时器，用于定期刷新视图
};

#endif // OSGQTCOMPOSEVIEWER_H
