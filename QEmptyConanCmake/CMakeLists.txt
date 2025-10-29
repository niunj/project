cmake_minimum_required(VERSION 3.24)
project(VSG VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加conan依赖
find_package(spdlog REQUIRED)

# 创建可执行文件
add_executable(vsg_app src/main.cpp)

# 链接依赖库
target_link_libraries(vsg_app PRIVATE spdlog::spdlog)

# 设置输出目录
set_target_properties(vsg_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)