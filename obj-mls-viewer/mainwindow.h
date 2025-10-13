#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>
#include <QDir>
#include <QFileInfoList>
#include <QGridLayout>
#include <QMap>

class QLineEdit;
class QPushButton;
class QScrollArea;
class QWidget;
class QToolBox;


class MainWindow : public QWidget
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void initializeToolBox();

    // 添加工具箱页面
    void addToolBoxPage(const QString &category, const QStringList &files);

    void setData(const QMap<QString, QStringList> &data);

private:

    QGridLayout *fileGridLayout;
    QLineEdit *directoryLineEdit;
    QPushButton *scanBtn;
    QPushButton *browseBtn;
    QScrollArea *scrollArea;
    QWidget *filesWidget;
    

private:
    QToolBox *toolBox;
    QMap<QString, QStringList>  dataStructure;

};
#endif // MAINWINDOW_H
