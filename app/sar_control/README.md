# SAR 显控进程 (待实现)

本目录为 v5 多进程架构中的 **SAR 显控进程**占位（进程拓扑见
`docs/architecture_v5_multi_process.md` §1.1）。

**规划职责**：订阅仿真进程发布的 SAR 聚焦图像帧，渲染 SAR 图像显示
（复用 `gui/ScrollableImageView`）、分辨率标尺、目标解读标注。

**当前状态**：空目录占位，CMake 未引用，无实现。落地时：
1. 新增 `main.cpp`（Qt 应用入口）
2. 在 `app/CMakeLists.txt` 补 `add_executable(vsgraysim_sar_control ...)`
   + RPATH（见 `docs/build_dependencies.md` §3）
3. 数据链：`SarBackprojectProcessor`（`src/layer3b_signal`）→
   `product_dispatch` → `SimDataBroker` → 本进程订阅。

**相关已验证组件**（可直接复用，勿重写）：
- SAR 信号级处理：`src/layer3b_signal/SarBackprojectProcessor.cpp`
- 图像显示部件：`gui/ScrollableImageView.h/cpp`
- 进程框架：Launcher 已具备 QProcess 生命周期管理
