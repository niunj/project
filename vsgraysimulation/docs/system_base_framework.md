# 仿真大系统统一框架设计 v3.1

> 基于"真实世界自循环"隐喻的面向对象系统基类设计。
> 所有系统(无论大小)继承自 SystemBase,具有统一的生命周期、时序、自循环、通信能力。
>
> **v2.0 新增**:
> - 通信架构可配置(单进程/多进程/跨机,Transport 抽象)
> - 数据格式可配置(JSON/XML/二进制/Protobuf/FlatBuffer,Serializer 抽象)
> - 系统自定义状态机/频率模式/运行机制
>
> **v3.0 新增**:
> - 计算后端可配置(CPU/CUDA/Vulkan,系统级选择)
> - CPU 亲和性(核心绑定 + NUMA 感知)
> - GPU 设备管理(多 GPU 分布 + 显存管理)
> - 跨设备数据传输(CPU↔GPU 零拷贝/异步)
>
> **v3.1 新增**:
> - 三种部署模式明确:单进程 / 多进程单机 / 多进程多机
> - 多机节点发现与注册(NodeRegistry)
> - 分布式 MasterClock(主备切换 + 选举)
> - 跨机系统树同步与消息路由
> - 网络分区容错与节点故障恢复

---

## 1. 设计哲学:真实世界隐喻

### 1.1 核心思想

把仿真系统想象成**真实世界的物体集合**:

| 真实世界概念 | 系统对应 | 说明 |
|------------|---------|------|
| 宇宙时间 | MasterClock | 全局唯一时间源 |
| 一个物体 | SystemBase | 任何系统都是"物体" |
| 物体的心跳 | onTick() 自循环 | 按自身节奏运行 |
| 物体含子部件 | 树形组合(Composite) | 坦克含发动机、炮塔、传感器 |
| 物体间相互作用 | 消息总线(MessageBus) | 通过"物理作用"通信 |
| 物体的固有频率 | TickStrategy | 坦克 120Hz、雷达 1kHz、卫星 1Hz |
| 物体的状态 | 自定义状态机 | 出生/运行/暂停/死亡 + 业务状态 |
| 物体间通信介质 | Transport | 空气/真空/导线 → 进程内/SHM/DDS |
| 通信信号编码 | Serializer | 声波/电磁波 → JSON/二进制/Protobuf |
| 物体的"大脑" | ComputeBackend | 神经系统 → CPU/GPU/Vulkan |
| 物体的工作位置 | Deployment | 物理位置 → 单进程/多进程/跨机 |

### 1.2 v3.1 设计目标(增强)

1. **统一抽象**: 从整个仿真器到单个传感器,都用 SystemBase 表达
2. **树形组合**: 大系统 = 子系统的递归组合(Composite 模式)
3. **自循环自治**: 每个系统自定义频率策略、状态机、运行机制
4. **消息通信**: 系统间通过频道发布/订阅,松耦合
5. **通信架构可配置**: 单进程/多进程/跨机,Transport 后端可切换
6. **数据格式可配置**: JSON/XML/二进制/Protobuf/FlatBuffer,频道级配置
7. **三种部署模式**: 单进程 / 多进程单机 / 多进程多机,配置驱动,代码不变
8. **计算后端可配置**: CPU/CUDA/Vulkan,系统级选择,多 GPU 分布
9. **CPU 亲和性**: 核心绑定 + NUMA 感知,避免跨 NUMA 内存访问
10. **多机节点管理**: 节点发现/注册/健康监控/故障恢复
11. **分布式时序**: MasterClock 主备切换,网络分区容错
12. **全面可配置**: 系统树、参数、连接、通信、序列化、部署、计算全部配置驱动
13. **可扩展**: 新增系统/Transport/Serializer/ComputeBackend 只需注册,不改框架

---

## 2. SystemBase 核心抽象(v3.0 增强)

### 2.1 v3.0 增强点

| 能力 | v1.0 | v2.0 | v3.0 |
|------|------|------|------|
| 状态 | 固定 6 态枚举 | 自定义状态机 | 同 v2.0 |
| 频率 | 固定 tick_rate | TickStrategy 策略 | 同 v2.0 |
| 运行机制 | onTick 钩子 | 自定义运行机制 | 同 v2.0 |
| 通信 | 内置 MessageBus | Transport 可配置 | 同 v2.0 |
| 数据格式 | 内置序列化 | Serializer 可配置 | 同 v2.0 |
| **部署模式** | 单进程 | 单进程 | **单进程/多进程/混合可切换** |
| **计算后端** | CPU only | CPU only | **CPU/CUDA/Vulkan 可配置** |
| **CPU 亲和性** | 无 | 无 | **核心绑定 + NUMA 感知** |
| **GPU 设备** | 无 | 无 | **多 GPU 分布 + 显存管理** |

### 2.2 SystemBase 类图(v3.0)

```
┌───────────────────────────────────────────────────────────────┐
│                        SystemBase                              │
├───────────────────────────────────────────────────────────────┤
│  身份: id / name / type                                       │
│  树形: parent_ / children_                                    │
│  时序: tick_strategy_ (TickStrategy)  ← v2.0 策略对象          │
│  状态: state_machine_ (StateMachine)  ← v2.0 自定义状态机      │
│  通信: transport_ (ITransport)         ← v2.0 通信后端         │
│  序列化: serializer_ (ISerializer)      ← v2.0 数据编码        │
│  计算: compute_backend_ (IComputeBackend) ← v3.0 计算后端     │
│  部署: deployment_ (DeploymentConfig)   ← v3.0 部署配置        │
├───────────────────────────────────────────────────────────────┤
│  生命周期(子类实现):                                         │
│    virtual bool onInit(Config)        = 0                     │
│    virtual void onStart()             {}                      │
│    virtual void onTick(TimeTick)      = 0  ← 自循环核心       │
│    virtual void onStop()              {}                      │
│    virtual void onCustomState(int)    {}  ← v2.0 自定义状态    │
│  通信(子类可重写):                                           │
│    virtual void onMessage(Message&)   {}                      │
│  运行机制(v2.0 子类可重写):                                 │
│    virtual TickDecision decideTick(TimeTick)  ← 自定义决策     │
│    virtual void onSubStep(TimeTick, int)      ← 子步长         │
│    virtual void onBatch(const std::vector<TimeTick>&) ← 批处理 │
│  计算(v3.0 子类可重写):                                     │
│    virtual ComputeRequirement computeReq() const ← 声明计算需求│
└───────────────────────────────────────────────────────────────┘
```

### 2.3 SystemBase 核心定义(v2.0)

```cpp
// ============ 基础类型 ============
using SystemID = uint64_t;
using ChannelID = uint64_t;
using StateID = int32_t;  // v2.0: 状态 ID,可扩展

// v2.0: 预定义状态(可扩展自定义)
namespace SystemStates {
    constexpr StateID CREATED     = 0;
    constexpr StateID INITIALIZED = 1;
    constexpr StateID RUNNING     = 2;
    constexpr StateID PAUSED      = 3;
    constexpr StateID STOPPED     = 4;
    constexpr StateID ERROR_STATE = 5;
    constexpr StateID CUSTOM_BASE = 100;  // 自定义状态从此开始
}

enum class SystemType {
    ROOT, TIME, SCENE, PHYSICS, SENSOR, SIGNAL, RT, VISUAL,
    ATMOSPHERE, ORBIT, CUSTOM
};

// ============ 时间 ============
struct TimeTick {
    double   sim_time;       // 全局仿真时间(秒)
    uint64_t frame_id;       // 全局帧号
    double   dt;             // 本次步长
    double   wall_clock;     // 墙钟(诊断)
    SystemID source;         // 触发者
};

// v2.0: tick 决策结果
enum class TickDecision {
    SKIP,           // 本次跳过(频率未到)
    EXECUTE,        // 执行 onTick
    EXECUTE_SUBSTEPS, // 执行子步长(调 onSubStep 多次)
    DEFER,          // 延迟到下次(批处理模式)
    ERROR_ABORT     // 错误,中止
};

// ============ 消息 ============
struct Message {
    ChannelID channel;
    SystemID  source;
    SystemID  target;        // 0 = 广播
    double    sim_time;
    uint32_t  type;
    
    // v2.0: 载荷用变体类型,支持多种编码
    enum class PayloadFormat : uint8_t {
        JSON = 0,
        XML = 1,
        BINARY = 2,
        PROTOBUF = 3,
        FLATBUFFER = 4,
        YAML = 5,
        RAW_POINTER = 99  // 进程内零拷贝
    };
    PayloadFormat format;
    std::vector<uint8_t> payload;  // 序列化后的字节
};

// ============ 配置 ============
struct SystemConfig {
    std::string name;
    std::string type_name;          // v2.0: 字符串类型名(工厂用)
    SystemType  type;
    
    // v2.0: 频率策略配置
    std::string tick_strategy;      // "fixed" / "event" / "adaptive" / "multi_rate" / "custom"
    double      tick_rate;          // fixed 模式的频率
    nlohmann::json tick_params;     // 策略特定参数
    
    // v2.0: 状态机配置
    nlohmann::json state_machine;   // 自定义状态转移表
    
    // v2.0: 通信配置
    std::string transport;          // "in_process" / "shm" / "dds" / "grpc" / "zmq"
    nlohmann::json transport_params;
    
    // v2.0: 序列化配置(频道级)
    std::string default_serializer; // "json" / "xml" / "binary" / "protobuf" / "flatbuffer"
    std::map<std::string, std::string> channel_serializers;  // 频道覆盖
    
    // v3.0: 部署配置
    std::string deployment;        // "in_process" / "separate_process" / "remote_node"
    nlohmann::json deployment_params;
    
    // v3.0: 计算后端配置
    std::string compute_backend;   // "cpu" / "cuda" / "vulkan" / "auto"
    nlohmann::json compute_params; // {device_id, cpu_cores, numa_node, memory_limit}
    
    // 通用
    nlohmann::json params;
    std::vector<std::string> subscriptions;
    std::vector<std::string> publishes;
    std::vector<SystemConfig> children;
};
```

### 2.4 v3.0 五大可配置组件

SystemBase 持有五个策略对象,均可配置可替换:

```cpp
class SystemBase {
public:
    // ... 身份、树形 ...
    
protected:
    // v2.0: 通信与序列化
    std::unique_ptr<TickStrategy>    tick_strategy_;     // 频率策略
    std::unique_ptr<StateMachine>    state_machine_;     // 状态机
    std::shared_ptr<ITransport>      transport_;         // 通信后端
    std::shared_ptr<ISerializer>     serializer_;        // 数据编码
    
    // v3.0: 计算与部署
    std::shared_ptr<IComputeBackend> compute_backend_;   // 计算后端(CPU/GPU)
    DeploymentConfig                 deployment_;        // 部署模式
    
    // ... 生命周期钩子 ...
};

// 框架方法 v2.0
void SystemBase::tick(const TimeTick& tick) {
    if (!isInState(SystemStates::RUNNING)) return;
    
    // v2.0: 委托给策略对象决策
    auto decision = tick_strategy_->decide(tick);
    
    switch (decision.action) {
        case TickDecision::SKIP:
            break;
        case TickDecision::EXECUTE:
            onTick(tick);
            tick_strategy_->afterExecute(tick);
            break;
        case TickDecision::EXECUTE_SUBSTEPS:
            for (int i = 0; i < decision.substep_count; ++i) {
                onSubStep(tick, i);
            }
            break;
        case TickDecision::DEFER:
            tick_strategy_->deferToBuffer(tick);
            break;
        case TickDecision::ERROR_ABORT:
            transitionTo(SystemStates::ERROR_STATE);
            return;
    }
    
    tickChildren(tick);
}
```

---

## 3. 自定义状态机(v2.0 新增)

### 3.1 设计动机

v1.0 只有 6 个固定状态,无法表达业务状态(如雷达的 STANDBY/SEARCHING/TRACKING/SAR_COLLECTING)。

v2.0 提供**可配置状态机**,支持:
- 预定义生命周期状态(0~99)
- 自定义业务状态(100+)
- 状态转移表(配置驱动)
- 状态进入/退出回调

### 3.2 StateMachine 实现

```cpp
class StateMachine {
public:
    using TransitionCallback = std::function<void(StateID from, StateID to)>;
    
    // 配置状态转移表
    void addTransition(StateID from, StateID to, 
                       TransitionCallback on_enter = nullptr,
                       TransitionCallback on_exit = nullptr);
    
    // 当前状态
    StateID current() const;
    bool isInState(StateID s) const;
    
    // 状态转换(必须符合转移表,否则失败)
    bool transitionTo(StateID target);
    
    // v2.0: 从 JSON 加载状态机定义
    void loadFromConfig(const nlohmann::json& cfg);
    
private:
    StateID current_ = SystemStates::CREATED;
    std::map<std::pair<StateID, StateID>, TransitionInfo> transitions_;
    std::map<StateID, std::string> state_names_;  // 调试用
};
```

### 3.3 雷达状态机示例

```json
{
    "state_machine": {
        "states": [
            { "id": 0,   "name": "CREATED" },
            { "id": 1,   "name": "INITIALIZED" },
            { "id": 2,   "name": "RUNNING" },
            { "id": 100, "name": "STANDBY" },
            { "id": 101, "name": "SEARCHING" },
            { "id": 102, "name": "TRACKING" },
            { "id": 103, "name": "SAR_COLLECTING" },
            { "id": 104, "name": "SAR_PROCESSING" }
        ],
        "transitions": [
            { "from": 0,   "to": 1,   "trigger": "init_complete" },
            { "from": 1,   "to": 100, "trigger": "enter_standby" },
            { "from": 100, "to": 101, "trigger": "start_search" },
            { "from": 101, "to": 102, "trigger": "target_detected" },
            { "from": 102, "to": 101, "trigger": "target_lost" },
            { "from": 101, "to": 103, "trigger": "start_sar" },
            { "from": 103, "to": 104, "trigger": "aperture_complete" },
            { "from": 104, "to": 101, "trigger": "image_output" }
        ]
    }
}
```

```cpp
class RadarSystem : public SystemBase {
    bool onInit(const SystemConfig& cfg) override {
        state_machine_->loadFromConfig(cfg.params["state_machine"]);
        state_machine_->addTransition(101, 102, [this](auto,auto){ onTargetDetected(); });
        return true;
    }
    
    void onCustomState(StateID state) override {
        switch (state) {
            case 101: /* SEARCHING */ break;
            case 102: /* TRACKING */ break;
            case 103: /* SAR_COLLECTING */ startAperture(); break;
        }
    }
    
    void onTick(const TimeTick& tick) override {
        if (state_machine_->isInState(103)) {
            collectSARPulse(tick);  // SAR 孔径积累
        }
    }
};
```

---

## 4. TickStrategy 频率策略(v2.0 新增)

### 4.1 设计动机

v1.0 只有固定 tick_rate,无法表达:
- **事件驱动**(RT 系统脉冲来才处理)
- **自适应频率**(根据负载动态调整)
- **多速率**(同一系统不同任务不同频率)
- **子步长**(Bullet3 物理 max_substeps)
- **批处理**(RT 按时间窗合并)

v2.0 提供 **TickStrategy 策略对象**,可配置可自定义。

### 4.2 TickStrategy 接口

```cpp
struct TickDecision {
    TickDecision action;
    int substep_count;       // EXECUTE_SUBSTEPS 时使用
    double substep_dt;       // 子步长
    std::string reason;      // 决策原因(诊断)
};

class TickStrategy {
public:
    virtual ~TickStrategy() = default;
    virtual TickDecision decide(const TimeTick& tick) = 0;
    virtual void afterExecute(const TimeTick& tick) {}
    virtual void deferToBuffer(const TimeTick& tick) {}
    virtual void loadFromConfig(const nlohmann::json& cfg) = 0;
};
```

### 4.3 内置策略

#### 4.3.1 FixedRateStrategy(固定频率)

```cpp
class FixedRateStrategy : public TickStrategy {
public:
    void loadFromConfig(const nlohmann::json& cfg) override {
        rate_ = cfg.value("tick_rate", 60.0);
        period_ = 1.0 / rate_;
    }
    TickDecision decide(const TimeTick& tick) override {
        if (tick.sim_time - last_tick_ >= period_ - 1e-9) {
            last_tick_ = tick.sim_time;
            return {TickDecision::EXECUTE, 0, 0, "fixed_rate"};
        }
        return {TickDecision::SKIP, 0, 0, "not_due"};
    }
private:
    double rate_, period_, last_tick_ = 0;
};
```

#### 4.3.2 EventDrivenStrategy(事件驱动)

```cpp
class EventDrivenStrategy : public TickStrategy {
public:
    TickDecision decide(const TimeTick& tick) override {
        if (!events_.empty()) {
            return {TickDecision::EXECUTE, 0, 0, "event_pending"};
        }
        return {TickDecision::SKIP, 0, 0, "no_event"};
    }
    void pushEvent(const Event& e) { events_.push(e); }
private:
    std::queue<Event> events_;
};
```

#### 4.3.3 SubStepStrategy(子步长,Bullet3 风格)

```cpp
class SubStepStrategy : public TickStrategy {
public:
    void loadFromConfig(const nlohmann::json& cfg) override {
        fixed_dt_ = cfg.value("fixed_dt", 1.0/120.0);
        max_substeps_ = cfg.value("max_substeps", 4);
    }
    TickDecision decide(const TimeTick& tick) override {
        accumulated_ += tick.dt;
        int substeps = std::min(int(accumulated_ / fixed_dt_), max_substeps_);
        if (substeps > 0) {
            accumulated_ -= substeps * fixed_dt_;
            return {TickDecision::EXECUTE_SUBSTEPS, substeps, fixed_dt_, "substep"};
        }
        return {TickDecision::SKIP, 0, 0, "no_substep"};
    }
private:
    double fixed_dt_, accumulated_ = 0;
    int max_substeps_;
};
```

#### 4.3.4 BatchStrategy(批处理,RT 风格)

```cpp
class BatchStrategy : public TickStrategy {
public:
    void loadFromConfig(const nlohmann::json& cfg) override {
        batch_window_ = cfg.value("batch_window_us", 100) * 1e-6;
        max_batch_ = cfg.value("max_batch", 32);
    }
    TickDecision decide(const TimeTick& tick) override {
        batch_.push_back(tick);
        if (batch_.size() >= max_batch_ || 
            (tick.sim_time - batch_.front().sim_time) >= batch_window_) {
            return {TickDecision::EXECUTE, 0, 0, "batch_ready"};
        }
        return {TickDecision::DEFER, 0, 0, "batching"};
    }
private:
    double batch_window_;
    int max_batch_;
    std::vector<TimeTick> batch_;
};
```

#### 4.3.5 AdaptiveStrategy(自适应)

```cpp
class AdaptiveStrategy : public TickStrategy {
public:
    void loadFromConfig(const nlohmann::json& cfg) override {
        min_rate_ = cfg.value("min_rate", 10);
        max_rate_ = cfg.value("max_rate", 1000);
        target_load_ = cfg.value("target_load", 0.8);
    }
    TickDecision decide(const TimeTick& tick) override {
        // 根据上次 tick 耗时调整频率
        double load = last_tick_duration_ / period_;
        if (load > target_load_ && rate_ > min_rate_) {
            rate_ *= 0.9;  // 降频
        } else if (load < target_load_ * 0.5 && rate_ < max_rate_) {
            rate_ *= 1.1;  // 升频
        }
        period_ = 1.0 / rate_;
        // ... 判断是否到点 ...
    }
    void afterExecute(const TimeTick& tick) override {
        last_tick_duration_ = wallNow() - tick.wall_clock;
    }
private:
    double min_rate_, max_rate_, rate_, period_;
    double target_load_, last_tick_duration_ = 0;
};
```

#### 4.3.6 MultiRateStrategy(多速率)

```cpp
class MultiRateStrategy : public TickStrategy {
public:
    void loadFromConfig(const nlohmann::json& cfg) override {
        for (auto& task : cfg["tasks"]) {
            tasks_.push_back({
                task["name"],
                task["rate"],
                0  // last_tick
            });
        }
    }
    TickDecision decide(const TimeTick& tick) override {
        // 返回所有到点任务的列表(需扩展 TickDecision)
        // 简化:任一任务到点则 EXECUTE
        for (auto& t : tasks_) {
            if (tick.sim_time - t.last_tick >= 1.0/t.rate) {
                t.last_tick = tick.sim_time;
                due_tasks_.push_back(t.name);
            }
        }
        if (!due_tasks_.empty()) {
            return {TickDecision::EXECUTE, 0, 0, "multi_rate"};
        }
        return {TickDecision::SKIP, 0, 0, "none_due"};
    }
private:
    struct Task { std::string name; double rate; double last_tick; };
    std::vector<Task> tasks_;
    std::vector<std::string> due_tasks_;
};
```

### 4.4 策略工厂与配置

```cpp
class TickStrategyFactory {
public:
    static std::unique_ptr<TickStrategy> create(const std::string& name) {
        if (name == "fixed")     return std::make_unique<FixedRateStrategy>();
        if (name == "event")     return std::make_unique<EventDrivenStrategy>();
        if (name == "substep")   return std::make_unique<SubStepStrategy>();
        if (name == "batch")     return std::make_unique<BatchStrategy>();
        if (name == "adaptive")  return std::make_unique<AdaptiveStrategy>();
        if (name == "multi_rate") return std::make_unique<MultiRateStrategy>();
        return nullptr;
    }
};

// 自定义策略注册
#define REGISTER_TICK_STRATEGY(ClassName, Name) \
    static bool reg_##ClassName = [] { \
        TickStrategyFactory::registerType(Name, []{ \
            return std::make_unique<ClassName>(); \
        }); \
        return true; \
    }();
```

### 4.5 配置示例

```json
{
    "name": "PhysicsSystem",
    "type": "PHYSICS",
    "tick_strategy": "substep",
    "tick_params": {
        "fixed_dt": 0.00833,
        "max_substeps": 4
    }
},
{
    "name": "RTSystem",
    "type": "RT",
    "tick_strategy": "batch",
    "tick_params": {
        "batch_window_us": 100,
        "max_batch": 32
    }
},
{
    "name": "RadarSystem",
    "type": "RADAR",
    "tick_strategy": "fixed",
    "tick_params": { "tick_rate": 1000 }
},
{
    "name": "AdaptiveLoader",
    "type": "CUSTOM",
    "tick_strategy": "adaptive",
    "tick_params": {
        "min_rate": 10, "max_rate": 1000, "target_load": 0.8
    }
}
```

---

## 5. 通信架构:Transport 可配置(v2.0 新增)

### 5.1 设计动机

v1.0 通信固定为进程内 MessageBus,无法支持:
- 多进程间通信(SHM)
- 跨机通信(DDS/gRPC)
- 不同通信模式(发布/订阅 vs 请求/响应)

v2.0 抽象 **ITransport** 接口,通信后端可配置可替换。

### 5.2 ITransport 接口

```cpp
// 通信模式
enum class TransportMode {
    PUB_SUB,        // 发布/订阅(默认,广播)
    REQ_REP,        // 请求/响应(同步调用)
    PIPELINE,       // 管道(单向流)
    PAIR            // 双向点对点
};

// QoS 配置
struct TransportQoS {
    enum class Reliability { BEST_EFFORT, RELIABLE };
    enum class Durability { VOLATILE, TRANSIENT, PERSISTENT };
    
    Reliability reliability = Reliability::RELIABLE;
    Durability  durability  = Durability::VOLATILE;
    int         history_depth = 1;        // KEEP_LAST N
    int         priority = 0;
    double      deadline_sec = 0;         // 0 = 无限
};

class ITransport {
public:
    virtual ~ITransport() = default;
    
    // 生命周期
    virtual bool init(const nlohmann::json& params) = 0;
    virtual void shutdown() = 0;
    
    // 频道管理
    virtual void createChannel(const std::string& name, 
                               TransportMode mode = TransportMode::PUB_SUB,
                               TransportQoS qos = {}) = 0;
    
    // 通信
    virtual void publish(const std::string& channel, const Message& msg) = 0;
    virtual void subscribe(const std::string& channel, SystemBase* subscriber) = 0;
    virtual void unsubscribe(const std::string& channel, SystemBase* subscriber) = 0;
    
    // 同步请求(REQ_REP 模式)
    virtual Message request(const std::string& channel, 
                            const Message& req, double timeout_sec = 1.0) = 0;
    
    // 元信息
    virtual std::string backendName() const = 0;
    virtual TransportStats stats() const = 0;  // 吞吐/延迟/丢包
};
```

### 5.3 内置 Transport 实现

#### 5.3.1 InProcessTransport(单进程,默认)

```cpp
class InProcessTransport : public ITransport {
public:
    void publish(const std::string& channel, const Message& msg) override {
        // v2.0: 零拷贝优化(同进程内直接传指针)
        Message zero_copy = msg;
        if (msg.format == Message::PayloadFormat::RAW_POINTER) {
            // 直接传 vector 的 move,避免序列化
        }
        auto it = subscribers_.find(channel);
        if (it != subscribers_.end()) {
            for (auto* sub : it->second) {
                sub->onMessage(msg);  // 同步调用
            }
        }
    }
private:
    std::unordered_map<std::string, std::vector<SystemBase*>> subscribers_;
};
```

**特点**: 零延迟、零拷贝、无序列化开销(用 RAW_POINTER)。

#### 5.3.2 SharedMemoryTransport(单机多进程)

```cpp
class SharedMemoryTransport : public ITransport {
public:
    bool init(const nlohmann::json& params) override {
        shm_name_ = params.value("shm_name", "vsgraysim_shm");
        shm_size_ = params.value("shm_size_mb", 256) * 1024 * 1024;
        shm_ = std::make_unique<SharedMemory>(shm_name_, shm_size_);
        ring_buffer_ = shm_->getRingBuffer();
        return true;
    }
    
    void publish(const std::string& channel, const Message& msg) override {
        // 序列化 + 写入共享内存环形缓冲
        auto bytes = serialize(msg);
        ring_buffer_->write(channel, bytes);
        // 通知订阅进程(eventfd 或 futex)
        notifySubscribers(channel);
    }
    
    void subscribe(const std::string& channel, SystemBase* subscriber) override {
        // 启动后台线程轮询共享内存
        startPoller(channel, subscriber);
    }
private:
    std::unique_ptr<SharedMemory> shm_;
    RingBuffer* ring_buffer_;
};
```

**特点**: 跨进程、低延迟(<100μs)、需序列化。

#### 5.3.3 DDSTransport(跨机)

```cpp
class DDSTransport : public ITransport {
public:
    bool init(const nlohmann::json& params) override {
        domain_id_ = params.value("domain_id", 0);
        // 初始化 FastDDS / CycloneDDS
        participant_ = createDDSParticipant(domain_id_);
        return participant_ != nullptr;
    }
    
    void createChannel(const std::string& name, TransportMode mode, 
                       TransportQoS qos) override {
        // 创建 DDS Topic + DataWriter + DataReader
        auto topic = createTopic(name);
        writers_[name] = createDataWriter(topic, toDDSQoS(qos));
        readers_[name] = createDataReader(topic, toDDSQoS(qos));
    }
    
    void publish(const std::string& channel, const Message& msg) override {
        auto bytes = serialize(msg);
        writers_[channel]->write(bytes);
    }
private:
    DDSParticipant* participant_;
    std::map<std::string, DDSDataWriter*> writers_;
    std::map<std::string, DDSDataReader*> readers_;
};
```

**特点**: 跨机、自动发现、QoS 丰富、需 DDS 中间件。

#### 5.3.4 GrpcTransport(跨机,请求/响应)

```cpp
class GrpcTransport : public ITransport {
    // 适合 REQ_REP 模式,如查询 EntityRegistry
    bool init(const nlohmann::json& params) override {
        endpoint_ = params.value("endpoint", "localhost:50051");
        channel_ = grpc::CreateChannel(endpoint_, grpc::InsecureChannelCredentials());
        return true;
    }
    
    Message request(const std::string& channel, 
                    const Message& req, double timeout) override {
        // gRPC 同步调用
        auto stub = SimService::NewStub(channel_);
        // ...
    }
};
```

#### 5.3.5 ZeroMQTransport(灵活)

```cpp
class ZeroMQTransport : public ITransport {
    // 支持 PUB/SUB、REQ/REP、PUSH/PULL 等多种模式
    bool init(const nlohmann::json& params) override {
        context_ = std::make_unique<zmq::context_t>(1);
        // 根据 params 创建 socket
    }
};
```

### 5.4 Transport 工厂与配置

```cpp
class TransportFactory {
public:
    static std::shared_ptr<ITransport> create(const std::string& name) {
        if (name == "in_process") return std::make_shared<InProcessTransport>();
        if (name == "shm")        return std::make_shared<SharedMemoryTransport>();
        if (name == "dds")        return std::make_shared<DDSTransport>();
        if (name == "grpc")       return std::make_shared<GrpcTransport>();
        if (name == "zmq")        return std::make_shared<ZeroMQTransport>();
        return nullptr;
    }
};

#define REGISTER_TRANSPORT(ClassName, Name) \
    static bool reg_##ClassName = [] { \
        TransportFactory::registerType(Name, []{ \
            return std::make_shared<ClassName>(); \
        }); \
        return true; \
    }();
```

### 5.5 混合部署示例

不同频道可用不同 Transport(同一系统内):

```json
{
    "name": "HybridSystem",
    "transport": {
        "default": "in_process",
        "channels": {
            "scene.state":     { "transport": "shm",  "qos": {"reliability": "reliable"} },
            "sensor.pulse_tx": { "transport": "shm",  "qos": {"history_depth": 10} },
            "remote.query":    { "transport": "grpc", "endpoint": "192.168.1.10:50051" },
            "cross_node.sync": { "transport": "dds",  "domain_id": 0 }
        }
    }
}
```

**关键**: 同一系统可同时用多种 Transport,按频道路由。

---

## 6. 数据序列化:Serializer 可配置(v2.0 新增)

### 6.1 设计动机

v1.0 序列化固定,无法平衡:
- **调试友好**(JSON/XML,可读但慢)
- **高性能**(二进制,快但不可读)
- **跨语言**(Protobuf,需 schema)
- **零拷贝**(FlatBuffer,需 schema)

v2.0 抽象 **ISerializer** 接口,频道级可配置。

### 6.2 ISerializer 接口

```cpp
class ISerializer {
public:
    virtual ~ISerializer() = default;
    
    // 序列化:对象 → 字节流
    virtual std::vector<uint8_t> serialize(const Message& msg) = 0;
    
    // 反序列化:字节流 → 对象
    virtual Message deserialize(const std::vector<uint8_t>& bytes) = 0;
    
    // 元信息
    virtual std::string name() const = 0;
    virtual bool isTextFormat() const = false;  // 文本格式?
    virtual bool requiresSchema() const = false; // 需要 schema?
};
```

### 6.3 内置 Serializer 实现

#### 6.3.1 JsonSerializer(文本,调试友好)

```cpp
class JsonSerializer : public ISerializer {
    std::vector<uint8_t> serialize(const Message& msg) override {
        nlohmann::json j;
        j["channel"]   = msg.channel;
        j["source"]    = msg.source;
        j["target"]    = msg.target;
        j["sim_time"]  = msg.sim_time;
        j["type"]      = msg.type;
        // payload 已是 JSON 字节(约定)
        j["payload"]   = nlohmann::json::parse(msg.payload);
        auto str = j.dump();
        return std::vector<uint8_t>(str.begin(), str.end());
    }
    
    Message deserialize(const std::vector<uint8_t>& bytes) override {
        auto j = nlohmann::json::parse(bytes);
        Message msg;
        msg.channel  = j["channel"];
        msg.source   = j["source"];
        msg.sim_time = j["sim_time"];
        msg.payload  = j["payload"].dump();
        msg.format   = Message::PayloadFormat::JSON;
        return msg;
    }
    
    std::string name() const override { return "json"; }
    bool isTextFormat() const override { return true; }
};
```

**特点**: 可读、调试友好、性能中等。

#### 6.3.2 XmlSerializer(文本,legacy 互操作)

```cpp
class XmlSerializer : public ISerializer {
    std::vector<uint8_t> serialize(const Message& msg) override {
        tinyxml2::XMLDocument doc;
        auto root = doc.NewElement("Message");
        root->SetAttribute("channel", std::to_string(msg.channel).c_str());
        root->SetAttribute("source",  std::to_string(msg.source).c_str());
        // ... payload 作为 CDATA ...
        tinyxml2::XMLPrinter printer;
        doc.Print(&printer);
        return std::vector<uint8_t>(printer.CStr(), printer.CStr() + printer.CStrSize());
    }
    // ...
    std::string name() const override { return "xml"; }
    bool isTextFormat() const override { return true; }
};
```

**特点**: 可读、与 legacy XML 系统互操作。

#### 6.3.3 BinarySerializer(二进制,高性能)

```cpp
class BinarySerializer : public ISerializer {
    std::vector<uint8_t> serialize(const Message& msg) override {
        // 紧凑二进制布局
        std::vector<uint8_t> buf;
        buf.reserve(64 + msg.payload.size());
        
        writeLE(buf, msg.channel);     // 8B
        writeLE(buf, msg.source);      // 8B
        writeLE(buf, msg.target);      // 8B
        writeLE(buf, msg.sim_time);    // 8B
        writeLE(buf, msg.type);        // 4B
        writeLE(buf, uint32_t(msg.format)); // 1B
        writeLE(buf, uint32_t(msg.payload.size())); // 4B
        buf.insert(buf.end(), msg.payload.begin(), msg.payload.end());
        
        return buf;
    }
    
    Message deserialize(const std::vector<uint8_t>& bytes) override {
        Message msg;
        auto it = bytes.begin();
        msg.channel  = readLE<uint64_t>(it);
        msg.source   = readLE<uint64_t>(it);
        msg.target   = readLE<uint64_t>(it);
        msg.sim_time = readLE<double>(it);
        msg.type     = readLE<uint32_t>(it);
        msg.format   = Message::PayloadFormat(readLE<uint32_t>(it));
        uint32_t len = readLE<uint32_t>(it);
        msg.payload.assign(it, it + len);
        return msg;
    }
    
    std::string name() const override { return "binary"; }
};
```

**特点**: 最高性能、不可读、零依赖。

#### 6.3.4 ProtobufSerializer(跨语言,需 schema)

```cpp
class ProtobufSerializer : public ISerializer {
    std::vector<uint8_t> serialize(const Message& msg) override {
        vsgraysim::proto::Message pb;
        pb.set_channel(msg.channel);
        pb.set_source(msg.source);
        pb.set_sim_time(msg.sim_time);
        pb.set_payload(msg.payload.data(), msg.payload.size());
        std::vector<uint8_t> out(pb.ByteSizeLong());
        pb.SerializeToArray(out.data(), out.size());
        return out;
    }
    // ...
    std::string name() const override { return "protobuf"; }
    bool requiresSchema() const override { return true; }
};
```

**特点**: 跨语言(C++/Python/Java)、需 .proto schema、紧凑。

#### 6.3.5 FlatBufferSerializer(零拷贝)

```cpp
class FlatBufferSerializer : public ISerializer {
    std::vector<uint8_t> serialize(const Message& msg) override {
        flatbuffers::FlatBufferBuilder builder;
        auto payload_offset = builder.CreateVector(msg.payload);
        auto msg_offset = vsgraysim::flatbuf::CreateMessage(
            builder, msg.channel, msg.source, msg.target,
            msg.sim_time, msg.type, payload_offset);
        builder.Finish(msg_offset);
        return std::vector<uint8_t>(builder.GetBufferPointer(),
                                     builder.GetBufferPointer() + builder.GetSize());
    }
    // 反序列化零拷贝:直接读 buffer
    Message deserialize(const std::vector<uint8_t>& bytes) override {
        auto* fb = vsgraysim::flatbuf::GetMessage(bytes.data());
        Message msg;
        msg.channel  = fb->channel();
        msg.source   = fb->source();
        msg.sim_time = fb->sim_time();
        auto* payload = fb->payload();
        msg.payload.assign(payload->begin(), payload->end());
        return msg;
    }
    std::string name() const override { return "flatbuffer"; }
    bool requiresSchema() const override { return true; }
};
```

**特点**: 零拷贝反序列化、最高性能、需 .fbs schema。

#### 6.3.6 RawPointerSerializer(进程内零拷贝)

```cpp
class RawPointerSerializer : public ISerializer {
    // 仅 InProcessTransport 用,直接传指针
    std::vector<uint8_t> serialize(const Message& msg) override {
        // 不真正序列化,把指针编码进 payload
        // (仅同进程安全)
        return msg.payload;  // 原样返回
    }
    std::string name() const override { return "raw"; }
};
```

### 6.4 Serializer 工厂与配置

```cpp
class SerializerFactory {
public:
    static std::shared_ptr<ISerializer> create(const std::string& name) {
        if (name == "json")        return std::make_shared<JsonSerializer>();
        if (name == "xml")         return std::make_shared<XmlSerializer>();
        if (name == "binary")      return std::make_shared<BinarySerializer>();
        if (name == "protobuf")    return std::make_shared<ProtobufSerializer>();
        if (name == "flatbuffer")  return std::make_shared<FlatBufferSerializer>();
        if (name == "raw")         return std::make_shared<RawPointerSerializer>();
        return nullptr;
    }
};

#define REGISTER_SERIALIZER(ClassName, Name) \
    static bool reg_##ClassName = [] { \
        SerializerFactory::registerType(Name, []{ \
            return std::make_shared<ClassName>(); \
        }); \
        return true; \
    }();
```

### 6.5 频道级序列化配置

```json
{
    "default_serializer": "binary",
    "channel_serializers": {
        "scene.state":     "flatbuffer",   // 高频态势,零拷贝
        "sensor.pulse_tx": "binary",       // 高频脉冲,紧凑
        "signal.rd_frame": "binary",       // 大块数据
        "debug.log":       "json",         // 调试日志,可读
        "config.update":   "xml",          // 配置互操作
        "remote.query":    "protobuf"      // 跨语言查询
    }
}
```

**关键**: 不同频道用不同序列化,按需求平衡性能/可读性/互操作。

### 6.6 选择指南

| 场景 | 推荐 Serializer | 理由 |
|------|----------------|------|
| 单进程内部高频 | `raw` | 零拷贝零开销 |
| 单进程内部调试 | `json` | 可读 |
| 多进程同机高频 | `binary` | 紧凑快速 |
| 多进程同机大块 | `flatbuffer` | 零拷贝反序列化 |
| 跨机高频 | `binary` | 网络紧凑 |
| 跨语言互操作 | `protobuf` | 多语言支持 |
| Legacy XML 系统 | `xml` | 互操作 |
| 配置文件 | `yaml` 或 `json` | 可读 |

---

## 7. MessageBus v2.0(整合 Transport + Serializer)

### 7.1 架构

```
                ┌─────────────────────────────────┐
                │          MessageBus              │
                ├─────────────────────────────────┤
   publish ────►│  Serializer (按频道选)           │
                │       ↓                          │
                │  Transport (按频道选)            │────► 远端/他进程
                │       ↓                          │
   subscribe ◄──│  Deserializer (按频道选)         │
                └─────────────────────────────────┘
```

### 7.2 MessageBus v2.0 实现

```cpp
class MessageBus {
public:
    // 配置(从 SystemConfig 加载)
    void configure(const nlohmann::json& cfg) {
        default_transport_ = TransportFactory::create(cfg.value("default_transport", "in_process"));
        default_serializer_ = SerializerFactory::create(cfg.value("default_serializer", "binary"));
        
        // 频道级覆盖
        if (cfg.contains("channels")) {
            for (auto& [ch_name, ch_cfg] : cfg["channels"].items()) {
                ChannelConfig cc;
                cc.transport = TransportFactory::create(
                    ch_cfg.value("transport", cfg.value("default_transport", "in_process")));
                cc.serializer = SerializerFactory::create(
                    ch_cfg.value("serializer", cfg.value("default_serializer", "binary")));
                cc.qos = parseQoS(ch_cfg.value("qos", nlohmann::json{}));
                channels_[hash(ch_name)] = cc;
            }
        }
    }
    
    void publish(const std::string& channel, Message msg) {
        auto& cc = getChannelConfig(channel);
        msg.channel = hash(channel);
        msg.format = toPayloadFormat(cc.serializer->name());
        auto bytes = cc.serializer->serialize(msg);
        cc.transport->publish(channel, bytes);
    }
    
    void subscribe(const std::string& channel, SystemBase* sub) {
        auto& cc = getChannelConfig(channel);
        // 包装反序列化
        cc.transport->subscribe(channel, 
            [sub, serializer = cc.serializer](const std::vector<uint8_t>& bytes) {
                auto msg = serializer->deserialize(bytes);
                sub->onMessage(msg);
            });
    }
    
private:
    struct ChannelConfig {
        std::shared_ptr<ITransport>   transport;
        std::shared_ptr<ISerializer>  serializer;
        TransportQoS                  qos;
    };
    
    std::shared_ptr<ITransport>    default_transport_;
    std::shared_ptr<ISerializer>   default_serializer_;
    std::unordered_map<ChannelID, ChannelConfig> channels_;
    
    ChannelConfig& getChannelConfig(const std::string& name) {
        auto id = hash(name);
        auto it = channels_.find(id);
        if (it != channels_.end()) return it->second;
        // 用默认
        static ChannelConfig def;
        def.transport = default_transport_;
        def.serializer = default_serializer_;
        return def;
    }
};
```

### 7.3 频道抽象

频道名约定(类似"电磁波频段"):

| 频道名 | 真实世界隐喻 | 推荐 Transport | 推荐 Serializer |
|--------|------------|---------------|----------------|
| `scene.state` | 物体可见状态 | shm/dds | flatbuffer |
| `sensor.pulse_tx` | 电磁波发射 | shm | binary |
| `sensor.echo` | 回波 | shm | binary |
| `signal.rd_frame` | 信号图像 | shm | binary |
| `debug.log` | 日志 | in_process | json |
| `config.update` | 配置变更 | grpc/dds | xml/json |
| `remote.query` | 远程查询 | grpc | protobuf |
| `recording.stream` | 录制流 | file | binary |

---

## 8. 系统自定义运行机制(v2.0 新增)

### 8.1 自定义 onTick 行为

除了 TickStrategy 控制频率,系统可完全自定义 onTick 内部逻辑:

```cpp
class MySystem : public SystemBase {
    void onTick(const TimeTick& tick) override {
        // 完全自定义,想干啥干啥
        switch (state_machine_->current()) {
            case SEARCHING: doSearch(tick); break;
            case TRACKING:  doTrack(tick);  break;
            case SAR_COLLECTING: doSAR(tick); break;
        }
    }
    
    void onSubStep(const TimeTick& tick, int substep_idx) override {
        // Bullet3 风格子步长
        bullet_->stepSimulation(tick.substep_dt, 1);
    }
    
    void onBatch(const std::vector<TimeTick>& batch) override {
        // RT 风格批处理
        for (auto& t : batch) processOnePulse(t);
    }
};
```

### 8.2 自定义状态转换触发

```cpp
class RadarSystem : public SystemBase {
    void onTick(const TimeTick& tick) override {
        if (state_machine_->isInState(SEARCHING)) {
            if (detectTarget()) {
                state_machine_->transitionTo(TRACKING);  // 自主转换
            }
        }
    }
    
    void onMessage(const Message& msg) override {
        if (msg.type == CMD_START_SAR) {
            state_machine_->transitionTo(SAR_COLLECTING);  // 外部触发
        }
    }
};
```

### 8.3 自定义健康检查

```cpp
class SystemBase {
protected:
    virtual HealthStatus selfCheck() const {
        return {state_machine_->current(), tick_strategy_->stats(), "ok"};
    }
};

class RTSystem : public SystemBase {
    HealthStatus selfCheck() const override {
        auto h = SystemBase::selfCheck();
        if (gpu_memory_usage_ > 0.9) h.status = "gpu_oom_warning";
        return h;
    }
};
```

---

## 9. 配置系统 v2.0(完整可配置)

### 9.1 完整配置示例

```json
{
    "name": "MultiDomainSim",
    "type": "ROOT",
    "tick_strategy": "fixed",
    "tick_params": { "tick_rate": 60 },
    
    "transport": {
        "default": "in_process",
        "channels": {
            "scene.state": {
                "transport": "shm",
                "serializer": "flatbuffer",
                "qos": { "reliability": "reliable", "history_depth": 2 }
            },
            "sensor.pulse_tx": {
                "transport": "shm",
                "serializer": "binary",
                "qos": { "history_depth": 10 }
            },
            "remote.query": {
                "transport": "grpc",
                "serializer": "protobuf",
                "endpoint": "192.168.1.10:50051"
            }
        }
    },
    
    "state_machine": {
        "states": [
            { "id": 0, "name": "CREATED" },
            { "id": 2, "name": "RUNNING" },
            { "id": 4, "name": "STOPPED" }
        ],
        "transitions": [
            { "from": 0, "to": 2, "trigger": "start" },
            { "from": 2, "to": 4, "trigger": "stop" }
        ]
    },
    
    "children": [
        {
            "name": "MasterClock",
            "type": "TIME",
            "tick_strategy": "event",
            "params": { "major_frame_period": 0.1 }
        },
        {
            "name": "PhysicsSystem",
            "type": "PHYSICS",
            "tick_strategy": "substep",
            "tick_params": {
                "fixed_dt": 0.00833,
                "max_substeps": 4
            },
            "params": {
                "engine": "bullet3",
                "gravity": [0, -9.81, 0]
            },
            "subscriptions": [],
            "publishes": ["scene.state"]
        },
        {
            "name": "RadarSystem",
            "type": "RADAR",
            "tick_strategy": "fixed",
            "tick_params": { "tick_rate": 1000 },
            
            "state_machine": {
                "states": [
                    { "id": 100, "name": "STANDBY" },
                    { "id": 101, "name": "SEARCHING" },
                    { "id": 102, "name": "TRACKING" },
                    { "id": 103, "name": "SAR_COLLECTING" },
                    { "id": 104, "name": "SAR_PROCESSING" }
                ],
                "transitions": [
                    { "from": 100, "to": 101, "trigger": "start_search" },
                    { "from": 101, "to": 102, "trigger": "target_detected" },
                    { "from": 101, "to": 103, "trigger": "start_sar" },
                    { "from": 103, "to": 104, "trigger": "aperture_complete" }
                ]
            },
            
            "params": {
                "prf": 1000,
                "wavelength": 0.03,
                "mount_entity": "tank_001"
            },
            "subscriptions": ["scene.state"],
            "publishes": ["sensor.pulse_tx"]
        },
        {
            "name": "RTSystem",
            "type": "RT",
            "tick_strategy": "batch",
            "tick_params": {
                "batch_window_us": 100,
                "max_batch": 32
            },
            "subscriptions": ["sensor.pulse_tx"],
            "publishes": ["sensor.echo"]
        },
        {
            "name": "AdaptiveLoader",
            "type": "CUSTOM",
            "tick_strategy": "adaptive",
            "tick_params": {
                "min_rate": 10,
                "max_rate": 1000,
                "target_load": 0.8
            }
        },
        {
            "name": "VisualSystem",
            "type": "VISUAL",
            "tick_strategy": "fixed",
            "tick_params": { "tick_rate": 60 },
            "subscriptions": ["scene.state", "signal.rd_frame"]
        }
    ]
}
```

### 9.2 配置覆盖规则

配置优先级(高到低):
1. **频道级配置**(channels 内的具体频道)
2. **系统级配置**(系统的 default_serializer / default_transport)
3. **根系统配置**(全局 default)
4. **框架默认值**(in_process + binary)

### 9.3 环境变量覆盖

```bash
# 部署时覆盖配置
export VSGRAY_TRANSPORT_DEFAULT=dds
export VSGRAY_TRANSPORT_SCENE_STATE=shm
export VSGRAY_SERIALIZER_DEFAULT=binary
export VSGRAY_DDS_DOMAIN_ID=0

# v3.0: 计算后端环境变量
export VSGRAY_COMPUTE_DEFAULT=cuda
export VSGRAY_COMPUTE_RT_DEVICE=0
export VSGRAY_COMPUTE_SIGNAL_DEVICE=1
export VSGRAY_CPU_CORES_PHYSICS="0,1,2,3"
export VSGRAY_NUMA_NODE=0
```

---

## 10. 计算后端:ComputeBackend 可配置(v3.0 新增)

### 10.1 设计动机

不同系统的计算特性差异巨大:

| 系统 | 计算特性 | 最佳后端 |
|------|---------|---------|
| PhysicsSystem | 标量逻辑 + 碰撞检测 | CPU(多核) |
| RTSystem | 大规模并行射线求交 | Vulkan RT(GPU) |
| SignalModule | FFT/矩阵运算 | CUDA(GPU) |
| VisualSystem | 图形渲染 | Vulkan(GPU) |
| OrbitSystem | 双精度标量 | CPU(单核) |
| SensorModule | 事件生成,轻量 | CPU(任意) |

v3.0 抽象 **IComputeBackend** 接口,每个系统可独立选择计算后端。

### 10.2 IComputeBackend 接口

```cpp
// 计算设备类型
enum class ComputeDeviceType {
    CPU,        // CPU 计算
    CUDA,       // CUDA GPU
    VULKAN,     // Vulkan GPU(含 RT)
    OPENCL,     // OpenCL(可选)
    AUTO        // 自动选择
};

// 计算设备描述
struct ComputeDevice {
    ComputeDeviceType type;
    int device_id;              // GPU 0/1/2...,CPU 时为 -1
    std::string name;           // "NVIDIA RTX 4090" / "Intel Xeon..."
    
    // CPU 专属
    std::vector<int> cpu_cores; // 绑定的核心列表
    int numa_node = -1;         // NUMA 节点(-1 = 不感知)
    
    // GPU 专属
    size_t total_memory = 0;    // 显存/内存总量
    size_t free_memory = 0;     // 可用
    int compute_capability = 0; // CUDA compute capability
    bool supports_rt = false;   // 是否支持光线追踪
    
    // 能力
    bool supports_double = true;
    int max_threads = 0;        // CPU 核心数 / GPU CUDA 核心数
};

// 计算需求声明(系统告诉框架自己需要什么)
struct ComputeRequirement {
    ComputeDeviceType preferred = ComputeDeviceType::AUTO;
    size_t min_memory_mb = 0;           // 最小显存/内存
    bool requires_double = false;        // 是否需要双精度
    bool requires_rt = false;            // 是否需要光线追踪
    int preferred_gpu_id = -1;           // 指定 GPU(-1 = 自动分配)
    std::vector<int> preferred_cpu_cores; // 指定 CPU 核心
    int preferred_numa_node = -1;
    bool exclusive = false;              // 是否独占设备
};

// 计算后端接口
class IComputeBackend {
public:
    virtual ~IComputeBackend() = default;
    
    // 生命周期
    virtual bool init(const ComputeDevice& device) = 0;
    virtual void shutdown() = 0;
    
    // 内存管理
    virtual void* allocate(size_t bytes) = 0;
    virtual void free(void* ptr) = 0;
    virtual size_t getFreeMemory() const = 0;
    
    // 同步
    virtual void sync() = 0;  // 等待设备完成所有操作
    
    // 跨设备数据传输
    virtual void copyFromHost(void* device_dst, const void* host_src, size_t bytes) = 0;
    virtual void copyToHost(void* host_dst, const void* device_src, size_t bytes) = 0;
    virtual void copyFromDevice(void* dst, const void* src, size_t bytes,
                                 const IComputeBackend& src_backend) = 0;
    
    // 设备信息
    virtual ComputeDeviceType type() const = 0;
    virtual const ComputeDevice& device() const = 0;
    virtual std::string name() const = 0;
    
    // v3.0: 流(stream)支持(异步操作)
    virtual void* createStream() = 0;
    virtual void destroyStream(void* stream) = 0;
    virtual void syncStream(void* stream) = 0;
};
```

### 10.3 内置 ComputeBackend 实现

#### 10.3.1 CPUBackend(多核 CPU)

```cpp
class CPUBackend : public IComputeBackend {
public:
    bool init(const ComputeDevice& device) override {
        device_ = device;
        if (!device_.cpu_cores.empty()) {
            setCpuAffinity(device_.cpu_cores);  // 绑定核心
        }
        if (device_.numa_node >= 0) {
            setNumaAffinity(device_.numa_node);  // NUMA 感知
        }
        return true;
    }
    
    void* allocate(size_t bytes) override {
        // NUMA 感知分配
        if (device_.numa_node >= 0) {
            return numa_alloc_onnode(bytes, device_.numa_node);
        }
        return aligned_alloc(64, bytes);  // 64 字节对齐(缓存行)
    }
    
    void free(void* ptr) override {
        if (device_.numa_node >= 0) numa_free(ptr, 0);
        else ::free(ptr);
    }
    
    void sync() override { /* CPU 同步无操作 */ }
    void copyFromHost(void* dst, const void* src, size_t n) override { memcpy(dst, src, n); }
    void copyToHost(void* dst, const void* src, size_t n) override { memcpy(dst, src, n); }
    ComputeDeviceType type() const override { return ComputeDeviceType::CPU; }
    
private:
    void setCpuAffinity(const std::vector<int>& cores) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        for (int c : cores) CPU_SET(c, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    }
    
    void setNumaAffinity(int node) {
        struct bitmask* mask = numa_allocate_nodemask();
        numa_bitmask_setbit(mask, node);
        numa_bind(mask);
        numa_bitmask_free(mask);
    }
    
    ComputeDevice device_;
};
```

**特点**:
- 支持核心绑定(`pthread_setaffinity_np`)
- NUMA 感知内存分配(`numa_alloc_onnode`)
- 缓存行对齐(64B)
- 零拷贝(本身就是 host 内存)

#### 10.3.2 CUDABackend(NVIDIA GPU)

```cpp
class CUDABackend : public IComputeBackend {
public:
    bool init(const ComputeDevice& device) override {
        device_ = device;
        cudaSetDevice(device_.device_id);
        cudaGetDeviceProperties(&props_, device_.device_id);
        return true;
    }
    
    void* allocate(size_t bytes) override {
        void* ptr = nullptr;
        cudaMalloc(&ptr, bytes);
        return ptr;
    }
    
    void free(void* ptr) override {
        cudaFree(ptr);
    }
    
    size_t getFreeMemory() const override {
        size_t free, total;
        cudaMemGetInfo(&free, &total);
        return free;
    }
    
    void sync() override { cudaDeviceSynchronize(); }
    
    void copyFromHost(void* dst, const void* src, size_t n) override {
        cudaMemcpy(dst, src, n, cudaMemcpyHostToDevice);
    }
    
    void copyToHost(void* dst, const void* src, size_t n) override {
        cudaMemcpy(dst, src, n, cudaMemcpyDeviceToHost);
    }
    
    void copyFromDevice(void* dst, const void* src, size_t n,
                         const IComputeBackend& src_backend) override {
        if (src_backend.type() == ComputeDeviceType::CUDA) {
            // GPU ↔ GPU(可能跨设备)
            cudaMemcpy(dst, src, n, cudaMemcpyDeviceToDevice);
        } else {
            // CPU → GPU
            copyFromHost(dst, src, n);
        }
    }
    
    // v3.0: 异步流
    void* createStream() override {
        cudaStream_t s;
        cudaStreamCreate(&s);
        return s;
    }
    void syncStream(void* s) override { cudaStreamSynchronize((cudaStream_t)s); }
    
    ComputeDeviceType type() const override { return ComputeDeviceType::CUDA; }
    
private:
    ComputeDevice device_;
    cudaDeviceProp props_;
};
```

**特点**:
- 设备显式选择(`cudaSetDevice`)
- 显存管理(`cudaMalloc/cudaFree`)
- 异步流(`cudaStream`)
- 跨设备拷贝(`cudaMemcpyDeviceToDevice`)

#### 10.3.3 VulkanBackend(Vulkan GPU,含 RT)

```cpp
class VulkanBackend : public IComputeBackend {
public:
    bool init(const ComputeDevice& device) override {
        device_ = device;
        // 创建 VkInstance + VkPhysicalDevice + VkDevice
        createInstance();
        selectPhysicalDevice(device_.device_id);
        createLogicalDevice();
        // 检测 RT 支持
        if (device_.supports_rt) {
            rt_properties_ = queryRTProperties();
        }
        return vk_device_ != VK_NULL_HANDLE;
    }
    
    void* allocate(size_t bytes) override {
        // Vulkan 显存分配
        VkDeviceMemory memory;
        VkBuffer buffer;
        allocateBuffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, buffer, memory);
        buffers_[buffer] = memory;
        return buffer;  // 返回 VkBuffer 句柄
    }
    
    void free(void* ptr) override {
        VkBuffer buffer = (VkBuffer)ptr;
        vkDestroyBuffer(vk_device_, buffer, nullptr);
        vkFreeMemory(vk_device_, buffers_[buffer], nullptr);
        buffers_.erase(buffer);
    }
    
    void sync() override {
        vkDeviceWaitIdle(vk_device_);
    }
    
    // v3.0: Vulkan 外部内存(跨进程共享)
    VkDeviceMemory allocateExportable(size_t bytes) {
        // 用 VK_KHR_external_memory_fd 导出显存句柄
        // 其他进程通过 fd 导入,实现零拷贝
    }
    
    ComputeDeviceType type() const override { return ComputeDeviceType::VULKAN; }
    
private:
    VkInstance instance_;
    VkPhysicalDevice physical_device_;
    VkDevice vk_device_;
    std::map<VkBuffer, VkDeviceMemory> buffers_;
    VkPhysicalDeviceRayTracingPropertiesKHR rt_properties_;
};
```

**特点**:
- 支持 Vulkan RT(光线追踪)
- 显存可跨进程共享(外部内存扩展)
- 适合 RTSystem + VisualSystem 共享 GPU

### 10.4 ComputeBackend 工厂与自动选择

```cpp
class ComputeBackendFactory {
public:
    static std::shared_ptr<IComputeBackend> create(const std::string& name) {
        if (name == "cpu")     return std::make_shared<CPUBackend>();
        if (name == "cuda")    return std::make_shared<CUDABackend>();
        if (name == "vulkan")  return std::make_shared<VulkanBackend>();
        if (name == "opencl")  return std::make_shared<OpenCLBackend>();
        if (name == "auto")    return autoSelect();
        return nullptr;
    }
    
    // 自动选择:根据系统声明的 ComputeRequirement 匹配最佳设备
    static std::shared_ptr<IComputeBackend> autoSelect(const ComputeRequirement& req) {
        auto& dm = DeviceManager::instance();
        
        if (req.requires_rt) {
            // 需要光线追踪 → 找支持 RT 的 Vulkan GPU
            auto dev = dm.findVulkanRTDevice(req.min_memory_mb);
            if (dev) return createFromDevice(*dev);
        }
        
        if (req.preferred == ComputeDeviceType::CUDA || 
            req.min_memory_mb > 1024) {
            // 大内存或偏好 CUDA → 找 GPU
            auto dev = dm.findCUDADevice(req.min_memory_mb, req.preferred_gpu_id);
            if (dev) return createFromDevice(*dev);
        }
        
        // 默认 CPU
        auto dev = dm.findCPUDevice(req.preferred_cpu_cores, req.preferred_numa_node);
        return createFromDevice(*dev);
    }
};

#define REGISTER_COMPUTE_BACKEND(ClassName, Name) \
    static bool reg_##ClassName = [] { \
        ComputeBackendFactory::registerType(Name, []{ \
            return std::make_shared<ClassName>(); \
        }); \
        return true; \
    }();
```

### 10.5 DeviceManager(设备发现与分配)

```cpp
class DeviceManager {
public:
    static DeviceManager& instance();
    
    // 启动时发现所有设备
    void discoverDevices() {
        // 1. 发现 CPU 拓扑
        discoverCPUTopology();  // 核心数、NUMA 节点、缓存层级
        
        // 2. 发现 CUDA 设备
        int cuda_count;
        cudaGetDeviceCount(&cuda_count);
        for (int i = 0; i < cuda_count; ++i) {
            ComputeDevice d;
            d.type = ComputeDeviceType::CUDA;
            d.device_id = i;
            cudaGetDeviceProperties(&props, i);
            d.name = props.name;
            d.total_memory = props.totalGlobalMem;
            d.compute_capability = props.major * 10 + props.minor;
            devices_.push_back(d);
        }
        
        // 3. 发现 Vulkan 设备(可能与 CUDA 重叠)
        discoverVulkanDevices();
    }
    
    // 查询
    std::vector<ComputeDevice> listDevices() const;
    std::optional<ComputeDevice> findCUDADevice(size_t min_mem_mb, int preferred_id) const;
    std::optional<ComputeDevice> findVulkanRTDevice(size_t min_mem_mb) const;
    std::optional<ComputeDevice> findCPUDevice(const std::vector<int>& cores, int numa) const;
    
    // 分配跟踪(避免冲突)
    bool allocate(int device_id, SystemID system_id, bool exclusive);
    void release(int device_id, SystemID system_id);
    
private:
    std::vector<ComputeDevice> devices_;
    std::map<int, std::vector<SystemID>> allocations_;  // device_id → 使用者
};
```

### 10.6 系统计算需求声明

每个系统在 onInit 时声明自己的计算需求:

```cpp
class RTSystem : public SystemBase {
public:
    ComputeRequirement computeReq() const override {
        ComputeRequirement req;
        req.preferred = ComputeDeviceType::VULKAN;
        req.requires_rt = true;              // 必须支持 RT
        req.min_memory_mb = 1024;            // 至少 1GB 显存
        req.preferred_gpu_id = config_.value("gpu_device", -1);
        return req;
    }
    
    bool onInit(const SystemConfig& cfg) override {
        // 框架已根据 computeReq() 分配好 compute_backend_
        auto* vk = dynamic_cast<VulkanBackend*>(compute_backend_.get());
        rt_pipeline_ = std::make_unique<RayTracingPipeline>(vk->device());
        return true;
    }
};

class SignalSystem : public SystemBase {
public:
    ComputeRequirement computeReq() const override {
        ComputeRequirement req;
        req.preferred = ComputeDeviceType::CUDA;
        req.min_memory_mb = 512;
        return req;
    }
};

class PhysicsSystem : public SystemBase {
public:
    ComputeRequirement computeReq() const override {
        ComputeRequirement req;
        req.preferred = ComputeDeviceType::CPU;
        req.preferred_cpu_cores = {0, 1, 2, 3};  // 绑定 4 核
        req.preferred_numa_node = 0;
        return req;
    }
};
```

### 10.7 跨设备数据传输

不同后端的系统间需要数据搬运:

```cpp
// RTSystem (Vulkan GPU) → SignalSystem (CUDA GPU)
// 需要跨设备传输散射点云

class CrossDeviceTransfer {
public:
    // GPU↔GPU 跨厂商传输(先到 host 再到设备)
    static void transfer(IComputeBackend& src, IComputeBackend& dst,
                         void* src_ptr, void* dst_ptr, size_t bytes) {
        if (src.type() == dst.type()) {
            // 同类型设备,直接拷贝
            dst.copyFromDevice(dst_ptr, src_ptr, bytes, src);
        } else {
            // 跨类型(如 Vulkan→CUDA),经 host 中转
            void* host_buf = malloc(bytes);
            src.copyToHost(host_buf, src_ptr, bytes);
            dst.copyFromHost(dst_ptr, host_buf, bytes);
            free(host_buf);
        }
    }
    
    // v3.0: 零拷贝优化(同 GPU 上的 Vulkan+CUDA)
    static void* enableZeroCopy(IComputeBackend& a, IComputeBackend& b) {
        // 通过 Vulkan 外部内存 + CUDA 导入
        // 实现同 GPU 上不同 API 的显存共享
        // 仅当 a 和 b 在同一物理 GPU 时可用
    }
};
```

### 10.8 计算后端配置示例

```json
{
    "name": "RTSystem",
    "type": "RT",
    "compute_backend": "vulkan",
    "compute_params": {
        "device_id": 0,
        "min_memory_mb": 1024,
        "requires_rt": true
    }
},
{
    "name": "SignalSystem",
    "type": "SIGNAL",
    "compute_backend": "cuda",
    "compute_params": {
        "device_id": 1,
        "min_memory_mb": 512
    }
},
{
    "name": "PhysicsSystem",
    "type": "PHYSICS",
    "compute_backend": "cpu",
    "compute_params": {
        "cpu_cores": [0, 1, 2, 3],
        "numa_node": 0
    }
},
{
    "name": "OrbitSystem",
    "type": "ORBIT",
    "compute_backend": "auto",
    "compute_params": {
        "preferred": "cpu",
        "requires_double": true
    }
}
```

### 10.9 多 GPU 分布示例

```
机器配置:2× NVIDIA RTX 4090 + 16 核 CPU

GPU 0 (RTX 4090 #0):
├── RTSystem (Vulkan RT,1GB 显存)
└── VisualSystem (Vulkan 渲染,2GB 显存)
    → 共享同一 GPU,场景数据零拷贝

GPU 1 (RTX 4090 #1):
└── SignalSystem (CUDA,1GB 显存)
    → 独立 GPU,避免与 RT 争抢资源

CPU 核心 0-3:
└── PhysicsSystem (Bullet3,4 核)

CPU 核心 4-7:
├── SensorSystem (脉冲生成,轻量)
├── OrbitSystem (轨道计算,双精度)
└── MasterClock (时序驱动)

CPU 核心 8-15:
└── 备用 / 其他系统
```

---

## 11. 部署模式:单进程/多进程单机/多进程多机(v3.1 增强)

### 11.1 三种部署模式总览

```
模式 1: 单进程 (single_process)
┌─────────────────────────────────┐
│         一个进程                 │
│  ┌─────┐ ┌─────┐ ┌─────┐       │
│  │Phys │ │ RT  │ │ Vis │ ...   │  ← 所有系统同进程
│  └─────┘ └─────┘ └─────┘       │
│  Transport: in_process (函数调用)│
└─────────────────────────────────┘
适用:开发调试、功能验证

模式 2: 多进程单机 (multi_process_single_machine)
┌─────────────────────────────────────────────┐
│              一台机器                         │
│  ┌────────┐  ┌────────┐  ┌────────┐         │
│  │进程 A   │  │进程 B   │  │进程 C   │       │
│  │Phys+Sen│  │ RT+Vis │  │Signal  │        │
│  └───┬────┘  └───┬────┘  └───┬────┘         │
│      │SHM/SHM       │SHM        │SHM          │
│      └──────────────┴───────────┘            │
│  Transport: shm (共享内存)                    │
└─────────────────────────────────────────────┘
适用:单机性能优化、多核多 GPU 利用

模式 3: 多进程多机 (multi_process_multi_machine)
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│    机器 A        │    │    机器 B        │    │    机器 C        │
│  ┌────────────┐ │    │  ┌────────────┐ │    │  ┌────────────┐ │
│  │进程: core  │ │    │  │进程: rt    │ │    │  │进程: signal│ │
│  │Phys+Scene  │ │    │  │RT+Vis      │ │    │  │Signal+Rec  │ │
│  │+Sensor     │ │    │  │            │ │    │  │            │ │
│  └─────┬──────┘ │    │  └─────┬──────┘ │    │  └─────┬──────┘ │
│        │        │    │        │        │    │        │        │
│        └────────┼────┼────────┼────────┼────┼────────┘        │
│                 │ DDS/gRPC     │        │                     │
│                 │ (网络通信)    │        │                     │
└─────────────────┘    └─────────────────┘    └─────────────────┘
适用:大规模仿真、跨机扩展、容错部署
```

### 11.2 部署模式抽象

```cpp
enum class DeploymentMode {
    SINGLE_PROCESS,              // 模式 1: 所有系统同进程
    MULTI_PROCESS_SINGLE_MACHINE,// 模式 2: 多进程,同机
    MULTI_PROCESS_MULTI_MACHINE  // 模式 3: 多进程,跨机
};

// 节点配置(v3.1: 多机时的单台机器)
struct NodeConfig {
    std::string name;              // 节点名(如 "ground_node")
    std::string hostname;          // 主机名或 IP
    int port = 0;                  // 通信端口
    std::string ssh_target;        // SSH 远程启动(ssh user@host)
    
    // 节点硬件描述
    int cpu_core_count = 0;        // CPU 核心数
    int gpu_count = 0;             // GPU 数量
    size_t memory_gb = 0;          // 内存
    
    // 该节点上的进程组
    std::vector<ProcessGroupConfig> process_groups;
};

// 进程组配置
struct ProcessGroupConfig {
    std::string name;                       // 进程组名
    std::string executable;                 // 可执行文件路径
    std::vector<std::string> system_paths;  // 哪些系统在此进程
    std::vector<std::string> args;          // 启动参数
    nlohmann::json env;                     // 环境变量
    
    // v3.0: 计算资源
    std::vector<int> cpu_cores;             // 绑定的 CPU 核心
    int numa_node = -1;                     // NUMA 节点
    std::vector<int> gpu_devices;           // 可用的 GPU 设备 ID
    
    // v3.1: 跨机通信
    std::string transport_to_local;         // 同机进程间通信(默认 shm)
    std::string transport_to_remote;        // 跨机进程间通信(默认 dds)
    nlohmann::json transport_params;
    
    // v3.1: 所在节点(多机时)
    std::string node_name;                  // 在哪个节点上
};

// 部署配置
struct DeploymentConfig {
    DeploymentMode mode = DeploymentMode::SINGLE_PROCESS;
    
    // 模式 2/3: 进程组
    std::vector<ProcessGroupConfig> process_groups;
    
    // 模式 3: 节点列表
    std::vector<NodeConfig> nodes;
    
    // v3.1: 分布式 MasterClock 配置
    struct {
        bool enabled = false;               // 启用分布式时钟
        std::string primary_node;           // 主时钟节点
        std::string backup_node;            // 备时钟节点
        double heartbeat_interval = 0.1;    // 心跳间隔
        double failover_timeout = 3.0;      // 故障切换超时
        std::string sync_protocol = "ntp";  // 时间同步协议(ntp/ptp)
    } distributed_clock;
    
    // 模式 1: 线程模式
    enum class ThreadingMode {
        SINGLE_THREAD, PER_SYSTEM, THREAD_POOL
    } threading = ThreadingMode::SINGLE_THREAD;
    int thread_count = 0;
};
```

### 11.3 单进程模式(开发期)

```json
{
    "deployment": {
        "mode": "single_process",
        "threading": "single_thread"
    },
    "transport": {
        "default": "in_process",
        "channels": { "*": { "serializer": "raw" } }
    },
    "compute": {
        "default": "cpu"
    }
}
```

**特点**:
- 所有系统同进程同线程
- Transport = InProcess(同步函数调用)
- Serializer = Raw(零拷贝传指针)
- ComputeBackend = CPU(无 GPU 依赖)
- 延迟 < 1μs,适合开发调试

**单进程多线程变体**:
```json
{
    "deployment": {
        "mode": "single_process",
        "threading": "per_system",
        "thread_count": 8
    }
}
```
- 每个顶层系统独立线程
- 线程间通过无锁队列通信
- 共享内存空间,无需序列化

### 11.4 多进程模式(测试/生产期)

```json
{
    "deployment": {
        "mode": "multi_process",
        "process_groups": [
            {
                "name": "main",
                "executable": "./vsgraysim_app",
                "system_paths": ["Root/MasterClock", "Root/SceneSystem", "Root/VisualSystem"],
                "cpu_cores": [0, 1, 2, 3],
                "gpu_devices": [0],
                "env": { "VSGRAY_ROLE": "main" }
            },
            {
                "name": "physics",
                "executable": "./vsgraysim_app",
                "system_paths": ["Root/PhysicsSystem"],
                "cpu_cores": [4, 5, 6, 7],
                "gpu_devices": [],
                "env": { "VSGRAY_ROLE": "physics" }
            },
            {
                "name": "rt",
                "executable": "./vsgraysim_app",
                "system_paths": ["Root/RTSystem"],
                "cpu_cores": [8, 9],
                "gpu_devices": [0],
                "env": { "VSGRAY_ROLE": "rt" }
            },
            {
                "name": "signal",
                "executable": "./vsgraysim_app",
                "system_paths": ["Root/SignalSystem"],
                "cpu_cores": [10, 11],
                "gpu_devices": [1],
                "env": { "VSGRAY_ROLE": "signal" }
            }
        ]
    },
    "transport": {
        "default": "shm",
        "channels": {
            "scene.state":     { "serializer": "flatbuffer" },
            "sensor.pulse_tx": { "serializer": "binary" },
            "sensor.echo":     { "serializer": "binary" }
        }
    }
}
```

**进程拆分策略**:
1. **按计算后端拆分**: GPU 系统(RT/Signal)与 CPU 系统(Physics)分离
2. **按频率拆分**: 高频系统(Sensor 1kHz)与低频系统(Orbit 1Hz)分离
3. **按资源争抢拆分**: RT 和 Visual 共享 GPU 0,Signal 独占 GPU 1
4. **按崩溃影响拆分**: 易崩溃的实验系统与核心系统分离

### 11.5 多进程多机模式(大规模仿真)

跨机部署,每个节点运行一个或多个进程组:

```json
{
    "deployment": {
        "mode": "multi_process_multi_machine",
        
        "nodes": [
            {
                "name": "ground_node",
                "hostname": "192.168.1.10",
                "port": 50000,
                "cpu_core_count": 16,
                "gpu_count": 1,
                "memory_gb": 64
            },
            {
                "name": "rt_node",
                "hostname": "192.168.1.20",
                "port": 50000,
                "cpu_core_count": 8,
                "gpu_count": 2,
                "memory_gb": 32
            },
            {
                "name": "signal_node",
                "hostname": "192.168.1.30",
                "port": 50000,
                "cpu_core_count": 8,
                "gpu_count": 1,
                "memory_gb": 32
            }
        ],
        
        "process_groups": [
            {
                "name": "core",
                "node_name": "ground_node",
                "executable": "./vsgraysim_app",
                "system_paths": [
                    "Root/MasterClock",
                    "Root/SceneSystem",
                    "Root/PhysicsSystem",
                    "Root/SensorSystem"
                ],
                "cpu_cores": [0, 1, 2, 3, 4, 5, 6, 7],
                "gpu_devices": [],
                "transport_to_local": "shm",
                "transport_to_remote": "dds",
                "env": { "VSGRAY_ROLE": "core" }
            },
            {
                "name": "rt_vis",
                "node_name": "rt_node",
                "executable": "./vsgraysim_app",
                "system_paths": ["Root/RTSystem", "Root/VisualSystem"],
                "cpu_cores": [0, 1],
                "gpu_devices": [0],
                "transport_to_local": "shm",
                "transport_to_remote": "dds",
                "env": { "VSGRAY_ROLE": "rt_vis" }
            },
            {
                "name": "signal",
                "node_name": "signal_node",
                "executable": "./vsgraysim_app",
                "system_paths": ["Root/SignalSystem", "Root/RecorderSystem"],
                "cpu_cores": [0, 1],
                "gpu_devices": [0],
                "transport_to_local": "shm",
                "transport_to_remote": "dds",
                "env": { "VSGRAY_ROLE": "signal" }
            }
        ],
        
        "distributed_clock": {
            "enabled": true,
            "primary_node": "ground_node",
            "backup_node": "rt_node",
            "heartbeat_interval": 0.1,
            "failover_timeout": 3.0,
            "sync_protocol": "ptp"
        }
    },
    
    "transport": {
        "default": "dds",
        "channels": {
            "scene.state":     { "serializer": "flatbuffer", "qos": {"reliability": "reliable"} },
            "sensor.pulse_tx": { "serializer": "binary",     "qos": {"history_depth": 10} },
            "sensor.echo":     { "serializer": "binary" },
            "signal.rd_frame": { "serializer": "binary" },
            "system.health":   { "serializer": "json" }
        }
    }
}
```

**多机部署的关键设计**:

1. **节点分层**: 按计算特性分配到不同机器(GPU 机做 RT/Signal,CPU 机做 Physics)
2. **通信分层**: 同机用 SHM(低延迟),跨机用 DDS(自动发现 + QoS)
3. **分布式时钟**: 主备 MasterClock,故障自动切换
4. **容错**: 节点崩溃自动检测,可降级运行或重新分配

### 11.6 NodeRegistry(多机节点发现与注册)

```cpp
class NodeRegistry {
public:
    static NodeRegistry& instance();
    
    // 启动时注册本节点
    bool registerSelf(const NodeConfig& cfg);
    
    // 发现其他节点(三种方式)
    enum class DiscoveryMethod {
        STATIC_CONFIG,    // 从配置文件读取节点列表
        DDS_AUTODISCOVERY,// DDS 自动发现(多播)
        CENTRAL_REGISTRY  // 中心注册服务
    };
    
    void discoverNodes(DiscoveryMethod method);
    
    // 节点查询
    struct NodeInfo {
        std::string name;
        std::string hostname;
        int port;
        bool alive;
        double last_heartbeat;
        NodeStatus status;  // ONLINE / DEGRADED / OFFLINE
        int cpu_core_count;
        int gpu_count;
        size_t memory_gb;
        std::vector<std::string> process_groups;  // 该节点运行的进程组
    };
    
    std::vector<NodeInfo> listNodes() const;
    std::optional<NodeInfo> findNode(const std::string& name) const;
    
    // 健康监控
    void startHeartbeat(double interval_sec);
    void onHeartbeat(const std::string& node_name, const NodeStatus& status);
    
    // 节点故障回调
    using NodeFailureCallback = std::function<void(const std::string& node)>;
    void onNodeFailure(NodeFailureCallback cb);
    
private:
    std::map<std::string, NodeInfo> nodes_;
    std::vector<NodeFailureCallback> failure_callbacks_;
    std::mutex nodes_mutex_;
};
```

### 11.7 多机进程启动与监控

```cpp
class MultiMachineProcessManager {
public:
    // 启动整个集群
    bool launchCluster(const DeploymentConfig& cfg) {
        if (cfg.mode == DeploymentMode::SINGLE_PROCESS) {
            return launchSingleProcess(cfg);
        }
        
        // 1. 注册本节点
        NodeRegistry::instance().registerSelf(myNodeConfig());
        
        // 2. 发现所有节点
        NodeRegistry::instance().discoverNodes(
            NodeRegistry::DiscoveryMethod::STATIC_CONFIG);
        
        // 3. 启动各节点上的进程组
        for (auto& group : cfg.process_groups) {
            if (isLocalGroup(group)) {
                // 本节点的进程组:本地 fork
                launchLocalProcess(group);
            } else {
                // 远程节点的进程组:SSH 启动
                launchRemoteProcess(group);
            }
        }
        
        // 4. 等待所有进程就绪
        waitForAllProcessesReady();
        
        // 5. 启动健康监控
        NodeRegistry::instance().startHeartbeat(
            cfg.distributed_clock.heartbeat_interval);
        
        return true;
    }
    
private:
    // 本地进程启动(fork)
    pid_t launchLocalProcess(const ProcessGroupConfig& group) {
        pid_t pid = fork();
        if (pid == 0) {
            setCpuAffinity(group.cpu_cores);
            setGpuVisibility(group.gpu_devices);
            execl(group.executable.c_str(), group.executable.c_str(),
                  "--role", group.name.c_str(),
                  "--node", myNodeName_.c_str(),
                  nullptr);
            _exit(1);
        }
        local_processes_[group.name] = pid;
        return pid;
    }
    
    // 远程进程启动(SSH)
    bool launchRemoteProcess(const ProcessGroupConfig& group) {
        // 通过 SSH 在远程节点启动
        std::string cmd = "ssh " + group.node_name + " '" +
                          group.executable + " --role " + group.name +
                          " --node " + group.node_name + " &'";
        int ret = system(cmd.c_str());
        return ret == 0;
    }
    
    // 监控所有进程
    void monitor() {
        // 本地进程:waitpid
        for (auto& [name, pid] : local_processes_) {
            int status;
            if (waitpid(pid, &status, WNOHANG) != 0) {
                handleProcessExit(name, status);
            }
        }
        // 远程进程:靠心跳超时检测
        // NodeRegistry 自动处理
    }
    
    // 节点故障处理
    void onNodeFailure(const std::string& node_name) {
        log_error("Node " + node_name + " failed!");
        
        // 策略 1: 降级运行(丢失的系统用默认值)
        // 策略 2: 重新分配到其他节点
        // 策略 3: 暂停整个仿真
        auto strategy = config_.failure_strategy;
        
        if (strategy == "degrade") {
            degradeSimulation(node_name);
        } else if (strategy == "migrate") {
            migrateProcesses(node_name);
        } else {
            pauseSimulation();
        }
    }
};
```

### 11.8 分布式 MasterClock(主备切换)

多机部署时,MasterClock 必须高可用:

```cpp
class DistributedMasterClock : public MasterClockSystem {
public:
    enum class Role { PRIMARY, BACKUP, FOLLOWER };
    
    void onInit(const SystemConfig& cfg) override {
        role_ = determineRole(cfg);
        
        if (role_ == Role::PRIMARY) {
            // 主时钟:正常驱动
            startDriving();
        } else if (role_ == Role::BACKUP) {
            // 备时钟:监听主时钟心跳,准备接管
            startMonitoring();
        }
    }
    
    // 主时钟:广播 TimeTick + 心跳
    void drive() override {
        while (running_) {
            if (role_ != Role::PRIMARY) {
                std::this_thread::sleep_for(100ms);
                continue;
            }
            
            // 生成 TimeTick
            TimeTick tick = generateTick();
            
            // 广播到所有节点
            publish("time.tick", tick);
            
            // 心跳(告诉备时钟我还活着)
            publish("clock.heartbeat", {sim_time_, frame_id_});
            
            // 同步状态给备时钟
            if (frame_id_ % 10 == 0) {  // 每 10 帧同步一次
                publish("clock.state", serializeState());
            }
            
            sleepUntilNextFrame();
        }
    }
    
    // 备时钟:监听心跳,超时则接管
    void startMonitoring() {
        last_heartbeat_ = wallNow();
        subscribe("clock.heartbeat");
        
        monitor_thread_ = std::thread([this] {
            while (running_) {
                double now = wallNow();
                if (now - last_heartbeat_ > failover_timeout_) {
                    // 主时钟心跳超时,接管!
                    log_warn("Primary clock timeout, taking over!");
                    role_ = Role::PRIMARY;
                    startDriving();
                    break;
                }
                std::this_thread::sleep_for(100ms);
            }
        });
    }
    
    // 接收主时钟心跳
    void onMessage(const Message& msg) override {
        if (msg.channel == hash("clock.heartbeat")) {
            last_heartbeat_ = wallNow();
            auto state = deserialize(msg);
            syncState(state);  // 同步仿真时间
        }
    }
    
private:
    Role role_;
    double last_heartbeat_;
    double failover_timeout_;
    std::thread monitor_thread_;
};
```

**主备切换流程**:
```
正常:
  PRIMARY (ground_node) ──heartbeat──→ BACKUP (rt_node)
  PRIMARY 驱动仿真,BACKUP 监听

故障:
  PRIMARY 崩溃 ──✗── BACKUP 3 秒未收到心跳
  BACKUP 升级为 PRIMARY,继续驱动
  广播 "clock.takeover" 通知所有节点

恢复:
  原 PRIMARY 重启 → 发现已有新 PRIMARY → 降为 BACKUP
```

### 11.9 跨机消息路由

多机时,消息需要跨网络路由:

```
[ground_node]                    [rt_node]
PhysicsSystem                    RTSystem
    │                                │
    │ publish("scene.state")         │
    ▼                                ▼
  SHM (本地)                     SHM (本地)
    │                                │
    └──► DDS Transport ◄─────────────┘
              │
              │ DDS 网络多播
              │
    ┌─────────┴─────────┐
    │                   │
[signal_node]      [vis_node]
SignalSystem        VisualSystem
    │                   │
    │ subscribe         │ subscribe
    ▼                   ▼
  SHM (本地)         SHM (本地)
    │                   │
    └─► DDS ◄───────────┘
```

**路由规则**(MessageBus 自动处理):
1. 同机订阅者 → SHM 直接投递
2. 跨机订阅者 → DDS 网络投递
3. 混合订阅者 → 先 SHM 再 DDS(或反之)

```cpp
class MultiMachineMessageBus : public MessageBus {
public:
    void publish(const std::string& channel, Message msg) override {
        // 1. 本地订阅者:SHM 投递(低延迟)
        local_transport_->publish(channel, msg);
        
        // 2. 远程订阅者:DDS 投递(跨网络)
        if (hasRemoteSubscribers(channel)) {
            remote_transport_->publish(channel, msg);
        }
    }
    
    void subscribe(const std::string& channel, SystemBase* sub) override {
        // 本地订阅
        local_transport_->subscribe(channel, sub);
        
        // 如果是首次订阅此频道,在 DDS 上也订阅
        if (local_subscribers_[channel].size() == 1) {
            remote_transport_->subscribe(channel, 
                [this, channel](const auto& bytes) {
                    // DDS 收到远程消息,转发给本地订阅者
                    local_transport_->forwardToLocal(channel, bytes);
                });
        }
    }
    
private:
    std::shared_ptr<ITransport> local_transport_;   // SHM
    std::shared_ptr<ITransport> remote_transport_;  // DDS
    std::map<ChannelID, std::vector<SystemBase*>> local_subscribers_;
};
```

### 11.10 网络分区容错

```cpp
class NetworkPartitionHandler {
public:
    // 检测网络分区
    void checkPartition() {
        auto alive_nodes = NodeRegistry::instance().getAliveNodes();
        
        if (alive_nodes.size() < total_nodes_ * 0.5) {
            // 少数派,进入隔离模式
            enterIsolationMode();
        }
        
        if (isMasterClockNode() && !isPrimaryPartition()) {
            // 主时钟在少数派分区,必须停止驱动
            // 避免脑裂(split-brain)
            stopDriving();
        }
    }
    
    // 隔离模式:只运行本地系统,不与其他节点通信
    void enterIsolationMode() {
        log_warn("Network partition detected, entering isolation mode");
        remote_transport_->pause();  // 暂停跨机通信
        // 本地系统继续运行,但标记数据为 "stale"
    }
    
    // 分区恢复
    void onPartitionHealed() {
        log_info("Network partition healed, rejoining cluster");
        // 1. 同步缺失的状态
        // 2. 恢复跨机通信
        // 3. 重新对齐仿真时间
    }
};
```

### 11.11 部署模式选择指南

| 场景 | 推荐模式 | 通信 | 时钟 | 适用规模 |
|------|---------|------|------|---------|
| 开发调试 | single_process | in_process | 单机 | < 5 系统 |
| 功能测试 | single_process + multi_thread | in_process | 单机 | < 10 系统 |
| 单机性能 | multi_process_single_machine | shm | 单机 | < 20 系统,1-2 GPU |
| 大规模仿真 | multi_process_multi_machine | shm + dds | 分布式主备 | 20+ 系统,多 GPU |
| 容错部署 | multi_process_multi_machine | shm + dds | 分布式主备 | 关键任务 |

### 11.12 单进程↔多机切换的无感知性

**核心设计**: 系统代码完全不感知部署模式

```cpp
// 这段代码在单进程、多进程单机、多进程多机下都能工作
class PhysicsSystem : public SystemBase {
    void onTick(const TimeTick& tick) override {
        engine_->step(tick.dt);
        publish("scene.state", serializeState());
        // 单进程:  in_process 同步调用
        // 多进程:  shm 共享内存传输
        // 多机:    dds 网络传输(自动序列化)
    }
};
```

**切换只需改配置**:
```bash
# 单进程(开发)
./vsgraysim_app --config single_process.json

# 多进程单机(测试)
./vsgraysim_app --config multi_process_local.json

# 多机(生产)
./vsgraysim_app --config multi_machine.json  # 自动 SSH 启动远程节点
```
### 11.13 进程间系统树同步

多进程时,每个进程只构建**自己负责的子系统**,但需要知道完整系统树:

```cpp
// 每个进程启动时:
void initProcess(const std::string& role, const SystemConfig& full_tree) {
    // 1. 构建完整系统树(用于消息路由)
    auto full_root = buildSystemTree(full_tree);
    
    // 2. 过滤出本进程负责的系统
    auto my_systems = filterByRole(full_root, role);
    
    // 3. 只 init/start 本进程的系统
    for (auto& sys : my_systems) {
        sys->init(...);
        sys->start();
    }
    
    // 4. 消息总线连接到其他进程
    bus_->connectToPeers();  // 本地 SHM + 跨机 DDS
}
```

---

## 12. 可扩展性 v3.1

### 12.1 新增系统

```cpp
class MySystem : public SystemBase { ... };
REGISTER_SYSTEM(MySystem, "MY_SYSTEM")
```

### 12.2 新增 TickStrategy

```cpp
class MyStrategy : public TickStrategy { ... };
REGISTER_TICK_STRATEGY(MyStrategy, "my_strategy")
```

### 12.3 新增 Transport

```cpp
class MyTransport : public ITransport { ... };
REGISTER_TRANSPORT(MyTransport, "my_transport")
```

### 12.4 新增 Serializer

```cpp
class MySerializer : public ISerializer { ... };
REGISTER_SERIALIZER(MySerializer, "my_serializer")
```

### 12.5 新增 ComputeBackend

```cpp
class MyBackend : public IComputeBackend { ... };
REGISTER_COMPUTE_BACKEND(MyBackend, "my_backend")
```

### 12.6 配置引用新组件

```json
{
    "type": "MY_SYSTEM",
    "tick_strategy": "my_strategy",
    "transport": { "default": "my_transport" },
    "channel_serializers": { "*": "my_serializer" },
    "compute_backend": "my_backend"
}
```

**无需修改框架**,五类组件均可独立扩展。

---

## 13. 完整工作流 v3.0

### 13.1 启动流程

```cpp
int main(int argc, char** argv) {
    // 1. 加载配置
    auto cfg = loadJson(argv[--config]);
    
    // 2. 应用环境变量覆盖
    applyEnvOverrides(cfg);
    
    // 3. v3.0: 设备发现
    DeviceManager::instance().discoverDevices();
    
    // 4. v3.0: 根据部署模式决定启动方式
    auto& deploy = cfg["deployment"];
    if (deploy["mode"] == "single_process") {
        launchSingleProcess(cfg);
    } else if (deploy["mode"] == "multi_process") {
        launchMultiProcess(cfg);  // fork 子进程
    } else {
        launchHybrid(cfg);
    }
}

void launchSingleProcess(const Config& cfg) {
    // 构建完整系统树(递归)
    SystemID next_id = 1;
    auto root = buildSystemTree(cfg, next_id);
    //   buildSystemTree 内部:
    //   - SystemFactory::create(type_name) 创建系统
    //   - TickStrategyFactory::create(tick_strategy) 创建策略
    //   - StateMachine::loadFromConfig(state_machine) 加载状态机
    //   - TransportFactory + SerializerFactory 配置 MessageBus
    //   - v3.0: ComputeBackendFactory::create(compute_backend) 创建计算后端
    //   - v3.0: DeviceManager 分配设备(根据 computeReq())
    //   - 递归 children
    
    root->init(cfg);
    root->start();
    
    auto* clock = root->findChild("MasterClock")->as<MasterClockSystem>();
    clock->drive();  // 进入主循环
    
    root->stop();
}

void launchMultiProcess(const Config& cfg) {
    // 主进程:fork 所有子进程组
    ProcessManager pm;
    pm.launch(cfg.deployment);
    
    // 等待所有子进程就绪
    pm.waitForAllReady();
    
    // 本进程构建自己负责的系统
    auto my_role = getRoleFromEnv();
    auto my_systems = filterSystemsByRole(cfg, my_role);
    for (auto& sys : my_systems) {
        sys->init(...);
        sys->start();
    }
    
    // 主循环(如果是 MasterClock 所在进程)
    if (my_role == "main") {
        clock->drive();
    } else {
        // 子进程等待时序驱动
        waitForTimeTick();
    }
    
    pm.monitor();  // 监控进程健康
}
```

### 13.2 单帧执行 v3.0

```
MasterClock.drive():
  生成 TimeTick
  调用 root->tick(tick)
    │
    ▼
  SimulationRoot.tick(tick):
    tick_strategy_->decide(tick) → EXECUTE
    onTick(tick)
    tickChildren(tick):
      │
      ├── PhysicsSystem.tick [CPU 核心 0-3]:
      │     compute_backend_ = CPUBackend (NUMA 0)
      │     tick_strategy_->decide → EXECUTE_SUBSTEPS (substep=4)
      │     onSubStep: engine_->step(dt)  // CPU 计算
      │     publish("scene.state", msg)
      │         → serializer=flatbuffer → transport=shm
      │
      ├── RadarSystem.tick [CPU 核心 4]:
      │     compute_backend_ = CPUBackend
      │     tick_strategy_->decide → EXECUTE (1kHz)
      │     state_machine: SEARCHING → TRACKING
      │     publish("sensor.pulse_tx", msg)
      │
      ├── RTSystem.tick [GPU 0, Vulkan]:
      │     compute_backend_ = VulkanBackend (device=0, RT)
      │     tick_strategy_->decide → BATCH (累积 100μs)
      │     onTick: vkCmdTraceRays(...)  // GPU RT 计算
      │     CrossDeviceTransfer: GPU0 → CPU → SHM
      │     publish("sensor.echo", msg)
      │
      ├── SignalSystem.tick [GPU 1, CUDA]:
      │     compute_backend_ = CUDABackend (device=1)
      │     收到 sensor.echo (从 SHM 读)
      │     cudaMemcpy H2D → cudaFFT → cudaMemcpy D2H
      │     publish("signal.rd_frame", msg)
      │
      └── VisualSystem.tick [GPU 0, Vulkan]:
            compute_backend_ = VulkanBackend (与 RT 共享 GPU 0)
            零拷贝读取 RT 的场景数据
            渲染 + 显示
```

### 13.3 多进程时的单帧执行

```
[main 进程] MasterClock 生成 TimeTick
    │
    ├── 本进程: SceneSystem.tick, PhysicsSystem.tick, SensorSystem.tick
    │
    ├── SHM 广播 TimeTick → [rt 进程]
    │       [rt 进程] RTSystem.tick (GPU 0)
    │       SHM 发布 sensor.echo
    │
    ├── SHM 广播 TimeTick → [signal 进程]
    │       [signal 进程] SignalSystem.tick (GPU 1)
    │       SHM 发布 signal.rd_frame
    │
    └── SHM 广播 TimeTick → [vis 进程]
            [vis 进程] VisualSystem.tick (GPU 0)
            读取 scene.state + signal.rd_frame
            渲染
```

---

## 14. 与现有代码对接

### 14.1 改造对照

| 现有 | 改造为 | 计算后端 | 改动 |
|------|--------|---------|------|
| [Simulation](../include/vsgraysim/Simulation.h) | SimulationRoot : SystemBase | CPU | 中 |
| [PhysicsEngine](../include/vsgraysim/layer0_phys/PhysicsEngine.h) | PhysicsSystem(SubStep) | CPU(多核) | 小 |
| [SensorScheduler](../include/vsgraysim/layer4_sched/SensorScheduler.h) | RadarSystem(FixedRate) | CPU | 小 |
| [RayTracingPipelineWrap](../include/vsgraysim/layer2_rt/RayTracingPipelineWrap.h) | RTSystem(Batch) | **Vulkan GPU** | 小 |
| [SignalProcessor](../include/vsgraysim/layer3_cuda/SignalProcessor.h) | SignalSystem(状态机) | **CUDA GPU** | 小 |
| [Visualizer](../include/vsgraysim/layer5_vis/Visualizer.h) | VisualSystem(FixedRate) | **Vulkan GPU** | 小 |

### 14.2 PhysicsSystem 改造示例(带计算后端)

```cpp
class PhysicsSystem : public SystemBase {
    std::unique_ptr<PhysicsEngine> engine_;
public:
    PhysicsSystem(SystemID id, const std::string& name)
        : SystemBase(id, name, SystemType::PHYSICS) {}
    
    // v3.0: 声明计算需求
    ComputeRequirement computeReq() const override {
        ComputeRequirement req;
        req.preferred = ComputeDeviceType::CPU;
        req.preferred_cpu_cores = {0, 1, 2, 3};
        req.preferred_numa_node = 0;
        return req;
    }
    
    bool onInit(const SystemConfig& cfg) override {
        // v3.0: 框架已根据 computeReq() 分配 CPUBackend(绑定核心 0-3)
        engine_ = std::make_unique<PhysicsEngine>();
        auto g = cfg.params.value("gravity", std::vector<double>{0, -9.81, 0});
        engine_->setGravity({g[0], g[1], g[2]});
        return true;
    }
    
    void onSubStep(const TimeTick& tick, int idx) override {
        engine_->step(tick.substep_dt);
    }
    
    void onTick(const TimeTick& tick) override {
        for (auto& [id, body] : engine_->bodies()) {
            publish("scene.state", serializeEntityState(id, body, tick.sim_time));
        }
    }
};
REGISTER_SYSTEM(PhysicsSystem, "PHYSICS")
```

### 14.3 RTSystem 改造示例(带 GPU 后端)

```cpp
class RTSystem : public SystemBase {
    std::unique_ptr<RayTracingPipelineWrap> rt_;
public:
    // v3.0: 声明需要 Vulkan RT GPU
    ComputeRequirement computeReq() const override {
        ComputeRequirement req;
        req.preferred = ComputeDeviceType::VULKAN;
        req.requires_rt = true;
        req.min_memory_mb = 1024;
        req.preferred_gpu_id = config_.value("gpu_device", -1);
        return req;
    }
    
    bool onInit(const SystemConfig& cfg) override {
        // 框架已分配 VulkanBackend(device=0, supports_rt=true)
        auto* vk = dynamic_cast<VulkanBackend*>(compute_backend_.get());
        rt_ = std::make_unique<RayTracingPipelineWrap>(vk->device());
        return true;
    }
    
    void onTick(const TimeTick& tick) override {
        // 批量处理脉冲(BatchStrategy 累积)
        for (auto& pulse : pending_pulses_) {
            rt_->traceRays(pulse);  // GPU 计算
        }
        // 跨设备传输:GPU → CPU → SHM
        auto* scatter = rt_->getScatterPoints();  // GPU 内存
        std::vector<uint8_t> host_buf(scatter_size);
        compute_backend_->copyToHost(host_buf.data(), scatter, scatter_size);
        publish("sensor.echo", host_buf);
    }
};
REGISTER_SYSTEM(RTSystem, "RT")
```

---

## 15. 实施路线 v3.1

### 阶段 1: 核心框架 + 五大可配置组件(4-5 周)

- [ ] SystemBase v3.1(状态机 + TickStrategy + Transport + Serializer + ComputeBackend)
- [ ] StateMachine + JSON 加载
- [ ] TickStrategy 接口 + 6 个内置策略
- [ ] ITransport 接口 + InProcessTransport
- [ ] ISerializer 接口 + Json/Binary/Raw 三个内置
- [ ] IComputeBackend 接口 + CPUBackend
- [ ] DeviceManager(设备发现)
- [ ] 五个 Factory + REGISTER_* 宏
- [ ] buildSystemTree v3.1(解析全部配置)
- [ ] 单元测试

**验收**: 配置驱动构建系统树,各组件可切换

### 阶段 2: 现有代码接入(2-3 周)

- [ ] 包装 6 层为 SystemBase 子类(含 computeReq 声明)
- [ ] 编写 default.json 配置
- [ ] 验收:现有功能不退化

### 阶段 3: 多进程单机部署(3-4 周)

- [ ] SharedMemoryTransport
- [ ] MultiMachineProcessManager(本地 fork)
- [ ] DeploymentConfig 解析(单进程/多进程单机)
- [ ] 拆分系统到多进程
- [ ] 验收:多进程单机版与单进程行为一致

### 阶段 4: GPU 计算后端(3-4 周)

- [ ] CUDABackend(显存管理 + 异步流)
- [ ] VulkanBackend(VkDevice + RT)
- [ ] CrossDeviceTransfer(跨设备数据传输)
- [ ] RTSystem 迁移到 VulkanBackend
- [ ] SignalSystem 迁移到 CUDABackend
- [ ] 验收:GPU 加速生效,多 GPU 分布

### 阶段 5: 高级序列化(2 周)

- [ ] FlatBufferSerializer(零拷贝)
- [ ] ProtobufSerializer(跨语言)
- [ ] XmlSerializer(legacy 互操作)
- [ ] 验收:不同序列化可按频道切换

### 阶段 6: 多机部署 + 分布式时钟(4-5 周)

- [ ] **NodeRegistry(节点发现/注册/心跳)**
- [ ] **DDSTransport(FastDDS/CycloneDDS)**
- [ ] **MultiMachineMessageBus(SHM+DDS 分层路由)**
- [ ] **DistributedMasterClock(主备切换)**
- [ ] **远程进程 SSH 启动**
- [ ] **NetworkPartitionHandler(分区容错)**
- [ ] 验收:3 节点集群,跨机态势延迟 < 50ms,主备切换 < 3s

### 阶段 7: 高级策略 + CPU 亲和性(2 周)

- [ ] AdaptiveStrategy(自适应频率)
- [ ] MultiRateStrategy(多速率)
- [ ] CPU 亲和性 + NUMA 感知
- [ ] 验收:负载变化时频率自动调整,CPU 缓存命中率提升

### 阶段 8: 新域扩展(持续)

- [ ] OrbitSystem(SGP4,1Hz,CPU 双精度)
- [ ] AeroSystem(空气动力学,1ms,CPU)
- [ ] SARSystem(状态机驱动,CUDA)
- [ ] CrossInducSystem(事件驱动,CPU)

---

## 16. 设计信条 v3.1

1. **万物皆系统**: 从仿真根到单个传感器,都是 SystemBase
2. **树形组合**: 大系统 = 子系统递归组合(Composite)
3. **自循环自治**: 每个系统**自定义频率策略**(TickStrategy)
4. **状态机驱动**: 每个系统**自定义状态机**(StateMachine)
5. **运行机制自定义**: onTick/onSubStep/onBatch 完全由子类决定
6. **通信架构可配置**: Transport 后端可切换(in_process/shm/dds/grpc/zmq)
7. **数据格式可配置**: Serializer 可切换(json/xml/binary/protobuf/flatbuffer/raw)
8. **计算后端可配置**: ComputeBackend 可切换(cpu/cuda/vulkan/auto)
9. **三种部署模式**: 单进程 / 多进程单机 / 多进程多机,配置驱动,代码不变
10. **频道级配置**: 不同频道可用不同 Transport + Serializer
11. **CPU 亲和性**: 核心绑定 + NUMA 感知,避免跨 NUMA 内存访问
12. **多 GPU 分布**: 不同系统可分配不同 GPU,避免资源争抢
13. **跨设备零拷贝**: 同 GPU 上 Vulkan+CUDA 可共享显存
14. **多机节点管理**: 节点发现/注册/健康监控/故障恢复
15. **分布式时序**: MasterClock 主备切换,网络分区容错
16. **通信分层**: 同机 SHM(低延迟)+ 跨机 DDS(自动发现)
17. **配置驱动**: 全部 JSON 配置 + 环境变量覆盖
18. **五类组件可扩展**: System/Strategy/Transport/Serializer/ComputeBackend 均可注册新增

---

## 17. 版本对比总结

| 维度 | v1.0 | v2.0 | v3.0 | v3.1 |
|------|------|------|------|------|
| 状态 | 6 态固定 | 自定义状态机 | 同 v2.0 | 同 v2.0 |
| 频率 | 固定 | TickStrategy(6 种) | 同 v2.0 | 同 v2.0 |
| 通信 | 内置 | ITransport(5 种) | 同 v2.0 | 同 v2.0 |
| 序列化 | 内置 | ISerializer(6 种) | 同 v2.0 | 同 v2.0 |
| 计算后端 | CPU only | CPU only | IComputeBackend(4 种) | 同 v3.0 |
| **部署模式** | 单进程 | 单进程 | 单进程/多进程 | **单进程/多进程单机/多进程多机** |
| **CPU 亲和性** | 无 | 无 | 核心绑定+NUMA | 同 v3.0 |
| **GPU 管理** | 无 | 无 | 多 GPU 分布 | 同 v3.0 |
| **多机节点** | 无 | 无 | 无 | **NodeRegistry + 发现/监控** |
| **分布式时钟** | 无 | 无 | 无 | **主备切换 + 分区容错** |
| **跨机通信** | 无 | 无 | 无 | **SHM + DDS 分层路由** |
| 扩展点 | 1 类 | 4 类 | 5 类 | 5 类 |

---

**文档版本**: v3.1
**更新日期**: 2026-07-31
**核心增强**: 三种部署模式 + 多机节点管理 + 分布式 MasterClock + 网络分区容错