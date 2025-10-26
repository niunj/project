# ConanTest 项目

这是一个使用 Conan 包管理器和 CMake 构建系统的简单 C++ 项目示例，使用 GLFW 和 OpenGL 创建基本图形应用。

## 项目结构

```
ConanTest/
├── CMakeLists.txt      # CMake 构建配置
├── conanfile.txt       # Conan 依赖配置
├── src/                # 源代码目录
│   └── main.cpp        # 主程序文件
└── README.md           # 项目说明文档
```

## 依赖项

- glfw/3.4 - 跨平台窗口和 OpenGL 上下文创建库
- opengl/system - OpenGL 图形 API 支持

## 构建步骤

1. **安装依赖**

   ```bash
   conan install . -s build_type=Debug --build=missing
   ```

2. **创建构建目录并配置项目**

   ```bash
   mkdir build
   cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
   ```

3. **构建项目**

   ```bash
   cmake --build .
   ```

4. **运行程序**

   ```bash
   ./Debug/ConanTest.exe
   ```

## 说明

- 项目使用 Conan 的 CMakeToolchain 和 CMakeDeps 生成器进行集成
- 包含了一个基本的 OpenSceneGraph 示例，显示一个红色立方体
- 支持使用鼠标进行视角操作（通过 TrackballManipulator）
- 窗口大小为 800x600，位置在屏幕左上角 (100, 100)
- 支持 Debug 构建模式