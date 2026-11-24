# 统一底层库包

`linux-x86_64-ubuntu22.04.tar.gz` 是 Ubuntu 22.04 x86-64 基础库包，
包括 GDAL 3.14.0dev-30a51a8e98、PROJ 9.3.1、GEOS 3.12.0、SQLite、
OpenEXR/IlmBase 2.5.7、Zlib、libzip、图像解码相关库、oneTBB 2022.3.0，
以及构建产品所用的 xcb、X11 和 GLVND 开发文件及客户端库。
归档保存配套头文件、库、CMake 包、许可证清单与 GDAL/PROJ 数据。
归档的 SHA-256 与解包标记由 CMake 校验。生成的 SDK 目录为
`<构建目录>/third_party/base/`。

目标系统提供可用的 Qt、CUDA、Vulkan、Linux C/C++ 运行库、桌面显示环境和显卡驱动。
`notices/` 保存 PROJ、GEOS 许可和随包 GEOS 版本的上游源码归档。
从仓库编译的 VSG、Rocky、Bullet、Assimp 和 OpenUSD 放在 `third_party/`。
台北影像与高程仍按地球数据配置提供。

在维护机器上刷新包时，传入上述版本的地理 SDK 和 oneTBB 的 Linux 发布包目录，
运行 `tools/dependencies/pack_base.sh <地理 SDK> <oneTBB>`。
完成后一起检查归档、SHA-256、依赖审计和产品测试。
