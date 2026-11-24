# 时序系统(TimeSystem)完善设计

> 本文档聚焦**时序系统的完整设计**,特别是雷达/传感器的精细时序,
> 以及如何通过时序系统串联所有模块的推演。

---

## 1. 设计目标

时序系统是大系统的"中枢神经",必须解决:

1. **整体推进**: 全局仿真时间如何往前走
2. **多速率协调**: 物理模块 120Hz、雷达 PRF 1kHz、红外 60Hz、卫星 1Hz 如何共存
3. **雷达精细时序**: 快时间(脉冲内)、慢时间(脉冲间)、孔径时间(SAR)三层时间表达
4. **跨模块对齐**: 雷达脉冲发射时刻,平台位姿如何精确插值
5. **事件因果**: 互扰、命中、检测等事件的时序保证
6. **异常容错**: 脉冲丢失、采样溢出、模块落后的处理

---

## 2. 五层时序模型

时序系统采用**五层金字塔**,从粗到细:

```
┌────────────────────────────────────────────────┐
│ L0  演练时序  Epoch      小时~天    演练批次    │  ← 演练开始/结束/重置
├────────────────────────────────────────────────┤
│ L1  主帧时序  Major     100ms     全局同步点   │  ← 所有模块对齐
├────────────────────────────────────────────────┤
│ L2  子帧时序  Minor     1~16ms    模块自治步长  │  ← 各模块按自身频率跑
├────────────────────────────────────────────────┤
│ L3  脉冲时序  Pulse     1μs~1ms   传感器级     │  ← 雷达 PRF、激光脉冲率
├────────────────────────────────────────────────┤
│ L4  采样时序  Sample    10ns~1μs  信号级       │  ← ADC 采样、距离门
└────────────────────────────────────────────────┘
```

### 2.1 各层职责

| 层 | 时间尺度 | 步长 | 主导者 | 用途 |
|----|---------|------|--------|------|
| L0 epoch | 小时~天 | 不可比 | 演练管理员 | 演练隔离、录制分卷 |
| L1 major | 100ms | 固定 | TimeSystem | 全局同步、态势对齐 |
| L2 minor | 1~16ms | 模块自定义 | 各 SimModule | 物理步进、渲染帧 |
| L3 pulse | 1μs~1ms | PRF 倒数 | SensorModule | 脉冲发射/接收 |
| L4 sample | 10ns~1μs | 采样率倒数 | SignalModule | ADC、距离门 |

### 2.2 时间编码

```cpp
// 64-bit 时间戳编码,跨层统一
struct Timestamp {
    uint64_t epoch_id : 16;    // L0: 演练批次(可重置)
    uint64_t major    : 24;    // L1: 主帧号(0~16M,够 19 天 @10Hz)
    uint64_t minor    : 16;    // L2: 子帧号(0~65535)
    uint64_t pulse    : 8;     // L3: 当前主帧内脉冲序号(0~255)
};  // 共 64 bit

// 高精度仿真时间(双精度秒)
double sim_time;  // 从 epoch 开始累计的秒数
```

**关键约定**:
- 跨模块同步用 `Timestamp`
- 物理计算用 `sim_time`(double,精度足够)
- L4 采样级不进 Timestamp,只在 SignalModule 内部用 `sample_index`

---

## 3. 各层时序详细设计

### 3.1 L0 演练时序(Epoch)

```
epoch 启动 → 演练进行 → 演练暂停 → 演练恢复 → 演练结束 → 新 epoch
```

- 一个 epoch 对应一次完整演练(可重置)
- epoch_id 改变时,所有模块清空状态、重新注册实体
- 录制文件按 epoch 分卷,便于回放隔离

### 3.2 L1 主帧时序(Major Frame)

```
主帧边界(每 100ms):
  TimeSystem 广播 SyncPoint(major=N)
  → 所有模块完成当前 minor
  → 各模块 commit 状态到 SharedSceneState
  → 各模块发 SyncPointAck(N)
  → TimeSystem 收齐后广播 TimeTick(major=N+1)
```

**主帧 100ms 的选择依据**:
- DIS 标准 PDU 心跳默认 5Hz(200ms),100ms 留余量
- 卫星轨道 1Hz,100ms 内插值误差 < 1m
- 雷达 PRF 1kHz,100ms 内 100 脉冲足够 RD 处理
- 总帧间延迟 60ms < 100ms,有 40ms 余量

### 3.3 L2 子帧时序(Minor Frame)

各模块按自身步长跑 minor:

| 模块 | minor 步长 | 每 major 子步数 | 备注 |
|------|-----------|---------------|------|
| PhysicsModule | 8.33ms | 12 | Bullet3 120Hz |
| RTModule | 16.6ms | 6 | 60Hz,与渲染对齐 |
| VisModule | 16.6ms | 6 | 60Hz |
| SignalModule | 16.6ms | 6 | 与 RT 同步 |
| SensorModule | 100ms | 1 | 调度级 |
| AtmosModule | 1s | 0.1(每 10 major 一次) | 慢变 |
| OrbitModule | 1s | 0.1 | 慢变 |

**子帧自治原则**:
- 模块在 minor 之间不互相阻塞
- 慢模块的态势由快模块用 DeadReckoning 外推(见 [system_master_design.md §4.6](system_master_design.md))
- 只在主帧边界强制对齐

### 3.4 L3 脉冲时序(Pulse)

这是雷达/激光等主动传感器的核心时间层:

```
1 个 major frame (100ms)
├── pulse 0 (t=0.000ms)
├── pulse 1 (t=1.000ms)  ← PRF=1kHz
├── pulse 2 (t=2.000ms)
├── ...
└── pulse 99 (t=99.000ms)
```

**脉冲时序参数**:

```cpp
struct PulseTiming {
    double prf;              // 脉冲重复频率(Hz),1kHz 典型
    double pulse_width;      // 脉冲宽度(秒),1μs 典型
    double period;           // 1/prf,脉冲间隔
    double duty;             // pulse_width / period,占空比
    uint32_t pulses_per_major;  // 每 major 帧脉冲数 = 100ms × prf
};
```

**关键设计**: 脉冲时序**不进** TimeTick,而是 SensorModule 收到主帧 TimeTick 后,内部自主生成 100 个脉冲事件。

### 3.5 L4 采样时序(Sample)

信号级时间,仅存在于 SignalModule 内部:

```
单个脉冲回波(脉冲宽度 1μs):
├── sample 0 (t=0ns)
├── sample 1 (t=10ns)  ← ADC 采样率 100MHz
├── ...
└── sample 99 (t=990ns)

距离门(range gate):
  sample_k 对应距离 R = c × k × dt_sample / 2
  例:100MHz 采样,1.5m per sample
```

**完全本地化**: L4 不跨模块,只在 SignalModule 内部使用,避免污染全局时序。

---

## 4. 雷达时序的深度设计

### 4.1 三层时间表达(雷达核心)

雷达信号处理涉及**三个时间尺度**,必须严格区分:

```
┌────────────────────────────────────────────────┐
│ 慢时间(slow time)  脉冲间    1ms     方位维   │  ← 多普勒/方位
├────────────────────────────────────────────────┤
│ 快时间(fast time)  脉冲内    1μs     距离维   │  ← 距离/回波
├────────────────────────────────────────────────┤
│ 孔径时间(aperture) SAR 合成  1~10s   合成孔径 │  ← SAR 成像
└────────────────────────────────────────────────┘
```

| 时间 | 物理意义 | 典型尺度 | 数据维 | 处理 |
|------|---------|---------|--------|------|
| 慢时间 | 脉冲间隔 | 1ms (PRF=1kHz) | 方位(慢时间轴) | 多普勒 FFT |
| 快时间 | 脉冲内 | 1μs (脉宽) | 距离(快时间轴) | 距离压缩 |
| 孔径时间 | SAR 合成孔径 | 1~10s | 孔径积累 | 后向投影/FFT |

### 4.2 脉冲发射时序

每个脉冲发射是一个**事件**,精确触发:

```cpp
struct PulseEvent {
    uint64_t pulse_id;           // 全局脉冲号
    uint32_t sensor_id;          // 发射传感器
    double   tx_time;            // 发射精确时刻(sim_time,秒)
    Timestamp tx_timestamp;     // 编码时间戳
    
    vsg::dvec3 tx_position;      // 发射时刻平台位姿(已插值)
    vsg::quat  tx_orientation;
    vsg::dvec3 platform_velocity;// 平台速度(多普勒源)
    
    TxParams  params;             // 功率/波长/波束宽度
    BeamShape beam;               // 发射波束方向图
};
```

**发射时刻的平台位姿精确插值**(关键):

平台位姿由 PhysicsModule 以 120Hz(8.33ms)更新,但脉冲发射以 1kHz(1ms)发生。
发射时刻位姿必须**时间精确插值**:

```cpp
vsg::dvec3 interpolatePose(const EntityState& s_prev, const EntityState& s_curr,
                           double tx_time) {
    double dt_total = s_curr.frame_id * major_dt - s_prev.frame_id * major_dt;
    double alpha = (tx_time - s_prev.frame_id * major_dt) / dt_total;
    // 三次 Hermite 插值(用位置+速度)
    return hermiteInterp(s_prev.position, s_prev.velocity,
                         s_curr.position, s_curr.velocity, alpha);
}
```

**误差容忍**: 1ms 内平台移动 ~0.01m(10m/s 速度),Hermite 插值误差 < 1mm,满足多普勒精度。

### 4.3 回波接收时序

脉冲发射后,回波在 `2R/c` 后到达(R 是目标距离):

```
脉冲发射 t_tx
    │
    │ 电磁波传播(大气积分)
    ▼
目标散射(t_tx + R/c)
    │
    │ 回波传播
    ▼
回波接收(t_tx + 2R/c)
    │
    │ 距离门采样(L4)
    ▼
距离-时间序列 samples[0..N]
```

**距离门对齐**:

```cpp
struct EchoWindow {
    double t_start;        // 接收窗开始 = t_tx + 2*R_min/c
    double t_end;          // 接收窗结束 = t_tx + 2*R_max/c
    double sample_dt;      // 采样间隔 = 1/adc_rate
    uint32_t sample_count; // 采样点数 = (t_end - t_start) / sample_dt
    
    // 每个 sample 对应距离:
    // R(k) = c * (t_start + k*sample_dt - t_tx) / 2
};
```

**例**: R_min=1km, R_max=100km, ADC=100MHz
- t_start = 6.67μs, t_end = 667μs
- sample_count = 66033 个采样点
- 距离分辨率 = c/(2*adc_rate) = 1.5m

### 4.4 脉冲积累时序

单个脉冲信噪比低,需积累多个脉冲:

```
脉冲 0  ──→ echo[0][0..N]   (1 个距离序列)
脉冲 1  ──→ echo[1][0..N]
...
脉冲 M-1 ──→ echo[M-1][0..N]
                │
                ▼
        距离-多普勒矩阵 echo[M][N]
                │
                ▼
        慢时间 FFT(方位维)
                │
                ▼
        RD 图(距离-多普勒图)
```

**积累策略**:

| 积累类型 | 脉冲数 M | 时长 | 适用 |
|---------|---------|------|------|
| 相干积累 | 64~256 | 64~256ms | 多普勒分辨 |
| 非相干积累 | 8~32 | 8~32ms | 检测概率提升 |
| SAR 孔径积累 | 1000~10000 | 1~10s | 合成孔径成像 |

**积累与主帧的关系**:
- 相干积累 256 脉冲 = 256ms = 2.56 个主帧
- 积累跨越主帧边界时,**冻结平台位姿序列**用于多普勒补偿
- 输出 RD 图的频率 = 100ms / 256ms ≈ 0.4Hz(每 2.5 主帧出 1 张 RD 图)

### 4.5 RD 图生成时序

```cpp
struct RDFrame {
    uint64_t frame_id;            // 主帧号
    uint32_t sensor_id;
    double   t_start;             // 积累开始 sim_time
    double   t_end;               // 积累结束 sim_time
    uint32_t pulse_count;         // M
    
    std::vector<std::vector<float>> rd_map;  // [距离门][多普勒通道]
    uint32_t range_bins;          // N
    uint32_t doppler_bins;        // 多普勒 FFT 点数
    
    // 元数据
    double   range_resolution;    // 距离分辨率(m)
    double   doppler_resolution;  // 多普勒分辨率(Hz)
    double   velocity_resolution; // 速度分辨率(m/s) = λ × PRF / (2 × M)
};
```

**输出节奏**: RD 图不是每主帧产生,而是按积累周期产生。SensorModule 维护一个**输出节拍器**:

```cpp
class RDOutputPacer {
    double last_output_time_ = 0;
    double output_period_;     // = M / PRF,例 256ms
    
    bool shouldOutput(double sim_time) {
        if (sim_time - last_output_time_ >= output_period_) {
            last_output_time_ = sim_time;
            return true;
        }
        return false;
    }
};
```

### 4.6 SAR 合成孔径时序

SAR 时序更复杂,涉及**孔径时间**概念:

```
孔径时间 T_aperture = 1~10s(远大于主帧 100ms)
   │
   ├── 飞机/卫星匀速直线运动
   ├── 持续发射脉冲(PRF)
   ├── 每个脉冲的回波按"距离-慢时间"存储
   └── 孔径结束时后向投影成像
```

**SAR 时序状态机**:

```
IDLE ──startAperture()──→ APERTURE_COLLECTING
                              │
                              │ 持续 T_aperture(100~1000 主帧)
                              │ 每主帧收 100 脉冲回波
                              ▼
                          APERTURE_COMPLETE ──backproject()──→ SAR_IMAGE
                              │                                    │
                              │ 输出图像                            │ 下一个孔径
                              ▼                                    ▼
                          IDLE ←────────────────────────────────────
```

**跨主帧状态保持**:
- SAR 必须跨 100+ 个主帧保持回波矩阵
- 用 `ApertureBuffer` 持久化在 SignalModule 内存
- 主帧边界只对齐"孔径推进",不重置数据

### 4.7 多普勒时序耦合

平台运动产生多普勒,时序必须精确耦合:

```cpp
// 多普勒频率 = 2 * v_radial / λ
// v_radial = 平台速度在目标方向的投影
double computeDoppler(const PulseEvent& p, const ScatterPoint& target) {
    vsg::dvec3 to_target = normalize(target.position - p.tx_position);
    double v_radial = dot(p.platform_velocity, to_target);
    return 2.0 * v_radial / p.params.wavelength;
}
```

**时序耦合点**:
1. **发射时刻位姿**(L3)→ 决定波束指向
2. **传播中大气状态**(L1 主帧)→ 影响折射(慢变,主帧级足够)
3. **散射时刻目标位姿**(L3 插值)→ 影响 v_radial
4. **接收时刻平台位姿**(L3 插值)→ 影响波束接收方向图

**关键**: 大气用主帧级(L1),位姿用脉冲级(L3)插值——分层精度匹配物理特性。

---

## 5. 其他传感器时序

### 5.1 红外时序

红外是无源传感器,时序相对简单:

```
红外帧率: 30/60Hz(典型)
   │
   ├── 每帧积分时间: 1~10ms(热积累)
   ├── 读出时间: <1ms
   └── 输出: 热图像
```

- 时序层级: L2(16.6ms,与 RT/Vis 同步)
- 无脉冲概念,直接用主帧级位姿
- 温度场用主帧级更新(热惯性大,ms 级变化可忽略)

### 5.2 激光时序

激光雷达类似雷达,但 PRF 更高:

```
激光 PRF: 10kHz~100kHz
脉冲宽度: 10ns(窄)
距离分辨率: c/(2*B) ≈ cm 级
```

- 时序层级: L3(脉冲)+ L4(光子级采样)
- 100ms 主帧内 1000~10000 脉冲
- 单脉冲即成像(无需积累),但需扫描

### 5.3 可见光时序

```
可见光帧率: 60Hz
曝光时间: 1~16ms
全局快门/滚动快门
```

- 时序层级: L2(16.6ms)
- 运动模糊: 曝光时间内平台位移 → 卷积模糊核
- 与 VisModule 共享 60Hz 节拍

---

## 6. 跨模块时序对齐

### 6.1 平台位姿时间对齐(最关键)

不同模块需要同一时刻的平台位姿,但各模块时间步不同:

```
PhysicsModule 输出位姿(t=0, 8.33, 16.67, 25, ... ms)
RTModule 需要位姿  (t=0, 16.6, 33.3, ... ms)         ← 物理插值
SensorModule 需要位姿(t=0.5, 1.5, 2.5, ... ms, 脉冲级) ← Hermite 插值
```

**统一插值服务**:

```cpp
class PoseInterpolationService {
public:
    // 任何模块查询任意时刻位姿
    EntityState queryAt(uint64_t entity_id, double sim_time);
    
private:
    // 双端缓冲:保留最近 2 个主帧的位姿历史
    struct EntityHistory {
        EntityState samples[4];  // 环形缓冲,4 个采样点
        // 用 Hermite 三次插值(位置+速度)
    };
    std::unordered_map<uint64_t, EntityHistory> histories_;
};
```

**插值策略**:
- 查询时刻在最近两个采样点之间 → Hermite 插值
- 查询时刻超出范围(未来) → DeadReckoning 外推
- 查询时刻在历史范围外(太久以前) → 报错或返回最近

### 6.2 大气状态时间对齐

大气慢变,主帧级足够:

```cpp
class AtmosphereTimeAlign {
public:
    // 脉冲传播路径上的大气状态(主帧级快照)
    AtmosphereSnapshot snapshotAt(double sim_time) {
        // 大气每 1s 更新一次,100ms 内插值
        return interpolate(last_snapshot_, curr_snapshot_, sim_time);
    }
};
```

- 路径积分用主帧级大气快照
- 折射/衰减误差 < 0.1%(100ms 大气变化可忽略)

### 6.3 多传感器并发仲裁

同一主帧内,多个传感器并发发射,RT 资源有限:

```
主帧 N 内脉冲事件:
  sensor 1 (radar X): pulse 0..99 (1kHz)
  sensor 2 (radar C): pulse 0..99 (1kHz, 与 1 错相 0.5ms)
  sensor 3 (laser):   pulse 0..9999 (100kHz)
  sensor 4 (IR):      无脉冲,帧积分
```

**RT 调度策略**:

```cpp
class RTScheduler {
public:
    // 收集主帧内所有脉冲事件,排序后批量调度
    void scheduleMajorFrame(const std::vector<PulseEvent>& pulses) {
        // 按发射时刻排序
        std::sort(pulses.begin(), pulses.end(), 
                  [](auto& a, auto& b){ return a.tx_time < b.tx_time; });
        
        // 合并相近脉冲(< 100μs 间隔)成批次
        auto batches = batchByTime(pulses, 100e-6);
        
        // 每批次一次 RT 调度(Vulkan RT 提交开销摊销)
        for (auto& batch : batches) {
            rtModule_->traceBatch(batch);
        }
    }
};
```

**关键**: RT 不是每脉冲一次,而是**按时间窗批量**,降低 Vulkan 提交开销。

### 6.4 事件因果链

某些事件需要严格时序保证:

```
激光发射(t=10.000ms)
   │
   │ 互扰耦合
   ▼
红外接收干扰(t=10.000ms + 传播延迟)  ← 必须在激光发射之后
   │
   │ 信号处理
   ▼
红外图像输出(t=10.016ms)             ← 必须在接收之后
```

**事件总线**(不同于态势总线):

```cpp
struct SimEvent {
    uint64_t event_id;
    uint64_t cause_event_id;      // 因果父事件(0=根事件)
    double   sim_time;             // 事件发生时刻
    uint32_t source_module;
    uint32_t target_module;
    EventType type;                // PULSE_TX / ECHO_RX / CROSS_INDUCTION / DETECTION
    std::vector<uint8_t> payload;
};

class EventBus {
public:
    void publish(const SimEvent&);
    std::vector<SimEvent> query(double t_start, double t_end);
    // 因果一致性:target 处理事件时,cause 必已完成
};
```

---

## 7. 时序路由器(TimeRouter)

### 7.1 职责

TimeRouter 是 TimeSystem 的核心调度组件,负责把时序事件路由到正确的模块:

```
                  ┌──────────────────┐
                  │   TimeRouter     │
                  └────────┬─────────┘
                           │
        ┌──────────────────┼──────────────────┐
        ▼                  ▼                  ▼
   ┌─────────┐        ┌─────────┐        ┌─────────┐
   │Physics  │        │Sensor   │        │Signal   │
   │Module   │        │Module   │        │Module   │
   └─────────┘        └─────────┘        └─────────┘
```

### 7.2 路由表

```cpp
struct TimeRouteEntry {
    EventType event_type;
    uint32_t  target_module_id;
    uint8_t   priority;        // 0=高 1=中 2=低
    bool      requires_ack;    // 是否需要确认
};

class TimeRouter {
    std::map<EventType, std::vector<TimeRouteEntry>> routes_;
public:
    void route(const SimEvent& event) {
        auto it = routes_.find(event.type);
        if (it != routes_.end()) {
            for (auto& route : it->second) {
                dispatchToModule(route.target_module_id, event);
            }
        }
    }
};
```

### 7.3 典型路由

| 事件 | 路由到 | 优先级 | 需 ACK |
|------|--------|--------|--------|
| TIME_TICK | 所有模块 | 高 | 否(实时模式) |
| SYNC_POINT | 所有模块 | 高 | 是 |
| PULSE_TX | RTModule + CrossInducModule | 高 | 否 |
| ECHO_RX | SignalModule | 中 | 否 |
| CROSS_INDUCTION | 目标 SensorModule | 中 | 否 |
| DETECTION | VisModule + 录制 | 低 | 否 |
| HEALTH_TIMEOUT | HealthMonitor | 高 | 是 |

---

## 8. 时序异常处理

### 8.1 脉冲丢失

某脉冲未能按时发射(模块卡顿):

```cpp
struct PulseGap {
    uint32_t sensor_id;
    uint64_t expected_pulse_id;
    double   expected_tx_time;
    GapReason reason;  // MODULE_BUSY / RT_OVERFLOW / NETWORK
};

class PulseGapHandler {
public:
    void onPulseGap(const PulseGap& gap) {
        // 策略 1: 跳过该脉冲,RD 图标记该慢时间为 0
        // 策略 2: 用上一个脉冲回波填充(短期 gap)
        // 策略 3: 重启积累(长期 gap)
        if (gap.reason == RT_OVERFLOW && gap.duration < 5) {
            fillWithLastEcho(gap);
        } else {
            markInvalidSlowTime(gap);
            if (consecutiveGaps_ > 8) resetCoherentIntegration();
        }
    }
};
```

### 8.2 采样溢出

回波采样数超出缓冲:

```cpp
// 检测:sample_count > buffer_capacity
// 处理:
//   1. 截断远距离门(损失远距目标)
//   2. 告警,建议减小 R_max 或提高 ADC
```

### 8.3 模块落后

模块处理速度跟不上时序:

```
检测: module heartbeat 显示 local_progress < 0.5 持续 3 个主帧
处理:
  ├── 实时模式: 该模块用上一主帧数据,丢弃当前
  ├── 逻辑模式: 全联邦等待(超时后强制推进)
  └── 严重落后: 标记 degraded,从 expected_mask 移除
```

### 8.4 时钟漂移

跨机部署时墙钟漂移:

- PTP 对齐墙钟 < 1ms
- 模块上报 skew,Master 决策是否强制重同步
- 关键: **业务逻辑只看 frame_id**,墙钟漂移只影响实时模式的"实时性",不影响仿真正确性

---

## 9. 时序录制与回放

### 9.1 录制格式

```cpp
struct TimeRecordingHeader {
    uint64_t epoch_id;
    double   start_sim_time;
    double   end_sim_time;
    uint32_t major_frame_count;
    uint32_t module_count;
};

struct TimeRecordingEntry {
    double   sim_time;
    uint64_t frame_id;
    EventType type;
    uint32_t source_module;
    uint32_t target_module;
    uint16_t payload_size;
    // payload follows
};
```

录制内容:
- 所有 TimeTick(主帧级)
- 所有 SyncPoint / SyncPointAck
- 关键事件(PULSE_TX / ECHO_RX / DETECTION)
- 模块心跳

### 9.2 回放策略

```
回放模式:
  ├── 实时回放: 按原墙钟节奏播放(演示)
  ├── 快进回放: 倍速播放(分析)
  ├── 单步回放: 一帧一帧(调试)
  └── 跳转回放: 跳到指定 sim_time(定位)
```

### 9.3 时序分析工具

```bash
# 时序分析 CLI
./time_analyzer --recording rec_20260731.bin \
  --report frame_jitter \
  --report module_latency \
  --report pulse_gaps \
  --report event_causality \
  --output report.html
```

输出:
- 帧抖动直方图
- 各模块响应延迟分布
- 脉冲丢失统计
- 事件因果链可视化

---

## 10. 完整时序流程图

### 10.1 单个主帧内的完整时序

```
T=0ms:   TimeSystem 广播 TimeTick(major=N)
         │
T=0~1ms: 各模块收到 TimeTick
         ├── PhysicsModule: 查 SharedSceneState,准备本帧 12 子步
         ├── SensorModule: 生成 100 脉冲事件(PULSE_TX)
         │   └── 每脉冲精确插值平台位姿(L3)
         └── AtmosModule: 更新大气快照(L1)
         
T=1~80ms: 并发执行
   ├── PhysicsModule: 跑 12 子步(8.33ms × 12)
   │   └── 每 8.33ms commit 一次位姿到 SharedSceneState
   ├── SensorModule + RTModule: 批量处理脉冲
   │   ├── 按时间窗分组(L3)
   │   ├── 每批 RT 调度(散射点云)
   │   └── 输出 ScatterPoint 流
   ├── SignalModule: 处理回波
   │   ├── 距离门采样(L4)
   │   ├── 脉冲积累(L3)
   │   └── 必要时输出 RD 图
   └── VisModule: 60Hz 渲染(L2)
   
T=80ms:  各模块到达主帧边界
         ├── commit 最终状态到 SharedSceneState
         └── 发 SyncPointAck(N)
         
T=80~90ms: TimeSystem 收齐 Ack
           ├── 健康检查(心跳)
           └── 决策推进(实时模式直接推,逻辑模式等 Ack)
           
T=100ms: TimeSystem 广播 TimeTick(major=N+1)
```

### 10.2 雷达一次完整 RD 图生成的时序

```
主帧 N (T=0~100ms):
  ├── 收 100 脉冲(pulse 0~99)
  ├── 每脉冲 RT → ScatterPoint
  └── SignalModule 积累 echo[0..99][0..66033]

主帧 N+1 (T=100~200ms):
  ├── 收 100 脉冲(pulse 100~199)
  └── 积累 echo[100..199][0..66033]

主帧 N+2 (T=200~300ms):
  ├── 收 56 脉冲(pulse 200~255)
  ├── 积累满 256 脉冲
  ├── 慢时间 FFT(256 点)
  └── 输出 RD 图(frame_id=N+2)
```

**关键**: RD 图输出节拍 = 256ms / 100ms ≈ 每 2.5 主帧 1 张,与 VisModule 60Hz 不同步,VisModule 用最近可用 RD 图叠加。

---

## 11. 与现有代码对接

### 11.1 现有 [SensorScheduler](../include/vsgraysim/layer4_sched/SensorScheduler.h) 改造

```cpp
// 现有: 单进程内按 PRR 排序发射
class SensorScheduler {
    void buildFrameSchedule(float dt);
};

// 改造为: 生成 PulseEvent 流,精确时间戳
class SensorModule : public SimModule {
    void onTimeTick(const TimeTick& tick) override {
        // 主帧内生成 pulses_per_major 个脉冲事件
        for (uint32_t i = 0; i < timing_.pulses_per_major; ++i) {
            PulseEvent e;
            e.tx_time = tick.sim_time + i * timing_.period;
            e.tx_timestamp = encodeTimestamp(tick, i);
            e.tx_position = poseService_->queryAt(owner_id, e.tx_time);
            // ... 填充其他字段
            pulse_queue_.push(e);
        }
    }
    
    void step(double sim_time, uint64_t frame_id) override {
        // 派发本步内的脉冲给 RTModule(通过事件总线)
        while (!pulse_queue_.empty() && 
               pulse_queue_.front().tx_time <= sim_time + minor_dt) {
            eventBus_->publish(pulse_queue_.front());
            pulse_queue_.pop();
        }
    }
};
```

### 11.2 现有 [SignalProcessor](../include/vsgraysim/layer3_cuda/SignalProcessor.h) 改造

```cpp
class SignalModule : public SimModule {
    void onEvent(const SimEvent& e) override {
        if (e.type == ECHO_RX) {
            // L4 采样级处理,完全本地
            echo_buffer_[current_pulse_++] = deserialize(e.payload);
            
            // 积累满,触发 RD 处理
            if (current_pulse_ == pulses_for_coherent_integration_) {
                generateRDFrame();
                current_pulse_ = 0;
            }
        }
    }
    
    void generateRDFrame() {
        // 慢时间 FFT
        cuda_->rangeDopplerFFT(echo_buffer_);
        
        // 输出 RD 图
        RDFrame frame;
        frame.frame_id = current_major_frame_;
        eventBus_->publish(DETECTION, frame);
    }
};
```

### 11.3 现有 [CrossInductionDetector](../include/vsgraysim/layer2_rt/CrossInductionDetector.h) 改造

```cpp
// 改造为事件订阅者
class CrossInducModule : public SimModule {
    void onEvent(const SimEvent& e) override {
        if (e.type == PULSE_TX) {
            // 检查互扰路由表
            auto routes = rules_->queryRoutes(e.source_module);
            for (auto& route : routes) {
                // 计算耦合能量,延迟到回波时刻
                SimEvent cross;
                cross.type = CROSS_INDUCTION;
                cross.cause_event_id = e.event_id;
                cross.sim_time = e.sim_time + propagation_delay;
                cross.target_module = route.target_sensor;
                eventBus_->publishDelayed(cross);
            }
        }
    }
};
```

---

## 12. 实施路线

### 阶段 A: 时序基础设施(2 周)

- [ ] 定义 `Timestamp` / `TimeTick` / `SimEvent` 数据结构
- [ ] 实现 `PoseInterpolationService`(Hermite 插值)
- [ ] 实现 `EventBus`(进程内 pub-sub)
- [ ] 现有 [Simulation::step()](../src/Simulation.cpp) 加 frame_id 参数

**验收**: 现有功能不退化

### 阶段 B: 脉冲时序化(2 周)

- [ ] 改造 [SensorScheduler](../include/vsgraysim/layer4_sched/SensorScheduler.h) 生成 `PulseEvent` 流
- [ ] 实现脉冲级平台位姿插值
- [ ] RT 批量调度(按时间窗分组)
- [ ] 验证脉冲时序精度 < 1μs

**验收**: 单传感器 RD 图多普勒精度提升

### 阶段 C: 多普勒/RD 时序(2 周)

- [ ] SignalModule 实现脉冲积累状态机
- [ ] 实现 RD 输出节拍器
- [ ] 跨主帧积累缓冲
- [ ] 验证 RD 图与平台运动一致

**验收**: 平台运动时 RD 图多普勒频移正确

### 阶段 D: SAR 时序(3 周)

- [ ] ApertureBuffer 跨主帧状态保持
- [ ] SAR 孔径状态机
- [ ] 后向投影成像
- [ ] 验证 SAR 图像质量

### 阶段 E: 跨模块对齐(2 周)

- [ ] 大气时间对齐服务
- [ ] 多传感器并发仲裁
- [ ] 事件因果链
- [ ] 异常处理(脉冲丢失/采样溢出/模块落后)

### 阶段 F: 录制与回放(2 周)

- [ ] 时序录制格式
- [ ] 回放引擎
- [ ] 时序分析工具

---

## 13. 关键设计信条

1. **五层时序金字塔**: epoch/major/minor/pulse/sample,各层职责清晰
2. **业务只认 frame_id**: 墙钟仅诊断,不参与逻辑判断
3. **分层精度匹配物理**: 大气用主帧级,位姿用脉冲级,采样用信号级
4. **雷达三层时间**: 慢时间(方位)/快时间(距离)/孔径时间(SAR)严格分离
5. **脉冲级位姿插值**: Hermite 三次插值,误差 < 1mm
6. **RT 批量调度**: 按时间窗合并脉冲,降低 Vulkan 提交开销
7. **积累跨主帧**: RD/SAR 输出节拍与主帧不同步,VisModule 用最近可用
8. **事件因果链**: 互扰/检测事件通过 EventBus 保证时序
9. **异常降级不崩溃**: 脉冲丢失/模块落后自动降级,不拖垮全系统
10. **录制可回放**: 所有时序事件可录制,支持实时/快进/单步回放

---

**文档版本**: v1.0
**更新日期**: 2026-07-31
**依赖**: [system_master_design.md](system_master_design.md)、[master_clock_design.md](master_clock_design.md)