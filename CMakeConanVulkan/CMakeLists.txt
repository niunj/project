cmake_minimum_required(VERSION 3.20)
project(VSGExample VERSION 1.0.0 LANGUAGES CXX)

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

# 查找VSG库，使用conan生成的配置文件
set(CMAKE_PREFIX_PATH ${CMAKE_SOURCE_DIR}/build_deps ${CMAKE_PREFIX_PATH})
set(vsg_DIR ${CMAKE_SOURCE_DIR}/build_deps CACHE PATH "Path to vsg-config.cmake")
set(VulkanLoader_DIR ${CMAKE_SOURCE_DIR}/build_deps CACHE PATH "Path to VulkanLoaderConfig.cmake")

find_package(vsg REQUIRED)

# Vulkan头文件路径会通过vsg的依赖自动引入

# 添加源文件
file(GLOB SOURCES "src/*.cpp")

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
    vsg::vsg
)

# 设置包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${vsg_INCLUDE_DIRS}
)

# 设置调试信息
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG=1)
endif()

# 复制必要的DLL文件（Windows）
if(WIN32)
    # 找到VSG的DLL文件
    find_program(VSG_DLL_FINDER NAMES vsg-14.dll vsg.dll PATHS ${CMAKE_BINARY_DIR}/Debug)
    if(VSG_DLL_FINDER)
        # 复制VSG DLL到输出目录
        add_custom_command(
            TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${VSG_DLL_FINDER}
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        )
    else()
        message(WARNING "VSG DLL文件未找到，请确保正确安装了VSG库")
    endif()
endif()