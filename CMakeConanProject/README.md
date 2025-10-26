# OpenSceneGraph项目模板

这个项目模板包含了使用CMake、Conan和Visual Studio 2022构建OpenSceneGraph应用程序所需的基础配置。您可以直接拷贝这个文件夹到其他目录，快速搭建一个新的OpenSceneGraph项目。

## 目录结构

```
project_template/
├── CMakeLists.txt      # CMake构建配置
├── conanfile.txt       # Conan依赖管理
├── build_debug.bat     # Debug模式构建脚本
├── build_release.bat   # Release模式构建脚本
├── README.md           # 使用说明
└── src/                # 源代码目录
    └── main.cpp        # 示例代码
```

## 前提条件

1. 安装Visual Studio 2022
2. 安装CMake (3.20或更高版本)
3. 安装Conan包管理器

## 使用方法

### 1. 拷贝项目模板

将整个`project_template`文件夹拷贝到您希望创建新项目的目录中。

### 2. 自定义项目

根据需要修改以下文件：

- 在`CMakeLists.txt`中修改项目名称
- 在`conanfile.txt`中调整依赖版本或添加其他依赖
- 修改`src/main.cpp`实现您自己的功能

### 3. 构建项目

#### Debug模式构建

双击运行`build_debug.bat`脚本，该脚本会：
- 创建`build_debug`目录
- 使用Conan安装依赖
- 配置CMake生成Visual Studio项目
- 构建Debug版本的应用程序

#### Release模式构建

双击运行`build_release.bat`脚本，该脚本会：
- 创建`build_release`目录
- 使用Conan安装依赖
- 配置CMake生成Visual Studio项目
- 构建Release版本的应用程序

## 注意事项

1. 确保您的系统已正确安装并配置了Visual Studio 2022、CMake和Conan
2. 首次构建时，Conan会下载并构建所有必要的依赖项，这可能需要一些时间
3. 构建完成后，可执行文件将位于`build_debug/Debug/`或`build_release/Release/`目录下
4. 如果需要添加其他依赖，可以在`conanfile.txt`的`[requires]`部分添加，并在`CMakeLists.txt`中添加相应的链接配置

## 扩展项目

要添加其他源文件，请将它们放在`src`目录中，CMake会自动查找并包含这些文件。

要添加其他OpenSceneGraph模块或第三方库，请在`conanfile.txt`中添加相应的依赖，并在`CMakeLists.txt`中配置链接。