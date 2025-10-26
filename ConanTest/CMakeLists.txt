cmake_minimum_required(VERSION 3.15)
project(ConanTest LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含Conan生成的构建信息
if(EXISTS ${CMAKE_BINARY_DIR}/conan_toolchain.cmake)
    include(${CMAKE_BINARY_DIR}/conan_toolchain.cmake)
endif()

# 寻找Conan提供的依赖
find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)

# 创建可执行文件
add_executable(${PROJECT_NAME} "src/main.cpp")

# 链接依赖
target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    OpenGL::GL
)