# 红外显控进程 (待实现)

本目录为 v5 多进程架构中的**红外显控进程**占位（进程拓扑见
`docs/architecture_v5_multi_process.md` §1.1）。

**规划职责**：订阅仿真进程发布的 IR/MWIR/LWIR 传感器产品帧（经
`SimDataBroker` + `lib/ipc` DataChannel），渲染红外探测画面 + 波段切换
（复用 `env_viewer` 已验证的 D 方案切换）、跟踪波门标注、目标列表。

**当前状态**：空目录占位，CMake 未引用，无实现。落地时：
1. 新增 `main.cpp`（Qt 应用入口，参照 `app/sim_control/main.cpp` 模板）
2. 在 `app/CMakeLists.txt` 补 `add_executable(vsgraysim_ir_control ...)`
   + RPATH（见 `docs/build_dependencies.md` §3）
3. 订阅/发布走 `vsgraysim::SimDataBroker`（`lib/broker`）与
   `lib/ipc` 传输层。

**相关已验证组件**（可直接复用，勿重写）：
- 多波段传感器链：`env_modules`（rocky_env）
- TV/MWIR 波段切换：`env_modules/tools/env_viewer.cpp`（D 方案，单 Pipeline 重建）
- 数据发布：`(lib/)product_dispatch`（ProductDispatcher + SimDataBroker）
- 进程框架：Launcher 已具备 QProcess 生命周期管理（`gui/LauncherWindow.cpp`）
