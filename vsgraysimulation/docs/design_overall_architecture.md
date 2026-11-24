# vsgraysimulation 整体业务架构

> 本文档梳理 vsgraysimulation（基于 VulkanSceneGraph 的多光谱仿真系统）的整体业务架构，并说明物理引擎（Bullet3 + vsgBullet）在其中的集成位置。
> 物理引擎细节与处理系统设计见 [`design_physics_and_processing.md`](design_physics_and_processing.md)。

## 1. 系统定位

多光谱传感器仿真平台：构建一个 Y-up 的虚拟场景（坦克等对象，含几何-物理参数绑定），
调度 VIS / LASER / IR / RADAR / SAR 五类传感器的发射时序，经光线追踪（GPU Vulkan RT，CPU 参考实现兜底）
获得散射点云，叠加大气衰减/散射/折射与互扰（cross-induction）检测，再经各频段信号处理器
（RD 图、SAR 后向投影、可见光/红外成像）产出 `SignalFrame`，最终可视化并落盘。
**物理引擎提供对象的真实运动/碰撞，并作为传感器平台（载体）将平台速度注入多普勒处理。**

## 2. 分层架构

```
┌──────────────────────────────────────────────────────────────────────┐
│                     入口 main.cpp / gui（Qt 前端）                     │
├──────────────────────────────────────────────────────────────────────┤
│                        Simulation（总控，见 §4）                      │
├──────────┬─────────────┬──────────────┬────────────┬────────────────┤
│ Layer 0  │ Layer 1     │ Layer 2      │ Layer 3    │ Layer 4    L5  │
│ 物理参数  │ 场景构建     │ 光线追踪      │ 信号处理    │ 调度/分发  可视化 │
│          │             │              │            │               │
│ PhysParam │ SceneBuilder│ RayTracer    │ CUDA(Signal│ SensorSched- │ Visualizer  │
│ Manager   │ (VSG 场景图)│ (CPU 参考)   │ Processor) │ uler         │ (VSG Viewer)│
│ Atmosphere│ MultiSpect- │ RayTracing-  │ 3b: Sensor │ DataDispatch-│             │
│ Model     │ ralGeometry-│ PipelineWrap │ Signal-    │ er           │             │
│ CrossInd- │ Node        │ (Vulkan RT)  │ Processor  │              │             │
│ uction-   │ Dynamic-    │ Atmosphere-  │ (Radar/SAR│              │             │
│ Rules     │ MultiSpect- │ Integrator   │ /VIS/IR)   │              │             │
│           │ ralNode     │ CrossInduct- │            │              │             │
│ Physics-  │             │ ionDetector  │            │              │             │
│ Engine ①  │ ②           │              │            │              │             │
└──────────┴─────────────┴──────────────┴────────────┴───────────────┴───────────┘
  ① Bullet3 + vsgBullet（刚体动力学）       ② 刚体变换节点挂入场景图
```

- **Layer 0（物理参数域）**：`PhysParamManager`（对象/面组配置，`ObjectPhysConfig`，含物理字段
  `physics_enabled / mass / friction / initial_position / initial_velocity / use_convex_hull`）、
  `AtmosphereModel`（大气分层/天气）、`CrossInductionRules`（传感器配置/互扰路由）、
  **`PhysicsEngine`（物理引擎，见 §3）**。
- **Layer 1（场景构建）**：`SceneBuilder::build()` 读 Layer 0 配置，加载 gltf/obj 网格，
  实例化 `MultiSpectralGeometryNode`（静态）/ `DynamicMultiSpectralNode`（动态参数更新），
  绑定面组→geo_unit，注册传感器孔径；**对 `physics_enabled` 对象创建刚体并挂 `MatrixTransform`**。
- **Layer 2（光追）**：CPU `RayTracer`（大气积分 + 场景求交）与 GPU `RayTracingPipelineWrap`
  （Vulkan RT 管线）双实现，`AtmosphereIntegrator` 做路径积分，`CrossInductionDetector` 检测互扰。
- **Layer 3（信号处理）**：CUDA `SignalProcessor`（当前禁用）+ **Layer 3b** 面向对象信号形成：
  `SensorSignalProcessor` 工厂按传感器类型产出 `RadarPulseProcessor`（多普勒）/ `SarBackprojectProcessor` /
  `VisCameraProcessor` / `IrThermalProcessor`，输出 `SignalFrame`（RD 图 / SAR / 图像）。
- **Layer 4（调度与分发）**：`SensorScheduler::buildFrameSchedule()`（按脉冲重复率排发射时序，
  按 fire_time/priority 排序）；`DataDispatcher::dispatch()`（散射点按接收机路由，互扰按优先级分发）。
- **Layer 5（可视化）**：`Visualizer` 驱动 VSG 视口，叠加大气 overlay、互扰路径、每频段传感器图像。

## 3. 物理引擎集成（本会话新增部分）

| 集成点 | 文件 | 说明 |
|---|---|---|
| 依赖接入 | `CMakeLists.txt` | `find_package(Bullet/vsgBullet)`，链接 `vsgbDynamics/vsgbCollision` + Bullet 库 |
| 引擎本体 | `src/layer0_phys/PhysicsEngine.cpp` + `include/.../PhysicsEngine.h` | `vsgbDynamics::World` 封装；`createRigidBodyFromMesh`（MeshGeometryCollector 手动收顶点→btBox/ConvexHull/BVH 形状→CreationRecord→MotionState）；COM/parent 约定；`step/position/velocity/raycast/…` |
| 场景集成 | `src/layer1_scene/SceneBuilder.cpp` | `physics_enabled` 对象：刚体变换节点 `mt`（Bullet 每物理步写入位姿）挂到对象根，`build()` 幂等 |
| 帧循环 | `src/Simulation.cpp` `step()` 第 1b 步 | `physics_->step(dt)`（120 Hz 固定子步）→ `syncMountedSensors()` |
| 传感器挂载 | `Simulation::mountSensorOnObject()` + `SensorMount`（`Simulation.h`） | 孔径 position/direction 每帧由刚体位姿 + 局部偏移/方向变换而来 |
| 多普勒注入 | `Simulation.cpp` 第 5 步 → `RadarPulseProcessor.cpp` | `signal_cfg_.sensor_velocity = physics_->velocity(object_id)`；`v_los=(v·r̂)/range`，未挂载回退 `v_eff=50` |
| 测试 | `test/test_physics.cpp` | 下落/静止/移动/静态/MotionState 同步/射线检测，**ALL PASSED** |

配置侧：`config/tank_physics_demo.json` 中对象带 `"physics_enabled": true` 与质量/摩擦/初速度等字段。
演示：headless 模式把传感器 1 挂到 `tank_001`，雷达日志显示真实平台速度（5.88 → 5.76 m/s 摩擦减速）。

## 4. 总控与帧循环（Simulation）

### 4.1 初始化 `Simulation::init(cfg)`

```
Layer 0: PhysParamManager(load/validate/buildGeoUnitArray) → AtmosphereModel(+天气) → CrossInductionRules(buildRoutes)
       → PhysicsEngine(setGravity -Y, addGroundPlane(0))
Layer 1: SceneBuilder::build()          ← 物理刚体在此创建
       → 注册传感器孔径（固定站）
Layer 2: RayTracingPipelineWrap（Vulkan RT，headless 时无 Device 则纯 CPU）→ uploadGeoPhys/Atmosphere/BandMap/Apertures/Routes
Layer 2: RayTracer（CPU 参考）
Layer 3: SignalProcessor（CUDA 禁用）
Layer 4: SensorScheduler + DataDispatcher
Layer 5: Visualizer（非 headless）
```

### 4.2 每帧 `Simulation::step(dt)`（7 步）

```
 1. 场景动态更新：dynNodes 排空参数更新（RCS/温度），runPerFrameHook
 1b.物理步进：physics_->step(dt) → syncMountedSensors()（孔径跟随平台）★物理引擎
 2. 发射调度：sched_->buildFrameSchedule(dt)（可只保留 --sensor-id）
 3. 光线追踪：GPU RT 优先（Vulkan RT → 散射点回读+滤波），否则 CPU RayTracer
 4. 互扰检测：CrossInductionDetector → 标记散射点
 5. 信号形成：按 tx 传感器分组 → signal_cfg_（PRF/载频/孔径位置/平台速度）→ 各频段 SignalFrame ★多普勒注入
 6. 可视化：互扰路径、大气 overlay、传感器图像纹理
 7. 日志：帧统计（scatter/batches/crosstalk），per-frame-detail 逐发射明细
```

### 4.3 运行模式（main.cpp）

| 模式 | 行为 |
|---|---|
| `run` | 完整管线 + VSG 视口，`--frames` 限帧 |
| `headless` | 完整管线无视口；**演示：`mountSensorOnObject(1, "tank_001", {}, {0,0,-1})`**；落盘 PPM |
| `list-sensors` | 打印传感器/路由/孔径/大气表格 |
| `validate` | 全参数健全性检查 |
| `trace-sensor` | 单传感器 N 帧 + 大气积分/互扰功率解析验证 |

### 4.4 数据流（核心业务闭环）

```
发射调度 ScheduledTx ──► 光线追踪（大气积分 + 场景求交 + 互扰）
     │                      │
     │                      ▼
     │              ScatterPoint 云（位置/幅度/相位/频段/收发 id）
     │                      │
     └──── DataDispatcher：按接收机路由（正常回波 vs 互扰）
                                  │
                                  ▼
                        SensorSignalProcessor（Radar/SAR/VIS/IR）
                                  │
                         SignalFrame（RD 图/图像）──► Visualizer / PPM 落盘
```

## 5. 工程结构与构建

```
vsgraysimulation/
├── main.cpp / gui/           入口与 Qt 前端
├── src/                      Layer 0-5 实现 + Simulation.cpp
├── include/vsgraysim/        各层头文件
├── cuda/signal_processing.cu CUDA 核（Layer 3，当前禁用）
├── shaders/                  Vulkan RT 着色器（rgen/rchit/rmiss → SPIR-V）
├── config/                   JSON 配置（对象/大气/传感器）
├── models/                   gltf 模型
├── test/                     test_objloader / test_physics
└── docs/                     设计文档（本文件 + 物理引擎/处理系统）
```

- 库：`vsgraysim_common`（接口）/ `vsgraysim_cuda`（共享）/ `vsgraysim_core`（静态，Layer 0-5）；
  可执行：`vsgraysim_app`、`test_objloader`、`test_physics`、GUI 目标；
- 依赖根：`/home/mte/Code`（VSG、vsgXchange、Bullet3、vsgBullet、Vulkan、CUDA、Qt6、nlohmann-json、spdlog）；
- RPATH 已烘焙，无需手动设置 `LD_LIBRARY_PATH`（但 vsgBullet 的构建产物路径未全部烘焙，见文档 §3 运行示例）。

## 6. 关键设计约定

1. **单一场景根**：`SceneBuilder::build()` 返回根节点，RT 管线、物理引擎、可视化共享同一场景图；
   刚体位姿由 MotionState 写入 `MatrixTransform`，各消费者零拷贝读取。
2. **Y-up 约定**：重力 `(0,-9.81,0)`，传感器孔径以 y 为高度，地面平面 y=0。
3. **COM/parent 约定**：`position() = mt->matrix[3] + com`，com = mesh 局部 bounds 中心
   （详见 design_physics_and_processing.md §1.4）。
4. **CPU/GPU 双路径**：RT 与信号处理均有 GPU 优先 + CPU 参考实现，无 Vulkan 设备时系统仍可运行（headless）。
5. **配置驱动**：对象/大气/传感器全部 JSON 配置，运行时无需改码即可更换场景与传感器组合。
