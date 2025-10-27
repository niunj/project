cmake_minimum_required(VERSION 3.20)
project(ConanTest VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 配置构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the build type (Debug or Release)" FORCE)
endif()

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})

# 查找Conan生成的包
find_package(OpenSceneGraph REQUIRED)

# 添加源文件
file(GLOB SOURCES "src/*.cpp")

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
    osgViewer::osgViewer
    osg::osg
    osg::osgDB
    osg::osgGA
    osg::osgUtil
    osg::osgText
    osg::osgFX
    osg::osgSim
    osg::osgManipulator
    osg::osgParticle
    osg::osgTerrain
    OpenThreads::OpenThreads
)

# 设置包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${OPENSCENEGRAPH_INCLUDE_DIRS}
)

# 设置调试信息
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG=1)
endif()

# 复制必要的DLL文件（Windows）
if(WIN32)
    # 找到OpenSceneGraph的DLL文件
    find_program(OSG_DLL_FINDER NAMES osgViewer.dll PATHS ${CONAN_OPENSCENEGRAPH_ROOT}/bin)
    if(OSG_DLL_FINDER)
        add_custom_command(
            TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CONAN_OPENSCENEGRAPH_ROOT}/bin
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        )
    endif()
endif()