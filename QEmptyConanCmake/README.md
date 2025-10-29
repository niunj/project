# VSG C++ 项目

这是一个使用 Conan 和 CMake 构建的基础 C++ 项目示例。

## 生成和构建项目的命令

### 1. 使用 Conan 配置依赖

```bash
# 方法1：使用conanfile.txt（推荐）
conan install . --output-folder=build --build=missing

# 方法2：使用conanfile.py
conan install . -if=build -b missing
```

### 2. 使用 CMake 生成项目

```bash
# 在Windows上使用MinGW
cmake -B build -G "MinGW Makefiles" -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release

# 或者在Windows上使用Visual Studio
cmake -B build -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake

# 在Linux/Mac上
cmake -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
```

### 3. 构建项目

```bash
cmake --build build --config Release
```

### 4. 运行程序

```bash
# Windows
build\bin\vsg_app.exe

# Linux/Mac
./build/bin/vsg_app
```

## 项目结构说明

- `conanfile.txt` - Conan 包管理配置文件
- `CMakeLists.txt` - CMake 构建系统配置文件
- `src/main.cpp` - 主程序源代码
- `conanfile.py` - 高级 Conan 配置（可选）

## 使用 Conan 自动生成项目的其他方法

### 创建新项目模板

```bash
# 使用conan new命令创建基础项目
conan new vsg/0.1.0 -d "requires=spdlog/1.14.1"
```

### 使用 Conan 2.0 特性

```bash
# 使用现代布局生成项目
conan install . -s compiler.cppstd=17 -if=build
```

## 注意事项

1. 确保已安装 Conan 和 CMake
2. 根据您的编译器环境选择合适的生成器
3. 首次构建时需要下载依赖，可能需要一些时间