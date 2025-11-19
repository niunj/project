// UTF-8 BOM
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVBoxLayout>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE


class OSGWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // 重置视图槽函数
    void on_actionReset_View_triggered();
    
    // 关于对话框槽函数
    void on_actionAbout_triggered();

private:
    Ui::MainWindow *ui;
    OSGWidget *m_osgWidget;     // OSG渲染窗口
    QVBoxLayout *m_layout;      // 主布局
    
    // 初始化UI和OSG集成
    void setupUI();

    void createMenus();

};
#endif // MAINWINDOW_H
