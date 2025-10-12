#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDir>
#include <QFileInfoList>
#include <QGridLayout>

class QLineEdit;
class QPushButton;
class QScrollArea;
class QWidget;


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_scanBtn_clicked();
    void on_browseBtn_clicked();

private:

    QGridLayout *fileGridLayout;
    QLineEdit *directoryLineEdit;
    QPushButton *scanBtn;
    QPushButton *browseBtn;
    QScrollArea *scrollArea;
    QWidget *filesWidget;
    
    // 扫描目录中的OBJ和对应的MLS文件
    QFileInfoList scanObjFiles(const QString &directory);
    
    // 清除现有布局中的所有控件
    void clearLayout(QLayout *layout);
    
    // 创建文件显示控件并添加到布局
    void addFileWidgets(const QFileInfoList &objFiles);
};
#endif // MAINWINDOW_H
