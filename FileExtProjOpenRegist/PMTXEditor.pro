QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = PMTXEditor
TEMPLATE = app

SOURCES += \
    main.cpp \
    FileAssociation.cpp

HEADERS += \
    FileAssociation.h

# Windows平台需要管理员权限注册注册表
win32 {

 # 链接必要的系统库（解决SHChangeNotify依赖）
    LIBS += -lshell32 -lole32
}
