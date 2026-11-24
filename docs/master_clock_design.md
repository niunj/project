# 时序推演核心(Master Clock)详细设计

> 本文档聚焦分布式仿真联邦的**心脏**:全局时序推演进程。
> 解决"如何把时序同步到各子进程各子模块"的核心问题。

---

## 1. 为什么必须有专门的时序核心

### 1.1 当前 [Simulation](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/Simulation.h) 的时间模型缺陷

当前 `Simulation::run()` 自驱动 60Hz,内部 7 步串行:

```
while (running) {
    step(frame_dt=1/60);  // 自驱动,无外部时钟
    frame_count_++;
}
```

**问题**:
1. **没有外部时钟源** — 慢节点拖累全局,无法发现
2. **没有帧号锁** — 跨进程无法对齐"第 N 帧"
3. **没有推进许可** — 慢 federate 落后时,快 federate 已读到旧数据
4. **没有事件因果** — 互扰/命中事件无严格时序保证
5. **没有暂停/恢复** — 一旦启动无法受控

### 1.2 时序核心的不可替代性

| 子系统出问题 | 没有时序核心 | 有时序核心 |
|------------|------------|-----------|
| 某节点崩溃 | 整个联邦卡死或数据错乱 | 时序核心检测到,降级或重试 |
| 某节点慢 | 其他节点用过期数据 | 时序核心延迟推进或告警 |
| 网络抖动 | 态势跳跃 | 时序核心插值/外推 |
| 演练暂停 | 无法暂停 | 时序核心冻结全局帧号 |
| 回放分析 | 无法对齐 | 时序核心统一时间戳 |

**结论**: 时序核心是联邦的"指挥官",**必须独立进程**,不能是某个 federate 的子模块。

---

## 2. Master Clock 进程的职责边界

### 2.1 核心职责(必须做)

1. **全局时间源**: 维护 `sim_time`(仿真时间)和 `frame_id`(全局帧号)
2. **推进决策**: 根据模式(实时/逻辑/超实时)决定下一帧何时推进
3. **帧号广播**: 把 `(frame_id, sim_time, wall_clock)` 周期发布
4. **推进许可**: 在逻辑时间模式下,等待所有 federate 报"完成"才推进
5. **健康监控**: 检测 federate 心跳,异常时降级/重启/告警

### 2.2 不做的事(避免膨胀)

- ❌ 不持有任何场景数据(只管时间)
- ❌ 不参与物理计算
- ❌ 不路由态势数据(那是 DDS 的事)
- ❌ 不渲染(VisFed 独立)

**原则**: 时序核心要**极简、极稳、极快**。代码量应控制在 2000 行以内,任何复杂逻辑都外移。

---

## 3. 三种时间推进模式

### 3.1 模式 A: 实时模式(Real-Time, 默认)

```
wall_clock 驱动,sim_time = wall_start + elapsed * time_scale
```

- 适用于: 训练、演示、人在回路
- 推进策略: 每 1/60s 推一帧,无论 federate 是否完成(慢节点用旧帧)
- 优点: 低延迟、直观
- 缺点: 慢节点会被丢帧

```
            wall: 0ms   16.6ms  33.3ms  50ms
            ─────┬───────┬───────┬───────┬────
frame_id:        1       2       3       4
                 │       │       │       │
GroundFed:       ✓───────│       ✓───────│
AirFed:          ✓───────│       ✓───────│
SpaceFed:        ✓──────────────✓──────────  (1Hz, 跳过中间帧)
RTWorker#0:      ✓───────✓───────✓───────✓
RTWorker#1:      ✓───────✗(慢)───✓(用frame2数据)─
```

### 3.2 模式 B: 逻辑时间模式(Stepped, 严格因果)

```
所有 federate 报告 "frame N 完成" → 时序核心推进到 N+1
```

- 适用于: 分析、回放、严格因果要求
- 推进策略: TAR(Time Advance Request) / TAG(Time Advance Grant)
- 优点: 严格一致、可复现
- 缺点: 受最慢节点拖累

```
frame N:
  Master → broadcast "advance_to(N+1)"
  所有 federate 执行 step(N+1)
  各 federate → "ready(N+1)"
  Master 收齐 → broadcast "grant(N+1)"
  所有 federate publish state(N+1)
```

### 3.3 模式 C: 超实时/欠实时模式

```
time_scale = 0.1 (慢放,看清细节)
time_scale = 10  (快进,长期演进)
```

- 适用于: 卫星轨道长期推演、导弹末段慢放分析
- 实现: 实时模式基础上乘以 scale,sim_time 增量 = wall_dt * scale

### 3.4 模式选择

| 场景 | 推荐模式 |
|------|---------|
| 战术训练(人在回路) | A 实时 |
| 武器效能分析 | B 逻辑 |
| 卫星轨道演化 | C 超实时 10x |
| 导弹末段拦截 | C 欠实时 0.1x |
| 故障回放 | B 逻辑 + 录制 |

---

## 4. 同步协议设计

### 4.1 双层同步架构

```
┌─────────────────────────────────────────────────────┐
│  Master Clock 进程                                   │
│  ├── FrameScheduler     (帧号生成, wall→sim)         │
│  ├── AdvanceCoordinator (推进许可, 仅模式B)           │
│  ├── HealthMonitor      (心跳/超时)                  │
│  └── TimePublisher      (DDS/SHM 广播)              │
└────────────────────┬────────────────────────────────┘
                     │ TimeTick PDU (1ms~1s 周期)
        ┌────────────┼────────────┐
        ▼            ▼            ▼
   ┌─────────┐  ┌─────────┐  ┌─────────┐
   │Fed A    │  │Fed B    │  │Fed C    │
   │TickQueue│  │TickQueue│  │TickQueue│
   │  (1帧)  │  │  (1帧)  │  │  (1帧)  │
   └────┬────┘  └────┬────┘  └────┬────┘
        │            │            │
        └────────────┼────────────┘
                     │ ReadyAck (仅模式B)
                     ▼
              Master Clock
```

### 4.2 TimeTick PDU(时序核心→各 federate)

```cpp
struct TimeTick {
    uint64_t frame_id;          // 全局帧号,单调递增
    double   sim_time;          // 仿真时间(秒)
    double   wall_clock;        // 墙钟(秒,用于诊断漂移)
    double   time_scale;        // 当前倍速
    uint8_t  mode;              // 0=real 1=stepped 2=fastforward
    uint8_t  command;           // 0=run 1=pause 2=resume 3=stop
    uint64_t epoch_id;          // 演练批次 ID(用于录制隔离)
    uint64_t expected_fed_mask; // 本帧应参与的 federate 位图
};
```

- 大小: 64 字节,可塞进一个缓存行
- 发布频率: 1ms(快域) ~ 1s(慢域),由最小公倍步长决定
- QoS: DDS RELIABLE + KEEP_LAST 1(只要最新)

### 4.3 ReadyAck(federate→时序核心, 仅模式 B)

```cpp
struct ReadyAck {
    uint64_t frame_id;          // 已完成的帧号
    uint32_t federate_id;
    uint8_t  status;            // 0=ok 1=behind 2=error 3=recovering
    double   local_progress;    // 0~1,本帧进度(诊断用)
};
```

### 4.4 推进决策状态机(模式 B)

```
                  ┌──────────┐
                  │ IDLE     │ ← 初始/暂停
                  └────┬─────┘
                       │ start()
                       ▼
            ┌─────────────────────┐
       ┌───→│ WAIT_ACK(frame=N+1) │
       │    └──────────┬──────────┘
       │               │ 所有 expected fed 报 ready
       │               ▼
       │    ┌─────────────────────┐
       │    │ GRANT(frame=N+1)    │── broadcast TimeTick(command=run, frame=N+1)
       │    └──────────┬──────────┘
       │               │ 发出后
       └───────────────┘
```

超时处理:
- WAIT_ACK 超过 `step_deadline`(默认 2x 期望步长)
- 标记未应答 federate 为 `behind`
- 决策: 降级(让该 fed 用旧帧) / 重试 / 全联邦暂停

---

## 5. 多速率协奏(关键难点)

### 5.1 问题: 不同域时间步差异巨大

| Federate | 自然步长 | 帧率 |
|---------|---------|------|
| AirFed | 1ms | 1000 Hz |
| GroundFed (Bullet3) | 8.33ms | 120 Hz |
| VisFed | 16.6ms | 60 Hz |
| SpaceFed | 1s | 1 Hz |
| AtmosFed | 1s | 1 Hz |

**不能强制统一为 1ms**(浪费)、**也不能统一为 1s**(实时性丢失)。

### 5.2 解决: 主帧 + 子帧分层

```
主帧(major frame): 100ms (10 Hz, 全局同步点)
  │
  ├── 子帧计数:
  │     AirFed:    每 major 跑 100 子步(1ms × 100)
  │     GroundFed: 每 major 跑 12 子步(8.33ms × 12)
  │     VisFed:    每 major 跑 6 子步(16.6ms × 6)
  │     SpaceFed:  每 10 major 跑 1 步(1s)
  │
  └── 主帧边界:
        所有 federate 必须到达主帧边界才能进入下一 major
        (允许模式 B 的严格同步)
```

### 5.3 帧号编码

```cpp
// 64-bit 帧号: 高 32 位 major, 低 32 位 minor
uint64_t encodeFrame(uint32_t major, uint32_t minor) {
    return (uint64_t(major) << 32) | minor;
}

// 每个 federate 知道自己的 minor 步长
struct FedRate {
    uint32_t minor_per_major;  // AirFed=100, GroundFed=12, VisFed=6
    uint32_t major_stride;     // SpaceFed=10 (每 10 major 才推进一次)
};
```

### 5.4 同步点(主帧边界)

```
major frame N 结束:
  Master: broadcast SyncPoint(major=N)
  所有 fed: 完成当前 minor,进入 SyncPoint 等待
  Master: 收齐所有 fed 的 SyncPointAck
  Master: broadcast SyncPointRelease(major=N+1)
  所有 fed: 进入 major N+1
```

**作用**: 防止慢域拖累快域的同时,保证全局周期性一致(每 100ms 对齐一次)。

### 5.5 死区外推(Dead Reckoning)

主帧之间的 minor 步,快域需要慢域的最新位姿。慢域 publish 频率低,快域用 DR 模型外推:

```cpp
struct EntityState {
    uint64_t frame_id;           // 最后更新帧
    vsg::dvec3 position;
    vsg::dvec3 velocity;
    vsg::dvec3 acceleration;
    vsg::quat   orientation;
    vsg::dvec3 angular_velocity;
};

// 快域在 minor 步 k 用 DR 外推慢域位姿:
vsg::dvec3 predictAt(const EntityState& s, double t) {
    double dt = t - s.frame_id * major_dt;
    return s.position + s.velocity * dt + 0.5 * s.acceleration * dt * dt;
}
```

- 误差阈值: 位置 0.5m / 姿态 1°
- 超阈值时,慢域主动发"强制更新"包
- DR 模型按对象类型选择: 车辆=CV(匀速)/飞机=CA(匀加速)/卫星=开普勒

---

## 6. 容错与恢复

### 6.1 心跳与超时

```cpp
struct Heartbeat {
    uint32_t federate_id;
    uint64_t last_frame_processed;
    double   wall_clock;
    uint8_t  load_pct;      // CPU/GPU 占用
    uint8_t  queue_depth;   // 待处理消息数
};
```

- 心跳频率: 1 Hz
- 超时阈值: 3 秒未收到 → 标记 `degraded`
- 5 秒未收到 → 标记 `dead`,从 expected_fed_mask 移除

### 6.2 降级策略

| 异常类型 | 降级动作 |
|---------|---------|
| federate 落后 1 帧 | 继续运行,该 fed 用上一帧数据 |
| federate 落后 > 3 帧 | 时序核心暂停 1 major 帧等待 |
| federate 崩溃 | 从 mask 移除,广播事件 `FedDown` |
| 网络分区 | 时序核心切到"分区降级"模式,只与可达 fed 同步 |
| Master 自己崩溃 | 备份 Master 接管(见 6.4) |

### 6.3 暂停/恢复/快进

```cpp
// 时序核心支持的命令(可来自 GUI 或 gRPC)
enum class TimeCommand {
    RUN,
    PAUSE,           // 冻结 frame_id, 各 fed 完成当前 minor 后停
    RESUME,
    STEP_ONCE,       // 单步推进一帧(调试用)
    SET_SCALE,       // 改 time_scale
    JUMP_TO,         // 跳转到指定 sim_time(需录制系统配合)
    RESET_EPOCH,     // 重置 epoch_id, 清空录制
};
```

### 6.4 Master 高可用

```
Active Master (主)  ──心跳──→  Standby Master (备)
     │                              │
     │  时序状态机                  │  同步镜像
     │  frame_id / epoch / mask     │
     │                              │
     └──────────┬───────────────────┘
                │
        各 federate 同时收两份 TimeTick
        (主优先, 主失联 3s 后切备)
```

- 同步方式: 主每 100ms 把状态序列化发给备
- 切换时间: < 3 秒
- 部署: 主备不同节点(防整机故障)

---

## 7. 时钟漂移与对齐

### 7.1 墙钟同步(跨机)

- 节点间用 **PTP(IEEE 1588)** 对齐墙钟,精度 < 1ms
- 退而求其次: **chrony/NTP**,精度 < 10ms
- 单机多进程: 共享 `CLOCK_MONOTONIC`,无需对齐

### 7.2 仿真时间漂移诊断

每秒一次,各 federate 上报:

```cpp
struct ClockSkewReport {
    uint64_t frame_id;        // 当前处理的帧
    double   sim_time_local;  // 本地认为的 sim_time
    double   wall_local;      // 本地墙钟
    double   master_sim_time; // 收到的 master sim_time
    double   skew_ms;         // = (master_sim_time - sim_time_local) / time_scale * 1000
};
```

- |skew| < 5ms: 正常
- 5ms < |skew| < 50ms: 告警,但不调整
- |skew| > 50ms: 时序核心强制该 fed 重同步(跳到当前 frame)

### 7.3 monotonic 帧号 vs wall clock

**关键设计**: federate 之间**只认 frame_id,不认 wall_clock**。

- frame_id 单调递增,由 Master 独家生成
- wall_clock 仅用于诊断,不参与逻辑判断
- 这样即使墙钟漂移,联邦逻辑仍然一致

---

## 8. 同步到子模块(federate 内部)

### 8.1 federate 收到 TimeTick 后的内部扩散

```
Federate 进程
├── TimeTickReceiver (DDS sub) ──→ 入队 TickQueue (无锁 SPSC)
│
├── FederateMainThread:
│     while (running) {
│       tick = TickQueue.pop();           // 等下一帧
│       if (tick.command == PAUSE) {
│         waitUntilResume();
│         continue;
│       }
│       updateRemoteEntities(tick);       // 从态势总线拉最新位姿
│       step(tick.sim_time, tick.frame_id);
│       publishState(tick.frame_id);
│       if (mode == STEPPED) sendReadyAck(tick.frame_id);
│     }
│
└── 子模块(本 federate 内):
      PhysicsEngine::step()      ← 同步
      SensorScheduler::tick()    ← 同步
      RayTracer::trace()         ← 同步
      SignalProcessor::process() ← 同步
```

### 8.2 federate 内部子模块的并发

federate 内部仍可多线程,但**对外可见的状态必须在帧边界提交**:

```
frame N:
  ┌─ physics_thread:   step physics    ─┐
  ├─ rt_thread:        trace rays       │  并发执行
  ├─ signal_thread:    process sig      │  (用 frame N-1 的输入)
  └─ atmos_thread:     update atmos    ─┘
       
  barrier (帧边界)
  
  commit_state(frame=N)  ← 原子提交,可被其他 fed 读取
```

### 8.3 双缓冲避免撕裂

```cpp
class FederateState {
    std::atomic<StateBuffer*> readable_;   // 读指针
    std::unique_ptr<StateBuffer> buf_[2];  // 双缓冲
public:
    void commit(StateBuffer&& new_state) {
        auto* w = (readable_.load() == buf_[0].get()) ? buf_[1].get() : buf_[0].get();
        *w = std::move(new_state);
        readable_.store(w);  // 原子切换
    }
    const StateBuffer& read() const { return *readable_.load(); }
};
```

---

## 9. 与现有 [Simulation](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/Simulation.h) 的对接

### 9.1 改造点(最小侵入)

```cpp
// 现有:
class Simulation {
    std::size_t run();           // 自驱动循环
    bool step(float dt);         // 内部用
};

// 改造后:
class GroundFederate : public FederateBase, private Simulation {
public:
    bool init(const FederateConfig&) override;
    
    // 由 Master Clock 驱动,替代原 run()
    void step(double sim_time, uint64_t frame_id) override {
        float dt = sim_time - last_sim_time_;
        Simulation::step(dt);   // 复用现有 7 步串行
        last_sim_time_ = sim_time;
    }
    
    void publishState(uint64_t frame_id) override {
        // 把 sensor_mounts_ 的位姿发到态势总线
    }
    
    void onRemoteState(const EntityState& s) override {
        // 远程飞机/卫星位姿注入到本地场景图(代理节点)
    }
    
private:
    double last_sim_time_ = 0;
};
```

### 9.2 关键改动清单

| 文件 | 改动 |
|------|------|
| [Simulation.h](file:///home/mte/FOLD/GitProject/vsgraysimulation/include/vsgraysim/Simulation.h) | `step()` 改 public,新增 frame_id 参数 |
| [Simulation.cpp](file:///home/mte/FOLD/GitProject/vsgraysimulation/src/Simulation.cpp) | 删 `run()` 自循环,改由外部驱动 |
| [main.cpp](file:///home/mte/FOLD/GitProject/vsgraysimulation/main.cpp) | 新增 `--federate ground` 模式,启动 GroundFederate |
| 新增 `federate/MasterClock.cpp` | 时序核心进程 main |
| 新增 `federate/FederateBase.h` | 公共接口 |
| 新增 `ipc/TimeTickPub.h` | 时序广播适配器 |

### 9.3 启动顺序

```
1. Master Clock 启动,等待 federate 注册
2. 各 federate 启动,完成本地 init()
3. 各 federate 向 Master 发 RegisterRequest
4. Master 收齐 expected fed 集合
5. Master 广播 SyncPoint(epoch_start)
6. 各 fed 应答 SyncPointAck
7. Master 进入主循环,广播第一帧 TimeTick
8. 联邦开始推演
```

---

## 10. 性能预算

### 10.1 时序核心自身开销

| 操作 | 预算 |
|------|------|
| 生成 TimeTick | < 10 μs |
| 序列化 + DDS publish | < 100 μs |
| 收 ReadyAck × N fed | < 1 ms (N<32) |
| 决策(是否推进) | < 10 μs |
| 总单帧开销 | < 1.5 ms |

→ Master Clock 进程 CPU 占用应 < 5%

### 10.2 端到端同步延迟

| 路径 | 延迟 |
|------|------|
| Master → fed (DDS 多播) | 0.5-2 ms (同机 SHM < 0.1ms) |
| fed step() | 5-50 ms (视负载) |
| fed publish state | 0.5-2 ms |
| 其他 fed 收到新态势 | 0.5-2 ms |
| **总帧间延迟** | **10-60 ms** → 16-100 Hz 实时可行 |

### 10.3 主帧 100ms 的合理性

- 100ms = 10 Hz 主同步,大部分军事仿真标准(DIS 默认 5Hz)
- 慢域(SpaceFed 1Hz)在 100ms 内只跑 0.1 步,可外推
- 快域(AirFed 1ms)在 100ms 内跑 100 子步,DR 误差可控
- 100ms 内总帧间延迟(60ms)< 100ms,有 40ms 余量

---

## 11. 测试与验证策略

### 11.1 时序核心单元测试

```
test_master_clock:
  ├── test_frame_monotonic          帧号单调
  ├── test_pause_resume             暂停后恢复帧号连续
  ├── test_scale_change             倍速切换平滑
  ├── test_fed_timeout              fed 超时降级
  ├── test_master_failover          主备切换
  └── test_skew_detection           漂移检测
```

### 11.2 联邦级集成测试

```
test_federation_integration:
  ├── 1 master + 3 mock fed (最低延迟)
  ├── 验证: 帧号一致 / 态势一致 / 无死锁
  ├── 注入: 1 fed 慢 50ms,验证降级
  ├── 注入: 1 fed 崩溃,验证移除
  └── 注入: 网络延迟 100ms,验证 DR
```

### 11.3 一致性检查工具

```bash
# 录制所有 TimeTick + ReadyAck,回放检查
./replay_tool --recording federation_20260731.rec \
              --check frame_continuity \
              --check fed_response_time \
              --report
```

---

## 12. 总结:时序核心设计要点

1. **独立进程**: 时序核心必须是独立进程,极简极稳,不掺和其他业务
2. **三层时间模型**: sim_time(逻辑) / frame_id(单调) / wall_clock(诊断)
3. **三种推进模式**: 实时(默认) / 逻辑(严格) / 倍速(分析)
4. **主帧+子帧**: 100ms 主帧全局同步 + 各域自治子帧,解决多速率
5. **死区外推**: 慢域低频 publish + 快域 DR 外推,平衡带宽与精度
6. **双层同步**: 跨进程(DDS/SHM) + 进程内(双缓冲原子提交)
7. **容错**: 心跳 + 降级 + 主备切换,单点故障不致命
8. **现有代码**: `Simulation::step()` 改 public + 外部驱动,即可对接

**核心信条**:
> 时序核心只管时间,不管数据;
> federate 只认 frame_id,不认墙钟;
> 主帧对齐,子帧自治;
> 慢可降级,崩可隔离,错可回放。

---

**文档版本**: v1.0
**更新日期**: 2026-07-31
**依赖**: [多进程架构演进方案](architecture_evolution_multiprocess.md)