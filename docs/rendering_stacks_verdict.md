# 双渲染栈裁决：layer2_rt vs env_modules（P1 架构收敛）

日期：2026-10-18

## 1. 调研结论：两套不是重复，是分工

调研时一度怀疑 `src/layer2_rt` 是 env_modules 双渲染栈的遗留死代码，核实后推翻该判断——**两者定位不同，并存合理**：

| 维度 | `src/layer2_rt` | `env_modules`（rocky_env） |
|---|---|---|
| 定位 | **CPU 参考实现**（仿真内核的物理真值基线） | **GPU 生产路径**（真实时渲染） |
| 消费者 | `src/Simulation.cpp`（`AtmosphereIntegrator` 157 行、`RayTracer` 272 行，"CPU reference path" 日志） | `env_viewer`、GUI 视口 |
| 场景来源 | `SceneBuilder` 构建的仿真场景对象 | `SceneSource`（JSON 驱动 + 程序化热场景） |
| 输出 | CPU 辐亮度（无 GPU 管线开销，用于交叉验证） | Vulkan RT TraceRays + G-buffer + BandRT 着色 |
| 依赖 | `vsgraysim_core` 内部，无外部分支 | 独立可复用库（`vsgraysim_rocky_env`） |

**关键证据**：`Simulation.cpp` 的 `.o.d` 依赖文件含 layer2_rt 头文件；272 行日志明确标注 "Layer2: RayTracer initialised (**CPU reference path**)"。

## 2. 裁决：保留双栈，划定边界

**结论**：不合并、不删除 layer2_rt。它的价值在于：
- 提供与 GPU 实现**同公式**的 CPU 参考值，是数值回归的基准
- 不依赖 Vulkan RT 扩展，可在无 RT 硬件环境跑通仿真内核
- `CrossInductionDetector` / `AtmosphereIntegrator` 属仿真内核业务逻辑，不属于渲染层

## 3. 缺口与建议

### 3.1 缺统一"同公式"契约声明
两套实现目前**无机制保证同公式**。一旦 env_modules 的物理公式升级（如 cloud 密度场、ocean JONSWAP 参数），layer2_rt 不会自动跟随，CPU/GPU 结果悄悄分叉。

**建议**：在 `docs/design_physics_and_processing.md` 增加"CPU 参考实现 ↔ GPU 实现 同公式对照表"章节，每处物理公式标注两处实现位置；改公式时双改。

### 3.2 缺 CPU/GPU 交叉验证回归
现在只有 smoke 数值断言（`env_smoke_sky` 42/42）和视觉回归，**没有** layer2_rt CPU 值 vs env_modules GPU 值的差值断言。

**建议**：新增 `env_smoke_cross.cpp` headless 冒烟：对同一程序化场景，CPU `RayTracer::trace()` 与 GPU P0 回读逐像素比较，容差 5%（对齐历史"CPU 采样与 GPU 画面误差目标 <5%"的规划）。接入 `run_regress.sh` 阶段 1。

### 3.3 layer2_rt 命名易误导
"l2_rt" 字面意为 "ray tracing"，实际它是仿真内核的 CPU 参考层，且与 env_modules 的 GPU RT 并存。

**建议**：文档 `docs/system_modules.md` 的 layer2_rt 条目补充一句："CPU 参考实现，非渲染路径；渲染走 env_modules"。暂不改目录名（改名牵动 core 库通配符 `src/layer2_rt/*.cpp` 与所有引用）。

## 4. 后续动作

- [ ] 补"同公式对照表"（责任人：维护者，随 P2 数据产品化一起做）
- [ ] 实现 `env_smoke_cross.cpp` CPU/GPU 交叉验证（纳入 P3 功能补全）
- [ ] 更新 `docs/system_modules.md` layer2_rt 描述（低成本，可立即做）
