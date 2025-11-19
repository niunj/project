# Qt + vcpkg 示例项目

这是一个集成了vcpkg包管理工具的Qt项目示例，展示了如何在Qt应用中使用vcpkg管理的第三方库。

## 项目概述

- 支持Qt图形界面和控制台应用两种模式
- 集成了spdlog日志库作为第三方库示例
- 通过CMake构建系统配置
- 支持跨平台编译（Windows、Linux、macOS）

## 环境要求

- CMake 3.16 或更高版本
- C++17 兼容的编译器
- vcpkg（已安装在 D:\vcpkg）
- 可选：Qt6（如使用图形界面版本）

## 安装第三方库

项目默认使用spdlog作为示例第三方库，可以通过以下命令安装：

```bash
# 在Windows PowerShell中运行
D:\vcpkg\vcpkg.exe install spdlog

# 如果需要使用Qt版本，还需安装Qt
D:\vcpkg\vcpkg.exe install qtbase
```

## 编译步骤

### Windows (PowerShell)

```powershell
# 清理并创建构建目录
Remove-Item -Recurse -Force build
New-Item -ItemType Directory -Force build
cd build

# 配置项目（使用vcpkg工具链）
cmake .. -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake

# 构建项目
cmake --build . --config Release
```

### Windows (命令提示符)

```cmd
# 清理并创建构建目录
rmdir /s /q build
mkdir build
cd build

# 配置项目
cmake .. -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake

# 构建项目
cmake --build . --config Release
```

### Linux/macOS

```bash
# 清理并创建构建目录
rm -rf build
mkdir -p build
cd build

# 配置项目（调整vcpkg路径为实际安装位置）
cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

# 构建项目
cmake --build .
```

## 运行程序

编译完成后，可以在以下位置找到可执行文件：

- Windows: `build\Release\QtVcpkgExample.exe` 或 `build\QtVcpkgExample.exe`
- Linux/macOS: `build/QtVcpkgExample`

### 运行命令

Windows (PowerShell):
```powershell
# 从项目根目录运行
.uild\QtVcpkgExample.exe
# 或从构建目录运行
cd build
.\QtVcpkgExample.exe
```

Linux/macOS:
```bash
# 从项目根目录运行
./build/QtVcpkgExample
# 或从构建目录运行
cd build
./QtVcpkgExample
```

## 项目结构

```
QtVcpkgEmptyProject/
├── CMakeLists.txt        # CMake构建配置文件
├── main.cpp              # Qt应用程序入口
├── main_console.cpp      # 控制台应用入口（当Qt不可用时）
├── mainwindow.h          # 主窗口类头文件
├── mainwindow.cpp        # 主窗口类实现
├── mainwindow.ui         # Qt Designer界面文件
├── build/                # 构建输出目录
└── README.md             # 项目说明文档
```

## 使用说明

- 程序会根据环境自动检测Qt和spdlog库
- 如果检测到Qt，将启动图形界面版本
- 如果未检测到Qt，将启动控制台版本
- 在图形界面中，点击"测试第三方库"按钮可以测试spdlog功能
- 日志将保存到应用程序运行目录下的`app.log`文件中

## 故障排除

### 找不到Qt6

如果遇到找不到Qt6的警告，可以通过以下方式解决：
1. 确保已安装Qt6
2. 设置环境变量：`set Qt6_DIR=C:\path\to\Qt\6.x.x\msvc2019_64\lib\cmake\Qt6`
3. 或通过vcpkg安装：`D:\vcpkg\vcpkg.exe install qtbase`

### 找不到spdlog

如果遇到找不到spdlog的警告，请确保已通过vcpkg安装：
```
D:\vcpkg\vcpkg.exe install spdlog
```

## 扩展项目

要添加更多vcpkg管理的第三方库：

1. 使用vcpkg安装库：`D:\vcpkg\vcpkg.exe install 库名`
2. 在CMakeLists.txt中添加：
   ```cmake
   find_package(库名 CONFIG QUIET)
   if(库名_FOUND)
       target_link_libraries(QtVcpkgExample PRIVATE 库名::库名)
       target_compile_definitions(QtVcpkgExample PRIVATE HAVE_库名)
   endif()
   ```
3. 在代码中使用条件编译：
   ```cpp
   #ifdef HAVE_库名
   // 使用库的代码
   #endif
   ```