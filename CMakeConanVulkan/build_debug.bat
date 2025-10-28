@echo off

rem 设置中文编码
chcp 65001 > nul

rem 清除旧的构建目录，确保使用新的生成器配置
if exist build_debug rmdir /s /q build_debug

rem 创建新的build_debug目录
mkdir build_debug 2> nul
cd build_debug

rem 设置环境变量，确保不使用Ninja
set CMAKE_GENERATOR=Visual Studio 17 2022
set CMAKE_GENERATOR_PLATFORM=x64

rem 使用Conan安装依赖
conan install .. --output-folder=. --build=missing --settings=build_type=Debug

rem 配置CMake，明确指定Visual Studio生成器和工具链文件
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_PREFIX_PATH=./ -Dvsg_DIR=./ -DVulkanLoader_DIR=./

rem 构建项目
cmake --build . --config Debug

rem 提示信息
echo Debug构建完成！
echo 可执行文件位于: %cd%\Debug\VSGExample.exe
cd ..