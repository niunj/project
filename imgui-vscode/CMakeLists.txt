cmake_minimum_required(VERSION 3.16)
project(ImGuiImPlotExample)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置vcpkg工具链文件
set(CMAKE_TOOLCHAIN_FILE D:/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake CACHE STRING "Vcpkg toolchain file")

# 查找所需的库
find_package(imgui CONFIG REQUIRED)
find_package(implot CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

# 添加可执行文件
add_executable(main main.cpp)

# 链接库
target_link_libraries(main PRIVATE
    imgui::imgui
    implot::implot
    glfw
    OpenGL::GL
)

# 添加包含目录
target_include_directories(main PRIVATE
    ${IMGUI_INCLUDE_DIRS}
    ${IMPLOT_INCLUDE_DIRS}
    ${GLFW_INCLUDE_DIRS}
)
