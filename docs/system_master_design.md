# vsgraysimulation 大系统架构设计

> 本文档定义**三大子系统 + 串联总线**的整体架构,取代之前的"联邦"概念。
> 目标:支持地面/航空/航天/卫星/火箭等多域平台在同一套系统中协同仿真。

---

## 1. 设计理念

### 1.1 三大子系统 + 一条总线

```
        ┌─────────────────────────────────────┐
        │         时序系统 TimeSystem          │  ← 中枢神经
        │   (统一时钟 + 帧调度 + 推进控制)      │
        └─────────────────┬───────────────────┘
                          │ TimeTick (frame_id, sim_time)
          ┌───────────────┼───────────────┐
          ▼               ▼               ▼
   ┌─────────────┐ ┌─────────────┐ ┌─────────────┐
   │ 场景更新系统  │ │  仿真系统    │ │  仿真系统    │
   │ SceneSystem │ │ SimModule A │ │ SimModule B │  ← 肌肉骨骼
   │  (共享态势)  │ │  (物理)     │ │  (光线追踪)  │
   └──────┬──────┘ └──────┬──────┘ └──────┬──────┘
          │               │               │
          └───────────────┼───────────────┘
                          ▼
              ┌────────────────────────┐
              │  SharedSceneState 总线  │  ← 血液循环
              │  (实体注册 + 双缓冲态势) │
              └────────────────────────┘
```

| 子系统 | 角色 | 比喻 |
|--------|------|------|
| **TimeSystem** | 统一时钟、帧调度、推进控制 | 中枢神经 |
| **SceneSystem** | 实体注册、共享态势、远程代理、场景分块 | 血液循环 |
| **SimulationSystem** | 各专业计算模块(物理/RT/信号/可视化...) | 肌肉骨骼 |
| **SharedSceneState** | 模块间数据交换总线 | 共享血流 |

### 1.2 设计原则

1. **模块只认 frame_id,不认墙钟** — 时序统一由 TimeSystem 发号施令
2. **模块间不直接互调** — 通过 SceneSystem 共享态势交互,无环形依赖
3. **单进程也能跑,多进程也能跑** — SimModule 可以是线程/进程/远端节点的任意形态
4. **数据零等待读取** — 双缓冲 + 原子指针,读永远不阻塞写
5. **慢可降级,崩可隔离** — 任一模块挂掉不拖垮全系统
6. **现有 6 层代码全部保留** — 作为 SimModule 实现直接接入

### 1.3 与"联邦"概念的区别

| 维度 | 旧"联邦"概念 | 新"系统/模块"概念 |
|------|-------------|------------------|
| 组织单位 | Federate(独立仿真器) | SimModule(功能模块) |
| 通信 | 跨 federate PDU | 模块↔总线↔模块 |
| 顶层 | FederationManager | TimeSystem + SceneSystem |
| 复杂度 | 重,需 RTI/DIS | 轻,自研 IPC |
| 心智模型 | 多个独立仿真器互操作 | 一个系统内多个模块协作 |

---

## 2. 系统总览

### 2.1 完整架构图

```
┌──────────────────────────────────────────────────────────────────┐
│                          大系统容器                                │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │                   TimeSystem (时序系统)                     │ │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │ │
│  │  │ MasterClock  │→ │FrameScheduler│→ │ TickPublisher │     │ │
│  │  └──────────────┘  └──────────────┘  └──────┬───────┘     │ │
│  │                                            │ TimeTick      │ │
│  └────────────────────────────────────────────┼───────────────┘ │
│                                               │                 │
│  ┌────────────────────────────────────────────┼───────────────┐ │
│  │                 SceneSystem (场景系统)      │               │ │
│  │  ┌──────────────┐  ┌──────────────┐  ┌─────▼────────┐     │ │
│  │  │EntityRegistry│  │SceneStreaming│  │SharedScene   │     │ │
│  │  │ (实体注册)   │  │ (分块加载)   │  │State (态势)  │     │ │
│  │  └──────────────┘  └──────────────┘  └──────────────┘     │ │
│  │  ┌──────────────┐  ┌──────────────┐                       │ │
│  │  │ RemoteProxy  │  │DeadReckoning │                       │ │
│  │  │ (远程代理)   │  │ (外推)       │                       │ │
│  │  └──────────────┘  └──────────────┘                       │ │
│  └────────────────────────────────────────────────────────────┘ │
│                                               ▲                 │
│  ┌────────────────────────────────────────────┼───────────────┐ │
│  │           SimulationSystem (仿真系统)       │               │ │
│  │                                            │ 读/写态势      │ │
│  │  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌─────────┐ │ │
│  │  │PhysicsModule│ │RTModule    │ │SignalModule│ │VisModule│ │ │
│  │  │ (Bullet3)  │ │ (Vulkan RT)│ │ (CUDA)     │ │ (VSG)   │ │ │
│  │  └────────────┘ └────────────┘ └────────────┘ └─────────┘ │ │
│  │  ┌────────────┐ ┌────────────┐ ┌────────────┐              │ │
│  │  │SensorModule│ │AtmosModule │ │OrbitModule │ ...          │ │
│  │  │ (调度)     │ │ (大气)     │ │ (轨道)     │              │ │
│  │  └────────────┘ └────────────┘ └────────────┘              │ │
│  └────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘
```

### 2.2 三大子系统的边界

| 子系统 | 输入 | 输出 | 不做 |
|--------|------|------|------|
| TimeSystem | 命令(开始/暂停/倍速) | TimeTick | 不持业务数据 |
| SceneSystem | 实体注册 + 模块写态势 | 读态势 API | 不做计算 |
| SimulationSystem | TimeTick + 态势读 | 态势写 + 业务输出 | 不直接互调其他模块 |

---

## 3. 时序系统 (TimeSystem)

### 3.1 组成

```
TimeSystem 进程
├── MasterClock         单一全局时钟源
├── FrameScheduler      帧号生成 + 推进决策
├── TickPublisher       TimeTick 广播(SHM/DDS)
├── HealthMonitor       模块心跳/超时
└── CommandHandler      外部命令(gRPC)
```

### 3.2 三层时间模型

| 时间 | 性质 | 用途 |
|------|------|------|
| `sim_time` | 逻辑时间(秒) | 业务计算 |
| `frame_id` | 单调递增整数 | **模块同步唯一依据** |
| `wall_clock` | 墙钟 | 仅诊断漂移 |

**铁律**: 模块之间只认 frame_id,不认墙钟。

### 3.3 三种推进模式

```
模式 A: 实时模式(默认)
   sim_time = wall_start + elapsed × time_scale
   每 1/60s 推一帧,慢模块用旧帧

模式 B: 逻辑时间模式
   所有模块报 "frame N ready" → 推进到 N+1
   严格因果,可复现

模式 C: 倍速模式
   time_scale = 0.1 (慢放) / 10 (快进)
```

### 3.4 主帧 + 子帧(多速率协奏)

各模块自然步长差异巨大(1ms ~ 1s),不能强行统一:

```
主帧 (major frame) = 100ms  ← 全局同步点
   │
   ├── PhysicsModule:  每 major 跑 12 子步 (8.33ms × 12, Bullet3 120Hz)
   ├── RTModule:       每 major 跑 6 子步 (16.6ms × 6)
   ├── VisModule:      每 major 跑 6 子步 (16.6ms × 6)
   ├── SensorModule:   每 major 跑 1 步 (100ms)
   ├── OrbitModule:    每 10 major 跑 1 步 (1s)
   └── AtmosModule:    每 10 major 跑 1 步 (1s)

主帧边界: 所有模块必须到达主帧才能进入下一 major
```

### 3.5 TimeTick 协议

```cpp
struct TimeTick {
    uint64_t frame_id;          // 全局帧号(major << 32 | minor)
    double   sim_time;          // 仿真时间(秒)
    double   wall_clock;        // 墙钟(诊断用)
    double   time_scale;        // 当前倍速
    uint8_t  mode;              // 0=real 1=stepped 2=fastforward
    uint8_t  command;           // 0=run 1=pause 2=resume 3=stop
    uint64_t epoch_id;          // 演练批次 ID
    uint64_t expected_mod_mask; // 本帧应参与的模块位图
};  // 64 字节,塞进一个缓存行
```

- 发布频率: 主帧 100ms 一次 + 必要时 minor 帧
- QoS: RELIABLE + KEEP_LAST 1(只要最新)
- 通道: 同机 SHM(<0.1ms),跨机 DDS(0.5-2ms)

### 3.6 推进决策状态机(逻辑模式)

```
IDLE ──start()──→ WAIT_READY(frame=N+1)
                       │
                       │ 所有 expected 模块报 ready
                       ▼
                   GRANT(N+1) ──broadcast TimeTick(run, N+1)──→ 回到 WAIT_READY
                       │
                       │ 超时(2x 期望步长)
                       ▼
                   DEGRADED ──标记未应答模块为 behind──→ 强制推进
```

---

## 4. 场景更新系统 (SceneSystem)

### 4.1 核心职责

1. **实体注册**: 全局唯一 entity_id 分配
2. **态势共享**: 双缓冲原子读写
3. **场景分块**: 大地形/模型按需加载
4. **远程代理**: 跨进程实体的本地代表
5. **死区外推**: 慢模块低频发布,快模块外推

### 4.2 EntityRegistry(实体注册中心)

```cpp
struct EntityDesc {
    uint64_t entity_id;            // 全局唯一
    std::string name;              // "tank_001"
    EntityClass class_;            // GROUND_VEHICLE / AIRCRAFT / SATELLITE / MISSILE
    uint32_t owner_module_id;      // 谁拥有(写权限)
    BoundingBox local_bounds;      // 模型包围盒
    std::string mesh_path;         // 模型路径
    CoordinateFrame frame;         // LOCAL_YUP / ECEF / ECI
};

class EntityRegistry {
public:
    uint64_t registerEntity(const EntityDesc&);   // 创建
    void     unregisterEntity(uint64_t id);       // 销毁
    std::vector<EntityDesc> all() const;
};
```

- 注册中心进程独立,所有模块通过 gRPC/SHM 查询
- entity_id 由注册中心独家分配,避免冲突
- 写权限:`owner_module_id` 唯一,其他模块只读

### 4.3 SharedSceneState(共享态势总线)

```cpp
struct EntityState {
    uint64_t entity_id;
    uint64_t frame_id;             // 最后更新帧
    vsg::dvec3 position;           // 世界坐标
    vsg::dvec3 velocity;
    vsg::dvec3 acceleration;
    vsg::quat   orientation;
    vsg::dvec3 angular_velocity;
    uint8_t     flags;             // 活跃/损坏/隐蔽...
};

class SharedSceneState {
public:
    // 写接口(仅 owner 模块调用)
    void commit(uint64_t entity_id, EntityState&& new_state);
    
    // 读接口(任何模块可调用,永不阻塞)
    const EntityState& read(uint64_t entity_id) const;
    
    // 批量读
    void snapshot(std::vector<EntityState>& out) const;
    
private:
    std::unordered_map<uint64_t, DoubleBuffer<EntityState>> table_;
};
```

**双缓冲实现**(避免读写撕裂):

```cpp
template<typename T>
class DoubleBuffer {
    std::atomic<T*> readable_;
    std::unique_ptr<T> buf_[2];
public:
    void commit(T&& v) {
        auto* w = (readable_.load() == buf_[0].get()) ? buf_[1].get() : buf_[0].get();
        *w = std::move(v);
        readable_.store(w, std::memory_order_release);  // 原子切换
    }
    const T& read() const { return *readable_.load(std::memory_order_acquire); }
};
```

### 4.4 RemoteProxy(远程实体代理)

跨进程模块需要看到对方实体,但不应直接访问对方内存:

```
模块 A 进程                    模块 B 进程
┌──────────────┐              ┌──────────────┐
│ PhysicsModule│              │  RTModule    │
│ 拥有 tank_001│              │              │
│   ↓commit    │              │   ↑read      │
│ LocalState   │              │ RemoteProxy  │
└──────┬───────┘              └──────▲───────┘
       │                           │
       │ DDS/SHM 态势总线          │
       └───────────────────────────┘
```

- 每个 SimModule 进程都有本地 SharedSceneState 副本
- 写:本地 commit + 发布到总线
- 读:本地查 + 后台线程订阅总线更新本地副本
- 远程实体的精细模型按需流式加载

### 4.5 SceneStreaming(场景分块)

大地形/城市/卫星轨道数据无法全部驻留:

```
SceneStreaming 服务
├── 瓦片索引(空间四叉树/八叉树)
├── 兴趣区(AOI)注册: 每个模块报自己的视野
├── 按需下发: 瓦片进入 AOI 才加载,离开则卸载
├── LOD: 近景高模,远景低模
└── 纹理虚拟化: Vulkan sparse binding
```

### 4.6 DeadReckoning(死区外推)

慢模块(OrbitModule 1Hz)和快模块(PhysicsModule 120Hz)的协调:

```cpp
class DeadReckoning {
public:
    // 慢模块每 1s 调用一次
    void updateMaster(uint64_t entity_id, const EntityState& s);
    
    // 快模块每 8ms 调用,得到外推位姿
    EntityState predict(uint64_t entity_id, double sim_time);
    
private:
    struct DRModel {
        EntityState last;
        DRModelType type;  // CV / CA / KEPLER
    };
    std::unordered_map<uint64_t, DRModel> models_;
};

// 外推公式
vsg::dvec3 predictCV(const EntityState& s, double dt) {
    return s.position + s.velocity * dt;
}
vsg::dvec3 predictCA(const EntityState& s, double dt) {
    return s.position + s.velocity * dt + 0.5 * s.acceleration * dt * dt;
}
```

- 误差阈值: 位置 0.5m / 姿态 1°
- 超阈值时慢模块主动发"强制更新"
- 车辆用 CV(匀速),飞机用 CA(匀加速),卫星用开普勒

---

## 5. 仿真系统 (SimulationSystem)

### 5.1 SimModule 抽象接口

```cpp
class SimModule {
public:
    virtual ~SimModule() = default;
    
    // 生命周期
    virtual bool init(const ModuleConfig&) = 0;
    virtual void shutdown() = 0;
    
    // 时间驱动(由 TimeSystem 调度)
    virtual void onTimeTick(const TimeTick& tick) = 0;
    
    // 帧步进(逻辑模式下回报完成)
    virtual void step(double sim_time, uint64_t frame_id) = 0;
    virtual void onSyncPoint(uint64_t major_frame) = 0;  // 主帧边界
    
    // 态势交互
    virtual void commitStates(SharedSceneState&) = 0;     // 写
    virtual void readStates(const SharedSceneState&) = 0; // 读
    
    // 心跳
    virtual Heartbeat heartbeat() const = 0;
    
    // 元信息
    virtual std::string name() const = 0;
    virtual ModuleRate rate() const = 0;  // minor_per_major + major_stride
};
```

### 5.2 模块清单

| 模块 | 现有代码对应 | 步长 | 职责 |
|------|-------------|------|------|
| **PhysicsModule** | [PhysicsEngine](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/layer0_phys/PhysicsEngine.h) | 8.33ms (120Hz) | Bullet3 刚体动力学 |
| **SceneBuildModule** | [SceneBuilder](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/layer1_scene/SceneBuilder.h) | 启动时 | 加载 gltf/obj,构建场景图 |
| **RTModule** | [RayTracingPipelineWrap](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/layer2_rt/RayTracingPipelineWrap.h) | 16.6ms (60Hz) | Vulkan RT 光线追踪 |
| **SignalModule** | [SignalProcessor](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/layer3_cuda/SignalProcessor.h) | 16.6ms (60Hz) | CUDA 信号处理 |
| **SensorModule** | [SensorScheduler](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/layer4_sched/SensorScheduler.h) | 100ms (10Hz) | 发射时序调度 |
| **VisModule** | [Visualizer](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/layer5_vis/Visualizer.h) | 16.6ms (60Hz) | VSG 可视化 |
| **AtmosModule** | [AtmosphereModel](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/layer0_phys/AtmosphereModel.h) | 1s (1Hz) | 大气环境 |
| **OrbitModule** | 新增 | 1s (1Hz) | 轨道动力学 |
| **AeroModule** | 新增 | 1ms (1000Hz) | 飞行器空气动力学 |
| **CrossInducModule** | [CrossInductionDetector](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/layer2_rt/CrossInductionDetector.h) | 事件驱动 | 跨频段互扰 |

### 5.3 ModuleRegistry(模块注册)

```cpp
class ModuleRegistry {
public:
    static ModuleRegistry& instance();
    
    void registerModule(std::unique_ptr<SimModule>);
    SimModule* find(const std::string& name);
    std::vector<SimModule*> all();
    
    // 启动顺序
    std::vector<std::string> initOrder() const;
    std::vector<std::string> shutdownOrder() const;  // 反序
};
```

### 5.4 模块间数据流

**关键原则**: 模块之间**不直接互调**,只通过 SharedSceneState 间接交互:

```
PhysicsModule.commit(tank_001_state)
        │
        ▼
SharedSceneState.buf_[tank_001]
        │
        ▼
RTModule.read(tank_001) ──→ trace rays ──→ RTModule.commit(scatter_points)
                                                          │
                                                          ▼
                                            SignalModule.read(scatter_points)
                                                  │
                                                  ▼
                                            SignalModule.commit(signal_frame)
                                                          │
                                                          ▼
                                            VisModule.read(signal_frame) ──→ 渲染
```

**禁止**:
- ❌ RTModule 直接调 SignalModule.process()
- ❌ PhysicsModule 直接读 RTModule 内部状态
- ❌ 任何模块持有其他模块的指针

**好处**:
- 模块可独立替换(PhysicsModule 换成 AeroModule 不影响 RTModule)
- 模块可独立部署(同进程/跨进程/跨机)
- 单元测试容易(模块只依赖 SharedSceneState 接口)

---

## 6. 三大系统的串联机制

### 6.1 启动顺序

```
1. TimeSystem 启动
   └── MasterClock 等待模块注册

2. SceneSystem 启动
   ├── EntityRegistry 进程起来
   └── SharedSceneState 总线就绪

3. 各 SimModule 启动(可并行)
   ├── 各模块本地 init()
   ├── 向 ModuleRegistry 注册
   ├── 向 EntityRegistry 注册拥有的实体
   └── 向 TimeSystem 报 "ready"

4. TimeSystem 收齐 expected 模块集合
   └── 广播第一个 SyncPoint(epoch_start)

5. 所有模块应答 SyncPointAck

6. TimeSystem 进入主循环
   └── 广播第一帧 TimeTick(run, frame=1)

7. 大系统进入推演
```

### 6.2 帧循环(单个 major frame 内部)

```
major frame N 开始(TimeSystem 广播 TimeTick frame=N)

┌─ T+0ms: 各模块收到 TimeTick
│
├─ T+0~80ms: 各模块并发执行子步
│   ├── PhysicsModule: 跑 12 子步(8.33ms × 12)
│   ├── RTModule:      跑 6 子步(16.6ms × 6)
│   ├── VisModule:     跑 6 子步(16.6ms × 6)
│   ├── SensorModule:  跑 1 步
│   ├── OrbitModule:   N % 10 == 0 才跑
│   └── 各模块 commit 到 SharedSceneState
│
├─ T+80ms: 各模块到达主帧边界
│   └── 发 SyncPointAck(N) 给 TimeSystem
│
├─ T+80~90ms: TimeSystem 收齐 Ack
│
└─ T+100ms: TimeSystem 广播下一帧 TimeTick(frame=N+1)
```

### 6.3 完整数据流图

```
┌─────────────────────────────────────────────────────────────┐
│                     TimeSystem                              │
│   MasterClock ──→ FrameScheduler ──→ TickPublisher          │
└─────────────────────────┬───────────────────────────────────┘
                          │ TimeTick (frame_id, sim_time)
                          ▼
┌─────────────────────────────────────────────────────────────┐
│  SimModule A (Physics)    SimModule B (RT)    SimModule C   │
│  ┌─────────────────┐      ┌─────────────────┐  ┌─────────┐ │
│  │ onTimeTick()    │      │ onTimeTick()    │  │ ...     │ │
│  │   ↓             │      │   ↓             │  │         │ │
│  │ step()          │      │ step()          │  │         │ │
│  │   ↓             │      │   ↓             │  │         │ │
│  │ commitStates() ─┼──────┼─→ SharedScene   │  │         │ │
│  │                 │      │   State         │  │         │ │
│  │ readStates() ◄──┼──────┼─← (其他模块)    │  │         │ │
│  │   ↓             │      │   ↓             │  │         │ │
│  │ onSyncPoint()   │      │ onSyncPoint()   │  │         │ │
│  └─────────────────┘      └─────────────────┘  └─────────┘ │
└─────────────────────────────┬───────────────────────────────┘
                              │ SyncPointAck
                              ▼
                     回到 TimeSystem
                     (推进到下一 major frame)
```

### 6.4 三大子系统的依赖关系

```
TimeSystem
   │ 驱动
   ▼
SimulationSystem ──── 读/写 ────→ SceneSystem
                         ▲
                         │ 注册实体
                         │
                    EntityRegistry
```

- TimeSystem **不依赖**任何其他系统(只管时间)
- SceneSystem **不依赖**任何其他系统(只管数据)
- SimulationSystem **依赖** TimeSystem(时钟) + SceneSystem(数据)

**关键**: 没有环形依赖,启动顺序清晰。

---

## 7. 部署形态

### 7.1 单进程多线程(开发期)

```
vsgraysim_app 进程
├── TimeSystemThread       (TimeSystem)
├── SceneSystemThread      (SceneSystem)
├── PhysicsModuleThread    (SimModule)
├── RTModuleThread
├── SignalModuleThread
├── VisModuleThread        (主线程,VSG 要求)
└── 共享内存 = 进程内 std::shared_ptr

通信: 无,直接函数调用 + 双缓冲
适用: 开发调试、单机演示
```

### 7.2 多进程同机(测试期)

```
单机多 GPU
├── vsgraysim_time         (TimeSystem 进程)
├── vsgraysim_scene        (SceneSystem 进程)
├── vsgraysim_physics      (PhysicsModule 进程)
├── vsgraysim_rt           (RTModule 进程, GPU0)
├── vsgraysim_sig          (SignalModule 进程, GPU0)
├── vsgraysim_vis          (VisModule 进程, GPU1)
└── vsgraysim_orbit        (OrbitModule 进程)

通信: POSIX SHM(态势) + DDS(控制) + gRPC(注册)
适用: 性能测试、负载评估
```

### 7.3 多机多进程(生产期)

```
K8s/Slurm 集群
├── node-master: TimeSystem + SceneSystem(EntityRegistry)
├── node-ground: PhysicsModule + SensorModule
├── node-air:    AeroModule + AeroPhysics
├── node-space:  OrbitModule
├── node-rt-0~N: RTModule 实例池(水平扩展)
├── node-sig-0~N: SignalModule 实例池
└── node-vis:    VisModule + 录制

通信: DDS 多播(态势) + gRPC(控制) + SHM(同机快路径)
适用: 大规模生产仿真
```

### 7.4 部署形态切换

**关键**: 三种形态**代码不变**,只换部署清单:

```yaml
# deploy_single.yaml
mode: single_process
modules:
  - physics: { thread: true }
  - rt:      { thread: true }
  - vis:     { thread: true }

# deploy_multi.yaml
mode: multi_process
modules:
  - physics: { process: true, gpu: 0 }
  - rt:      { process: true, gpu: 1 }
  - vis:     { process: true, gpu: 0 }

# deploy_cluster.yaml
mode: cluster
nodes:
  - master: [time, scene]
  - ground: [physics, sensor]
  - rt:     [rt, signal]
```

SimModule 框架自动根据部署清单选择通信方式(线程间 std::shared_ptr / 进程间 SHM / 跨机 DDS)。

---

## 8. 与现有代码的对接

### 8.1 改造策略: 现有 6 层代码 → SimModule

| 现有 | 改造为 | 改动量 |
|------|--------|--------|
| [Simulation](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/Simulation.h) | 大系统容器/编排器 | 中 |
| [PhysicsEngine](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/layer0_phys/PhysicsEngine.h) | PhysicsModule | 小(加 SimModule 包装) |
| [SceneBuilder](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/layer1_scene/SceneBuilder.h) | SceneBuildModule | 小 |
| [RayTracingPipelineWrap](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/layer2_rt/RayTracingPipelineWrap.h) | RTModule | 小 |
| [SignalProcessor](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/layer3_cuda/SignalProcessor.h) | SignalModule | 小 |
| [SensorScheduler](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/layer4_sched/SensorScheduler.h) | SensorModule | 小 |
| [Visualizer](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/layer5_vis/Visualizer.h) | VisModule | 小 |

### 8.2 Simulation 类的改造

```cpp
// 现有:
class Simulation {
    std::size_t run();           // 自驱动循环
    bool step(float dt);         // private
};

// 改造后:
class SimulationOrchestrator {
public:
    // 由 TimeSystem 驱动,替代原 run()
    void onTimeTick(const TimeTick& tick) {
        float dt = tick.sim_time - last_sim_time_;
        stepAllModules(dt, tick.frame_id);  // 调度各 SimModule
        last_sim_time_ = tick.sim_time;
    }
    
private:
    ModuleRegistry modules_;
    double last_sim_time_ = 0;
};

// PhysicsModule 包装现有 PhysicsEngine:
class PhysicsModule : public SimModule {
    bool init(const ModuleConfig& cfg) override {
        physics_ = std::make_unique<PhysicsEngine>();
        physics_->setGravity({0, -9.81, 0});
        physics_->addGroundPlane(0.0);
        return true;
    }
    void step(double sim_time, uint64_t frame_id) override {
        physics_->step(1.0/120.0);  // 现有方法,完全复用
    }
    void commitStates(SharedSceneState& s) override {
        for (auto& [id, body] : bodies_)
            s.commit(id, body->getWorldTransform());
    }
private:
    std::unique_ptr<PhysicsEngine> physics_;
};
```

### 8.3 [main.cpp](file:///home/mte/FOLD/GitProject/vsgraysimulation/main.cpp) 改造

```cpp
int main(int argc, char** argv) {
    // 新增 --deploy 模式
    if (mode == "single") {
        // 单进程: 所有模块作线程
        SingleProcessContainer container;
        container.loadConfig("deploy_single.yaml");
        container.run();
    } else if (mode == "time") {
        // 独立 TimeSystem 进程
        TimeSystemProcess proc;
        proc.run();
    } else if (mode == "module") {
        // 独立 SimModule 进程
        // --module physics → 启动 PhysicsModule
        ModuleProcess proc(argv[--module]);
        proc.run();
    }
}
```

---

## 9. 演进路线

### 阶段 0: 框架搭建(2-3 周)

**目标**: 在现有代码上引入 SimModule 接口,单进程跑通

- [ ] 定义 `SimModule` 抽象接口
- [ ] 定义 `SharedSceneState` 双缓冲类
- [ ] 定义 `TimeTick` 数据结构
- [ ] 把 `Simulation::step()` 改 public + 加 frame_id
- [ ] 写 `SimulationOrchestrator` 单线程版(顺序调度各模块)
- [ ] 现有所有功能不退化

**验收**: `--mode run` 与改造前行为完全一致

### 阶段 1: 多线程并发(2-3 周)

**目标**: 各模块并发执行,共享态势

- [ ] 实现 `SharedSceneState` 双缓冲 + 原子指针
- [ ] 各 SimModule 跑在独立线程
- [ ] 主帧 barrier 同步
- [ ] TimeSystem 线程驱动

**验收**: 多线程版与单线程版数值一致,性能提升 30%+

### 阶段 2: TimeSystem 独立进程(3-4 周)

**目标**: 把 TimeSystem 拆出独立进程

- [ ] 实现 SHM TimeTick 发布
- [ ] 实现 SHM SharedSceneState 共享
- [ ] MasterClock 进程独立部署
- [ ] 三种推进模式(实时/逻辑/倍速)
- [ ] 暂停/恢复/单步

**验收**: 双进程版与单进程版行为一致

### 阶段 3: 模块进程化(4-6 周)

**目标**: 各 SimModule 可独立进程部署

- [ ] 模块进程模板(ModuleProcess)
- [ ] DDS 态势总线(跨机)
- [ ] RemoteProxy 远程实体代理
- [ ] DeadReckoning 死区外推
- [ ] EntityRegistry 独立进程

**验收**: 5+ 进程协同运行,态势延迟 < 50ms

### 阶段 4: 新域扩展(持续)

**目标**: 加入航空/航天模块

- [ ] AeroModule(JSBSim 集成)
- [ ] OrbitModule(SGP4 + 数值积分)
- [ ] CoordinateService(ECEF↔局部)
- [ ] SceneStreaming 大地形分块
- [ ] 跨域互扰模块

**验收**: 卫星过顶可被地面雷达探测,飞机投弹可命中地面目标

### 阶段 5: 集群化(持续)

**目标**: 多机部署

- [ ] K8s/Slurm 部署模板
- [ ] RT/Signal 模块水平扩展
- [ ] DIS/HLA 网关(外部互操作)
- [ ] 录制/回放系统

---

## 10. 关键设计信条

1. **三大子系统职责清晰**:
   - TimeSystem 管时间,不管数据
   - SceneSystem 管数据,不做计算
   - SimulationSystem 管计算,不直接互调

2. **模块只认 frame_id**: 跨模块同步的唯一依据是帧号,不依赖墙钟

3. **数据通过总线交互**: 模块之间无直接指针/调用,只读写 SharedSceneState

4. **双缓冲零等待**: 读永不阻塞写,写永不阻塞读

5. **主帧对齐,子帧自治**: 100ms 全局对齐,中间各模块按自身步长跑

6. **慢可降级,崩可隔离**: 任一模块挂掉,TimeSystem 检测并降级,不拖垮全系统

7. **单进程能跑,多进程也能跑**: 部署形态由配置决定,代码不变

8. **现有 6 层代码是资产**: 包装为 SimModule 即可接入,不重写

---

## 11. 与时序核心文档的关系

本设计文档取代之前的"联邦架构"概念。详细的时序核心实现细节(状态机、协议字段、容错)见:

- [master_clock_design.md](master_clock_design.md) — TimeSystem 的深度设计
  (注:该文档中"联邦/federate"术语应替换为"系统/模块")

---

**文档版本**: v1.0
**更新日期**: 2026-07-31
**取代**: architecture_evolution_multiprocess.md(已删除)