# vsgraysimulation 产品架构设计 v4.0

> 状态: 已批准的设计基线
> 基座决策: 以第三代 **TimingBus + Products 内核** 为唯一运行时基座，整合三代代码资产
> 前置文档: [architecture_overview.md](./architecture_overview.md) (v3.1, 第二代框架视角)

---

## 1. 概述

### 1.1 产品定位

vsgraysimulation 是一个 **多光谱传感器仿真 + 地形/场景编辑一体化工作台**：

- 支持电磁波（可见光 / 红外 / 雷达 / SAR）在复杂大气环境中的传播、散射与传感器互扰仿真
- 提供程序化地形生成、笔刷编辑、图层混合、场景部署、装备配置等编辑能力
- 支持 GUI 交互模式（Qt6 桌面应用）与 headless 批跑模式（CLI）

### 1.2 三代代码资产现状

| 代际 | 代表代码 | 现状 | v4 处置 |
|------|----------|------|---------|
| 第一代 | [Simulation.h](../include/vsgraysim/Simulation.h) + [main.cpp](../main.cpp) | 单体 7 步流水线，GUI 正在使用 | **拆解**：step() 逻辑拆分为子系统，类在 M5 删除 |
| 第二代 | `include/vsgraysim/system/` + `systems/` + [main_system.cpp](../main_system.cpp) | SystemBase 树 + 消息总线 + 分布式骨架 | **选择性吸收**：TickDecision 模型、StateMachine 进内核；MessageBus/Transport/Deployment 冻结 |
| 第三代 | `kernel/` + `products/` + `subsystems/` + `terrain/` + `project/` | TimingBus + Products + 双模式子系统接口，地形编辑已可用 | **升级为唯一基座** |
| 业务核 | `layer0_phys/` ~ `layer5_vis/` + `cuda/` + `shaders/` | 6 层仿真管线，三代共用 | **零改动保留**，仅改变持有者 |

### 1.3 设计原则

1. **单一内核**：全产品只有一个主循环 —— [TimingBus](../include/vsgraysim/kernel/TimingBus.h)；GUI、CLI、测试全部驱动它
2. **产品即契约**：子系统间不直接互相引用，通过 [FrameContext](../include/vsgraysim/kernel/FrameContext.h) 中的 Products 交换数据
3. **双模式统一**：编辑模式与仿真模式共用同一套子系统（`IDualModeSubsystem`），编辑产物 = 仿真输入
4. **业务核冻结**：Layer 0-5 代码不做功能改动，只改变持有者与调用入口
5. **配置驱动**：时间模型、子系统开关、频率、场景参数全部 JSON 可配

---

## 2. 产品功能架构

### 2.1 四大功能区

```
┌────────────────────────────────────────────────────────────────┐
│                        v4.0 产品功能                            │
├──────────────┬──────────────┬──────────────┬──────────────────┤
│   编辑 Edit   │  仿真 Sim     │  显示 Display │  工具 Tool       │
├──────────────┼──────────────┼──────────────┼──────────────────┤
│ 地形生成      │ 刚体平台运动   │ 3D 视口      │ headless 批跑    │
│ 地形笔刷      │ 大气环境演化   │ 雷达回波(RD图)│ 参数校验         │
│ 图层混合      │ 传感器发射调度 │ IR/VIS 成像   │ 单传感器 trace   │
│ 水域          │ Vulkan RT 散射│ SAR 成像     │ 项目导入/导出     │
│ 场景部署      │ CUDA 信号形成 │ 场景检视      │ 撤销/重做        │
│ 装备/传感器   │ 传感器互扰    │ 状态监控      │ 播放/暂停/单步    │
│ 项目管理      │               │              │                  │
└──────────────┴──────────────┴──────────────┴──────────────────┘
```

### 2.2 功能清单与资产映射

| 功能区 | 功能项 | 现状 | 承载资产 |
|--------|--------|------|----------|
| 编辑 | 地形生成 / 笔刷 / 图层混合 / 水域 | 已有 | `terrain/`（TerrainSystem + BrushEngine + LayerStack + TileStore） |
| 编辑 | 场景部署（模型导入、实体摆放、孔径配置） | 待建 | 空目录 `include/vsgraysim/scene/`，吸收 SceneBuilder 编辑侧 |
| 编辑 | 装备/传感器参数编辑（波段、发射参数、互扰规则） | 待建 | 空目录 `include/vsgraysim/equipment/`，吸收 PhysParamManager/CrossInductionRules 配置侧 |
| 编辑 | 项目保存/加载/撤销重做 | 半有 | `project/`（ProjectManager + ProjectSerializer + UndoRedoSystem + ICommand） |
| 仿真 | 刚体平台运动 | 已有 | PhysicsEngine (Bullet3 + vsgBullet)，封装在 Simulation::step 第 1b 步 |
| 仿真 | 环境演化（10Hz） | 已有 | 第二代 EnvironmentSystem (`src/systems/EnvironmentSystem.cpp`) |
| 仿真 | 发射调度 → RT 散射 → 信号形成 | 已有 | Simulation::step 第 2-5 步 + Layer 2/3/3b/4 |
| 仿真 | 播放/暂停/单步/倍速 | 部分 | TimingBus（setPaused/time_scale 已有），GUI 工具栏待接入 |
| 显示 | 3D 视口 | 已有 | `gui/VsgQtWidget` + VSG Viewer |
| 显示 | 雷达回波 / 传感器结果 / 场景检视 | 已有 | `gui/RadarDisplay` + `SensorResultPanel` + `SceneInspectorDock` |
| 工具 | headless / validate / trace-sensor | 已有 | `main.cpp` CLI 模式，M1 迁移到 cli_main |

---

## 3. App 技术架构（六层）

```
┌─────────────────────────────────────────────────────────────────────┐
│ L6 App 壳层  apps/                                                  │
│    Application 会话对象(持有 TimingBus + ProjectManager)             │
│    入口: gui_main (Qt6 桌面)  /  cli_main (headless 批跑与诊断)      │
├─────────────────────────────────────────────────────────────────────┤
│ L5 展示层                                                           │
│    display/  RadarScope 等纯渲染组件(框架无关)                       │
│    gui/      Qt6 Widgets 宿主 + VsgQtWidget(VSG 视口嵌入)            │
├─────────────────────────────────────────────────────────────────────┤
│ L4 项目层  project/                                                 │
│    ProjectManager / ProjectSerializer(vsgproject) / UndoRedoSystem  │
│    ICommand 命令模式 / ImportExportHub / AssetIndex                  │
├─────────────────────────────────────────────────────────────────────┤
│ L3 子系统层  subsystems/  (ISimulationSubsystem / IEditorSubsystem) │
│    Terrain Scene Physics Environment Sensor RT Signal Visual        │
├─────────────────────────────────────────────────────────────────────┤
│ L2 产品层  products/                                                │
│    TerrainProduct / SceneProduct / EquipmentStateProduct /          │
│    DisplayFrameProduct / ScatterProduct* / SensorFrameProduct*      │
│    (* = M2 新增)                                                    │
├─────────────────────────────────────────────────────────────────────┤
│ L1 内核层  kernel/                                                  │
│    TimingBus(主循环+多速率调度+事件总线) / FrameContext / TimeTick    │
│    TickDecision* (移植自第二代) / StateMachine*(装备状态用)           │
├─────────────────────────────────────────────────────────────────────┤
│ L0 业务核  layer0_phys/ layer1_scene/ layer2_rt/ layer3_cuda/       │
│    layer3b_signal/ layer4_sched/ layer5_vis/ core/ cuda/ shaders/   │
│    (零改动保留，仅被 L3 子系统持有)                                   │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.1 层间依赖规则

- 只允许上层依赖下层；同层组件不直接互相调用（经 Products / 事件交换）
- L0 业务核不感知 L1 以上任何概念（无 TimingBus/Products 头文件依赖）
- L5 展示层只读 Products，不写
- 跨层事件（如"传感器锁定目标"）统一经 TimingBus 事件总线，postTick 阶段分发

### 3.2 CMake 构建目标调整

| 目标 | v4 状态 |
|------|---------|
| `vsgraysim_common` | 保留 |
| `vsgraysim_cuda` | 保留 |
| `vsgraysim_core` | 保留（Layer 0-5 + kernel/products/project/terrain/subsystems/scene/equipment） |
| `vsgraysim_gui` | 保留，M3 起改为依赖 TimingBus |
| `vsgraysim_cli`（新） | M1 新增，cli_main 入口 |
| `vsgraysim_framework` / `vsgraysim_systems` | M5 起转为 `option(VSGRAYSIM_BUILD_LEGACY_FRAMEWORK OFF)` 可选编译 |
| `vsgraysim_app` / `vsgraysim_system` | M5 删除 |

---

## 4. 内核层设计（TimingBus 增强）

### 4.1 现状与缺口

[TimingBus](../include/vsgraysim/kernel/TimingBus.h) 已具备：固定/可变步长、time_scale、max_frames/max_sim_time、子系统优先级排序、三阶段 tick（onPreTick → onTick → onPostTick）、sub-steps、事件总线（postTick 分发）、FrameContext 管理。

**缺口：多速率调度**。当前所有子系统每帧都执行，无法表达"环境 10Hz / 物理 120Hz 子步 / 主链路 60Hz"的差异。

### 4.2 多速率调度设计（移植第二代 TickDecision 模型）

在 `kernel/` 新增 `TickDecision.h`，从 [TickStrategy.h](../include/vsgraysim/system/TickStrategy.h) 移植决策模型（命名空间改为 `vsgraysim`）：

```cpp
enum class TickAction { SKIP, EXECUTE, EXECUTE_SUBSTEPS, DEFER, ERROR_ABORT };

struct TickDecision {
    TickAction action = TickAction::EXECUTE;
    int    substep_count = 0;
    double substep_dt    = 0.0;
    std::string reason;
};
```

`ISimulationSubsystem` 增加一个带默认实现的方法，保持向后兼容：

```cpp
/// 返回本次帧的调度决策；默认每帧执行
virtual TickDecision decideTick(const TimeTick& tick) {
    return {TickAction::EXECUTE, 0, 0.0, "every_frame"};
}
```

TimingBus::doTick() 调度伪代码：

```
for reg in subsystems(按优先级排序):
    decision = reg.subsystem->decideTick(current_tick)
    switch decision.action:
        SKIP             → 跳过该子系统本帧三阶段
        EXECUTE          → onPreTick → onTick → onPostTick
        EXECUTE_SUBSTEPS → onPreTick → for i in substep_count: onSubStep(dt=substep_dt) → onPostTick
        DEFER            → 仅 onPreTick(累积)，事件照常在帧末分发
        ERROR_ABORT      → 记录日志，本帧中止并置 stop 标志
```

### 4.3 子系统频率规划

| 子系统 | 优先级 | decideTick 行为 | 等效频率 |
|--------|--------|-----------------|----------|
| TerrainSystem | 0 | EXECUTE（仅上传预算内 dirty chunk） | 60Hz 预算制 |
| SceneSystem | 10 | EXECUTE（同步位姿/动态节点） | 60Hz |
| PhysicsSystem | 20 | EXECUTE_SUBSTEPS（dt=1/120 累积器） | 120Hz 子步 |
| EnvironmentSystem | 30 | 每 6 帧 EXECUTE 一次（sim_time 阈值判定） | 10Hz |
| SensorSystem | 40 | EXECUTE | 60Hz |
| RTSystem | 50 | EXECUTE | 60Hz |
| SignalSystem | 60 | EXECUTE | 60Hz |
| VisualSystem | 70 | EXECUTE | 60Hz |

### 4.4 帧时序图

```
TimingBus::tick()  (每帧)
│
├─ 1. 更新 current_tick (frame_id++, sim_time += dt)
├─ 2. clearTransient()          ← 清理产品瞬时数据，保留事件队列
├─ 3. for each subsystem (priority 升序):
│      decision = decideTick(tick)
│      ┌─ onPreTick   读输入产品，准备
│      ├─ onTick      计算，写输出产品 (或 N 次 onSubStep)
│      └─ onPostTick  发布事件到 FrameContext.events，清理
├─ 4. dispatchEvents()          ← 帧末统一分发事件给订阅者
└─ 5. 检查停止条件 (max_frames / max_sim_time / stop 标志)
```

---

## 5. 产品层设计

### 5.1 产品清单

| 产品 | 可变性 | 写入方 | 读取方 | 现状 |
|------|--------|--------|--------|------|
| `TerrainProduct` | 不可变（编辑期可写） | TerrainSystem | SceneSystem / RTSystem（地形高度） | 已有 |
| `SceneProduct` | 不可变（编辑期可写） | SceneSystem | RT / Signal / Visual | 已有，M2 充实 |
| `EquipmentStateProduct` | 每帧可写 | SensorSystem / EnvironmentSystem | Signal / Visual / GUI | 已有 |
| `DisplayFrameProduct` | 每帧可写 | VisualSystem | GUI 显示组件 | 已有 |
| `ScatterProduct`（新） | 每帧替换 | RTSystem | SensorSystem / SignalSystem | M2 新增 |
| `SensorFrameProduct`（新） | 每帧替换 | SignalSystem | VisualSystem / GUI | M2 新增 |

### 5.2 新增产品定义（M2 落位）

`ScatterProduct`：承载 RTSystem 每帧输出，替代旧 `SimContext::scatter_points`：

```cpp
struct ScatterProduct {
    uint64_t frame_id = 0;
    std::vector<ScatterPoint> points;         // GPU/CPU RT 输出(已过滤噪声底)
    std::vector<RoutedBatch>  routed_batches; // DataDispatcher 路由结果
    void clearTransient() { points.clear(); routed_batches.clear(); }
};
```

`SensorFrameProduct`：承载 SignalSystem 每帧输出，替代旧 `SimContext::signal_frames` 与 `Simulation::lastFrames()`：

```cpp
struct SensorFrameProduct {
    uint64_t frame_id = 0;
    std::map<uint32_t, SignalFrame> frames;   // sensor_id -> 信号帧(RD图/IR/VIS/SAR)
    const SignalFrame* find(uint32_t sensor_id) const;
    void clearTransient() { frames.clear(); }
};
```

两者挂入 [FrameContext](../include/vsgraysim/kernel/FrameContext.h)（新增成员 + clearTransient 联动）。

### 5.3 高频数据通道原则（继承 v3.1）

- **高频大带宽**（scatter points / signal frames / 位姿）→ Products 指针直读，零拷贝
- **低频控制**（环境切换、传感器开关、模式变更）→ TimingBus 事件总线
- **临时跨子系统数据**（Vulkan 设备等一次性资源）→ FrameContext scratchpad（`std::any`，类型安全模板已具备）

---

## 6. 子系统层设计

### 6.1 八个子系统与来源资产

| 子系统 | 双模式 | 来源资产拆解 | 核心职责 |
|--------|--------|--------------|----------|
| `TerrainSystem` | 是 | `src/terrain/` 已实现 | 地形产品管理、笔刷、chunk GPU 上传 |
| `SceneSystem` | 是 | 新建；吸收 `SceneBuilder` 构建侧 + `Simulation::init` 场景装配 | 从 SceneProduct 构建/重建 vsg 场景图，维护动态节点与孔径注册 |
| `PhysicsSystem` | 否 | 拆自 `Simulation::step` 第 1b 步 + `syncMountedSensors()` | Bullet3 子步推进，平台位姿 → 孔径同步 |
| `EnvironmentSystem` | 否 | 迁移自 `src/systems/EnvironmentSystem.cpp`（第二代），接口改 ISubsystem | 太阳/温度/大气/风 10Hz 演化，写 EquipmentStateProduct.env |
| `SensorSystem` | 是 | 拆自 step 第 2 步（SensorScheduler）+ 装备编辑侧新建 | 发射调度表构建；编辑模式提供传感器参数 CRUD |
| `RTSystem` | 否 | 拆自 step 第 3 步（GPU RT + CPU fallback + AtmosphereIntegrator + CrossInductionDetector） | 光线追踪 → ScatterProduct |
| `SignalSystem` | 否 | 拆自 step 第 4-5 步（DataDispatcher + Layer3b 处理器 + CUDA SignalProcessor） | 散射点 → SignalFrame → SensorFrameProduct |
| `VisualSystem` | 否 | Layer5 `Visualizer` + `src/systems/VisualSystem.cpp` 的 Vulkan 设备创建 | 创建 Vulkan 设备（经 scratchpad 共享），渲染 3D 视口 + 生成 DisplayFrameProduct |

### 6.2 子系统注册与创建

沿用已有 [SubsystemRegistry](../include/vsgraysim/subsystems/ISubsystem.h) 单例工厂；`Application` 启动时按 JSON 配置（`config/app_subsystems.json`）创建并注册到 TimingBus，优先级与频率参数亦来自配置。

### 6.3 状态机（装备状态）

第二代 [StateMachine](../include/vsgraysim/system/StateMachine.h) 作为独立工具类保留于 `system/` 目录，M4 起供 SensorSystem 表达装备状态（STANDBY / SEARCHING / TRACKING），不参与主循环调度。

---

## 7. 帧间数据流

### 7.1 每帧数据流图

```
                     TerrainProduct ─────────────┐
                     SceneProduct ──────────┐    │
                                            ▼    ▼
 EnvironmentSystem ──写──► EquipmentStateProduct ──► SensorSystem
 (10Hz, decideTick 分频)        ▲                      │ 发射调度表
                                │ 装备状态              ▼
                          PhysicsSystem ──位姿──► RTSystem
                          (120Hz 子步)                 │ ScatterProduct
                                                       ▼
 VisualSystem ◄── DisplayFrameProduct ◄── SignalSystem
 (60Hz)                                        │ SensorFrameProduct
    │                                          ▼
    └────────────────► GUI 显示组件 ◄──────────┘
                       (RadarDisplay 等只读)
```

### 7.2 与旧 SimContext 的对照

| 旧 SimContext 成员 | v4 落点 |
|--------------------|---------|
| phys / atm / rules / physics / scene | Application 持有，注入相关子系统构造 |
| scene_root | SceneProduct.vsg_root |
| vk_device | FrameContext scratchpad (`"vulkan.device"`) |
| env_state | EquipmentStateProduct.env |
| current_schedule | EquipmentStateProduct.schedule（SensorSystem 写） |
| scatter_points / routed_batches | ScatterProduct |
| signal_frames | SensorFrameProduct |
| mtx / current_frame / current_sim_time | TimeTick（TimingBus 单线程驱动，无需锁） |

---

## 8. App 壳层设计（L6）

### 8.1 Application 会话对象

`include/vsgraysim/apps/Application.h`（M1 新建）：

```cpp
class Application {
public:
    bool init(const AppConfig& cfg);       // 加载项目 → 创建子系统 → 注入 TimingBus
    bool startSimulation();                // 进入仿真模式
    bool stopSimulation();
    TimingBus& bus();
    ProjectManager& project();
private:
    std::unique_ptr<TimingBus>        bus_;
    std::unique_ptr<ProjectManager>   project_;
    // Layer0 资产(PhysParamManager/AtmosphereModel/CrossInductionRules)
    // 由 Application 创建并注入子系统
};
```

### 8.2 两个入口

| 入口 | 文件 | 驱动方式 |
|------|------|----------|
| GUI 桌面 | `gui/main_gui.cpp` | QTimer → `app.bus().tick()` + VSG 渲染提交 |
| CLI 批跑 | `apps/cli_main.cpp` | `app.bus().run()` 或 `runForFrames(n)`；子命令 headless / validate / trace-sensor / list-sensors 平移自 main.cpp |

### 8.3 GUI 改造要点（M3）

- [MainWindow::advanceOneFrame()](../gui/MainWindow.cpp)：`sim_.step(dt)` → `app_->bus().tick()`
- [MainWindow::pushRadarFrame()](../gui/MainWindow.cpp)：`sim_.lastFrames()` → `SensorFrameProduct::frames`
- 移除 `Simulation sim_` 成员与 `initSimulation()` 中的 headless Simulation 初始化
- 播放/暂停/单步按钮接入 `bus().setPaused()` / 单帧 `tick()` / `setTimeScale()`

---

## 9. 项目文件格式（vsgproject）

升级 [ProjectSerializer](../src/project/ProjectSerializer.cpp) 的容器格式（`vsgterrain` → `vsgproject`，旧格式保持可读兼容）：

```
my_project.vsgproject/            (目录或 zip 容器)
├── manifest.json                 # container_type / version / 子产品清单
├── terrain/product.json          # TerrainProduct 序列化(现有)
├── scene/product.json            # SceneProduct: 实体列表(模型引用+位姿+挂载)
├── equipment/product.json        # 传感器/波段/发射参数/互扰规则
└── scenarios/*.json              # 仿真场景(天气、时间、目标脚本)
```

`scene/product.json` 示意：

```json
{
  "entities": [
    { "id": "tank_01", "asset": "models/M1A2/M1A2.obj",
      "pose": { "pos": [10, 0, 20], "yaw_deg": 45 },
      "physics": { "dynamic": true, "mass_kg": 62000 },
      "mounts": [ { "sensor_id": 3, "local_offset": [0, 2.4, 0] } ] }
  ]
}
```

`equipment/product.json` 复用现有 `config/sensor_rich_set.json` 的 schema，使配置文件与项目内配置同构，便于互相导入（ImportExportHub）。

---

## 10. 实施路线图

### M1 内核统一（1-2 周）

- [ ] `kernel/TickDecision.h`：移植决策模型；`ISimulationSubsystem::decideTick()` 默认实现
- [ ] `TimingBus::doTick()` 接入决策过滤 + EXECUTE_SUBSTEPS 路径
- [ ] `apps/Application.h/.cpp`：会话对象，持有 Layer0 资产与 TimingBus
- [ ] 拆解 `Simulation::step()` 为 PhysicsSystem / SensorSystem / RTSystem / SignalSystem 四个 ISimulationSubsystem（业务核代码原样搬入，仅改数据来源）
- [ ] `apps/cli_main.cpp`：headless 模式跑通
- [ ] 验收：新增 `test/test_timingbus_pipeline.cpp`，同一场景配置下新旧管线逐帧 scatter 数量 / 信号帧统计一致

### M2 产品化（1 周）

- [ ] 新增 `ScatterProduct` / `SensorFrameProduct` 并挂入 FrameContext
- [ ] 充实 `SceneProduct`（实体表 + vsg_root + 孔径列表）
- [ ] EnvironmentSystem 迁移（第二代实现 → ISubsystem 接口，10Hz 分频）
- [ ] VisualSystem 迁移（Vulkan 设备创建 → scratchpad 共享）
- [ ] 验收：SimContext 全部读写点清零，`src/systems/` 不再被新链路引用

### M3 GUI 切换（1-2 周）

- [ ] MainWindow 改为 Application + TimingBus 驱动
- [ ] RadarDisplay / SensorResultPanel 数据源切换到 SensorFrameProduct
- [ ] 播放/暂停/单步/倍速工具栏接入 TimingBus
- [ ] 验收：GUI 功能回归（地形编辑 + 仿真播放 + 雷达显示）与切换前一致；`gui/` 不再包含 `Simulation.h`

### M4 编辑闭环（2-3 周）

- [ ] `scene/` SceneSystem + 场景部署编辑器（模型导入、实体摆放、孔径挂载）
- [ ] `equipment/` 装备/传感器参数编辑器（复用 sensor_rich_set schema）
- [ ] ProjectSerializer 升级 vsgproject（四段式容器，兼容 vsgterrain 读取）
- [ ] UndoRedo 覆盖场景/装备编辑命令（ICommand）
- [ ] 验收：新建项目 → 编辑地形/场景/装备 → 保存 → 重新加载 → 直接播放仿真，全链路无手工配置文件

### M5 清理与可选扩展（1 周）

- [ ] 删除 `Simulation.h/.cpp`、`main.cpp`（vsgraysim_app）、`main_system.cpp`（vsgraysim_system）
- [ ] `vsgraysim_framework` / `vsgraysim_systems` 转 `option(... OFF)` 可选编译（分布式资产冻结保留）
- [ ] architecture_overview.md 升级为 v4（本文档成为主架构文档）
- [ ] 验收：默认构建不含旧入口；`grep -r "SimContext\|Simulation::" src/ gui/ apps/` 无业务引用

---

## 11. 风险与缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| 拆解 step() 引入数值偏差 | 仿真结果不可信 | M1 验收测试逐帧对比；业务核代码零改动搬运 |
| GUI 与 TimingBus 线程模型冲突 | 渲染卡顿/竞态 | 保持单线程驱动（QTimer 线程内 tick + 提交），与现状一致 |
| Vulkan 设备跨子系统共享生命周期 | RTSystem 先于 VisualSystem 初始化时崩溃 | Application 控制子系统 init 顺序；RTSystem GPU 路径在拿到设备前自动降级 CPU |
| vsgproject 格式演进 | 旧项目不可读 | manifest 带 version；ProjectSerializer 保留 vsgterrain 读取分支 |
| 第二代分布式能力需求回归 | 架构返工 | Transport 抽象冻结保留在可选库中，未来可作为事件总线的远程投递插件挂回 |

## 12. 约束备忘

- 一期不做分布式/多进程（DDS/SHM/Deployment 资产冻结，不进主干）
- Layer 0-5 代码零改动，仅改变持有者与调用入口
- CUDA 库与 RT 着色器构建流程不变
- 主循环单线程驱动，多速率靠决策表而非多线程
