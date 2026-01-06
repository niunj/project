#include "mainwindow.h"
#include "scene/scenewindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    MainWindow mainWindow;
    mainWindow.show();
    
    SceneWindow sceneWindow;
    sceneWindow.show();
    
    return a.exec();
}