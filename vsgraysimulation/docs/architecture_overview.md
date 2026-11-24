# vsgraysimulation 系统架构文档 (v3.1)

## 1. 项目概述

本项目是一个**多光谱仿真系统**，支持电磁波（可见光、红外、雷达、SAR）在复杂大气环境中的传播、散射、交互感应仿真。系统采用**真实世界自循环隐喻**设计：每个子系统是一个"物体"，有自己的心跳（tick）、频率策略、状态机，通过消息总线进行频道级发布/订阅通信。

---

## 2. 构建系统与依赖

### 2.1 构建目标

| 目标 | 类型 | 说明 |
|------|------|------|
| `vsgraysim_common` | INTERFACE | 全局公共头文件 + 公共依赖 |
| `vsgraysim_cuda` | SHARED | CUDA 信号处理核函数库 (Layer 3) |
| `vsgraysim_core` | STATIC | 核心仿真库 (Layer 0-5 + Simulation) |
| `vsgraysim_framework` | STATIC | 系统框架库 (SystemBase + 5 类可配置组件) |
| `vsgraysim_systems` | STATIC | 子系统包装库 (8 个子系统) |
| `vsgraysim_app` | EXEC | 旧版入口 (main.cpp) |
| `vsgraysim_system` | EXEC | 新版框架入口 (main_system.cpp) |

### 2.2 外部依赖

```
Vulkan SDK 1.3.296    — GPU 渲染 + 光线追踪
CUDA 12.4             — GPU 信号处理
VSG (VulkanSceneGraph) — 场景图 + 可视化
vsgXchange            — 资源加载
vsgImGui              — 图形界面 (ImGui + ImPlot)
Bullet3 + vsgBullet   — 物理引擎 (刚体力学)
nlohmann/json         — JSON 配置解析
glm                   — 数学库
spdlog                — 日志框架
```

---

## 3. 分层架构

系统采用 **6 层纵向分层** 架构，从底层物理参数到顶层可视化：

```
Layer 5: 可视化 (Visualizer)          ← 场景渲染 + 传感器图像显示
Layer 4: 调度分发 (Scheduler/Dispatch) ← 传感器发射调度 + 数据路由
Layer 3b: 信号形成 (Signal Processing)  ← 雷达脉冲/IR热辐射/可见光/SAR处理
Layer 3: CUDA 信号处理 (kernels)       ← GPU 加速信号生成
Layer 2: 光线追踪 (Ray Tracing)        ← CPU RT + Vulkan RT 管线
Layer 1: 场景构建 (Scene Builder)      ← 多光谱几何 + 动态节点
Layer 0: 物理参数 (Phys/Atmos/Rules)    ← 大气模型 + 交互感应规则 + 物理引擎
```

### 3.1 Layer 0 — 物理参数层

| 组件 | 文件 | 职责 |
|------|------|------|
| `PhysParamManager` | `layer0_phys/PhysParamManager.h` | 物理参数管理器，加载材质/波段属性 |
| `AtmosphereModel` | `layer0_phys/AtmosphereModel.h` | 大气模型，分层衰减/散射/折射参数 |
| `CrossInductionRules` | `layer0_phys/CrossInductionRules.h` | 交互感应规则，定义传感器间的频段耦合关系 |
| `PhysicsEngine` | `layer0_phys/PhysicsEngine.h` | 刚体物理引擎 (Bullet3 + vsgBullet) |

### 3.2 Layer 1 — 场景构建层

| 组件 | 文件 | 职责 |
|------|------|------|
| `SceneBuilder` | `layer1_scene/SceneBuilder.h` | 从 OBJ 模型构建 vsg 场景图 |
| `MultiSpectralGeometryNode` | `layer1_scene/MultiSpectralGeometryNode.h` | 多光谱几何节点，携带材质/波段属性 |
| `DynamicMultiSpectralNode` | `layer1_scene/DynamicMultiSpectralNode.h` | 动态多光谱节点，支持运动变换 |

### 3.3 Layer 2 — 光线追踪层

| 组件 | 文件 | 职责 |
|------|------|------|
| `RayTracer` | `layer2_rt/RayTracer.h` | CPU 参考光线追踪器 |
| `RayTracingPipelineWrap` | `layer2_rt/RayTracingPipelineWrap.h` | Vulkan RT 管线包装 (GPU 加速) |
| `AtmosphereIntegrator` | `layer2_rt/AtmosphereIntegrator.h` | 大气积分器，沿射线路径累积衰减 |
| `CrossInductionDetector` | `layer2_rt/CrossInductionDetector.h` | 交互感应检测器，识别交叉耦合路径 |
| `SensorProcessor` | `layer2_rt/SensorProcessor.h` | 传感器处理器，将 RT 结果转为散射点 |

### 3.4 Layer 3 — CUDA 信号处理层

| 组件 | 文件 | 职责 |
|------|------|------|
| `SignalProcessor` | `layer3_cuda/SignalProcessor.h` | CUDA 信号处理器，GPU 加速信号生成 |

### 3.5 Layer 3b — 信号形成层

| 组件 | 文件 | 职责 |
|------|------|------|
| `SensorSignalProcessor` | `layer3b_signal/SensorSignalProcessor.h` | 传感器信号处理器基类 |
| `RadarPulseProcessor` | `layer3b_signal/RadarPulseProcessor.cpp` | 雷达脉冲压缩 + 距离多普勒图生成 |
| `IrThermalProcessor` | `layer3b_signal/IrThermalProcessor.cpp` | 红外热辐射图像生成 |
| `VisCameraProcessor` | `layer3b_signal/VisCameraProcessor.cpp` | 可见光相机图像生成 |
| `SarBackprojectProcessor` | `layer3b_signal/SarBackprojectProcessor.cpp` | SAR 后向投影成像 |
| `SignalFrame` | `layer3b_signal/SignalFrame.h` | 信号帧数据结构 |

### 3.6 Layer 4 — 调度分发层

| 组件 | 文件 | 职责 |
|------|------|------|
| `SensorScheduler` | `layer4_sched/SensorScheduler.h` | 传感器发射调度器，构建时序发射表 |
| `DataDispatcher` | `layer4_sched/DataDispatcher.h` | 数据分发器，将散射点按传感器路由分发 |

### 3.7 Layer 5 — 可视化层

| 组件 | 文件 | 职责 |
|------|------|------|
| `Visualizer` | `layer5_vis/Visualizer.h` | VSG 可视化器，场景渲染 + 传感器图像叠加 |

---

## 4. 系统框架 (System Framework)

系统框架是**独立于业务层的通用基础设施**，提供 5 类可配置组件：

### 4.1 架构全景

```
┌─────────────────────────────────────────────────────────────────┐
│                      SimulationRoot (根系统)                     │
│  ┌──────────┐ ┌──────────┐ ┌────────┐ ┌────────┐ ┌──────────┐ │
│  │MasterClock│ │Physics   │ │Environ.│ │Sensor  │ │RT        │ │
│  │System    │ │System   │ │System  │ │System  │ │System    │ │
│  └──────────┘ └──────────┘ └────────┘ └────────┘ └──────────┘ │
│  ┌──────────┐ ┌──────────┐                                      │
│  │Signal    │ │Visual    │                                      │
│  │System    │ │System    │                                      │
│  └──────────┘ └──────────┘                                      │
├─────────────────────────────────────────────────────────────────┤
│                     MessageBus (消息总线)                        │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Transport 层: in_process / shm / dds / grpc / zmq       │  │
│  │  Serializer 层: binary / json / raw / xml / protobuf / flatbuffer│
│  └──────────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│  ComputeBackend: cpu / cuda / vulkan   +   CrossDeviceTransfer  │
├─────────────────────────────────────────────────────────────────┤
│  Deployment: single_process / multi_process / multi_machine     │
│  NodeRegistry / DistributedMasterClock / NetworkPartitionHandler│
└─────────────────────────────────────────────────────────────────┘
```

### 4.2 SystemBase — 统一系统基类

[SystemBase.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/system/SystemBase.h) 是所有子系统的根基。

**核心设计：模板方法模式**

```
tick() 是框架方法 (基类实现) → 自动处理频率判定 + 递归子系统
onTick() 是钩子方法 (子类实现) → 处理业务逻辑
```

**生命周期状态机：**

```
CREATED → INITIALIZED → RUNNING → STOPPED
                        ↓
                      PAUSED
                        ↓
                     ERROR_STATE
```

**关键成员：**

| 成员 | 类型 | 说明 |
|------|------|------|
| `id_` / `name_` / `type_` | 身份标识 | 系统唯一标识和类型 |
| `parent_` / `children_` | 树形结构 | 父子关系，支持 `findByPath("Root/PhysicsSystem")` |
| `state_machine_` | StateMachine | 可配置的状态机，支持自定义状态 (100+) |
| `tick_strategy_` | TickStrategy | 频率策略，决定何时执行 onTick |
| `bus_` | MessageBus | 消息总线，频道级发布/订阅 |
| `compute_backend_` | IComputeBackend | 计算后端，CPU/GPU 可配置 |
| `config_` | SystemConfig | 从 JSON 加载的配置 |

### 4.3 时间模型 (TimeTick)

[TimeTick.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/system/TimeTick.h) 定义三层时间：

```
sim_time    — 逻辑时间 (秒)，业务计算用
frame_id    — 单调递增帧号，跨进程同步的唯一依据
wall_clock  — 墙钟，仅诊断漂移，不参与逻辑
dt          — 本次步长
```

### 4.4 频率策略 (TickStrategy)

[TickStrategy.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/system/TickStrategy.h) 提供 6 种内置策略，工厂模式可扩展：

| 策略 | 类名 | 说明 | 典型场景 |
|------|------|------|----------|
| 固定频率 | `FixedRateStrategy` | 按固定频率 tick | 雷达 1kHz, 环境 10Hz |
| 事件驱动 | `EventDrivenStrategy` | 事件到来才执行 | 脉冲触发 |
| 子步长 | `SubStepStrategy` | 固定 dt 子步进 | 物理引擎 120Hz |
| 批处理 | `BatchStrategy` | 按时间窗累积批次 | RT 批量处理 |
| 自适应 | `AdaptiveStrategy` | 根据负载动态调整频率 | 自适应加载 |
| 多速率 | `MultiRateStrategy` | 同一系统内多个任务不同频率 | 传感器系统复合频率 |
| 始终执行 | `AlwaysTickStrategy` | 每帧都执行 | MasterClock, Root |

**决策机制：**

```
TickDecision { action, substep_count, substep_dt, reason }
- SKIP           → 跳过
- EXECUTE        → 执行 onTick
- EXECUTE_SUBSTEPS → 执行 onSubStep 多次
- DEFER          → 延迟 (批处理累积)
- ERROR_ABORT    → 错误中止
```

### 4.5 状态机 (StateMachine)

[StateMachine.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/system/StateMachine.h) 提供可配置的状态转换：

- **预定义状态 (0-99)**：CREATED / INITIALIZED / RUNNING / PAUSED / STOPPED / ERROR
- **自定义状态 (100+)**：业务定义，如雷达的 STANDBY / SEARCHING / TRACKING
- 支持 JSON 配置转移规则，含 `on_enter` / `on_exit` 回调
- 支持按触发器名称转换 (`transitionByTrigger`)

---

## 5. 通信架构

### 5.1 MessageBus — 消息总线

[MessageBus.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/system/MessageBus.h) 是系统间通信的枢纽。

**频道级配置：** 不同频道可用不同 Transport + Serializer

```json
{
  "scene.state":       { "serializer": "raw" },     // 进程内零拷贝
  "sensor.pulse_tx":   { "serializer": "raw" },     // 高频发射调度
  "rt.scatter_points": { "serializer": "binary" },  // 紧凑二进制
  "signal.frames":     { "serializer": "binary" },  // 信号帧
  "env.state":         { "serializer": "json" }     // 调试友好
}
```

**架构：**

```
SystemBase::publish(channel, msg)
  → MessageBus::publish(channel, msg)
    → ISerializer::serialize(msg) → bytes
    → ITransport::publish(channel, bytes)
      → 遍历 subscribers → deliver(msg)
```

### 5.2 Transport 层 — 通信后端

[ITransport.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/system/transport/ITransport.h) 提供 5 种可配置通信后端：

| Transport | 类名 | 说明 | 适用场景 |
|-----------|------|------|----------|
| 进程内 | `InProcessTransport` | 同步函数调用，零延迟 | 单进程开发 |
| 共享内存 | `ShmTransport` | POSIX 共享内存 + 环形缓冲区 | 多进程同机 |
| DDS | `DDSTransport` | 跨机自动发现 (骨架) | 多机分布式 |
| gRPC | `GrpcTransport` | REQ/REP 模式 (骨架) | 查询服务 |
| ZeroMQ | `ZmqTransport` | PUB/SUB 多模式 (骨架) | 跨机通信 |

**ShmTransport 详细设计：**

[ShmTransport.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/system/transport/ShmTransport.h) 使用 POSIX 共享内存 + 环形缓冲区：
- 每个频道一个共享内存段
- `ShmHeader` 含 `pthread_mutex_t` (PROCESS_SHARED) 互斥锁
- 发布者写入环形槽，订阅者后台轮询读取
- 可配置：前缀、容量、槽大小、轮询间隔

### 5.3 Serializer 层 — 序列化器

[ISerializer.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/system/serializer/ISerializer.h) 提供 6 种数据格式：

| Serializer | 类名 | 说明 | 状态 |
|------------|------|------|------|
| 二进制 | `BinarySerializer` | 紧凑二进制，高性能 | 完整实现 |
| JSON | `JsonSerializer` | 文本格式，调试友好 | 完整实现 |
| 原始指针 | `RawPointerSerializer` | 进程内零拷贝 | 完整实现 |
| XML | `XmlSerializer` | 文本格式，兼容 legacy | 完整实现 |
| Protobuf | `ProtobufSerializer` | 二进制跨语言 (骨架) | 骨架 |
| FlatBuffer | `FlatBufferSerializer` | 零拷贝二进制 (骨架) | 骨架 |

### 5.4 MultiMachineMessageBus — 多机消息总线

[MultiMachineMessageBus.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/system/MultiMachineMessageBus.h) 继承 `MessageBus`，实现 SHM + DDS 分层路由：

```
publish(channel, msg):
  1. 本地订阅者 → local_transport (SHM) 直接投递
  2. 远程订阅者 → remote_transport (DDS) 网络投递
  3. 远程消息到达 → forwardRemoteToLocal() → 转发给本地订阅者
```

---

## 6. 计算架构

### 6.1 ComputeBackend — 计算后端

[IComputeBackend.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/system/compute/IComputeBackend.h) 提供 3 种计算后端：

| 后端 | 类名 | 说明 | 状态 |
|------|------|------|------|
| CPU | `CPUBackend` | 多核 CPU，支持核心绑定 + NUMA | 完整实现 |
| CUDA | `CUDABackend` | GPU 加速 (骨架) | 骨架 |
| Vulkan | `VulkanBackend` | GPU 计算 (骨架) | 骨架 |

**设备发现与分配：**

[DeviceManager.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/system/compute/DeviceManager.h) 负责：
- CPU 拓扑发现（核心数、NUMA 节点）
- CUDA GPU 发现（设备数、显存、计算能力、RT 支持）
- Vulkan GPU 发现
- 设备分配：独占/共享模式，根据 `ComputeRequirement` 自动匹配

**跨设备传输：**

[CrossDeviceTransfer.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/system/compute/CrossDeviceTransfer.h) 支持：
- 同类型同设备：直接拷贝（零拷贝优化）
- 同类型不同设备：经 host 中转
- 跨类型 (CUDA↔Vulkan)：经 host 中转
- 同 GPU 不同 API：通过 Vulkan 外部内存扩展零拷贝

---

## 7. 部署架构

### 7.1 三种部署模式

[DeploymentConfig.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/system/deployment/DeploymentConfig.h) 通过 JSON 配置切换，代码完全不感知：

```
single_process:              所有系统同进程, Transport=in_process
multi_process_single_machine: 多进程同机, Transport=shm
multi_process_multi_machine:  多进程跨机, Transport=shm+dds
```

### 7.2 部署管理器

[DeploymentManager.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/system/deployment/DeploymentManager.h) 负责：
- 单进程：直接在当前进程运行
- 多进程单机：fork 子进程 + CPU 亲和性设置
- 多进程多机：SSH 启动远程进程
- 监控子进程健康，处理故障

### 7.3 节点注册与发现

[NodeRegistry.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/system/deployment/NodeRegistry.h) 负责：
- 启动时注册本节点
- 发现方式：静态配置 / DDS 自动发现 / 中心注册
- 周期性心跳广播（`node.heartbeat` 频道）
- 监控其他节点心跳，超时标记 OFFLINE 并触发故障回调

### 7.4 分布式主时钟

[DistributedMasterClock.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/systems/DistributedMasterClock.h) 继承 `MasterClockSystem`，实现主备切换：

```
PRIMARY:  正常驱动仿真，广播 time.tick + clock.heartbeat
BACKUP:   监听主时钟心跳，超时则升级为 PRIMARY
FOLLOWER: 非 master 节点，接收 time.tick 同步本地时间
```

**故障切换流程：**
```
正常: PRIMARY 驱动 + 心跳 → BACKUP 监听
故障: PRIMARY 崩溃 → BACKUP 超时(3s) → 升级 PRIMARY → 广播 clock.takeover
恢复: 原 PRIMARY 重启 → 发现已有新 PRIMARY → 降为 BACKUP
```

### 7.5 网络分区处理

[NetworkPartitionHandler.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/system/deployment/NetworkPartitionHandler.h) 负责：
- 检测网络分区（存活节点数 < 半数 → 少数派分区）
- 少数派分区：暂停跨机通信，主时钟停止驱动（防脑裂）
- 分区恢复：重新对齐仿真时间，恢复跨机通信

---

## 8. 子系统设计

### 8.1 子系统总览

| 子系统 | 类名 | 类型 | 频率 | 职责 |
|--------|------|------|------|------|
| 仿真根 | `SimulationRoot` | ROOT | always | 初始化 Layer0，创建 SimContext，注入分发 |
| 主时钟 | `MasterClockSystem` | TIME | always | 全局时序驱动，60Hz 主循环 |
| 物理 | `PhysicsSystem` | PHYSICS | substep 120Hz | 刚体力学 (Bullet3) |
| 环境 | `EnvironmentSystem` | ATMOSPHERE | 10Hz | 太阳/温度/大气/风速 |
| 传感器 | `SensorSystem` | SENSOR | 60Hz | 构建发射调度表 |
| 光线追踪 | `RTSystem` | RT | 60Hz | CPU/GPU 光线追踪 → 散射点 |
| 信号形成 | `SignalSystem` | SIGNAL | 60Hz | 散射点 → 信号帧 (RD图/IR/可见光) |
| 可视化 | `VisualSystem` | VISUAL | 60Hz | VSG 渲染 + 传感器图像显示 |

### 8.2 SimulationRoot — 仿真根系统

[SimulationRoot.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/systems/SimulationRoot.h) 是系统的顶层协调器：

```
onInit:  初始化 Layer 0 (物理参数/大气/场景)
onStart: 创建 SimContext → 注入到所有子系统
onTick:  同步物理状态到场景 (更新孔径位置)
```

### 8.3 MasterClockSystem — 主时钟

[MasterClockSystem.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/systems/MasterClockSystem.h) 是全局时序源：

```
drive(max_frames):
  while !stopRequested:
    tick = TimeTick { sim_time, frame_id++, dt, wall_clock }
    this->tick(tick)  // 递归整棵系统树
    sim_time += dt
```

### 8.4 SimContext — 共享上下文

[SimContext.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/systems/SimContext.h) 是子系统间高频数据的共享通道：

```
SimContext 结构:
├── Layer 0/1 共享数据 (由 SimulationRoot 创建)
│   ├── phys (PhysParamManager)
│   ├── atm (AtmosphereModel)
│   ├── rules (CrossInductionRules)
│   ├── physics (PhysicsEngine)
│   ├── scene (SceneBuilder)
│   └── scene_root (vsg::Node)
├── Vulkan 设备 (由 VisualSystem 创建，供 RTSystem GPU 路径使用)
│   └── vk_device
├── 环境状态 (由 EnvironmentSystem 写入)
│   └── env_state (EnvState)
├── 帧间共享数据 (各子系统读写)
│   ├── current_schedule (SensorSystem 写 → RTSystem 读)
│   ├── scatter_points (RTSystem 写 → SensorSystem/SignalSystem 读)
│   ├── routed_batches (SensorSystem 写 → SignalSystem 读)
│   └── signal_frames (SignalSystem 写 → VisualSystem 读)
└── 帧同步 (互斥锁保护)
    ├── mtx
    ├── current_frame
    └── current_sim_time
```

### 8.5 子系统间数据流

```
每一帧的数据流:
                                        ┌──────────────┐
                                        │EnvironmentSys│
                                        │  (10Hz)      │
                                        └──────┬───────┘
                                               │ env.state
                                               ▼
┌──────────┐   sensor.pulse_tx   ┌──────────────┐
│ SensorSys│────────────────────►│   RTSystem   │
│ (60Hz)   │                     │   (60Hz)     │
└──────────┘                     └──────┬───────┘
                                        │ rt.scatter_points
                                        ▼
                               ┌──────────────┐
                               │ SignalSystem │
                               │  (60Hz)      │
                               └──────┬───────┘
                                      │ signal.frames
                                      ▼
                               ┌──────────────┐
                               │ VisualSystem │
                               │  (60Hz)      │
                               └──────────────┘
```

**高频数据（零拷贝）走 SimContext：**
- `current_schedule`：SensorSystem → RTSystem
- `scatter_points`：RTSystem → SignalSystem
- `signal_frames`：SignalSystem → VisualSystem

**低频控制消息走 MessageBus：**
- `env.state`：EnvironmentSystem → SensorSystem/RTSystem
- `scene.state`：PhysicsSystem → VisualSystem

---

## 9. 配置系统

### 9.1 系统树配置

[system_tree.json](../config/system_tree.json) 定义完整系统树：

```json
{
  "name": "MultiDomainSim",
  "type": "ROOT",
  "tick_strategy": "always",
  "transport": { "default": "in_process", "channels": { ... } },
  "children": [
    { "name": "MasterClock", "type": "TIME", "tick_strategy": "always" },
    { "name": "PhysicsSystem", "type": "PHYSICS", "tick_strategy": "substep" },
    { "name": "EnvironmentSystem", "type": "ATMOSPHERE", "tick_strategy": "fixed", "tick_rate": 10 },
    { "name": "SensorSystem", "type": "SENSOR", "tick_strategy": "fixed", "tick_rate": 60 },
    { "name": "RTSystem", "type": "RT", "compute_backend": "cpu", "tick_strategy": "fixed", "tick_rate": 60 },
    { "name": "SignalSystem", "type": "SIGNAL", "tick_strategy": "fixed", "tick_rate": 60 },
    { "name": "VisualSystem", "type": "VISUAL", "tick_strategy": "fixed", "tick_rate": 60 }
  ]
}
```

### 9.2 SystemConfig 结构

[SystemConfig.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/system/SystemConfig.h) 每个子系统可配置：

| 配置项 | 说明 |
|--------|------|
| `tick_strategy` | 频率策略 (fixed/event/substep/batch/adaptive/multi_rate) |
| `tick_rate` | 固定频率 (Hz) |
| `tick_params` | 策略参数 |
| `state_machine` | 状态机定义 (JSON) |
| `transport` | 通信后端 (in_process/shm/dds/grpc/zmq) |
| `default_serializer` | 默认序列化器 |
| `channel_serializers` | 频道级序列化器覆盖 |
| `compute_backend` | 计算后端 (cpu/cuda/vulkan) |
| `deployment` | 部署模式 |
| `subscriptions` / `publishes` | 订阅/发布频道 |

### 9.3 工厂注册

新增子系统三步：
1. 继承 `SystemBase`
2. `REGISTER_SYSTEM(MySystem, "MY_SYSTEM")`
3. 配置文件中 `{"type": "MY_SYSTEM"}`

同理，Transport、Serializer、ComputeBackend、TickStrategy 均支持工厂注册扩展。

---

## 10. 目录结构

```
vsgraysimulation/
├── CMakeLists.txt                   # 顶层构建配置
├── main.cpp                         # 旧版入口 (Simulation 类)
├── main_system.cpp                  # 新版框架入口 (SystemBase 架构)
├── include/vsgraysim/
│   ├── Simulation.h                 # 旧版 Simulation 类
│   ├── core/                        # 核心数据结构
│   │   ├── AtmLayerParams.h         # 大气层参数
│   │   ├── BandTypes.h              # 波段类型
│   │   ├── RayPayload.h             # 射线载荷 + 散射点 (SSBO 兼容)
│   │   ├── SensorTypes.h            # 传感器类型
│   │   ├── GeoUnitPhysParams.h      # 几何单元物理参数
│   │   └── ObjLoader.h              # OBJ 加载器
│   ├── layer0_phys/                 # Layer 0: 物理参数
│   │   ├── PhysParamManager.h
│   │   ├── AtmosphereModel.h
│   │   ├── CrossInductionRules.h
│   │   └── PhysicsEngine.h
│   ├── layer1_scene/                # Layer 1: 场景构建
│   │   ├── SceneBuilder.h
│   │   ├── MultiSpectralGeometryNode.h
│   │   └── DynamicMultiSpectralNode.h
│   ├── layer2_rt/                   # Layer 2: 光线追踪
│   │   ├── RayTracer.h
│   │   ├── RayTracingPipelineWrap.h
│   │   ├── AtmosphereIntegrator.h
│   │   ├── CrossInductionDetector.h
│   │   └── SensorProcessor.h
│   ├── layer3_cuda/                 # Layer 3: CUDA 信号处理
│   │   └── SignalProcessor.h
│   ├── layer3b_signal/              # Layer 3b: 信号形成
│   │   ├── SensorSignalProcessor.h
│   │   └── SignalFrame.h
│   ├── layer4_sched/                # Layer 4: 调度分发
│   │   ├── SensorScheduler.h
│   │   └── DataDispatcher.h
│   ├── layer5_vis/                  # Layer 5: 可视化
│   │   └── Visualizer.h
│   ├── system/                      # 系统框架 (通用基础设施)
│   │   ├── SystemBase.h             # 统一系统基类
│   │   ├── SystemConfig.h           # 系统配置 + SystemType 枚举
│   │   ├── SystemFactory.h          # 系统工厂 + 注册宏 + 系统树构建
│   │   ├── TimeTick.h               # 三层时间模型
│   │   ├── TickStrategy.h           # 6种频率策略 + 工厂
│   │   ├── StateMachine.h           # 可配置状态机
│   │   ├── Message.h                # 消息结构 + 频道哈希
│   │   ├── MessageBus.h             # 消息总线 (频道级Transport+Serializer)
│   │   ├── MultiMachineMessageBus.h # 多机消息总线 (SHM+DDS分层路由)
│   │   ├── transport/
│   │   │   ├── ITransport.h         # 5种通信后端接口
│   │   │   └── ShmTransport.h       # 共享内存通信后端
│   │   ├── serializer/
│   │   │   └── ISerializer.h        # 6种序列化器接口
│   │   ├── compute/
│   │   │   ├── IComputeBackend.h    # 3种计算后端接口
│   │   │   ├── DeviceManager.h      # 设备发现与分配
│   │   │   └── CrossDeviceTransfer.h# 跨设备数据传输
│   │   └── deployment/
│   │       ├── DeploymentConfig.h   # 三种部署模式配置
│   │       ├── DeploymentManager.h  # 部署管理器
│   │       ├── NodeRegistry.h       # 节点注册与发现
│   │       └── NetworkPartitionHandler.h # 网络分区容错
│   └── systems/                     # 子系统包装
│       ├── SimulationRoot.h
│       ├── MasterClockSystem.h
│       ├── DistributedMasterClock.h
│       ├── PhysicsSystem.h
│       ├── EnvironmentSystem.h
│       ├── SensorSystem.h
│       ├── RTSystem.h
│       ├── SignalSystem.h
│       ├── VisualSystem.h
│       └── SimContext.h
├── src/                             # 实现文件 (镜像 include 结构)
│   ├── layer0_phys/ layer1_scene/ layer2_rt/ layer3_cuda/
│   ├── layer3b_signal/ layer4_sched/ layer5_vis/
│   ├── system/ systems/
│   └── Simulation.cpp
├── cuda/signal_processing.cu        # CUDA 核函数
├── shaders/                         # Vulkan RT 着色器
│   ├── raygen.rgen
│   ├── closesthit.rchit
│   └── miss.rmiss
├── config/                          # 配置文件
│   ├── system_tree.json             # 系统树配置
│   ├── atmosphere_layers.json       # 大气层配置
│   ├── tank_multispectral.json      # 多光谱材质配置
│   ├── tank_physics_demo.json       # 物理演示配置
│   ├── sensor_rich_set.json         # 传感器配置
│   ├── sensor_cross_induction.json  # 交互感应配置
│   └── scenarios/                   # 场景配置
├── models/                          # 3D 模型
├── gui/                             # Qt6 GUI 前端
└── test/                            # 测试
    ├── test_objloader.cpp
    └── test_physics.cpp
```

---

## 11. 设计原则

1. **真实世界自循环隐喻**：每个系统是"物体"，有自己的心跳、频率、状态机
2. **树形组合**：子系统形成父子树，父驱动子的 tick
3. **频道级发布/订阅**：系统间通过消息总线通信，不直接调用
4. **配置驱动**：系统树、频率、状态机、通信、计算后端全部 JSON 可配置
5. **部署透明**：单进程/多进程/多机通过配置切换，代码不感知
6. **工厂可扩展**：所有组件（System/Transport/Serializer/ComputeBackend/TickStrategy）通过工厂注册
7. **高频零拷贝**：大带宽数据走 SimContext 共享指针，低频控制走 MessageBus
8. **模板方法模式**：`tick()` 是框架方法，`onTick()` 是钩子方法