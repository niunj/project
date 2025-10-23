cmake_minimum_required(VERSION 3.14)
project(QtEmptyApplication VERSION 0.1 LANGUAGES CXX)

# 指定使用MSVC 2022编译器
if(WIN32)
    set(CMAKE_GENERATOR "Visual Studio 17 2022" CACHE INTERNAL "")
    set(CMAKE_VS_PLATFORM_TOOLSET "v143" CACHE STRING "Platform Toolset")
endif()

# 指定Qt版本
set(QT_VERSION_MAJOR 5 CACHE STRING "Qt major version")
set(QT_VERSION_MINOR 14 CACHE STRING "Qt minor version")
set(QT_VERSION_PATCH 2 CACHE STRING "Qt patch version")
set(QT_VERSION "${QT_VERSION_MAJOR}.${QT_VERSION_MINOR}.${QT_VERSION_PATCH}" CACHE STRING "Qt version")

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置Qt路径提示（如果需要）
set(Qt${QT_VERSION_MAJOR}_DIR "D:/Qt/Qt${QT_VERSION}/${QT_VERSION}/msvc2017_64/lib/cmake/Qt${QT_VERSION_MAJOR}" CACHE PATH "Qt${QT_VERSION_MAJOR} cmake directory")

find_package(QT NAMES Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets)

set(PROJECT_SOURCES 
    # Place your source files here
    mainwindow.h
    mainwindow.cpp
    mainwindow.ui
    main.cpp
    qpropertyobject.h
    qpropertyobject.cpp
    qpropertyobject_struct.h
)

add_executable(QtEmptyApplication ${PROJECT_SOURCES})
target_link_libraries(QtEmptyApplication Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets)
