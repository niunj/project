@echo off

:: 设置中文编码
chcp 65001 > nul

:: 设置构建目录
set BUILD_DIR=build_debug

:: 清除旧的构建目录，确保使用新的生成器配置
if exist %BUILD_DIR% rmdir /s /q %BUILD_DIR%

:: 创建构建目录
mkdir %BUILD_DIR% 2> nul

:: 进入构建目录
cd %BUILD_DIR%

:: 清理之前可能残留的构建文件
if exist CMakeCache.txt del CMakeCache.txt
if exist CMakeFiles rmdir /s /q CMakeFiles
if exist conan_toolchain.cmake del conan_toolchain.cmake

:: 设置环境变量，确保不使用Ninja
set CMAKE_GENERATOR=Visual Studio 17 2022
set CMAKE_GENERATOR_PLATFORM=x64

:: 使用Conan安装依赖
echo 正在安装依赖...
conan install .. --output-folder=. --build=missing --settings=build_type=Debug --settings compiler.runtime=MDd

:: 配置CMake，明确指定Visual Studio生成器和工具链文件
echo 正在配置CMake...
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug -Dvsg_DIR=%cd%\..

:: 构建项目
echo 正在构建项目...
cmake --build . --config Debug

:: 提示信息
echo Debug构建完成！
echo 可执行文件位置：%cd%\Debug\VSGExample.exe

:: 返回到上级目录
cd ..