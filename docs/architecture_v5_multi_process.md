# vsgraysim 架构演进设计 v5 — 多进程 + 算法抽象层 + 遥测体系

> 本文档基于当前 v4 单进程架构，规划向多进程分布式架构的演进方案。
> 核心变化：进程拆分、算法抽象层引入、全系统遥测体系集成。

---

## 一、总体架构

### 1.1 进程拓扑

```
┌─────────────────────────────────────────────────────────────────────┐
│  Launcher（进程管理器）                                                │
│  职责：进程生命周期管理、健康监控、重启策略、日志聚合查看                  │
│  界面：精简导航页（仅进程启动/停止/状态）                                 │
└────────┬──────────┬──────────┬──────────┬──────────┬────────────────┘
         │          │          │          │          │
         ▼          ▼          ▼          ▼          ▼
   ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐
   │地形编辑  │ │场景设计  │ │仿真控制  │ │雷达显控  │ │SAR显控   │
   │进程      │ │进程      │ │进程      │ │进程      │ │进程      │
   └─────────┘ └─────────┘ └─────────┘ └─────────┘ └─────────┘
                                          ┌─────────┐
                                          │红外显控  │
                                          │进程      │
                                          └─────────┘
```

### 1.2 进程间通信架构

```
┌─────────────────────────────────────────────────────────────────┐
│                   IPC 传输层 (抽象接口)                          │
│                                                                 │
│   ┌─────────────────┐  ┌────────────────┐  ┌──────────────────┐ │
│   │  InProcessTransport│  │  ShmTransport   │  │  ZmqTransport    │ │
│   │  (单进程调试用)    │  │  (共享内存, 默认) │  │  (多机部署用)     │ │
│   └─────────────────┘  └────────────────┘  └──────────────────┘ │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                   通信通道 (Channel)                              │
│                                                                 │
│  ┌──────────────────────┐  ┌──────────────────┐                  │
│  │  DataChannel          │  │  ControlChannel   │                  │
│  │  GPU 句柄 + 元数据传递 │  │  命令/响应         │                  │
│  │  发布/订阅模式         │  │  请求/回复模式      │                  │
│  └──────────────────────┘  └──────────────────┘                  │
│                                                                 │
│  ┌──────────────────────┐  ┌──────────────────┐                  │
│  │  TelemetryChannel     │  │  FileChannel      │                  │
│  │  日志/故障/性能数据    │  │  场景文件/配置同步  │                  │
│  │  发布/订阅模式         │  │  推送/拉取模式      │                  │
│  └──────────────────────┘  └──────────────────┘                  │
└─────────────────────────────────────────────────────────────────┘
```

### 1.3 进程间数据流

```
                   场景 JSON (文件系统)
                    ──────────────→
┌──────────┐                      ┌──────────┐
│ 地形编辑  │                      │ 场景设计  │
│ 进程      │ 地形参考 (FileChannel)│ 进程      │
└──────────┘ ←──────────────────┘  └──────────┘
                                          │
                                    场景 JSON (文件系统)
                                          ▼
                               ┌──────────────────────┐
                               │  仿真控制进程          │
                               │  TimingBus 驱动        │
                               │  RT 射线追踪生成数据    │
                               │  GPU 句柄发布          │
                               └──┬──────┬──────┬──────┘
                                  │      │      │
                     DataChannel  │      │      │  DataChannel
                    (GPU 句柄)     │      │      │  (GPU 句柄)
                                  ▼      ▼      ▼
                          ┌──────┐ ┌──────┐ ┌──────┐
                          │雷达   │ │SAR   │ │红外   │
                          │显控   │ │显控   │ │显控   │
                          │进程   │ │进程   │ │进程   │
                          └──┬───┘ └──┬───┘ └──┬───┘
                             │        │         │
                    ControlChannel (命令/响应)
                             │        │         │
                             └────────┴─────────┘
                                    │
                           返回到仿真控制进程
```

### 1.4 通信协议设计

**DataChannel** — GPU 数据传递：

```cpp
// 消息格式
struct DataMessage {
    uint64_t    frame_id;        // 帧序号
    SensorType  type;            // 传感器类型
    uint32_t    equipment_id;    // 装备 ID
    double      sim_time;        // 仿真时间戳
    uint32_t    width, height;   // 数据尺寸
    uint32_t    size_bytes;      // GPU 缓冲区大小
    // GPU 句柄（通过共享内存传递）
    // 实际数据通过 cudaIpcMemHandle 在 GPU 间共享
    // 不经过 IPC 传输层拷贝
};
```

**ControlChannel** — 命令/响应：

```cpp
// 命令消息
struct ControlMessage {
    enum class Type {
        PowerOn, PowerOff,           // 开关机
        SetParam,                    // 参数调整
        RequestStatus,               // 状态查询
        ReportHealth,                // 健康报告
        // ...
    };
    Type        type;
    uint32_t    equipment_id;
    SensorType  equipment_type;
    uint32_t    seq;                 // 序列号（去重）
    // 参数载荷（JSON 或 protobuf）
    std::vector<uint8_t> payload;
};
```

**TelemetryChannel** — 遥测数据：

```cpp
// 遥测消息
struct TelemetryMessage {
    enum class Category {
        Log, Fault, Performance, Health
    };
    Category    category;
    uint64_t    timestamp_ms;
    uint32_t    source_pid;        // 源进程 ID
    std::string source_name;       // 源进程名称
    // 载荷（JSON 或二进制）
    std::vector<uint8_t> payload;
};
```

---

## 二、项目目录结构演进

### 2.1 顶层目录

```
vsgraysimulation/
├── app/                     # 可执行文件入口（每个进程一个）
│   ├── launcher/            # 进程管理器
│   ├── terrain_editor/      # 地形编辑进程
│   ├── scene_designer/      # 场景设计进程
│   ├── sim_control/         # 仿真控制进程
│   ├── radar_control/       # 雷达显控进程
│   ├── sar_control/         # SAR 显控进程
│   └── ir_control/          # 红外显控进程
├── lib/                     # 库模块
│   ├── common/              # 公共基础（类型定义、工具函数）
│   ├── ipc/                 # 进程间通信层 [NEW]
│   │   ├── ITransport.h     # 传输抽象接口
│   │   ├── transports/      # 传输实现
│   │   │   ├── InProcessTransport.h/.cpp
│   │   │   ├── ShmTransport.h/.cpp
│   │   │   └── ZmqTransport.h/.cpp
│   │   ├── channels/        # 通信通道
│   │   │   ├── DataChannel.h/.cpp
│   │   │   ├── ControlChannel.h/.cpp
│   │   │   └── TelemetryChannel.h/.cpp
│   │   └── discovery/       # 进程发现
│   │       └── ServiceDiscovery.h/.cpp
│   ├── algo/                # 算法抽象层 [NEW]
│   │   ├── core/            # 公共算法基元
│   │   │   ├── IFftProcessor.h
│   │   │   ├── IWindowFunction.h
│   │   │   ├── IFilter.h
│   │   │   └── IThresholdDetector.h
│   │   ├── radar/           # 雷达算法
│   │   │   ├── IRadarProcessor.h
│   │   │   ├── IRadarDetector.h
│   │   │   ├── ITracker.h
│   │   │   └── impl/
│   │   │       ├── MtiMtdProcessor.h/.cpp
│   │   │       ├── CfarProcessor.h/.cpp
│   │   │       └── TrackManager.h/.cpp
│   │   ├── sar/             # SAR 算法
│   │   │   ├── ISarImagingEngine.h
│   │   │   ├── IAtrProcessor.h
│   │   │   └── impl/
│   │   │       ├── RdImagingEngine.h/.cpp
│   │   │       ├── CsImagingEngine.h/.cpp
│   │   │       └── SarAtrProcessor.h/.cpp
│   │   ├── infrared/        # 红外算法
│   │   │   ├── IIrProcessor.h
│   │   │   ├── ITemperatureMeasure.h
│   │   │   ├── IFusionEngine.h
│   │   │   └── impl/
│   │   │       ├── IrImageProcessor.h/.cpp
│   │   │       ├── TemperatureMeasure.h/.cpp
│   │   │       └── ImageFusion.h/.cpp
│   │   ├── simulation/      # 仿真生成算法
│   │   │   ├── IRadarSignalGenerator.h
│   │   │   ├── ISarSignalGenerator.h
│   │   │   └── IIrRadianceGenerator.h
│   │   ├── factory/         # 算法工厂
│   │   │   ├── AlgoFactory.h/.cpp
│   │   │   └── AlgoConfig.h/.cpp
│   │   └── CMakeLists.txt
│   ├── telemetry/           # 遥测体系 [NEW]
│   │   ├── log/             # 结构化日志
│   │   │   ├── LogManager.h/.cpp
│   │   │   ├── ILogSink.h
│   │   │   └── sinks/
│   │   │       ├── ConsoleSink.h/.cpp
│   │   │       ├── FileSink.h/.cpp
│   │   │       └── NetworkSink.h/.cpp
│   │   ├── fault/           # 故障管理
│   │   │   ├── FaultManager.h/.cpp
│   │   │   ├── FaultEvent.h
│   │   │   ├── IFaultDetector.h
│   │   │   ├── IFaultHandler.h
│   │   │   └── detectors/
│   │   │       ├── HealthCheckDetector.h/.cpp
│   │   │       ├── WatchdogDetector.h/.cpp
│   │   │       └── ThresholdDetector.h/.cpp
│   │   ├── perf/            # 性能监控
│   │   │   ├── PerfMonitor.h/.cpp
│   │   │   ├── Metrics.h
│   │   │   ├── IPerfCollector.h
│   │   │   └── collectors/
│   │   │       ├── FpsCollector.h/.cpp
│   │   │       ├── LatencyCollector.h/.cpp
│   │   │       ├── MemoryCollector.h/.cpp
│   │   │       ├── GpuCollector.h/.cpp
│   │   │       └── ThroughputCollector.h/.cpp
│   │   ├── health/          # 健康状态
│   │   │   ├── HealthReport.h
│   │   │   ├── HealthAggregator.h/.cpp
│   │   │   └── HealthStatus.h
│   │   └── CMakeLists.txt
│   ├── core/                # 仿真核心库 (现有)
│   ├── systems/             # 子系统库 (现有)
│   ├── cuda/                # CUDA 核函数 (现有)
│   └── framework/           # 框架库 (现有)
├── gui/                     # GUI 模块 (现有)
│   ├── base/                # 基础 UI 库 (现有)
│   ├── viewport3d/          # 3D 视口共享库 (现有)
│   └── ...                  # 各窗口 UI 适配
├── config/                  # 配置 (现有)
├── models/                  # 模型 (现有)
└── docs/                    # 文档 (现有)
```

### 2.2 库依赖关系

```
                  ┌───────────────┐
                  │  algo/        │  ← 纯算法，无 GUI 依赖
                  │  (算法抽象层)   │
                  └──────┬────────┘
                         │ 依赖
                         ▼
                  ┌───────────────┐
         ┌───────→│  telemetry/   │←────────┐
         │        │  (遥测体系)     │         │
         │        └──────┬────────┘         │
         │               │ 依赖              │
         │               ▼                  │
         │        ┌───────────────┐         │
         │        │  common/      │         │
         │        │  (公共基础)     │         │
         │        └──────┬────────┘         │
         │               │                  │
         ▼               ▼                  │
  ┌───────────────┐ ┌───────────────┐       │
  │  gui/         │ │  ipc/         │       │
  │  (GUI 层)     │ │  (IPC 通信层)  │       │
  └───────────────┘ └───────────────┘       │
         │               │                  │
         └───────────────┴──────────────────┘
                         │
                         ▼
                  ┌───────────────┐
                  │  core/        │
                  │  systems/     │
                  │  cuda/        │
                  │  framework/   │
                  └───────────────┘
```

---

## 三、算法抽象层设计

### 3.1 设计原则

- **接口与实现分离**：每个算法族定义纯虚接口，实现放在 `impl/` 子目录
- **工厂模式**：通过 `AlgoFactory` 按配置加载算法实现，运行时可替换
- **可配置化**：算法选择通过 JSON 配置驱动，无需重新编译
- **生命周期**：每个算法实现 `init() → process() → reset()` 三阶段生命周期
- **数据流**：输入/输出数据格式由接口定义，实现内部封装具体算法逻辑

### 3.2 算法接口示例

**雷达算法族**：

```cpp
// lib/algo/radar/IRadarProcessor.h
namespace vsgraysim::algo::radar {

/// 雷达信号处理器抽象接口
class IRadarProcessor {
public:
    virtual ~IRadarProcessor() = default;

    /// 初始化（加载参数、分配资源）
    virtual bool init(const AlgoConfig& cfg) = 0;

    /// 处理一帧雷达回波数据
    /// @param input  输入信号帧（时域/频域）
    /// @param output 处理结果（MTI/MTD 输出）
    virtual bool process(const SignalFrame& input, ProcessedFrame& output) = 0;

    /// 重置到初始状态
    virtual void reset() = 0;

    /// 返回处理器唯一标识
    virtual std::string name() const = 0;
};

/// 雷达检测器抽象接口
class IRadarDetector {
public:
    virtual ~IRadarDetector() = default;
    virtual bool init(const AlgoConfig& cfg) = 0;
    virtual bool detect(const ProcessedFrame& input, std::vector<Detection>& outputs) = 0;
    virtual void reset() = 0;
    virtual std::string name() const = 0;
};

/// 目标跟踪器抽象接口
class ITracker {
public:
    virtual ~ITracker() = default;
    virtual bool init(const AlgoConfig& cfg) = 0;
    virtual bool update(const std::vector<Detection>& detections,
                        std::vector<Track>& tracks) = 0;
    virtual void reset() = 0;
    virtual std::string name() const = 0;
};

} // namespace vsgraysim::algo::radar
```

**SAR 算法族**：

```cpp
// lib/algo/sar/ISarImagingEngine.h
namespace vsgraysim::algo::sar {

/// SAR 成像引擎抽象接口
class ISarImagingEngine {
public:
    virtual ~ISarImagingEngine() = default;
    virtual bool init(const AlgoConfig& cfg) = 0;

    /// 处理 SAR 回波并生成图像
    /// @param echoes   回波数据
    /// @param metadata 平台参数（轨迹、速度等）
    /// @param image    输出图像（复数或幅度）
    virtual bool image(const EchoData& echoes, const PlatformMetadata& metadata,
                       SarImage& image) = 0;
    virtual void reset() = 0;
    virtual std::string name() const = 0;
};

/// ATR 处理器抽象接口
class IAtrProcessor {
public:
    virtual ~IAtrProcessor() = default;
    virtual bool init(const AlgoConfig& cfg) = 0;
    virtual bool detect(const SarImage& image, std::vector<AtrDetection>& targets) = 0;
    virtual void reset() = 0;
    virtual std::string name() const = 0;
};

} // namespace vsgraysim::algo::sar
```

### 3.3 算法工厂

```cpp
// lib/algo/factory/AlgoFactory.h
namespace vsgraysim::algo {

/// 算法工厂 — 按类型和名称创建算法实例
class AlgoFactory {
public:
    /// 注册算法创建器（自动注册或手动注册）
    template <class T>
    static void registerCreator(const std::string& category,
                                const std::string& name);

    /// 创建算法实例
    /// @param category 算法类别 (radar/sar/infrared/simulation)
    /// @param name     算法名称 (cfar/mti/rd_imaging/...)
    /// @param cfg      算法配置
    static std::unique_ptr<IAlgoBase> create(
        const std::string& category,
        const std::string& name,
        const AlgoConfig& cfg);

    /// 列出某类别下所有已注册算法
    static std::vector<std::string> listAlgorithms(const std::string& category);
};

} // namespace vsgraysim::algo
```

### 3.4 算法配置格式

```json
{
    "radar_processor": {
        "algorithm": "mti_mtd",
        "params": {
            "mti_mode": "double_canceller",
            "mtd_fft_size": 64,
            "window": "hamming"
        }
    },
    "radar_detector": {
        "algorithm": "ca_cfar",
        "params": {
            "guard_cells": 4,
            "reference_cells": 16,
            "threshold_factor": 1.5
        }
    },
    "tracker": {
        "algorithm": "kf_jonker",
        "params": {
            "gate_size": 3.0,
            "m_n_logic": "3/4",
            "max_coast": 5
        }
    }
}
```

---

## 四、遥测体系设计

### 4.1 体系结构

```
┌─────────────────────────────────────────────────────────────────────┐
│                         TelemetryManager                            │
│                 每个进程一个实例，管理三类数据                         │
│                                                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │
│  │  LogManager   │  │  FaultManager │  │  PerfMonitor  │               │
│  │  结构化日志    │  │  故障管理     │  │  性能监控     │               │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘               │
│         │                 │                 │                        │
│         ▼                 ▼                 ▼                        │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                     TelemetryChannel                          │   │
│  │  通过 IPC 将数据发送到 Launcher 进程（或外部日志服务器）         │   │
│  └──────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│  Launcher 进程中的遥测聚合器                                         │
│                                                                     │
│  ┌──────────────────┐  ┌──────────────────┐                         │
│  │  LogAggregator    │  │  FaultAggregator  │                         │
│  │  (日志聚合展示)     │  │  (故障告警)       │                         │
│  └──────────────────┘  └──────────────────┘                         │
│  ┌──────────────────┐  ┌──────────────────┐                         │
│  │  PerfAggregator   │  │  HealthDashboard  │                         │
│  │  (性能总览)        │  │  (全局健康面板)    │                         │
│  └──────────────────┘  └──────────────────┘                         │
└─────────────────────────────────────────────────────────────────────┘
```

### 4.2 结构化日志

```cpp
// lib/telemetry/log/LogManager.h
namespace vsgraysim::telemetry {

enum class LogLevel {
    Trace, Debug, Info, Warn, Error, Fatal
};

struct LogEntry {
    uint64_t    timestamp_ms;
    LogLevel    level;
    std::string source;        // 模块/组件名
    std::string category;      // 日志类别
    std::string message;
    // 结构化字段（可选）
    nlohmann::json fields;
};

class LogManager {
public:
    static LogManager& instance();

    void addSink(std::unique_ptr<ILogSink> sink);

    void log(LogLevel level, const std::string& source,
             const std::string& category, const std::string& message,
             const nlohmann::json& fields = {});

    // 便捷方法
    void info(const std::string& source, const std::string& msg);
    void warn(const std::string& source, const std::string& msg);
    void error(const std::string& source, const std::string& msg);

    // 设置最低日志级别（可在运行时动态调整）
    void setMinLevel(LogLevel level);
};

/// 日志输出接口
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void write(const LogEntry& entry) = 0;
    virtual void flush() = 0;
};

} // namespace vsgraysim::telemetry
```

### 4.3 故障管理

```cpp
// lib/telemetry/fault/FaultManager.h
namespace vsgraysim::telemetry {

enum class FaultSeverity {
    Minor,      // 不影响功能
    Major,      // 部分功能降级
    Critical,   // 功能完全丧失
    Fatal       // 进程无法继续运行
};

struct FaultEvent {
    uint64_t        timestamp_ms;
    uint64_t        fault_id;        // 故障唯一 ID
    FaultSeverity   severity;
    std::string     source;          // 故障源模块
    std::string     description;
    bool            auto_recoverable; // 是否可自动恢复
    nlohmann::json  context;         // 故障上下文
};

enum class FaultAction {
    None,            // 仅记录告警
    Retry,           // 重试操作
    Reset,           // 重置模块
    Fallback,        // 切换到备用模式
    Restart,         // 重启进程
    NotifyOperator   // 通知操作员
};

class FaultManager {
public:
    static FaultManager& instance();

    /// 注册故障检测器
    void addDetector(std::unique_ptr<IFaultDetector> detector);

    /// 注册故障处理策略
    void setHandler(FaultSeverity severity,
                    std::unique_ptr<IFaultHandler> handler);

    /// 上报故障事件
    void report(const FaultEvent& event);

    /// 故障回调（供 GUI 订阅）
    using FaultCallback = std::function<void(const FaultEvent&)>;
    void setFaultCallback(FaultCallback cb);

    /// 获取当前活跃故障列表
    std::vector<FaultEvent> activeFaults() const;
};

/// 故障检测器接口
class IFaultDetector {
public:
    virtual ~IFaultDetector() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    /// 每 tick 调用一次，检测到故障时调用 FaultManager::report()
    virtual void check() = 0;
};

/// 故障处理接口
class IFaultHandler {
public:
    virtual ~IFaultHandler() = default;
    virtual FaultAction handle(const FaultEvent& event) = 0;
};

} // namespace vsgraysim::telemetry
```

### 4.4 性能监控

```cpp
// lib/telemetry/perf/PerfMonitor.h
namespace vsgraysim::telemetry {

/// 预定义指标键
struct MetricKey {
    // 帧率类
    static constexpr const char* kFps          = "fps";
    static constexpr const char* kFrameTime    = "frame_time_ms";
    // 延迟类
    static constexpr const char* kLatency      = "latency_ms";
    static constexpr const char* kPipelineLatency = "pipeline_latency_ms";
    // 吞吐类
    static constexpr const char* kThroughput   = "throughput_mbps";
    static constexpr const char* kPublishRate  = "publish_rate_hz";
    // 资源类
    static constexpr const char* kCpuUsage     = "cpu_usage_percent";
    static constexpr const char* kMemoryUsage  = "memory_mb";
    static constexpr const char* kGpuMemory    = "gpu_memory_mb";
    static constexpr const char* kGpuUtil      = "gpu_util_percent";
    // 仿真类
    static constexpr const char* kSimTimeRatio = "sim_time_ratio";
    static constexpr const char* kFrameCount   = "frame_count";
};

struct MetricSample {
    std::string name;
    double      value;
    uint64_t    timestamp_ms;
    std::string unit;
    nlohmann::json tags;   // 维度标签（如 {"equipment":"radar", "channel":"0"}）
};

class PerfMonitor {
public:
    static PerfMonitor& instance();

    /// 注册性能数据收集器
    void addCollector(std::unique_ptr<IPerfCollector> collector);

    /// 记录一个指标采样
    void record(const MetricSample& sample);

    /// 批量记录
    void record(const std::vector<MetricSample>& samples);

    /// 获取最近 N 秒的指标数据（供 GUI 绘制曲线）
    std::vector<MetricSample> history(const std::string& name,
                                      double seconds) const;

    /// 获取当前瞬时值
    double current(const std::string& name) const;

    /// 设置指标回调（供 PerfMonitorPanel 等 GUI 组件订阅）
    using MetricCallback = std::function<void(const MetricSample&)>;
    void setMetricCallback(MetricCallback cb);
};

/// 性能数据收集器接口
class IPerfCollector {
public:
    virtual ~IPerfCollector() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    /// 定期收集指标并调用 PerfMonitor::record()
    virtual void collect() = 0;
};

} // namespace vsgraysim::telemetry
```

### 4.5 各进程集成遥测的方式

每个进程的 `main()` 中初始化 TelemetryManager：

```cpp
// 伪代码示例：每个进程的入口
int main(int argc, char* argv[]) {
    // 1. 初始化遥测体系
    auto& telemetry = TelemetryManager::instance();
    telemetry.init("radar_control");  // 进程名

    // 添加日志输出
    telemetry.logManager().addSink(
        std::make_unique<FileSink>("logs/radar_control.log"));

    // 添加性能收集器
    telemetry.perfMonitor().addCollector(
        std::make_unique<FpsCollector>());
    telemetry.perfMonitor().addCollector(
        std::make_unique<MemoryCollector>());

    // 添加故障检测器
    telemetry.faultManager().addDetector(
        std::make_unique<WatchdogDetector>(5000));  // 5s 看门狗

    // 2. 启动 IPC 遥测通道（发送到 Launcher 聚合）
    telemetry.startTelemetryChannel();

    // 3. 启动应用
    QApplication app(argc, argv);
    // ...
}
```

---

## 五、演进路线

### 阶段一：代码模块化（当前可做，无需重构）

| 步骤 | 内容 | 影响范围 |
|------|------|----------|
| 1 | 创建 `lib/algo/` 目录结构，将现有算法代码按接口抽出 | 无功能变化，仅文件移动 |
| 2 | 将 `CfarProcessor`、`MtiMtdProcessor` 等实现 `IRadarDetector`/`IRadarProcessor` 接口 | 接口适配 |
| 3 | 将 `SarImagingEngine`、`SarAtrProcessor` 实现 `ISarImagingEngine`/`IAtrProcessor` 接口 | 接口适配 |
| 4 | 将 `IrImageProcessor`、`TemperatureMeasure` 实现 `IIrProcessor`/`ITemperatureMeasure` 接口 | 接口适配 |
| 5 | 创建 `lib/telemetry/` 目录结构，实现 `LogManager`、`FaultManager`、`PerfMonitor` 内核 | 新文件 |
| 6 | 各 GUI 窗口中的 PerfMonitorPanel 改为订阅 `PerfMonitor` 数据 | 适配现有 GUI |
| 7 | 创建 `lib/ipc/` 目录结构，定义传输抽象接口 | 新文件，不影响现有代码 |

### 阶段二：进程拆分（需较大重构）

| 步骤 | 内容 | 影响范围 |
|------|------|----------|
| 1 | `app/launcher/` — 将导航页改为进程管理器，通过 `QProcess` 启动子进程 | 替换 LauncherWindow |
| 2 | `app/sim_control/` — 仿真控制拆为独立进程，通过 IPC 发布数据 | 重构 SimulationControlWindow |
| 3 | `app/radar_control/` — 雷达显控拆为独立进程，通过 IPC 订阅数据 | 重构 RadarControlWindow |
| 4 | `app/sar_control/` — SAR 显控拆为独立进程 | 重构 SARControlWindow |
| 5 | `app/ir_control/` — 红外显控拆为独立进程 | 重构 InfraredControlWindow |
| 6 | `app/terrain_editor/` — 地形编辑拆为独立进程 | 较简单，不涉及 IPC |
| 7 | `app/scene_designer/` — 场景设计拆为独立进程 | 较简单，不涉及 IPC |

### 阶段三：遥测体系集成

| 步骤 | 内容 | 影响范围 |
|------|------|----------|
| 1 | 每个进程启动时初始化 TelemetryManager | 所有进程入口 |
| 2 | 用 `TelemetryChannel` 将日志/故障/性能数据发送到 Launcher | IPC 集成 |
| 3 | Launcher 实现 LogAggregator 展示所有进程日志 | 新 UI 界面 |
| 4 | Launcher 实现 HealthDashboard 展示全局健康状态 | 新 UI 界面 |
| 5 | 集成故障检测器（看门狗、健康检查、阈值检测） | 各进程 |
| 6 | 实现故障恢复策略（重试→重置→重启→通知操作员） | FaultManager |

---

## 六、与现有代码的兼容性

### 6.1 单进程调试模式

```cpp
// 在阶段二之前，可以通过配置切换为单进程模式
// 所有 IPC 调用退化为 InProcessTransport（直接函数调用）
// 算法接口和遥测接口在单进程/多进程下代码不变

// 启动配置示例
{
    "deployment_mode": "single_process",  // 或 "multi_process"
    "ipc": {
        "transport": "in_process",        // 或 "shm", "zmq"
        "data_channel": "shm",
        "control_channel": "zmq"
    }
}
```

### 6.2 接口兼容

- 算法接口是新增的，不影响现有代码的直接调用
- 现有代码可以继续直接调用 `CfarProcessor::process()`，同时新代码通过 `IRadarDetector` 接口调用
- 遥测接口是新增的，现有 `spdlog` 调用可以保留，同时在 `LogManager` 中包装 `spdlog` 作为后端
- 性能监控面板 `PerfMonitorPanel` 可以适配为订阅 `PerfMonitor` 数据

---

## 七、文档更新

本文档完成后，需要同步更新：
- 更新 `CMakeLists.txt` 顶层构建，新增 `lib/algo/`、`lib/telemetry/`、`lib/ipc/` 子目录
- 更新 `architecture_overview.md` 中的分层描述
- 更新 `system_modules.md` 中的模块描述