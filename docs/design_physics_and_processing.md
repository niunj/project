# 物理引擎与传感器处理系统设计

本文档记录 vsgraysimulation 中两套子系统在项目中的落地设计：

1. **物理引擎（Layer 0.5）**：基于 Bullet3 + vsgBullet 的刚体动力学，为场景对象提供真实运动/碰撞，并作为传感器平台（载体）。
2. **处理系统（挂载 + 信号链路）**：传感器孔径挂载到运动平台，平台速度注入雷达多普勒处理。

---

## 1. 物理引擎设计

### 1.1 总体架构

```
                         ┌──────────────────────────────────────────┐
                         │           PhysicsEngine (vsgraysim)      │
                         │  createRigidBodyFromMesh()               │
                         │  step() / position() / velocity()        │
                         │  raycast() / setVelocity() / forces      │
                         └───────────────┬──────────────────────────┘
                                         │ 持有
                          vsgbDynamics::World (btDiscreteDynamicsWorld)
                                         │
                    ┌────────────────────┼─────────────────────┐
                    │                    │                     │
            btRigidBody ── MotionState ──► vsg::MatrixTransform（视觉节点）
            碰撞形状（box / convex hull / BVH tri-mesh）      （写入场景图）
```

- 每个物理对象 = 一个 `btRigidBody` + 一个 `vsg::MatrixTransform`（`mt`）。
- `mt` 由 `SceneBuilder` 挂到场景根下；**矩阵由 Bullet 的 MotionState 每物理步重写**，场景图其他部分（光追、传感器、可视化）读取同一节点，天然保持同步。
- 坐标约定：**Y 轴向上**，重力 `(0, -9.81, 0)`；固定子步积分 120 Hz（`stepSimulation(dt, 8, 1/120)`），保证任意帧率下稳定。

### 1.2 刚体创建流程（createRigidBodyFromMesh）

```
mesh (VSG 子树)
   │
   ├─► MatrixTransform mt（Bullet 写位姿的目标）
   ├─► MatrixTransform poser = T(start_position)·R(euler)，挂 mesh 于其下
   │
   ├─► MeshGeometryCollector：手动收集顶点 + 包围盒（见 1.3）
   │
   ├─► mesh_center = bounds 中心（子树根局部坐标）
   │
   ├─► 碰撞形状（顶点 − mesh_center，即形状以刚体原点为中心）：
   │      mass==0 && use_trimesh_shape  → btBvhTriangleMeshShape（静态）
   │      use_convex_hull_shape         → btConvexHullShape（动态）
   │      否则                          → btBoxShape(half)
   │
   └─► CreationRecord：
          setSceneGraphNode(mt)
          setParentTransform(poser 矩阵)      ← 视觉放置（起点）
          setCenterOfMass(mesh_center)        ← COM 与形状共用 mesh 局部系
       → vsgbDynamics::createRigidBody(cr, shape)
       → setLinearVelocity / CF_STATIC_OBJECT → addRigidBody
```

### 1.3 网格几何收集（MeshGeometryCollector）

- 自定义 `vsg::Visitor`：沿 `Transform` 栈累计变换，直接读取 `VertexIndexDraw.arrays[0]`（`vsg::vec3Array`，POSITION）累加包围盒并收集顶点（相对子树根）。
- **为什么不用 `vsg::ComputeBounds`**：`ComputeBounds` 依赖 `ArrayState` 解析顶点布局，而 vsgXchange 的 gltf 读取器会在 `vid->arrays[1..2]` 放入**常量数组**（`vsg::vec3Value`/`vsg::vec2Value`）。`ArrayState` 按每顶点布局解析这些数组时**越界读取**，产生垃圾包围盒（实测 min/max 达 ±1e26…1e38，约 7/8 概率出现），导致 COM 错误、刚体飞离。手动收集只触碰真实的 `vec3Array`，完全规避该问题。

### 1.4 COM 与 Parent 语义（关键约定）

vsgBullet 的 `MotionState` 约定（源码 `vsgbDynamics/MotionState.cpp`）：

```
初始刚体位姿：  world0 = parentTransform · T(com_local)     (resetTransform)
每步写入视觉：  mt->matrix = world_pose · T(−com_local)    (setWorldTransform)
```

本项目取值：

| 项 | 值 | 含义 |
|---|---|---|
| `parentTransform` | `T(start_position)·R(euler)`（poser） | 网格作者系下的起始放置 |
| `com`（传给 vsgBullet） | `mesh_center`（**mesh 局部** bounds 中心） | 与碰撞形状共用同一局部系 |
| `coms_[i]`（引擎内部） | `mesh_center` | 用于从视觉矩阵还原刚体位姿 |

由此得到（无旋转时精确、有旋转时平移分量依然正确）：

```
position() = mt->matrix[3] + coms_[i]   = 刚体世界原点（= 世界 COM）
```

> 注意：**不是** `poser·mesh_center`（世界 COM）。早期实现把 poser 变换后的 COM 也加进矩阵偏移，导致双重偏移（下落体报告 y=5.5 而非 0.5）。

### 1.5 数据结构与 API

```cpp
class PhysicsEngine {
    world_      // vsgbDynamics::World（btDiscreteDynamicsWorld）
    transforms_ // vsg::MatrixTransform*（视觉节点，MotionState 写入）
    coms_       // 每刚体的 mesh 局部 COM（与形状共用系）
    bodies_     // btRigidBody*（所有权归 Bullet world）
    index_      // object_id → 序号
};
```

对外查询：`position() / rotation() / velocity() / transform() / bodyExists() / raycast()`；
控制：`setVelocity() / applyCentralForce() / setGravity()`；
场景：`addGroundPlane(y=0)`（无限静态平面，摩擦 0.9）。

### 1.6 测试覆盖（test/test_physics.cpp）

- 动态 box（2 s / 60 Hz）：下落 → 落地 y≈0.5、静止、水平位移符合初始速度×时间±摩擦；
- 静态 box：位置不变；
- MotionState 同步：`|m[3][i] − pos[i]| < 0.1`（视觉矩阵与查询位姿一致）；
- raycast：命中地面、命中点高度正确。

---

## 2. 传感器处理系统设计（挂载 + 信号链路）

### 2.1 集成点（SceneBuilder）

`ObjectPhysConfig.physics_enabled == true` 的对象：

```
node (对象根)
 └── mt (MatrixTransform，Bullet 拥有) ── poser ── mesh
```

- `SceneBuilder::build()` 幂等：重复调用不会重复创建刚体（防止多帧/多路径重复 addRigidBody）。
- 失败时回退 `node->addChild(mesh)`（静态渲染，无物理）。

### 2.2 传感器挂载（Simulation）

```cpp
mountSensorOnObject(sensor_id, object_id, local_offset, local_direction);
```

- 记录 `sensor_mounts_[sid] = {object_id, offset, direction}`；
- 立即把孔径位置/方向快照到平台位姿；
- 每帧 `step()` 内、物理步进之后调用 `syncMountedSensors()`：用 `body = mt->matrix`（= world·T(−com)）变换挂载偏移与方向，写回 `SceneBuilder::apertures()`。
  - 孔径位置 = `body · local_offset`，方向 = 归一化 `body · local_direction`；
  - 偏移以**刚体 COM 系**为参考（对中心对称网格即 mesh 世界中心）。

### 2.3 平台速度 → 多普勒（Layer 3b）

每帧信号形成阶段（Simulation::step 第 5 步）：

```
sensor_mounts_.find(sid) 存在 且 物理引擎有效
   → signal_cfg_.sensor_velocity = physics_->velocity(object_id)   // 真实平台速度
   否则
   → signal_cfg_.sensor_velocity = {0,0,0}                          // 固定站
```

`RadarPulseProcessor`（src/layer3b_signal/RadarPulseProcessor.cpp）：

```
v_sensor = |v|                    // 平台合速度
v_eff    = v_sensor > 1e-6 ? v_sensor : 50.0   // 固定站回退速度（默认 50 m/s）
v_los    = (v · 目标方向) / range              // 视线速度 → 多普勒频移 fd = 2·v_los/λ
```

验证结果（`--mode headless --object ../config/tank_physics_demo.json`）：

```
RadarPulseProcessor: platform velocity (5.88,-0.00,-0.00) m/s (v_sensor=5.88, v_eff=5.88)
…随后 5.76 → 5.65（坦克受地面摩擦减速）
```

### 2.4 数据流总览（每帧）

```
physics_->step(dt)
  → syncMountedSensors()             // 孔径跟随平台位姿
  → sched_->buildFrameSchedule()     // 发射调度
  → RT（GPU 或 CPU 参考）→ ScatterPoint
  → cross-induction 检测
  → disp_->dispatch()                // 数据分发
  → per-sensor 信号形成：
      sensor_velocity(物理引擎) + scatter points → SensorSignalProcessor
      → SignalFrame（RD 图 / SAR / VIS / IR）
  → 可视化 / PPM 落盘（headless）
```

---

## 3. 构建与运行

```bash
cd build
ninja vsgraysim_app test_physics

# 单元测试（需 vsgBullet/bullet3 运行时）
LD_LIBRARY_PATH=/home/mte/Code/vsgBullet/install/lib:/home/mte/Code/bullet3/install/lib \
  ./test_physics          # 期望：ALL PASSED

# 端到端演示（雷达多普勒 = 真实平台速度）
LD_LIBRARY_PATH=… ./vsgraysim_app --mode headless --object ../config/tank_physics_demo.json --frames 3
```

## 4. 关键文件

| 文件 | 职责 |
|---|---|
| `src/layer0_phys/PhysicsEngine.cpp` + `include/vsgraysim/layer0_phys/PhysicsEngine.h` | 物理引擎（收集、形状、COM 约定、查询） |
| `src/layer1_scene/SceneBuilder.cpp` | 网格加载 → 刚体包装（幂等 build） |
| `src/Simulation.cpp` | 每帧物理步进、挂载同步、速度注入信号配置 |
| `include/vsgraysim/Simulation.h` | `SensorMount`、`mountSensorOnObject` 接口 |
| `src/layer3b_signal/RadarPulseProcessor.cpp` | 多普勒处理（v_eff 回退） |
| `test/test_physics.cpp` | 物理回归测试 |
| `config/tank_physics_demo.json` | 演示配置（tank_001 挂载传感器 1） |
