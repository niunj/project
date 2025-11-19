cmake_minimum_required(VERSION 3.16)

project(QtVcpkgExample VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 集成vcpkg
set(CMAKE_TOOLCHAIN_FILE "D:/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")

# 设置Qt6的查找路径（根据实际Qt安装位置调整）
# 这里使用vcpkg的Qt，如果系统中没有，需要先通过vcpkg安装
# 例如: vcpkg install qtbase

# 强制使用Qt GUI应用
# 查找Qt6或Qt5
find_package(Qt6 COMPONENTS Widgets OpenGLWidgets QUIET)
if(Qt6_FOUND)
    message(STATUS "找到Qt6")
    qt_standard_project_setup()
    set(QT_VERSION 6)
else()
    find_package(Qt5 COMPONENTS Widgets OpenGL QUIET)
    if(Qt5_FOUND)
        message(STATUS "找到Qt5")
        set(QT_VERSION 5)
    else()
        message(FATAL_ERROR "未找到Qt6或Qt5")
    endif()
endif()
set(USE_QT ON)

# 查找OpenSceneGraph包 (使用更简单的方式，兼容vcpkg)
find_package(OpenSceneGraph QUIET)
if(OpenSceneGraph_FOUND)
    message(STATUS "找到OpenSceneGraph")
    include_directories(${OPENSCENEGRAPH_INCLUDE_DIRS})
    message(STATUS "OSG库路径: ${OPENSCENEGRAPH_LIBRARIES}")
else()
    message(WARNING "未找到OpenSceneGraph，尝试使用vcpkg的单独组件")
    # 尝试单独查找组件
    find_package(osg QUIET)
    find_package(osgDB QUIET)
    find_package(osgUtil QUIET)
    find_package(osgGA QUIET)
    # osgQt可能不是必需的，特别是在使用Qt6时
endif()

# 添加源文件
if(USE_QT)
    set(SOURCES
        main.cpp
        mainwindow.cpp
    )
    
    set(HEADERS
        mainwindow.h
    )
else()
    # 创建一个简单的控制台应用源文件
    set(SOURCES main_console.cpp)
endif()

# 创建可执行文件
if(USE_QT)
    if(QT_VERSION EQUAL 6)
        qt_add_executable(QtVcpkgExample ${SOURCES} ${HEADERS})
    else()
        add_executable(QtVcpkgExample ${SOURCES} ${HEADERS})
        # 对于Qt5，需要手动处理MOC
        set_target_properties(QtVcpkgExample PROPERTIES
            AUTOMOC ON
            AUTOUIC ON
            AUTORCC ON
        )
    endif()
else()
    add_executable(QtVcpkgExample ${SOURCES})
endif()

# 查找第三方库 (spdlog作为示例)
find_package(spdlog CONFIG QUIET)
if(spdlog_FOUND)
    message(STATUS "找到spdlog")
    set(HAVE_SPDLOG ON)
else()
    message(WARNING "未找到spdlog")
    set(HAVE_SPDLOG OFF)
endif()

# 链接库
if(USE_QT)
    if(QT_VERSION EQUAL 6)
        target_link_libraries(QtVcpkgExample PRIVATE Qt6::Widgets Qt6::OpenGLWidgets)
    else()
        target_link_libraries(QtVcpkgExample PRIVATE Qt5::Widgets Qt5::OpenGL)
    endif()
endif()

if(HAVE_SPDLOG)
    target_link_libraries(QtVcpkgExample PRIVATE spdlog::spdlog)
endif()

# 链接OpenSceneGraph库
if(OpenSceneGraph_FOUND)
    target_link_libraries(QtVcpkgExample PRIVATE
        ${OPENSCENEGRAPH_LIBRARIES}
    )
endif()

# 传递配置给源代码
if(HAVE_SPDLOG)
    target_compile_definitions(QtVcpkgExample PRIVATE HAVE_SPDLOG)
endif()

# 设置输出目录
set_target_properties(QtVcpkgExample PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)