QT       += core gui widgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = ObjMlsViewer
TEMPLATE = app

CODECFORSRC = UTF-8

win32 {
        #3rdparty
        MTE_3RDPARTY_PATH = $$PWD/3rdparty_windows

        #osg
        INCLUDEPATH += $$MTE_3RDPARTY_PATH/osg365/include
        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losg

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgDBd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgDB

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgFXd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgFX

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgGAd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgGA

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgManipulatord
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgManipulator

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgParticled
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgParticle

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgTextd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgText

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgQt5d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgQt5

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgTerraind
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgTerrain

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgUtild
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgUtil

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgViewerd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgViewer

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgVolumed
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgVolume

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgWidgetd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgWidget

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -lOpenThreadsd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -lOpenThreads



        #osgEarth
        INCLUDEPATH += $$MTE_3RDPARTY_PATH/oe32/include
        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/oe32/lib -losgEarthd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/oe32/lib -losgEarth

}

# 源文件
SOURCES += \
    imagepreviewwidget.cpp \
    main.cpp \
    mainwindow.cpp \
    objviewerwidget.cpp

# 头文件
HEADERS += \
    imagepreviewwidget.h \
    mainwindow.h \
    objviewerwidget.h

# UI文件
FORMS +=

# 资源文件（如果有）
# RESOURCES += \
#     resources.qrc

# 编译选项
QMAKE_CXXFLAGS += -std=c++11

# 平台特定配置
unix {
    target.path = /usr/local/bin
    INSTALLS += target
}

win32 {
    # Windows特定配置
    DEFINES += _CRT_SECURE_NO_WARNINGS
}
