QT       += core gui opengl

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
    main.cpp


# 配置vcpkg库路径
# 请根据您的vcpkg安装路径修改
VCPKG_ROOT = d:/vcpkg/vcpkg  # 默认路径，请根据实际情况修改


# 根据目标架构选择正确的库目录
CONFIG(debug, debug|release) {
    INCLUDEPATH   += $$VCPKG_ROOT/installed/x64-windows/include
    VCPKG_LIB_PATH = $$VCPKG_ROOT/installed/x64-windows/debug

# 添加库文件搜索路径
    LIBS += -L$$VCPKG_LIB_PATH/lib

# 链接必要的OSG库
    LIBS += -losgd
    LIBS += -losgViewerd
    LIBS += -losgGAd
    LIBS += -losgDBd
    LIBS += -losgTextd
    LIBS += -losgUtild
    LIBS += -losgSimd
    LIBS += -losgParticled
    LIBS += -lOpenThreadsd
    LIBS += -losgQtd

# 配置运行时路径，确保运行时能找到库文件
    QMAKE_RPATHDIR += $$VCPKG_LIB_PATH/bin

}

CONFIG(release, debug|release) {
    INCLUDEPATH    += $$VCPKG_ROOT/installed/x64-windows/include
    VCPKG_LIB_PATH  = $$VCPKG_ROOT/installed/x64-windows

# 添加库文件搜索路径
    LIBS += -L$$VCPKG_LIB_PATH/lib

# 链接必要的OSG库
    LIBS += -losg
    LIBS += -losgViewer
    LIBS += -losgGA
    LIBS += -losgDB
    LIBS += -losgText
    LIBS += -losgUtil
    LIBS += -losgSim
    LIBS += -losgParticle
    LIBS += -lOpenThreads
    LIBS += -losgQt


# 配置运行时路径，确保运行时能找到库文件
    QMAKE_RPATHDIR += $$VCPKG_LIB_PATH/bin

}

