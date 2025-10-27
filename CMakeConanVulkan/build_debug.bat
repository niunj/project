@echo off

rem 设置中文编码
chcp 65001 > nul

rem 创建build_debug目录
mkdir build_debug 2> nul
cd build_debug

rem 使用Conan安装依赖
conan install .. --output-folder=. --build=missing --settings=build_type=Debug

rem 配置CMake
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake

rem 构建项目
cmake --build . --config Debug

rem 提示信息
echo Debug构建完成！
echo 可执行文件位于: %cd%\Debug\ConanTest.exe
cd ..