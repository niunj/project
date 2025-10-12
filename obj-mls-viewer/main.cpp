#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    // 确保支持高DPI显示
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    
    MainWindow w;
    w.show();
    return a.exec();
}
