# v5 多进程架构落地差距核查（P1）

日期：2026-10-18
对照文档：`docs/architecture_v5_multi_process.md`（802 行，含进程拓扑、IPC 传输层、算法抽象层、遥测体系四大部分）

## 总评

v5 的三大支柱中，**遥测体系已实质落地**，**IPC 传输层和算法抽象层停留在"库已建成、业务未接线"状态**，**进程拆分（多进程拓扑）仅 Launcher 单臂落地**。

## 一、逐块核查结果

### 1. IPC 传输层（lib/ipc）— ⚠️ 库建成，业务基本未接线

| 组件 | 外部引用数 | 实际消费者 | 结论 |
|---|---|---|---|
| `ITransport` | 0 | — | 抽象接口，无外部消费（仅 lib/ipc 内部） |
| `InProcessTransport` | 0 | — | 单进程调试传输，未被任何进程使用 |
| `ShmTransport` | 1 | `src/broker/BrokerNet.cpp` | 仅数据链路网关一处在用，跨进程主通道未启用 |
| `DataChannel` | 0 | — | GPU 数据传递通道，无消费方 |
| `ControlChannel` | 0 | — | 命令/响应通道，无消费方 |
| `TelemetryChannel` | 1 | `lib/telemetry/TelemetryManager.cpp` | 遥测已走此通道 |
| `ServiceDiscovery` | 1 | `lib/telemetry/TelemetryManager.cpp` | 服务发现已用于遥测注册 |

**缺口**：DataChannel / ControlChannel / InProcessTransport 零业务接线——意味着跨进程"数据分发、命令下发"主链路尚未打通；当前进程间仍是 Qt/QProcess 起子进程 + 各自独立运行，未走 v5 设计的通道化通信。

### 2. 算法抽象层（lib/algo）— ⚠️ 建成但被单点消费

- `lib/algo` 组件仅被 `src/layer2_rt/*`（RayTracer / AtmosphereIntegrator / SceneBVH / CrossInductionDetector / RayTracingPipelineWrap）引用
- **没有被 `src/layer3b_signal`（IR/Vis/Radar/Sar 信号处理器）、`env_modules`、或任何 GUI 层引用**

**缺口**：算法抽象层的初衷是让各信号处理器（IR/雷达/SAR）与具体 RT/数值实现解耦，目前只有 layer2_rt 在用，layer3b 与 env_modules 的传感器物理仍是各自直连实现。

### 3. 遥测体系（lib/telemetry）— ✅ 实质落地

- `TelemetryManager` 已被 `app/scene_designer/main.cpp`、`app/sim_control/main.cpp`、`app/control/main.cpp`、`gui/base/BaseWindow.cpp` 接入
- Launcher 有健康监视面板（`gui/LauncherWindow.cpp` 显示 CPU/内存）
- 遥测经 `TelemetryChannel` + `ServiceDiscovery` 走通

**结论**：遥测是 v5 落地最好的一块。

### 4. 进程拆分（多进程拓扑）— ⚠️ Launcher 单臂，业务进程未拆

- **Launcher**（进程管理器）：✅ 落地。QProcess 生命周期管理、健康面板、进程启动/停止。
- **业务进程拓扑**（地形编辑/场景设计/仿真控制/雷达显控/SAR显控/红外显控 6 进程）：⚠️ 实际仍是**单进程多窗口**——scene_designer / sim_control / control 各自是独立可执行，但数据链不互通（无 DataChannel/ControlChannel 接线），且 Launcher 只负责"启动"，不感知业务进程的运行时数据。

## 二、落地差距分级清单

| # | 差距 | 阻塞级别 | 说明 |
|---|---|---|---|
| G1 | DataChannel / ControlChannel 业务未接线 | **高** | 跨进程数据分发/命令下发主链路缺失，"多进程"目前只是"多可执行并行跑" |
| G2 | 算法抽象层未被 layer3b_signal / env_modules 消费 | **中** | 抽象层价值未兑现；layer3b 信号处理器与 RT 实现仍耦合 |
| G3 | InProcessTransport 零使用 | **低** | 单进程调试用，本就轻量，待 G1 打通后自然启用 |
| G4 | 业务进程间无控制通道（仿真控制 → 显控的启停/参数下发） | **高** | 依赖 G1；v5 的 ControlChannel 场景未实现 |
| G5 | 场景文件跨进程同步（FileChannel） | **中** | v5 设计了 FileChannel，未在 lib/ipc 中实现（核查时无此组件） |

## 三、建议落地顺序

1. **G1 先行**：先在现有 `scene_designer ↔ sim_control` 之间打通一条 `DataChannel`（场景装载的跨进程传递），验证 ShmTransport 在真实两进程下的吞吐/延迟，再扩到显控进程。
2. **G4 跟进**：`ControlChannel` 实现仿真控制 → 显控的"启动/暂停/参数下发"，替换当前 Qt 信号直连。
3. **G2 择机**：待 env_modules 传感器链与 layer3b 信号处理器接口稳定后，把公共数值内核（JONSWAP 波场、大气消光、Planck LUT）抽到 `lib/algo`，两处改为消费抽象层。
4. **G5 随 G1**：FileChannel 可在 DataChannel 验证后，以"场景 JSON + 地形包路径"为载荷实现。

## 四、结论

v5 不是"没做"，是"基建先行、业务滞后"：库和框架都立起来了（lib/ipc 全套 + lib/algo + lib/telemetry + Launcher QProcess），但跨进程数据链是空的。当前系统实际是 **v4 单进程数据流 + v5 进程壳 + v5 遥测**。要兑现 v5 的分布式仿真价值，优先补 G1/G4 两条数据/控制通道。
