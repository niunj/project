
win32 {
#3rdparty
        MTE_3RDPARTY_PATH = $$PWD/3rdparty_win


}

unix {
    # 第三方根路径
    MTE_3RDPARTY_PATH = $$PWD/3rdparty_linux

    # ================================
    # Include Path (头文件)
    # ================================
    INCLUDEPATH += \
        $$MTE_3RDPARTY_PATH/osg365/include \
        $$MTE_3RDPARTY_PATH/oe32/include \
        $$MTE_3RDPARTY_PATH/3rdparty/include \
        $$MTE_3RDPARTY_PATH/3rdparty/curl \
        $$MTE_3RDPARTY_PATH/opencv411/include/opencv4 \
        $$MTE_3RDPARTY_PATH/vtk930/include/vtk-9.3 \
        $$MTE_3RDPARTY_PATH/SilverLining/include \
        $$MTE_3RDPARTY_PATH/Spark/include \
        $$MTE_3RDPARTY_PATH/quazip/include

    # ================================
    # OpenSceneGraph 库
    # ================================
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

    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgSimd
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgSim

    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgShadowd
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgShadow

    # ================================
    # osgEarth
    # ================================
    CONFIG(debug, debug|release):   LIBS += -L$$MTE_3RDPARTY_PATH/oe32/lib64 -losgEarth
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/oe32/lib64 -losgEarth

    # ================================
    # Google Protobuf
    # ================================
    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/3rdparty/lib -lprotobuf
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/3rdparty/lib -lprotobuf

    # ================================
    # GDAL
    # ================================
    CONFIG(debug, debug|release):   LIBS += -L$$MTE_3RDPARTY_PATH/3rdparty/lib -lgdald
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/3rdparty/lib -lgdal

    # ================================
    # JPEG / PNG
    # ================================
    CONFIG(debug, debug|release):   LIBS += -L$$MTE_3RDPARTY_PATH/3rdparty/lib -ljpeg -lpng
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/3rdparty/lib -ljpeg -lpng

    # ================================
    # cURL
    # ================================
    CONFIG(debug, debug|release):   LIBS += -L$$MTE_3RDPARTY_PATH/3rdparty/lib -lcurl
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/3rdparty/lib -lcurl

    # ================================
    # OpenCV 4.1.1
    # ================================
    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/opencv411/lib -lopencv_core
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/opencv411/lib -lopencv_core \
    -lopencv_imgproc \
    -lopencv_highgui \ # 如果你的OpenCV版本不同，请替换为你的版本号和后缀（如.so.4.5.5）
    -lopencv_imgcodecs \ # 如果需要图像编码解码功能
    -lopencv_videoio \ # 如果需要视频输入输出功能
    -lopencv_features2d \ # 根据需要添加其他模块
    -lopencv_dnn

    # ================================
    # VTK 9.3.0
    # ================================
    CONFIG(release, debug|release): {
        LIBS += -L$$MTE_3RDPARTY_PATH/vtk930/lib \
            -lvtkCommonCore-9.3 \
            -lvtkCommonColor-9.3 \
            -lvtkCommonDataModel-9.3 \
            -lvtkCommonExecutionModel-9.3 \
            -lvtkFiltersCore-9.3 \
            -lvtkFiltersGeneral-9.3 \
            -lvtkFiltersFlowPaths-9.3 \
            -lvtkFiltersSources-9.3 \
            -lvtkIOImage-9.3 \
            -lvtkIOXML-9.3 \
            -lvtkImagingStatistics-9.3 \
            -lvtkInteractionStyle-9.3 \
            -lvtkInteractionWidgets-9.3 \
            -lvtkRenderingCore-9.3 \
            -lvtkRenderingOpenGL2-9.3 \
            -lvtkRenderingAnnotation-9.3 \
            -lvtkRenderingQt-9.3 \
            -lvtkGeovisCore-9.3 \
            -lvtkGUISupportQt-9.3 \
            -lvtkzlib-9.3 \
            -lvtksys-9.3
    }

    # ================================
    # SilverLining
    # ================================
    CONFIG(release, debug|release): {
        LIBS += -L$$MTE_3RDPARTY_PATH/SilverLining/lib \
            -lSilverLiningOpenGL \
            -lSilverLiningOpenGL32Core \
            -lSilverLining-norenderer
    }

    # ================================
    # SPARK
    # ================================
    CONFIG(release, debug|release): {
        LIBS += -L$$MTE_3RDPARTY_PATH/Spark/lib \
            -lSPARK \
            -lSPARK_GL
    }

    # ================================
    # Quazip
    # ================================
    CONFIG(release, debug|release): {
        LIBS += -L$$MTE_3RDPARTY_PATH/quazip/lib  -lquazip1-qt5
    }

    # ================================
    # 系统库
    # ================================
    LIBS += -lGL -lGLU -lglut -lpthread -ldl
}



