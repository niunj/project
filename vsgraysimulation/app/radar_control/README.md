# 雷达显控进程 (待实现)

本目录为 v5 多进程架构中的**雷达显控进程**占位（进程拓扑见
`docs/architecture_v5_multi_process.md` §1.1）。

**规划职责**：订阅仿真进程发布的雷达产品帧（RD 图 / 脉压回波 / 距离-
多普勒），渲染 PPI 显示（`gui/PpiDisplay.cpp` 已有 PPI 显示部件可复用）、
CPI 边界事件、目标批号管理。

**当前状态**：空目录占位，CMake 未引用，无实现。落地时：
1. 新增 `main.cpp`（Qt 应用入口）
2. 在 `app/CMakeLists.txt` 补 `add_executable(vsgraysim_radar_control ...)`
   + RPATH（见 `docs/build_dependencies.md` §3）
3. 数据链：`RadarChain`（`env_modules`）→ `product_dispatch` →
   `SimDataBroker` → 本进程订阅。

**相关已验证组件**（可直接复用，勿重写）：
- 雷达回波链：`env_modules` RadarChain（纯 compute，含 coalesced 读重排）
- PPI 显示：`gui/PpiDisplay.h/cpp`（现有 PpiDisplay 类）
- 雷达段处理：`src/layer4_sched` RadarStages + `src/layer5_vis/RadarScope`
- 进程框架：Launcher 已具备 QProcess 生命周期管理
