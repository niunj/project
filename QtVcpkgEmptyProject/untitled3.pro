QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    mainwindow.h

FORMS += \
    mainwindow.ui

# 配置vcpkg库路径
# 请根据您的vcpkg安装路径修改
VCPKG_ROOT = d:/vcpkg/vcpkg  # 默认路径，请根据实际情况修改


# 根据目标架构选择正确的库目录
win32 {
    VCPKG_LIB_PATH = $$VCPKG_ROOT/installed/x64-windows
} else {
    # Linux/macOS设置
    VCPKG_LIB_PATH = $$VCPKG_ROOT/installed/x64-linux
}

# 添加头文件搜索路径
INCLUDEPATH += $$VCPKG_LIB_PATH/include

# 添加库文件搜索路径
LIBS += -L$$VCPKG_LIB_PATH/lib

# 链接必要的库
LIBS += -losg -losgViewer -losgGA -losgDB -losgQt -losgEarth -lOpenThreads -losgText  -losgUtil -losgSim -losgParticle
