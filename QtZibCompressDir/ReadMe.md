# QtEmptyApplication 项目说明

## 环境要求

- Visual Studio 2022
- Qt 5.14.2 (msvc2017_64)
- CMake 3.14 或更高版本
- Windows 操作系统

## 构建命令

在 PowerShell 中执行以下命令进行构建：

```powershell
# 清理构建目录（如果存在）
Remove-Item -Path build -Recurse -Force -ErrorAction SilentlyContinue

# 创建并进入构建目录
mkdir -p build
cd build

# 使用 CMake 配置项目
cmake -G "Visual Studio 17 2022" ..

# 构建项目
cmake --build . --config Debug
```

也可以使用一步式构建命令：

```powershell
Remove-Item -Path build -Recurse -Force -ErrorAction SilentlyContinue ; mkdir -p build ; cd build ; cmake -G "Visual Studio 17 2022" .. ; cmake --build . --config Debug
```

## 运行命令

构建成功后，可执行文件将位于 `build\Debug` 目录下。使用以下命令运行：

```powershell
# 从项目根目录运行
Start-Process -FilePath "build\Debug\QtEmptyApplication.exe"

# 或者从build目录运行
cd build\Debug
.\QtEmptyApplication.exe
```

## 目录打开器说明

项目还包含一个单独的目录打开器程序（DirectoryOpener.cpp），用于打开指定目录。要构建此程序，请使用：

```powershell
cd build
cmake -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Debug -S .. -B . -DCMAKE_TOOLCHAIN_FILE=../DirectoryOpenerCMakeLists.txt
cmake --build . --config Debug
```

## EOIRSimPlatform 项目配置

项目中包含 `cmakelist-eoir.txt` 文件，这是为 EOIRSimPlatform 项目生成的 CMake 配置文件，包含了所有必要的源文件、头文件、UI文件和第三方库配置。