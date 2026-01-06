QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET   = OBJimport
TEMPLATE = app


msvc {
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}

win32 {
        message("Windows配置生效")
        QMAKE_CXXFLAGS += /utf-8

        QMAKE_CXXFLAGS += /wd4100  # C++ 代码禁用 4100
        QMAKE_CFLAGS += /wd4100    # C 代码禁用 4100（可选，若有C代码）
}


# OSG库设置
INCLUDEPATH += $$PWD/osg365/include
INCLUDEPATH += $$PWD/osgQt/include


CONFIG(debug, debug|release):{
    LIBS += -L$$PWD/osg365/lib
    LIBS += -losgd
    LIBS += -losgDBd
    LIBS += -losgFXd
    LIBS += -losgGAd
    LIBS += -losgManipulatord
    LIBS += -losgParticled
    LIBS += -losgTextd
    LIBS += -losgTerraind
    LIBS += -losgUtild
    LIBS += -losgViewerd
    LIBS += -losgVolumed
    LIBS += -losgWidgetd
    LIBS += -lOpenThreadsd

}

CONFIG(release, debug|release):{
    LIBS += -L$$PWD/osg365/lib
    LIBS += -losg
    LIBS += -losgDB
    LIBS += -losgFX
    LIBS += -losgGA
    LIBS += -losgManipulator
    LIBS += -losgParticle
    LIBS += -losgText
    LIBS += -losgTerrain
    LIBS += -losgUtil
    LIBS += -losgViewer
    LIBS += -losgVolume
    LIBS += -losgWidget
    LIBS += -lOpenThreads
}


#osgQt
        INCLUDEPATH += $$PWD/osgQt/include
        CONFIG(debug, debug|release): LIBS += -L$$PWD/osgQt/lib -losgQtd
        CONFIG(release, debug|release): LIBS += -L$$PWD/osgQt/lib -losgQt

##add by niunj  mdd---解决debug不可调试问题
        INCLUDEPATH += $$PWD/SilverLining/include
        CONFIG(debug, debug|release): LIBS += -L$$PWD/SilverLining/lib -lSilverLining-MTD-DLL
        CONFIG(release, debug|release): LIBS += -L$$PWD/SilverLining/lib -lSilverLining-MT-DLL

#boost
        INCLUDEPATH += $$PWD/3rdparty/include
        CONFIG(debug, debug|release): {
            LIBS += -L$$PWD/3rdparty/lib
            LIBS += -llibboost_system-vc142-mt-gd-x64-1_70
            LIBS += -llibboost_thread-vc142-mt-gd-x64-1_70
            LIBS += -llibboost_date_time-vc142-mt-gd-x64-1_70
        }
        CONFIG(release, debug|release):{
            LIBS += -L$$PWD/3rdparty/lib
            LIBS += -llibboost_system-vc142-mt-x64-1_70
            LIBS += -llibboost_thread-vc142-mt-x64-1_70
            LIBS += -llibboost_date_time-vc142-mt-x64-1_70
        }

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    Mte3DServiceNew.cpp \
    scene/OsgRender/Mte3DService.cpp \
    scene/OsgRender/osgQtCompositeViewer.cpp \
    scene/scenewindow.cpp \
    scene/SceneEngine/sceneengine.cpp \
    scene/SceneEngine/scenetreewidget.cpp \
    scene/DragwEditor/drageditor.cpp \
    scene/DragwEditor/NodePropertyWidget.cpp \
    scene/DragwEditor/addmodeldialog.cpp \
    scene/DragwEditor/imagepreviewwidget.cpp \
    scene/Common/loginfo.cpp \
    scene/Common/readwritefile.cpp \
    scene/Common/MyMessageBox.cpp \
    scene/Log/log_manager.cpp \
    scene/Ribbon/QRibbon.cpp \
    scene/SceneEditor/modeledit.cpp \
    scene/SceneEditor/backgroundedit.cpp \
    scene/SceneEditor/modeltrackedit.cpp \
    scene/SceneEditor/terrainedit.cpp \
    scene/OsgRender/OsgContext.cpp \
    scene/OsgRender/coordconvert.cpp \
    scene/OsgRender/MouseIntersectionHandler.cpp

HEADERS += \
    mainwindow.h \
    Mte3DServiceNew.h \
    scene/OsgRender/Mte3DService.h \
    scene/OsgRender/osgQtCompositeViewer.h \
    scene/scenewindow.h \
    scene/SceneEngine/SceneEngine.h \
    scene/SceneEngine/SceneTreeWidget.h \
    scene/DragwEditor/drageditor.h \
    scene/DragwEditor/NodePropertyWidget.h \
    scene/DragwEditor/addmodeldialog.h \
    scene/DragwEditor/imagepreviewwidget.h \
    scene/Common/loginfo.h \
    scene/Common/readwritefile.h \
    scene/Common/MyMessageBox.h \
    scene/Common/MteStructDef.h \
    scene/Log/log_manager.h \
    scene/Ribbon/QRibbon.h \
    scene/SceneEditor/modeledit.h \
    scene/SceneEditor/backgroundedit.h \
    scene/SceneEditor/modeltrackedit.h \
    scene/SceneEditor/terrainedit.h \
    scene/OsgRender/OsgContext.h \
    scene/OsgRender/coordconvert.h \
    scene/OsgRender/MouseIntersectionHandler.h

FORMS += \
    mainwindow.ui \
    scene/scenewindow.ui \
    scene/Common/loginfo.ui \
    scene/Ribbon/qribbon.ui \
    scene/SceneEditor/modeledit.ui \
    scene/SceneEditor/backgroundedit.ui \
    scene/SceneEditor/modeltrackedit.ui \
    scene/SceneEditor/terrainedit.ui
    # scene/DragwEditor/addmodeldialog.ui


