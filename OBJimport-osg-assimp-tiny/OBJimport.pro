QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET   = OBJimport
TEMPLATE = app

# Assimp库设置
INCLUDEPATH += $$PWD/Assimp/include
LIBS += -L$$PWD/Assimp/lib -lassimp

# OSG库设置
INCLUDEPATH += $$PWD/osg365/include
INCLUDEPATH += $$PWD/osgQt/include


CONFIG(debug, debug|release): {
    LIBS += -L$$PWD/osg365/lib
    LIBS += -L$$PWD/osgQt/lib
    LIBS += -lOpenThreadsd
    LIBS += -losgd
    LIBS += -losgDBd
    LIBS += -losgUtild
    LIBS += -losgGAd
    LIBS += -losgViewerd
    LIBS += -losgQtd
}

CONFIG(release, debug|release):{
    LIBS += -L$$PWD/osg365/lib
    LIBS += -L$$PWD/osgQt/lib
    LIBS += -lOpenThreads
    LIBS += -losg
    LIBS += -losgDB
    LIBS += -losgUtil
    LIBS += -losgGA
    LIBS += -losgViewer
    LIBS += -losgQt
}




# TinyObj源文件
SOURCES += \
    main.cpp \
    mainwindow.cpp \
    osgQtViewer.cpp \
    $$PWD/TinyObj/tiny_obj_loader.cc

HEADERS += \
    mainwindow.h \
    osgQtViewer.h \
    $$PWD/TinyObj/tiny_obj_loader.h

FORMS += \
    mainwindow.ui


