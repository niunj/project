# AGENT.md — rocky_env 环境渲染模块（sky / cloud / ocean）

2026-11-12 构建约定：VSG、vsgXchange、vsgImGui、vsgBullet、Rocky及其小型头文件
依赖已纳入根目录 third_party，以锁定源码构建；具体版本与许可证以
third_party/upstream.lock.json 为准。环境模块继续保持独立库边界。宿主的
RuntimePaths.h 默认值由构建注入，Rocky shader/font 来自当前构建的 share/rocky，
PROJ/GDAL数据来自选定外部SDK；禁止重新写死个人机器的旧Rocky安装路径。
标准构建及本地修改清单更新见 docs/build_dependencies.md。

> 本文件是 **rocky_env** 模块的“活文档”：写给后续维护者与 AI 代理。
> 目标、架构、约定、路线图都在这里。**改动模块前先读本文；每次阶段性演进后回写本文。**

---

## 2026-11-17 正确性修订

- GPU 云的 BFECC 反向场使用独立 binding 8/9，压力 5/6 保留上一帧解。
  九张常驻 RGBA32F 三维图；参数 UBO 固定 120 个 vec4，碰撞预算 1..32 只限制有效槽。
- `CloudStepClock` 按仿真秒累计 20 Hz 固定步长，每次最多提交 8 步，余量保留；
  暂停不演化、回退/重定位重置、启动仅一个初始化步，不再按显示帧预演化 96 步。
  `GpuVoxelCloud::update(frame, n)` 保证仅第一步可能 reset，其余步连续演化。
- 暖雨独立有限体积向下沉降；水汽/雨水蒸发与潜热分开计算。空气不再承担雨滴终端速度。
  平流采用开放入流边界。爆炸蒸发的雨水返回水汽，不再直接删除水物质。
- `CloudGpuResources` 显式共享液态水纹理、ECEF 图表和天气参数。原生视图的全球表面、
  地形云影、海面和可见云绑定同一局部场，消光统一按 0.003 kg/m³ 参考量归一化。
  透过率耗尽后停止积分，移除不乘透过率的灰雾增亮项；修正采样半体素偏移。
- `createCloudSceneBinding(resources, localToECEF)` 可在编译传感器前绑定到
  `SharedSceneResources::voxelCloud` 或 `GpuSensorRuntime::setVoxelCloud`，RT 光谱着色和
  TV 光栅路径直接采样同一体素场。禁止跨 Vulkan 设备；宿主必须在采集时刻发布对应云场并
  完成写入同步，不能把任意时刻的预览状态自动当成测量真值。未绑定的旧场景保持兼容路径。
- CPU 全球/中间/稀疏密度公式对齐纹理采样与边界羽化；CPU 查询尚不隐式读取 GPU 图像。
- 体素地形 DDA 保存入射面、先作 slab 裁剪；球体使用连续球/AABB 距离求交。
  坍塌刷新列顶及快照元数据，不计为销毁。分块损伤使用写时复制，并重建网格/查询索引；
  `TiledVoxelTerrainPhysics::snapshot()` 返回新版本，由宿主显式发布。
- 目标体素化补齐 13 轴 SAT，按三角形包围盒栅格化，默认洪泛填充封闭内部；
  `fillInterior=false` 保留表面壳。直接创建占据网格，统一采用 400 万格上限。

这些是现有暖云及离散地形模型的工程修订，不代表已经通过混合相微物理、10 分钟总水守恒、
跨设备传感器版本传递、云的场景深度合成、结构断裂或全产品资产接入验收。

## 2026-11-17 体素场景运行时适配

- 宿主新增 `VoxelScene` 固定步多对象运行内核与 `vsgraysim_voxel_workshop` 原生入口。
  场景/检查点定义、体积 RLE、事务、定时损伤、运动及显示发布位于 `src/earth`，
  环境模块继续不反向依赖宿主。工坊先发布已提交的 tick 时间，再驱动日月与地球环境；
  UTC 起点由场景文档保存。该检查点尚不包含 GPU 云的完整动力学场。
- `SharedSceneResources::buildInstances` 接受可选 TexArraySpec，保留实例 UV 和颜色槽，
  不再把动态对象的可见光颜色全部替换为白色。`GpuSensorRuntime::initialize/replaceScene`
  可传同一纹理规格；捕获的场景构建器保留既有纹理表，新增颜色表须显式重新提供。
- 零实例场景使用内部 masked、零面积图元维持合法描述符和 TLAS，外部实例数仍为 0，
  空变换更新不解除掩膜。全毁和空场景仍产生背景测量。
- 新增 GPU 测试由宿主体素快照适配后实际测量，覆盖温度、RGB、损伤开孔、帧号连续及空场景。
  运行时使用者仍须选择采集时刻对应的快照；这里没有把预览最新状态当作历史曝光真值。

## 2026-11-17 目标断裂测量消费器

- 新目标物理在宿主体素库中使用 Bullet 占据长方体并集、面键断裂和固定步积分，
  env_modules 仍不依赖宿主。SDK OSG/IVE 解码也属于宿主导入阶段，不改变 vsgXchange 锁定配置。
- `GpuSensorRuntime::captureTexturedSceneBuilder` 为候选几何/材质/颜色表创建完整备用资源，
  `swap` 只替换资源实现，保留外层测量时钟与帧号；失败不会切换已发布消费者。
- 宿主 `VoxelSensorSession` 在场景发布前准备 GPU 资源，并按几何有效区间排空旧版测量。
  位姿来自有界已提交历史。跨断裂的曝光/CPI 不用最新几何补齐，
  `rejectedSceneWindows()` 记录拒绝的窗口；当前没有跨拓扑曝光积分。
- GPU 回读覆盖碎块下落进入测量射线、继承温度/RGB、销毁后的背景与采集时刻 DDA 一致性，
  同时保留已有云水消光测试。本阶段没有自动把预览云状态或地球背景注册为新刚体接触体。

## 全球与局部天气

### 2026-11-17 地球程序组合验收

- 最终组合验收使用既有 `sim_earth_gui_tests --unified-scene-capture OUTPUT SCENE CONTEXT_MAP`，
  交互入口为 `--unified-scene-view SCENE CONTEXT_MAP`；不以独立体素工坊代替地球、目标、云
  的共同验收。真实数据的全球 native volume 可由 `earth_voxel_bake --global-roots` 生成。
- 宿主把工作区外的地球显示网格保留，裁掉工作区内的旧地表；新地形、导入目标及碎块
  由同一 VoxelScene 发布。地球原生 GPU 云保持运行，传感器绑定同设备同一 CloudGpuResources。
- 测量先排空上一固定步期间的几何/保持云场，再执行下一个地球 GPU 云步；该零阶保持
  时间模型显式记录在验收报告中，不能把最新预览云场当成任意历史曝光。
- 地球宿主将目标的质心速度和位置经 ECEF 转换到云的 east/up/north 图表，注入既有
  `VoxelWeather::setColliders`；这是有限分辨率的球形尾流包络，刚体接触仍为精确占据体，
  不把尾流包络当作目标的传感器或刚体几何。云风参数在没有海洋特性时也能独立生效。
- 云图像没有时间累积，因此移除帧号 Bayer 抖动；近/远段均用确定性两点 Gauss 积分，
  近景提高步数并提前集中近场采样。暂停验收除检查 field revision 外，还比较真实截图像素。
- 自动组合验收仅在探针截图帧绘制昂贵的云颜色通道；每个固定步仍执行原生云计算、
  目标尾流、物理和测量。交互模式每帧正常呈现。物理检查点尚不包含完整 GPU 云场。
- `env_modules` 不反向依赖 Qt、宿主或体素场景；组合逻辑留在地球视口及地球测试程序。
- 软件 Vulkan 的地球组合功能验收：50 步、MWIR/TV 各 50 帧，111 个观测接触点，
  真 OSGB 目标断裂，云版本 51、尾流对象 1；暂停截图 113544 个像素变化 0。
  产物 `build/earth-unified-final/`。近景云 LOD 仍有分层外观，另存视觉审查记录，
  不能把组合功能通过当作最终云画质通过；NVIDIA 当前设备创建失败仍未完成硬件验收。

全球云海由本地 PlanetWeather 节点渲染。宿主传入统一仿真秒数与地理风场，
局部 EnvFrame 使用同一时钟和云图覆盖率。detailWeight/detailRadius 控制
高度与工作区边缘过渡，云层按光学厚度分配远近景贡献。
轨道期维持海况更新，局部 FFT 在近景权重非零时执行。
模型、数据来源和验收命令见 docs/build_dependencies.md 的全球云海部分。

### 2026-11-14 GPU 物理体素单云

产品原生 `VoxelWeather` 已直接持有 `GpuVoxelCloud`，不再依赖窗口对象创建
计算资源。局部高质量场为 96×144×96、24 m 间距的 RGBA32F 三维状态；R/G/B/A 分别
存水汽混合比、液态水含量、温度扰动和雨水，另有三维速度、散度及压力场。
预渲染命令按统一仿真时钟执行平流、凝结/蒸发与潜热、浮力/水负载、涡量约束、
压力投影和外物尾流。`planet_cloud.frag` 直接采样同一液态水场，局部实验区屏蔽
旧稀疏云表示；远处才连续回退到全球体素云。场重定位后执行有界启动预演化，
湿热胞具有紧支撑，禁止非零高斯尾部再次形成可见计算域方盒。
2026-11-14 后续修订已移除手工球形湿热胞；reset compute 只生成连续的边界层
温湿/CAPE 与无散速度扰动，凝结物由饱和分配及后续动力学产生。完整目标状态、
稀疏 LOD、微物理、光学和验收顺序见 `docs/PhysicalVoxelCloudArchitecture.md`。
标量和速度平流现使用 RK2 特征回溯与 BFECC 前/反向误差修正，并由邻域极值
限制器抑制非物理过冲；压力临时图在散度阶段清零后复用，不增加显存常驻量。

### 2026-11-14 天气型体素云与光学增强

- `cloud_voxel_seed.comp` 改为**覆盖度驱动的天气型种子**：cover 经
  `colliderMeta.w` 传入（flow.w 保持 solarHeating 不动），按
  cumulus/stratiform/storm 三型混合强迫场；背景湿度随 cover 逼近饱和，
  湿热胞内显式过饱和（不再 clamp 到 0.98），凝结仍只由微物理产生。
- `cloud_voxel_microphysics.comp` 增加雨水终端速度与云下蒸发（Stokes 近似
  `v_term=min(8.5, 0.45+900·qr)`），雨不再随空气被动平流。
- `planet_cloud.frag` 光学增加 **powder 项**（稀薄云缘前向散射增亮）与
  **云顶自阴影**（normalizedHeight 加权太阳路径阴影），积云体积感增强；
  全部光照仍共用 `surfaceSun/surfaceMoon` 与 `AtmosphereState` 单一真值。
- 修复 `VoxelWeather.cpp` 中 `vsg::vec3(phase)` 单标量构造（当前 vsg 无此
  构造器），环境测试恢复可编译。

### 2026-11-14 专题：体素云风阵 + 天空暮光精度

- **风阵进云**（GPU `GpuVoxelCloud` 与 CPU 稀疏云海同式）：环境风在均值上
  叠加三个不可通约正弦阵风（周期 7.3/2.9/1.7 s，振幅 .22/.13/.07），阵风
  振幅随风速增大（`0.75+0.35·min(v/15,1)`），云缘随风吹拂破碎、真实扩散，
  不再刚体平移。
- **天空暮光自适应采样**：`sky_dome.frag` 太阳高度角 > -12° 时视图采样
  16 → 24 步，消除低太阳角地平线附近 Mie 瓣的条带。

### 2026-11-15 体素地球物理层：vsgBullet 刚体碎块

- **大碎块提取**：`VoxelTerrainPhysics::extractClusters()` 用 6-邻域 BFS
  泛洪找连通体素簇，返回质心/AABB/体素数，按大小排序。
- **刚体下落**：`VoxelTerrainDebris`（`vsgraysim_core`）把最大簇注册为
  `PhysicsEngine::createBoxBody()` 刚体盒（质量=体素数×体积×密度，
  默认花岗岩 2600 kg/m³）；初始位置抬高半个 AABB + 0.1 m 避免穿透
  地面被 Bullet 弹起。每帧 `step(dt)` 后 `transforms()` 返回各碎块
  世界矩阵供渲染同步。
- **回归测试**：`sim_earth_voxel_debris_tests` 覆盖崩碎→簇提取→
  刚体生成→重力下落（0.5s 实测下落 1.24 m，与自由落体理论一致）。 + 体素地球物理层

- **云 ray-march 时域去闪**：帧号（`u[10].w`，0..3 循环）驱动 2×2 Bayer
  抖动偏移，相邻像素/相邻帧采样互补 stratum，抖动误差空时去相关；无需
  GBuffer 跨 pass 历史缓冲。
- **穿云物碰撞上限 8 → 32**：UBO 扩到 7+2×32 个 vec4，全部 cloud_voxel_*.comp
  声明同步；`VoxelWeather::setColliders()` 公开注入（锚点 ENU 米制），宿主
  每帧调用。新增 32 机 swarm 回归测试（守恒/非负/场不被擦除）。
- **天空多次散射**：`sky_dome.frag` 内联二阶近似（各向同性 1/4π 闭合，
  暮光权重 `1-smoothstep(2°,14°)`），厚暮光大气不再偏暗；同时预计算
  `sky_scatter_lut.comp`（GPU compute，(mu,h)→二阶辐射）已入构建清单，
  供产品后续替换为 LUT 查表（运行时零 ray-march 开销）。
- **体素地球物理层**（全新模块 `src/earth/VoxelTerrainPhysics`）：在
  `VoxelTerrain` 不可变快照之上维护可变 occupied 体素状态；3D-DDA
  射线/球体扫掠碰撞查询、爆炸/冲击崩碎（能量→弹坑半径→体素移除）、
  悬空体素坍塌（逐层下落直到有支撑）；`commit()` 重建渲染网格产出新的
  不可变快照（渲染线程原子切换）。回归测试 `sim_earth_voxel_physics_tests`
  覆盖射线相交/法线/占用查询/崩碎计数/坍塌/快照重建。

- **云 ray-march 时域去闪**：帧号（`u[10].w`，0..3 循环）驱动 2×2 Bayer
  抖动偏移，相邻像素/相邻帧采样互补 stratum，抖动误差空时去相关；无需
  GBuffer 跨 pass 历史缓冲。
- **穿云物碰撞上限 8 → 32**：UBO 扩到 7+2×32 个 vec4，全部 cloud_voxel_*.comp
  声明同步；`VoxelWeather::setColliders()` 公开注入（锚点 ENU 米制），宿主
  每帧调用。新增 32 机 swarm 回归测试（守恒/非负/场不被擦除）。
- **天空多次散射**：`sky_dome.frag` 内联二阶近似（各向同性 1/4π 闭合，
  暮光权重 `1-smoothstep(2°,14°)`），厚暮光大气不再偏暗；同时预计算
  `sky_scatter_lut.comp`（GPU compute，(mu,h)→二阶辐射）已入构建清单，
  供产品后续替换为 LUT 查表（运行时零 ray-march 开销）。

验收入口为 `sim_earth_environment_tests` 的 GPU 读回/碰撞测试，以及
`sim_earth_gui_tests --cloud-descent-capture PREFIX earth_display`。RTX 实拍当前
97 帧下降轨迹 P95 为约 26.5 ms；局部太阳射线先与 ENU 云场包围盒精确求交，
再按体素尺度积分液态水消光。画质仍需继续推进时间重投影和更长时段的湍流演化。

## 本地日月照明

解析天空与 LUT 天空共用 celestial_discs.glsl，月面底图为本地 NASA SVS
LROC 可视化产品，MoonTexture.cpp 负责无外部解码器的上传。
set=1 的 LUT 描述符现在为三个大气采样器加一个月面采样器。
全球表面共用 planet_light.glsl 的日/月照明和显示映射。
太阳、月亮开关同步到全局云海，相关验收见构建文档的太阳与月亮照明部分。

## 当前体素地球入口

三套产品和独立地球构建共用 `lib/voxel_earth/CMakeLists.txt` 定义的
`vsgraysim_earth_environment`。整球与环境 GLSL 在构建时编译并嵌入，环境节点
可由无窗口 VSG 宿主创建。海洋 FFT 支持注入 Vulkan 设备。海床数据和版本通过
`EnvironmentTerrain` 来自已发布的地形快照。原有 `vsgraysim_rocky_env`
为产品的传感器管线保留兼容链接入口。独立构建与验证命令见
`docs/build_dependencies.md` 的“独立构建体素地球”。

### 2026-11-14 轨道体素地球 front-to-back 排序

`OrbitalGlobe` 构建时把列四边形（含列间围墙三角形）按源列聚合，
按列顶高**升序**展开索引缓冲：低列先画、高列后画，轨道视角下被遮挡的
高列填充被 early-depth 直接拒绝，避免"高列先着色、前面所有列再覆盖"的
巨大 overdraw。三角形多重集与排序前完全一致（无裂缝、无增减），
`intersect()` 拾取路径不受影响。

## 0. 原 Rocky 后端定位

以 **rocky（VSG/Vulkan 三维地球）为基座**，提供一套**产品级、可独立开源（MIT）**的
天空 / 大气 / 云 / 海洋渲染与仿真采样模块。观感与能力对标商业仿真 SDK
**Sundog Triton（海洋）** 与 **SilverLining（天空/天气）**，但原生构建于 rocky + VSG 之上，
与地球地形、星历、气象**闭环一体**，而不是悬浮的贴片特效。

- 语言/引擎：C++17、VulkanSceneGraph（VSG）、rocky（MIT, Pelican Mapping）。
- 许可证目标：**MIT**（与 rocky 一致）。当前仓库尚未补齐 LICENSE / SPDX 头，见 Phase 4。
- 模块名 / 库名：`rocky_env`（命名空间 `rockyenv`），静态库 `vsgraysim_rocky_env`。

---

## 1. 设计原则（不可妥协）

1. **模块独立**：`env_modules/` 只依赖 VSG + rocky，**不依赖 Qt / 业务代码 / spdlog**。
   所有对外面仅 `include/rocky_env/EnvApi.h`。宿主通过工厂创建、`EnvFrame` 喂数据。
2. **接口规范、实现隐藏**：统一接口 `IEnvModule`；参数用 Plain Struct；rocky 内部类型
   现阶段以 `void* rocky_context` 传递以免头泄漏（独立成仓后可改为公开依赖 rocky 类型）。
3. **最小侵入宿主**：宿主侧只有一层“薄 Feature”（`gui/viewport3d/EnvFeatures.*`）负责
   取相机/挂载/注册；核心逻辑全部在模块内。
4. **单一真值源（Single Source of Truth）**：太阳/月亮位置、时间、气象（风/云量/能见度）
   全模块**共享一份**，由星历 + 天气状态推导，禁止各 shader / 各模块各算一套。
5. **与 rocky 地形闭环**：海要有海岸线/水深掩膜，云阴影要落到地形/海面，反射要反射
   真实天空与地形——**不允许出现“海穿山、云悬空、反射是写死颜色”这类穿帮**。
6. **渲染与仿真双输出**：既要画得对，也要能被仿真采样（波高/法线、太阳方向、海况），
   供舰船浮力、雷达、光照等业务使用。
7. **可演进**：接口按版本化设计（`EnvFrame` 预留填充），新能力以追加字段/新接口进入，
   不破坏既有宿主。

---

## 2. 目录结构

```
env_modules/
├── AGENT.md                     # 本文件
├── CMakeLists.txt               # 静态库 vsgraysim_rocky_env；rocky 硬依赖（恒链）
├── include/rocky_env/
│   └── EnvApi.h                 # ★ 唯一对外头：IEnvModule / EnvFrame / 参数结构 / 工厂
│                                 #   v2: ISkyControl / IEnvStack / createEnvironmentStack
├── docs/
│   └── EnvApi_Usage.md           # 接口使用指南（rocky 地球单一路径）
└── src/
    ├── EnvCommon.h / .cpp       # 内部工具：管线构建 + 环境 UBO 填充 + shader 路径解析
    ├── SkyModule.cpp            # rocky_sky  —— 封装 rocky::SkyNode（Rocky 单后端）
    ├── EnvStack.cpp             # 环境聚合栈（sky+cloud+ocean 一站式入口）
    ├── CloudModule.cpp          # rocky_cloud —— 层云片 + 积云公告板环（一期）
    └── OceanModule.cpp          # rocky_ocean —— ENU 相机跟随海面 + Gerstner（一期）

shaders/env/                     # GLSL（随 vsgraysim_assets 拷贝到 build/shaders/env）
├── cloud_sheet.vert/.frag       # 高空层云片
├── cloud_sprite.vert/.frag      # 积云公告板
└── ocean.vert/.frag             # 海面

gui/viewport3d/                  # 宿主薄封装（不属于模块本体）
├── EnvFeatures.h / .cpp         # SkyFeature / CloudFeature / OceanFeature + 注册
└── ViewportContext / RockyMapFeature / ViewportKit  # 提供相机、contentRoot、rocky 上下文
```

---

## 3. 当前架构（Phase 1，已实现）

### 3.1 模块边界与接口

对外头 `EnvApi.h`：

- `struct EnvFrame`：每帧由宿主填充。相机矩阵（`view_proj` / `model` / `cam_pos_world` /
  `cam_pos_enu`）、时间、太阳方向与高度角、风方向/风速、云量。
- `class IEnvModule`：
  - `const char* name()`
  - `vsg::Node* rootNode()` —— 挂到场景的根节点
  - `void onFrame(const EnvFrame&)` —— 每帧更新 UBO / 跟随 / 相位
  - `void setEnabled(bool)` / `bool enabled()`
- 工厂：`createSkyModule(void* rocky_context, SkyParams)`、
  `createCloudModule(CloudParams)`、`createOceanModule(OceanParams)`，均返回
  `std::unique_ptr<IEnvModule>`。
- 参数结构：`SkyParams` / `CloudParams` / `OceanParams`（均带默认值）。

### 3.2 坐标两级制（关键约定）

- **WGS84 地理坐标**为规范坐标；作业在**锚点 ENU 切平面**（局部米制，Y-up）：
  `+X = 东`，`+Y = 上`，`+Z = 南（= -北）`。
- `EnvFrame::model` = **锚点 ENU → ECEF**（rocky 地球基座恒定；无平面模式）。
- **天空**是地心级节点，工作在 ECEF，挂 **worldRoot**。
- **云 / 海**工作在锚点 ENU，挂 **contentRoot**（`RockyMapFeature` 的 `enu_root_`，
  一个 `vsg::MatrixTransform`）。
- ⚠️ VSG 的 `MatrixTransform` **不会**作用于自定义管线的 StateGroup 节点（StateGroup 自管
  transform），因此云/海 shader **必须在 UBO 里乘 `model`**：
  `gl_Position = env.view_proj * env.model * vec4(pos,1.0);`。这也是相机 UBO 扩到 36 float
  （追加 `mat4 model`）的原因。

### 3.3 统一环境 UBO（std140，48 float）

所有 `shaders/env/*` 共用同一块布局（见 `EnvCommon.h` 的 `kEnvUboFloats`）：

| offset(float) | 类型 | 含义 |
|---|---|---|
| 0–15  | mat4 `view_proj` | projection * view（世界→裁剪） |
| 16–31 | mat4 `model`     | ENU→世界 |
| 32–35 | vec4 `camPosTime`| xyz = 相机 ENU 位置，w = 时间(秒) |
| 36–39 | vec4 `sun`       | xyz = 太阳方向(ENU,归一化)，w = 太阳高度角(度) |
| 40–43 | vec4 `wind`      | x=风东向, y=风南向(=windDir.z), z=风速(m/s), w=云量 |
| 44–47 | vec4 `misc`      | 各模块自定义（见各 shader 注释） |

- 顶点输入约定：`loc0 vec3 pos`、`loc1 vec3 normal`、`loc2 vec2 uv`。
- 管线状态约定（`buildEnvPipeline`）：`cullMode = NONE`（双面）；半透明
  `depthWrite = FALSE`；混合 `SRC_ALPHA / ONE_MINUS_SRC_ALPHA`；DescriptorSet binding0 =
  UNIFORM_BUFFER。
- shader 路径由 `resolveEnvShader()` 解析 `shaders/env/<name>`（按 `/proc/self/exe`
  及其父目录回退）。

### 3.4 显隐控制

此版本 VSG 的 `vsg::Node` **没有 `mask` 成员**。显隐一律用 **`vsg::Switch`** 包裹：
`holder_->addChild(bool enabled, node)`，`setEnabled` 调 `holder_->setAllChildren(on)`。
（勿再使用 `vsg::MASK_ALL/MASK_OFF` 直接赋给节点。）

### 3.5 宿主薄封装（Feature 层）

`gui/viewport3d/EnvFeatures.*`：
- `EnvFeatureBase`：读 `sun_el/sun_az/wind_speed/wind_az/cover` 参数；`makeFrame()` 用
  `ViewportKit::cameraMatrices` 取 view_proj / cam_pos_world，`ctx.contentModel()` 取 model，
  逆矩阵求 cam_pos_enu；太阳/风方向由方位角（北=0 顺时针）转 ENU。
- `SkyFeature`（id `rocky_sky`）：取 `RockyMapFeature::rockyContext()` 调
  `createSkyModule`，挂 **worldRoot**。
- `CloudFeature`（id `rocky_cloud`）：挂 **contentRoot**。
- `OceanFeature`（id `rocky_ocean`）：挂 **contentRoot**。
- 文件末尾 `VSGVP_REGISTER_FEATURE` ×3；预设在 `ViewportConfig.cpp` 接入。

### 3.6 每帧数据流

```
Feature::onFrame(ctx, tick)
  └─ makeFrame(): 相机矩阵 + contentModel + 时间 + (手算)太阳/风
       └─ module->onFrame(frame)
            ├─ fillEnvUBO(): 写 48-float UBO（view_proj/model/cam/太阳/风/misc）
            ├─ Cloud: 积云公告板 CPU 重算四顶点（环心=相机ENU，面向相机）
            └─ Ocean/CloudSheet: 位移/漂移全在 vertex shader（顶点缓冲静态）
```

---

## 4. 现状差距（Phase 1 → 产品级 的硬伤）

> 这些是“正确性”问题，不是“缺功能”。Phase 2 优先解决。

1. **太阳双真值源会错位**：`SkyModule` 用 `setDateTime` 让 `rocky::SkyNode` 按星历算真实
   太阳；而云/海 shader 的太阳方向是 Feature 里用 `sun_el/sun_az` **手算**的，二者不同步
   → 云顶高光、海面耀斑与天上太阳位置不一致。
2. **海无海岸线/水深掩膜**：海面是固定尺寸、alpha≈0.92 的硬边平面，会**穿过山体、盖在
   陆地**上，边缘刀切。
3. **反射是假的**：海面反射的是 frag 里写死的天顶/地平线颜色，不反射真实 SkyNode/地形/云。
4. **层云片不跟随相机**：积云环与海面跟随相机，层云片顶点固定在锚点 ENU，相机走远被甩掉。
5. **云是平面 alpha 贴片**：侧看成纸片，无体积、无自阴影、无云阴影投影、无卷云/降水。
6. **波浪是 4 个写死 Gerstner 谐波**：不与风速/风区挂钩，无白沫物理、无破碎浪、无船行
   尾迹、无海面采样 API。
7. **工程未产品化**：无 LICENSE/SPDX、无独立 CMake install / 版本 / 示例 / 测试 / 文档，
   shader 路径靠 `/proc/self/exe` 回退，日志用 `fprintf`。

---

## 5. 目标架构（产品级）

### 5.1 分层

```
┌──────────────────────────────────────────────────────────────┐
│ 宿主集成层 (gui/viewport3d/EnvFeatures)  —— 薄，仅取数据/挂载    │
├──────────────────────────────────────────────────────────────┤
│ rocky_env 公共 API (EnvApi.h)                                 │
│   IEnvModule · EnvFrame · EnvWeather · IEnvSampler · 工厂       │
├───────────────┬───────────────┬──────────────────────────────┤
│  SkyModule    │  CloudModule  │  OceanModule                 │
│ 大气/星历/光照 │ 体积云/层云/降水│ FFT 海/反射/白沫/水深          │
├───────────────┴───────────────┴──────────────────────────────┤
│ 共享内核 (src/core)                                            │
│   · EphemerisBridge：rocky::Ephemeris → ENU 太阳/月亮（单一真值）│
│   · EnvWeather：风廓线/云量/能见度/海况(蒲福)/降水               │
│   · EnvUBO / PipelineFactory / ShaderCache / GpuBuffer         │
│   · TerrainBridge：rocky 高程/法线采样（海岸线掩膜、云阴影、反射）│
├──────────────────────────────────────────────────────────────┤
│ rocky (MapNode / SkyNode / Ephemeris / 高程)  +  VSG (Vulkan)   │
└──────────────────────────────────────────────────────────────┘
```

### 5.2 单一真值源：EphemerisBridge（最高优先）

rocky 已提供 `rocky::Ephemeris`（`#include <rocky/Ephemeris.h>`）：

```cpp
struct CelestialBody {
    Angle rightAscension, declination, latitude, longitude;
    Distance altitude;
    glm::dvec3 geocentric;   // ECEF 方向/位置
    glm::dvec3 eci;
};
class Ephemeris {
    CelestialBody sunPosition(const DateTime&)  const;
    CelestialBody moonPosition(const DateTime&) const;
};
```

设计：新增 `core/EphemerisBridge`，每帧（或时间/锚点变化时）：
1. `ephem.sunPosition(dateTime)` 得 `geocentric`（ECEF 太阳方向）。
2. 用锚点 ENU 的 ECEF→ENU 旋转（`contentModel` 的旋转部分的逆）把太阳方向变到 ENU。
3. 由 ENU 方向求 `sun_el`（仰角）/`sun_az`（方位角）与光照颜色/强度（按大气质量）。
4. **写入 `EnvFrame`**，供所有模块与 shader 使用；Feature 不再手算太阳。
5. 同一时间也驱动 `SkyNode::setDateTime` 与 `sky_->sun` 平行光方向，保证天空与地表一致。

时间推进：`SkyParams`/`EnvFrame` 支持运行时 `setTime`（可仿真倍速），替代当前固定 UTC。

### 5.3 TerrainBridge（地形闭环）

封装对 rocky 地形的查询，供三类用途：
- **海岸线/水深掩膜**：海面 fragment 采样地形高程，`terrainHeight > seaLevel` 处 discard
  （陆地）；水色按 `depth = seaLevel - terrainHeight` 做深浅插值与水深雾。
  实现路径：优先复用宿主已注册的 `ctx.geoQuery()->intersectRayGeo / 高程`；
  高密度需求走 rocky elevation layer 在 GPU 的纹理/采样（二期）。
- **云阴影投影**：把云密度沿太阳方向投影到地形/海面高度场（投影纹理 / ray-march 阴影）。
- **地形反射**：反射 pass 中包含真实地形与天空。

接口草案：
```cpp
class ITerrainBridge {
public:
    virtual float  heightECEF(const vsg::dvec3& ecef) const = 0;   // 米
    virtual float  heightENU (double ex, double ez) const = 0;    // 锚点 ENU
    virtual vsg::vec3 normalENU(double ex, double ez) const = 0;
    virtual bool   isLand(double ex, double ez, float seaLevel) const = 0;
};
```
由宿主实现并在工厂参数中传入（模块不直接依赖业务地形对象）。

### 5.4 海洋目标（对标 Triton）

- **可配置海洋边界（OceanBounds，由参数变量指定）**：海洋不再是“无限跟随”或写死尺寸，
  而是一块**带显式边界**的海域。边界内相机跟随铺满、边界处软渐隐到无。
  ```cpp
  struct OceanBounds {
      enum Shape { Disc, Rectangle } shape = Disc;
      vsg::dvec3 centerENU{0.0, 0.0, 0.0};  // 海域中心（锚点 ENU，米）；也可给经纬高
      float radius   = 8000.0f;             // Disc 半径（米）
      float halfX    = 8000.0f;             // Rectangle 半边长 X（米）
      float halfZ    = 8000.0f;             // Rectangle 半边长 Z（米）
      float seaLevel = 0.0f;                // 海平面（ENU +Y，米）
      float fadeWidth= 400.0f;              // 边界软渐隐带宽（米），消除刀切硬边
  };
  ```
  边界判断在 vertex/frag 内按到中心距离/矩形 SDF 做，超出 `fadeWidth` 即 alpha→0；
  `shape/中心/尺寸` 全部可由 `OceanParams` / 运行时变量配置。
- **波谱（强制 Vulkan compute，GPU FFT）**：用 **compute shader + GPU FFT** 生成方向波谱
  位移场（Tessendorf 法）：Phillips/JONSWAP/Pierson–Moskowitz 谱，风速/风向/风区 → 蒲福
  海况；ping-pong storage image 输出位移/choppy/法线/雅可比（白沫）纹理；顶点着色器
  **顶点纹理采样 (vertex texture fetch)** 取位移，替代当前 4 个写死 Gerstner 谐波。
  采用**级联瓦片**（近景小尺寸高频瓦片 + 远景大尺寸低频瓦片）复用同一份 FFT。
- **无限/跟随**：clip-space 投影网格（projected grid，屏幕空间细分，远处省顶点）或相机
  跟随网格 + 屏幕空间边缘渐隐；与 OceanBounds 结合（边界内跟随、边界软收）。曲率开关预留。
- **光学**：**平面反射 planar reflection**（见 §5.8）反射真实 SkyNode + 地形 + 云；
  菲涅尔（Schlick，可选精确）；折射 + 水深雾 + 次表面散射（浪涌透光）；太阳镜面耀斑随
  海况粗糙度（GGX/Blinn）。
- **白沫/破碎**：用 FFT 位移场的**雅可比行列式**（波陡收敛）生成白沫掩码 + 风成覆盖率，
  比单纯高度阈值物理；（远期）破碎浪与船行尾迹。
- **仿真采样 API**：
  ```cpp
  class IOceanSampler {
  public:
      virtual float waveHeightENU(double ex, double ez, double time) const = 0;
      virtual vsg::vec3 normalENU(double ex, double ez, double time) const = 0;
      virtual int   seaState() const = 0;   // 蒲福风级/海况
  };
  ```
  CPU 侧用与 GPU 相同的波谱参数（可回退到 Gerstner 求和），保证所见即所采。

### 5.5 云目标（对标 SilverLining）

- **体积积云**：ray-march 3D 密度（或 3D 噪声 impostor），光照自阴影（Beer–Lambert +
  银边/前向散射 powder 效应），顶亮底暗。
- **多层云**：卷云（高空 wispy anisotropic）+ 积云 + 层云/雾，各自高度/覆盖/风切变。
- **覆盖与天气**：`cover` 驱动密度阈值与覆盖率；接入 `EnvWeather`（云型、云底高、能见度）。
- **云阴影**：投到地形与海面（见 TerrainBridge）。
- **动态**：风漂移用世界坐标噪声（相机跟随不重复）、风切变分层速度；（远期）降水粒子、
  雷电、雾。

### 5.6 天空/大气目标

- 一期复用 `rocky::SkyNode`（atmosphere + stars + sun + moon + ambient/directional light）。
- 若 rocky 散射品质/可控性不足，提供可切换的 **Bruneton 预计算大气 LUT** 方案
  （transmittance / scattering / irradiance 表），统一曝光/色调映射与雾。
- 星表/银河、月相、暮光色温、能见度/气溶胶消光，与 `EnvWeather` 联动。

### 5.7 公共 API 演进（向后兼容）

`EnvFrame` 追加字段（末尾、带默认值），新增共享状态对象，不改既有签名：

```cpp
struct EnvDateTime { int year,month,day; double hoursUtc; double timeScale; }; // 运行时推进

struct EnvWeather {
    float windSpeed, windAzDeg;        // 海面风
    float windDirENU[2];
    float cloudCover;                  // 0..1
    float visibilityM;                 // 能见度（雾/消光）
    int   seaState;                    // 蒲福海况（可由风速推导）
    int   precipitation;               // 0=晴 1=雨 2=雪 ...
};

// 工厂扩展（新增，不破坏旧工厂）：
struct SkyCreateInfo   { void* rockyContext; /*ITerrainBridge*/ void* terrain; EnvDateTime dt; SkyParams p; };
struct CloudCreateInfo { CloudParams p;  void* terrain; };
struct OceanCreateInfo { OceanParams p;  void* terrain; /*IOceanSampler out*/ };
```
模块通过 `onFrame(EnvFrame)` 接收星历/气象结果；宿主可选实现 `ITerrainBridge` 注入。

### 5.8 反射方案选型（已决策：平面反射为主）

海面颜色大部分来自**反射环境**。三种真反射方案对比：

| 方案 | 做法 | 质量 | 开销 | 取舍 |
|---|---|---|---|---|
| **① 平面反射 Planar Reflection**（Triton 采用，**主选**） | 把海面当镜面，构造**关于海平面对称的反射相机**，把场景（SkyNode/地形/云/目标）渲染到离屏 RT，海面片元按反射向量采样 | ★★★★★ 真实岸/云/太阳倒影 | ≈多渲染一次场景 | 近海/岸边产品级 |
| ② 反射探针/环境贴图 Probe/EnvMap | 预渲染/用天空生 cubemap，按反射方向采样 | ★★★ 只反天空、岸边死 | 极低（静态） | 远海/低配 fallback |
| ③ 屏幕空间反射 SSR | 在已渲染画面沿反射向量 ray-march | ★★★☆ 屏外缺失需 fallback | 中 | 远期补近景细节 |

**决策：以 ① 平面反射为主**，反射 pass 用 VSG 离屏 View/RenderGraph（color+depth RT），
输出 `reflectionColor` 纹理绑定到海面 StateGroup；反射相机 = 主相机关于海平面反射平面
（`reflect(view, plane)`）裁剪到反射视锥。**高性能降级链**：
1. 反射 RT **半分辨率**（如 1/2 或 1/4），模糊代价低、水面有波纹本就不需锐利；
2. **隔帧更新**反射纹理（海面变化慢），复用上一帧；
3. 反射 pass **只渲染反射视锥内**的天空/地形/必要目标，剔除小物件；
4. 随**距离/粗糙度衰减**：远处或大浪（粗糙）时反射权重降到天空色/探针 ②；
5. 质量档（`EnvQuality`）可关反射回退 ②，供低端 GPU。

### 5.9 高性能渲染策略（贯穿所有模块）

- **GPU 计算优先**：波浪 FFT、法线/雅可比推导、（远期）泡沫/云光照放 **Vulkan compute**
  （VSG `ComputePipeline`/`BindComputePipeline` + dispatch），减轻 vertex/fragment。
- **级联 LOD**：海浪位移纹理多级瓦片（近高频/远低频）；云层近 impostor/远贴片。
- **屏幕空间细分**：海面用 projected grid，远处少顶点；视锥外不画。
- **降分辨率通道**：反射、白沫、云 ray-march 均可半分辨率 + 双边/模糊重建。
- **数据预烘焙**：云噪声烘焙到 2D/3D 纹理 LUT（替代每片元程序 fbm）；Phillips 谱一次
  生成复用。
- **早剔除/早退**：陆地 discard 在深度/填充早期；云 ray-march 自适应步长 + 低密早退。
- **资源复用**：FFT ping-pong 图像、反射 RT、UBO 描述符跨帧复用；indirect draw。
- **质量档 EnvQuality**：网格细分 / FFT 尺寸 / 反射分辨率与更新率 / ray-march 步数统一分级。

---

## 6. 功能实现列表（Backlog）

> 每项给 ID，便于提交/跟踪。阶段：P2=正确性地基，P3=物理升级，P4=产品化。
> 优先级：H=高（先做），M=中，L=低/远期。

### 6.1 工程产品化（ENG）
| ID | 功能 | 阶段 | 优先级 | 性能/备注 |
|---|---|---|---|---|
| ENG-1 | env_modules **独立 CMake 工程**：`project(rocky_env)`、可独立 configure 也可 `add_subdirectory`、install target、`rocky_envConfig.cmake`（find_package rocky/vsg）、版本号 | P4 | **H** | 用户已拍板：现在就按独立模块组织 |
| ENG-2 | `LICENSE`(MIT) + 所有源/shader 头 **SPDX 标识** | P4 | **H** | 与 rocky(MIT) 一致 |
| ENG-3 | `examples/` standalone demo（rocky 地球 + env，**不依赖 Qt**）+ README/截图 | P4 | M | 验证独立性 |
| ENG-4 | 结构化日志接口（宿主可注入回调，默认 fprintf）；资源路径可注入（替代 /proc/self/exe 猜测） | P4 | M | |
| ENG-5 | 测试：波高采样单测、UBO 布局静态校验、多海况/时段/云量**截图回归** | P4 | M | |
| ENG-6 | 健壮性：shader 热重载、device lost/resize 生命周期、`EnvQuality` LOD 预算 | P4 | M | 见 §5.9 |

### 6.2 共享内核（CORE）
| ID | 功能 | 阶段 | 优先级 | 备注 |
|---|---|---|---|---|
| CORE-1 | **EphemerisBridge**：`rocky::Ephemeris.sunPosition/moonPosition` → ECEF→ENU，太阳/月亮方向+仰角/方位+颜色强度；**单一真值源**驱动 EnvFrame 与 SkyNode 平行光 | P2 | **H** | 消灭太阳错位（现状硬伤#1） |
| CORE-2 | **EnvDateTime + timeScale**：运行时时间推进/仿真倍速，替代固定 UTC | P2 | **H** | 驱动 CORE-1 |
| CORE-3 | **EnvWeather**：海面风/风廓线、云量、能见度、蒲福海况（风速推导）、降水类型 | P3/P4 | M | 海/云/天共享 |
| CORE-4 | **TerrainBridge / ITerrainBridge**：`heightENU/normalENU/isLand`，宿主注入；GPU 高程纹理通道 | P2 | **H** | 海岸线/云阴影/反射共用 |
| CORE-5 | UBO/API 演进：追加 moon/visibility/seaState/oceanBounds/quality 字段（只增不改） | P2 | M | 见 §5.7 |

### 6.3 天空（SKY）
| ID | 功能 | 阶段 | 优先级 | 备注 |
|---|---|---|---|---|
| SKY-1 | 星历驱动 Sun/Moon + ambient/directional 光参数（颜色/强度随大气质量）输出给云/海 | P2 | **H** | 与 CORE-1 联动 |
| SKY-2 | 统一雾/能见度/曝光/色调映射，云海洋受 SkyNode 大气参数驱动 | P2 | M | |
| SKY-3 | （可选）Bruneton 大气 LUT 后备；星表/银河/月相/暮光色温 | P3/P4 | L | rocky 散射不足时启用 |

### 6.4 海洋（OCN，重点）
| ID | 功能 | 阶段 | 优先级 | 性能/备注 |
|---|---|---|---|---|
| OCN-1 | **OceanBounds 可配海洋边界**（Disc/Rectangle、中心、半径/半边长、海平面、fadeWidth），边界软渐隐；参数/运行时变量可改 | P2 | **H** | 用户拍板；见 §5.4 |
| OCN-2 | **海岸线 + 水深掩膜**：TerrainBridge 高程，陆地 discard，水深色/水深雾 | P2 | **H** | 消灭海穿山（硬伤#2） |
| OCN-3 | **Vulkan compute GPU FFT 波谱**：Phillips/JONSWAP/PM，风速/风区→海况；compute 出位移/choppy/法线/雅可比纹理；顶点纹理采样替代 Gerstner | P3 | **H** | 用户拍板强制 compute；级联瓦片 |
| OCN-4 | **投影网格/相机跟随 + 边缘渐隐**，与 OceanBounds 结合；曲率开关预留 | P2/P3 | M | projected grid 省顶点 |
| OCN-5 | **平面反射 planar reflection**：离屏反射 RT 反射真实天空/地形/云；半分辨率+隔帧+距离衰减降级链 | P3 | **H** | 见 §5.8；消灭假反射（硬伤#3） |
| OCN-6 | 菲涅尔(Schlick→精确) + 折射 + 水深雾 + 次表面散射 + GGX 太阳耀斑 | P3 | M | |
| OCN-7 | **物理白沫**：FFT 雅可比行列式波陡掩码 + 风成覆盖率（半分辨率通道） | P3 | M | |
| OCN-8 | **IOceanSampler**：CPU 与 GPU 同源波高/法线/海况采样（浮力/雷达） | P4 | M | CPU 回退谱求和 |
| OCN-9 | 远期：破碎浪、船行尾迹、SSR 近景补反射 | P4 | L | |

### 6.5 云（CLD）
| ID | 功能 | 阶段 | 优先级 | 性能/备注 |
|---|---|---|---|---|
| CLD-1 | **层云片相机跟随 + 世界坐标连续平铺** | P2 | **H** | 消灭硬伤#4 |
| CLD-2 | **体积积云**：ray-march 3D 密度/impostor，自阴影(Beer-Lambert/powder)+银边顶亮底暗；噪声烘焙 LUT、自适应步长、半分辨率 | P3 | **H** | 消灭纸片云（硬伤#5） |
| CLD-3 | **多层云**：卷云/积云/层云，各高度/覆盖/风切变 | P3 | M | |
| CLD-4 | **云阴影投影**地形/海面（沿太阳方向投影密度） | P3 | M | 依赖 CORE-1/CORE-4 |
| CLD-5 | 远期：降水（雨/雪粒子）、雾、雷电 | P4 | L | 接 EnvWeather |

---

## 7. 路线图（里程碑，按阶段）

### Phase 1 —— 效果占位（✅ 已完成）
层云片 + 积云环、相机跟随 Gerstner 海 + 菲涅尔近似、封装 SkyNode、Feature 接入、
独立库骨架与统一 UBO。**目标：立即可见昼夜/大气/云/海。**

### Phase 2 —— 正确性/一致性地基（进行中，大部分已落地）
- [x] **P2-1 太阳单一真值源**：`EphemerisBridge`（rocky）+ `NativeEphemeris`（纯 C++，
      双后端同源）统一驱动 EnvFrame + SkyNode；Feature 侧太阳优先取星历真值，
      手算 `sun_el/sun_az` 仅作无天空回退；支持运行时时间推进（年月日时+倍速）。
      两后端共享 `AtmospherePhysics`，光照/雾色/海天反射色与太阳位置严格一致。
- [x] **P2-2 海岸线 + 水深掩膜**：`ITerrainBridge` 顶点高程回写 UV + 海面 frag
      shoreFactor/discard、水深色、水深雾、距离雾（能见度由 EnvFrame 下发）。
- [x] **P2-3 无限海面（部分）**：ENU 相机跟随网格 + `OceanBounds` 软边界渐隐；
      投影网格/曲率留待 Phase 3。
- [ ] **P2-4 层云片相机跟随 + 世界坐标连续平铺**（与积云/海对齐）。
- [x] **P2-5 统一雾/能见度/曝光**：云/海洋 shader 消费 `AtmosphereState` 真值
      （`ILightingProvider`），暮光/夜间与天空颜色一致；天气参数（能见度/云量/
      风速/降水）联动大气计算（P1-1/P1-2）。

> **Sky 双端完善（2026-09-17 落地，冒烟 24/24 PASS）**：
> - `SkyModule.cpp` 修复历史损坏（后端 onFrame 错乱片段 + 文件尾残留）。
> - Native 后端 LUT 三纹理**真正绑定到 set=1**（此前为空占位，穹顶查不到表）；
>   LUT 数据迁移 `vec4Array2D/3D`（描述符要求维度元信息）；无 LUT 时回退解析散射。
> - Rocky 后端月盘改为**相机跟随公告板**并真正挂载 + 每帧 UBO/几何更新
>   （此前管线已建但未挂载未填充，几何球心在地心不可见）。
> - 新增 `IAtmosphereQuery`：任意两点视线透过率/单次散射路径辐射（8 段
>   Rayleigh/Mie 消光积分 + Kasten-Young 大气质量 + 相函数），供传感器链路
>   大气衰减消费；`AtmosphereLUT` 增加 CPU 采样（与 shader 归一化一致）。
> - `buildSkyLUTPipeline` 支持传入 LUT 描述符；冒烟测试 `env_smoke_sky`
>   （星历/暮光/透过率/接口全链路）常驻 `env_modules/tests/`。
>
> **Sky 接口产品化（2026-09-17 v2 落地，冒烟 41/41 PASS）**：
> - `EnvApi.h` 新增 **`ISkyControl`**：运行时控制（观测锚点 setObserver、曝光、
>   浊度、日/月/大气开关，get/set 成对），供调试面板/想定编辑器消费。
> - `EnvFrame` 追加 **动态锚点通道**（`anchor_lat/lon/alt_deg`，NaN=不更新，
>   仅 CPU 侧、不入 UBO）：平面模式相机跨区/长航线移动后星历太阳/星空跟随，
>   解决"平面模式锚点写死"的缺口。
> - 新增 **`IEnvStack` + `createEnvironmentStack`**（`src/EnvStack.cpp`）：
>   sky+cloud+ocean 一站式聚合入口，双根挂载（worldRoot/contentRoot 自动
>   分流）、`onFrame(EnvFrame&)` 内部保证 sky→cloud→ocean 的真值传播
>   （星历太阳 + AtmosphereState 覆写回 EnvFrame），宿主无需关心模块顺序；
>   `query<T>()` 模板能力转发。**常规 VSG 平面与 rocky 地球宿主统一走此入口。**
> - `IAtmosphereQuery::transmittance` 太阳侧透过率改走 **LUT CPU 采样**
>   （`ISkyBackend::atmosphereLUT()`，与 sky_dome_lut.frag 归一化同源），
>   无 LUT 回退解析公式。
> - 内部 `SkyTypes.h` 增加 `SkyRuntime`/`applyRuntime` 运行时通道（脏标记）。
> - 接口使用指南：`env_modules/docs/EnvApi_Usage.md`（rocky 地球路径
>   完整示例）。

> **Rocky 单后端收口（2026-10-09 落地，全线产品 rocky 地球基座化）**：
> - **NativeSkyBackend / NativeEphemeris / SkyParams::backend 三态选择 /
>   VSGRAYSIM_SKY_BACKEND 环境变量全部删除** —— 上文"双后端/平面模式/
>   Native LUT"条目为历史记录, 对应代码已退场; 天空恒为
>   `RockySkyBackend`（ECEF + rocky::SkyNode + 星历）。
> - `EnvFrame::model` 恒为锚点 ENU→ECEF（`anchor_*` 动态锚点通道与
>   EnvFrame 平面单位阵语义随之退役; geo_anchor 单一来源 = 场景文件,
>   RockyMapFeature::setGeoAnchor 运行时重锚）。
> - 传感器链地面三级链: TerrainProvider (.vsgterrain) > rocky 地表高程网格
>   （`GeoElevationQuery` 桥, SceneSource.h）> 常高平面兜底;
>   `GroundDesc.half_extent` 退役, `terrain_ref` 语义 = 地形工程/地形包/空。

### Phase 3 —— 物理模型升级（观感对标 Triton/SilverLining）
- [x] **P3-4a（v4, 2026-09-17 落地, 冒烟 25/25 PASS）Cloud 多层语义化 + 控制查询**:
- - `CloudLayerType`（卷云/层云/层积云/积云/积雨云）+ `CloudLayerParams`
      （高度/厚度/覆盖/密度逐层独立）; `CloudParams::layers` 空时回到一期
      单层兼容模式（行为不变）。
- - `ICloudControl`：全局云量调制 / setLayers 整体替换 / 逐层覆盖与开关
      （运行时, 下一帧生效）; `ICloudQuery`：云底/云顶、**cloudShadow
      shadowFactor（CPU 云量图与 cloud_sheet.frag fbm 逐公式同源）**、
      总云量 —— 仿真侧（云影下的光照/探测）可直接消费。
- - shader 类型化成形：卷云沿风向强拉伸薄纱 / 层积云高频团块 /
      积雨云塔身+砧状顶+雨幡 / 积云一期轮廓; **银边**（背光云缘前向散射
      发亮）; 降水→云体变暗+云底雨幡。
- - `ocean.frag` 接入**层云阴影**：海面沿太阳方向投影采样同一 fbm 云量图
      （最大压暗 45%）, 与画面云影一致。
- - CPU/GPU 同源要点：`cloudf2`（hash2/vnoise2/fbm）逐公式复刻 GLSL
      hash/noise/fbm（含 fract 语义与 dot 扰动）; 特征尺度 600m + 风漂移
      0.015 系数一致。
- - 冒烟 `env_smoke_cloud`（多层配置/层开关/云量调制/云影行为）常驻
      `env_modules/tests/`。
- [ ] P3-4b 体积积云 ray-march + 自阴影（半分辨率 pass 基建）。
- [ ] P3-5b 云阴影精确投影（云底平面 coverage→模糊贴图, 替代 fbm 内联）。
- [x] **P3-1a（S1, 2026-09-17 落地, 冒烟 33/33 PASS）Ocean 波场真值源 + 仿真采样闭环**：
- - `WaveField`（`src/WaveField.h/.cpp`, 纯 C++）：JONSWAP 谱（风速/风区/风时 →
      Hs/Tp/蒲福级, PM 上限钳制）→ 8~16 个 Gerstner 谐波表（能量按谱形状
      分配归一化到 Hs, 方向绕风向 ±60° 展开）→ CPU 采样
      （waveHeight/waveNormal/orbitalVelocity/jacobianFoam, 与 shader 逐公式一致）。
- - UBO 扩展 `kEnvUboFloats 92→124`：`oceanWave[8]`（offset 92..123, x=L y=A
      z=cosθ w=sinθ）——shader 不再有写死谐波, 改波场=改 UBO 零重建。
      sky/cloud shader 块止于 astro(91), 多余字节无害。
- - `ocean.vert`：逐谐波 Gerstner 累加（公式与 CPU 严格对齐, 无每谐波相移）
      + 输出雅可比 `vJacobian`。
- - `ocean.frag`：白沫物理化（雅可比波陡掩码 J<0.78..0.92 + Monahan 风成覆盖
      + 岸线 <3m 浪花环带）+ 高光粗糙度随蒲福级 + 降水增强消光。
- - `EnvApi.h` 新增 `SeaState` / `IOceanSampler` / `IOceanControl`
      （运行时 setWind/setWaveHeight(0=恢复自动)/水色/边界）;
      `OceanParams` 追加 fetch/duration/harmonics/choppy/auto_sea_state。
      `wave_length` 默认改 0=自动（谱峰 JONSWAP 派生）。
- - 冒烟 `env_smoke_ocean`（海况量级/单调性、Hs 谱-统计一致性、法线/轨道速度、
      白沫、运行时控制、恢复气象驱动）常驻 `env_modules/tests/`。
      数值样例: U10=10 → Hs 1.38m/Tp 8.2s/蒲福5; U10=20 → Hs 2.76m/蒲福8。
- [x] **P3-2a（S2, 2026-09-17 落地, 冒烟 47/47 PASS）Ocean 光学/几何升级**:
      涌浪叠加（addSwell 3 长波谐波, 总 Hs=√(风浪²+涌浪²)）、方向相关天空
      反射（skyProbe 探针级 + GGX 耀斑 + 粗糙度蒲福/距离调制）、次表面散射
      （背光波峰透光）、球面曲率（d²/2R 下沉, OceanParams::curvature）、
      UBO 160（oceanWave[16]+oceanOpt）、legacy 波形模式（wave_mode=0 复刻
      Phase 1 四写死谐波, 向后兼容）、降水消光。IOceanControl 扩展
      setSwell/setReflection/setCurvature。真·planar reflection 离屏 RT
      归入 P3-2b（需宿主 RenderGraph 基建）。
- [ ] P3-2b planar reflection（真实天空/地形/云 离屏 RT）+ 折射细化。
- [ ] P3-3 物理白沫 GPU 通道（雅可比已在 vert 计算, FFT 版升级纹理化）。
- [x] P3-6（部分）Bruneton 大气 LUT（CPU 预计算 + set=1 绑定 + CPU 采样接口）、
      星空/银河（天球投影 + LST 旋转 + 月辉压星）、月相/月盘/月光、暮光染色。
      剩余：LUT 与 rocky SkyNode 大气层的视觉融合调优。
- [x] **P3-6b（S2, 2026-09-23 落地, 冒烟 3 套全绿）天空 LUT 渲染链路修复 + 昼夜时序修复**:
      - **LUT 纹理上传根因**: 纹理格式必须与 Data 精度严格一致 ——
        `vec4Array2D/3D`(4×f32=16B/纹素) 配 RGBA16F(8B/纹素) 时 VSG
        `Context::copy` 按 `data->dataSize()` 原样拷贝 = 2 倍超量缓冲写入
        8B/纹素目标 → 未定义行为/采样恒 0（天空整片黑）。统一改
        `VK_FORMAT_R32G32B32A32_SFLOAT` + `data->properties.format`（LUT <1MB）。
      - `vsg::Image` 默认 `imageType=VK_IMAGE_TYPE_2D`、`ImageView` 默认
        `VIEW_TYPE_2D`: scattering 3D LUT 必须显式设 3D（2D 类型带 depth=32
        创建失败会拖垮整个 set=1 描述符组）; usage 需含 TRANSFER_DST_BIT。
      - `sky_dome_lut.frag`: 撤诊断分支; LUT 输出乘 `SUN_ILLUMINANCE=20`
        还原单位照度量纲; 撞地射线早退输出 haze（消除楔形碎片 + 下仰半球
        不再死黑）; 穹顶细分 24×48 → 96×192。
      - **env_viewer 昼夜时序**: 星历同步+太阳查询必须在天空 onFrame **之前**
        （此前 UBO 太阳来自 makeFrame 固定 --sun-el, --hours-utc 对天空无效）;
        大气状态（horizon/zenith/sun_light/is_day）也须先写回再分发
        （每帧新建 EnvFrame 的日间默认色会覆盖真实状态, 夜间 haze 死白）。
      - 验证: 正午/黄昏/午夜截图三态分化（夜空近黑 + 地平线深蓝）。
      剩余调优: 正午地平线过橙（multipleScatteringFactor 近似）、暮光红染。
- [x] **P3-6c（S2, 2026-09-23 落地, env_regress 9/9 PASS）天空/云/海 11 项问题修复
      （T1-T4/C1-C4/O1-O3）+ 三大根因排除**:
      - **T4 真根因（午夜星空不可见）**: `celestialLayer`/月盘被乘的是
        **太阳射线**透射率 `sampleTransmittance(viewHeight, sunCosZenith)` ——
        透射 LUT 参数化与射线方向无关，同一张表既查太阳射线也查视线;
        computeTransmittance 对撞地射线 `continue` 跳过 → 该 LUT 区域数组
        零初始化恒 0。夜间太阳在地平线下 → 星光/月辉整体乘 0 → 星空消失。
        修复: 星空/月盘改用**视线方向**透射率 `sampleTransmittance(viewHeight,
        viewCosZenith)`（星光沿视线穿大气, 与太阳无关）。
      - **scattering LUT NaN 纹素（832 个）**: computeScattering 中 h=0 切片
        向下视线, raySphereIntersect 因 c=0、近交点 t0=0 非正而退回对跖点
        远交点（t1≈2R）→ 积分穿入行星 → exp(+h/8.5km) 溢出 → NaN 纹素,
        经 GPU 三线性过滤（w 轴混合 z=0 切片）毒化地面附近全部采样。
        修复: `h<=0 && cosZenith<0` 早期特判置零（min(tFar, groundHit) 截断
        无效 —— 对跖点远交点在中等俯角下比大气出射点更近）。
      - **shader 加载错位（调试假象元凶）**: env_viewer 启动时 chdir 到含
        `shaders/env` 的目录, 恰好命中 `build/shaders/env` 过期副本 →
        "改 shader 不生效 / 探针失踪 / 语法错误无感"三重假象。修复:
        移除 chdir; `resolveEnvShader` 候选序改为 CWD 相对 → 源码树（exe
        上溯 3 级）→ exe 相对 → share, 并打印最终解析路径诊断。
      - **T1-T3**: 臭氧高斯廓线（Chappuis 带黄昏绿吸收）+ 多次散射近似 +
        撞地射线早退 haze（楔形碎片消除）、穹顶细分 96×192（另: 楔形伪影
        sky-only 仍复现, 属穹顶自身, 遗留）。
      - **C1-C4**: 云量 0.2/0.5/0.8 非均匀度单调 10.4→13.2→44.2（覆盖
        单调性/形态/光照）; 夜间云变暗（midnight 24.6 vs noon 227.5）。
      - **O1-O3**: 海况三档近场亮度 w5/w10/w20=147.9/150.8/159.8 单调,
        波面纹理能量 0.046/0.066/0.079 单调（白沫/波高随风速增长）。
      - **env_viewer**: mode 挂载级隔离（mode=sky 时仅挂 dome, 排除云/海
        探针污染）; X11 抓帧须 ≥720p（320×240 map_state=0 全黑竞态）。
      - 验证: `./env_modules/tools/env_regress.sh /tmp/env_regress` →
        9 PASS / 0 FAIL（sky_noon/dusk/midnit + cloud_02/05/08 +
        ocean_w05/10/20 + cloud_midnit）。
- [x] **P3-7（S2, 2026-09-27 落地, 双波段截图验收通过）多传感器仿真渲染 GPU 级联**
      （设计文档 `docs/design_3d_sensor_rendering.md` §9, 测试程序 `env_viewer`
      —— 2026-10-13 已与原 `sensor_viewer` 合并为单一程序, 前者以
      `--scene`/`--band` CLI 提供同等能力）:
      - **链路**: rad(蓝方物理辐射亮度 P0) → vis(观察口) → optics(PSF/渐晕 P2)
        → detector(QE/NETD/满阱 P3) → electronics(AGC/ADC/Gamma P4) →
        display(左观察口/右传感器口), 全离屏 RGBA32F 零回传。
      - **★ StateGroup 遍历根因（左半暗红 + rad/vis/optics 三图同值）**:
        VSG `RecordTraversal::apply(StateGroup)` 是 push→traverse→**pop** 配对,
        `StateStack::record` 仅在遍历到 Draw/VID 时应用栈顶且带 `stack[0]`
        去重缓存。**场景几何必须挂在 StateGroup 内部**（push 与 draw 之间）;
        若与 SG 并列挂 RG, pop 先于几何遍历 → draw 时栈空 → 管线/描述符不绑 →
        GPU 沿用上一 pass 管线（症状: rad/vis 执行 optics 行为自反馈 0.065→
        0.055→0.047）。chain 管线 (det/elec/disp) 因 draw 在 SG 内部而幸免。
      - **GPU readback 方法论**: 原生 Vulkan
        (vkCmdPipelineBarrier SHADER_READ_ONLY→TRANSFER_SRC +
        vkCmdCopyImageToBuffer + DeviceMemory::map) 直读五张离屏图真值,
        绕过采样/描述符疑点; 配合 shader 签名实验 (rad.frag 输出 9.0)
        实锤「frag 从未执行」。
      - **暖身 RG 保留**: scene 路径管线若为 command buffer 首个执行管线,
        NVIDIA + VSG 1.1.15 下 vkQueueSubmit 成功但 GPU 永不执行 (fence
        超时); 级联最前置 4×4 哑 RP 复用 chain 管线暖机。
      - **物理标定**: f_number 2.0→4.0 (与 well_ke=2e7 标定口径一致, F/2 下
        LWIR 地面 n_e≈4.3e7 超满阱 2 倍); AGC 分位 0.02/0.98→0.002/0.998
        (目标占比<2% 时 0.98 分位把 x_hi 拉到地面亮端淹没目标)。
      - **验证锚点 (960×640, 传感器 640×512)**: MWIR——引擎/尾焰饱和白、
        坦克体亮灰、地面中灰、天空黑; LWIR——常温坦克/地面整体亮且可辨;
        两波段 mean_lum 119.9/136.0 分化, det 链路 n_e/σ 数学自洽。

### Phase 4 —— 仿真 API + 开源产品化
- [x] P4-1 `IOceanSampler` 波高/法线/海况采样（CPU 与 GPU 同源, S1 落地;
      另有 `ICloudQuery` 云底/云顶/云影, v4 落地）。
- [ ] P4-2 `EnvWeather` 天气状态对象与星历/风廓线联动；降水/雾/雷电
      （部分已具备: 降水→云变暗/雨幡/海面消光; 缺统一状态机与 presets）。
- [ ] P4-3 独立开源工程：`LICENSE`(MIT) + 每个源/ shader 头 SPDX 标识、
      独立 CMake（`project(rocky_env)` / install / `find_package(rocky,vsg)` / 版本号）、
      `examples/` standalone demo、截图、README、API 文档。
- [ ] P4-4 测试：波高采样单测、UBO 布局静态校验、截图回归（多海况/时段/云量）。
- [ ] P4-5 工程健壮性：shader 热重载、device/resize 生命周期、结构化日志替代 fprintf、
      性能 LOD 预算（顶点数/ray-march 步数/反射分辨率分级）。

---

## 8. 开发约定

- **命名**：命名空间 `rockyenv`；类 `XxxModule`；工厂 `createXxxModule`；shader 小写
  `cloud_sheet.vert` 等；Feature id `rocky_sky / rocky_cloud / rocky_ocean`。
- **头文件**：对外只进 `include/rocky_env/`；内部工具放 `src/`（如 `EnvCommon.h`），
  不暴露 VSG 管线细节给宿主。
- **shader**：统一 `#version 460`；统一 `Env` UBO 块（布局见 §3.3）；新增字段只追加、
  同步改 `fillEnvUBO` 与所有 shader；顶点输入固定 loc0/1/2。
- **显隐**用 `vsg::Switch`；**坐标**云/海在 ENU、shader 内乘 `env.model`；天空在 ECEF。
- **不破坏**现有物理/光追/业务；宿主改动保持“薄”。
- **每次阶段完成回写本文件**（勾选路线图、更新现状与 UBO 表）。

### 构建 / 运行 / 验证
```bash
# 配置（cache 清掉时）
cd build && cmake .. -DQT_FORCE_MIN_CMAKE_VERSION_FOR_USING_QT=3.22
# 编译（先模块库，再 viewport3d 与三个 app）
make vsgraysim_rocky_env vsgraysim_viewport3d \
     vsgraysim_terrain_editor vsgraysim_scene_designer vsgraysim_sim_control -j$(nproc)
# 冒烟（期望看到挂载日志、无 shader compile failed / 断言）
cd build && timeout 14 ./app/vsgraysim_<name> 2>&1 \
  | grep -iE "rocky|env|sky|cloud|ocean|error|assert|shader"
```
shader 经顶层 `vsgraysim_assets` target 整目录拷贝到 `build/shaders/env/`。

---

## 9. 关键事实备忘（rocky / VSG）

- `rocky::VSGContext` = `rocky::VSGContextImpl*`；`SkyNode::create(ctx)` / `SkyNode(ctx)`；
  仅支持 geocentric（ECEF）世界 SRS：`setWorldSRS(rocky::SRS::ECEF)`。
- `SkyNode` 公有成员：`vsg::ref_ptr<vsg::AmbientLight> ambient;`
  `vsg::ref_ptr<vsg::DirectionalLight> sun;`，可 addChild 参与光照。
- 时间：`rocky::DateTime(int year,int month,int day,double hoursUtc)`；
  星历：`rocky::Ephemeris().sunPosition(dt)->geocentric`（glm::dvec3, ECEF）。
- 宿主：`RockyMapFeature::rockyContext()` 返回 `void*`（即 VSGContextImpl*）；
  `ctx.contentModel()` 返回 ENU→世界 dmat4；`ctx.geoQuery()` 提供地理/地形射线查询；
  `ctx.worldRoot()`（ECEF）与 `ctx.contentRoot()`（ENU MatrixTransform）。
- rocky 瓦片 GPU 编译需常驻 `vsg::CompileManager`（已在 RockyMapFeature 处理，勿回退）。
- 此版本 VSG 节点无 `mask`；用 `vsg::Switch` 控制显隐。
- **Vulkan compute**：VSG 提供 `vsg::ComputePipeline` / `vsg::BindComputePipeline` +
  `dispatch`，可对 storage image/buffer 跑 compute shader（GPU FFT 波浪谱用）；
  顶点着色器可通过 `sampler2D` 做顶点纹理采样（Vulkan 支持 vertex texture fetch）。
- **离屏反射**：用 VSG 离屏 `View` / RenderGraph 渲染到 color+depth RT，再把 color 作为
  `combined image sampler` 绑定进海面 StateGroup（平面反射，见 §5.8）。

---

## 10. 关键决策（已锁定）

1. **形态 = 独立模块**：env_modules 现在就按**独立 CMake 工程**组织（可独立 configure /
   install / find_package），同时保留 `add_subdirectory` 内嵌方式。对外只依赖 VSG + rocky，
   不依赖 Qt/业务。许可证 MIT（ENG-1/ENG-2）。
2. **海洋范围 = 可配边界**：不做“无限海”也不强制全球壳，而是 **OceanBounds 参数变量**
   指定海域（Disc/Rectangle 中心 + 半径/半边长 + 海平面 + 软渐隐带宽）。边界内跟随铺满、
   边界软收；曲率/全球壳作为远期开关（OCN-1）。
3. **反射 = 平面反射为主**（planar reflection，反射真实天空/地形/云），配
   半分辨率 + 隔帧更新 + 视锥剔除 + 距离/粗糙度衰减 + 探针 fallback 的高性能降级链（OCN-5/§5.8）。
4. **GPU 要求 Vulkan compute**：波浪谱强制 **GPU FFT（compute shader）**，法线/雅可比/白沫
   一并在 compute 出纹理；顶点纹理采样取位移（OCN-3）。
5. **高性能优先**：compute 卸载、级联 LOD、projected grid、降分辨率通道、噪声/谱预烘焙、
   早剔除早退、资源复用、`EnvQuality` 质量档（§5.9）。

> 落地顺序建议：**CORE-1/CORE-2（太阳单一真值源）+ OCN-1（可配海洋边界）+ OCN-2（海岸线
> 掩膜）+ CLD-1（层云跟随）** 先把“会穿帮/不一致”的硬伤清掉；随后 **OCN-3（GPU FFT）+
> OCN-5（平面反射）** 冲 Triton 观感；ENG-1/ENG-2 同步推进独立模块化。

## 11. 时间与数值契约改造（2026-11-06）

以下为本轮实际落地状态；前文路线图中的 GPU 常驻、SAR 聚焦和独立工程化描述
不能代替运行验收。

- [x] 通用时间/WGS84 数学迁到 `lib/simcore`，环境库移除 GUI 数学头依赖。
  纳秒有理时轴按事件序号计算，极区高程、局部 heading、地球射线命中修正。
- [x] 调度器输出曝光区间，CPI 节拍同源；积压分批保留。查看器以事件时钟
  合并 60 Hz 心跳和选中测量边界，不再 `frame/60` 或复用一帧发布多个测量。
  内核未实现的变步长/倍速明确拒绝；停止/帧数限制与事件回调重入有生命周期测试。
- [x] 真值位置/速度 Hermite 插值，拒绝非法/乱序数据；重复时间替换，外推
  过期返回失败。测量路径关闭外推，各实例位置分开，雷达相机使用 CPI 中点位置。
- [x] 雷达快时间 `sample_rate_hz` 与 FFT 点数解耦，CSR 分段累计与左邻桶边界
  修复；CPU 产品/传输/显示保留采样率和波束方向。脉压输入校验防止越界。
- [x] 云步进长度/中点/有限路径修复；BandRT 环境 UBO 按实例独立，MWIR/LWIR
  发射率参与着色，海面识别按显式水材质标记。环境公共 UBO 布局不变。
- [x] 主机产品缺载荷、维度错误、接收拒绝不计成功；帧 0 合法。`--publish`
  会读回真实产品；仅驻留标志不构成 GPU 产品句柄。P3/P4 当前分别是满阱比例
  与归一化显示值。曝光以配置为准，不再对 LWIR 隐式替换。
- [x] `ComplexImgStage` 不再把 RD 直通冒充聚焦图，未实现请求明确报错。
  普通雷达产品链为 Echo → RangeComp → RdMap → RD 幅度产品。
- [x] 新增 `tests/` 数值、内核生命周期、无窗口 Vulkan compute 回归并接入
  `run_regress.sh`；生产雷达 shader 与独立相干求和、生产云积分函数与解析
  均匀介质对照。软件 Vulkan 可验证 compute 数值；硬件 RT/视觉须另行验收。

验证记录：顶层全量构建通过；CTest 3/3 与 sky/cloud/ocean 三套 smoke 全部通过。
compute 设备为 llvmpipe LLVM 15。独立数值测试通过 ASan/UBSan；ptrace 环境不支持
LeakSanitizer，使用 `ASAN_OPTIONS=detect_leaks=0` 运行，未宣称完成泄漏检查。
视觉矩阵在无 DISPLAY 环境跳过，未完成硬件 RT 验收。

仍未完成：查看器的静态/匀速夹具改为完整物理快照、全实例无窗口后端接入主程序、
动态 TLAS/地形版本统一、曝光积分及电子/ADC 标定、完整雷达接收波形与功率模型、
真实 SAR/ISAR 聚焦、带所有权/同步的 GPU 跨进程产品。执行顺序与验收条件见
`docs/design_3d_sensor_rendering.md` 开头的实施状态表。

## 12. 世界真值与 GUI 环境调度接线（2026-11-06，第二阶段）

- `lib/simcore` 增加完整刚体状态、四元数插值和带 epoch/anchor/revision 的不可变
  `WorldSnapshot`。主程序按 Bullet 子步保存历史；测量时查询完整姿态和挂载杆臂速度，
  不再用帧末速度倒推。`env_viewer` 仍用解析夹具，接入世界快照尚待完成。
- 主程序场景体与平台控制器复用同一个碰撞体/可视变换，动态和规定轨迹模式显式选择。
  Bullet 资源归物理引擎所有；DBVT 宽相、单活动区浮动原点、旋转质心和索引网格修正。
  这些是主程序物理层改造，未向 `env_modules` 引入业务/Qt/spdlog 依赖。
- 设计软件和仿真软件共享持久化的 `frame_dt`、`physics_step`、`history_seconds`、
  `epoch_utc`；UTC 目前只接受整秒公历 Z 格式，不包含 TAI/闰秒/EOP。宿主把同一
  epoch 传给 SkyFeature，太阳、云、海按已有仿真时间入口推进。
- GUI 的 `SkyFeature` 默认启用现有大气 LUT，`CloudFeature` 默认体积云；云底变化
  实际更新 layer 几何并请求编译。`EnvFrame` 增加宿主原先漏传的降雨、实际云底、
  月光、消光值（结构本身未扩展）；`cloud_cover` 和 `cover` 支持同一语义。
- GUI 的 `OceanFeature` 在编译前调用 `setWindow`，把 `fftCommands()` 放到
  CommandGraph 中主 RenderGraph 之前的可开关节点，默认 FFT/曲率配置；此前只创建
  海洋模块，没有接入该 compute 调度。关闭海面同时关闭其 compute，卸载前等待
  GPU 空闲并移除节点。后续接离屏反射时也必须给出渲染前节点和生命周期契约。
- 观察口初始预览、运行和回放传递完整四元数，heading 从北顺时针。挂载偏移用米，
  不随网格缩放。旧正 Y yaw 的场景应检查朝向，显式孔径向量含义保持不变。
- 主程序调度保持跨帧 PRF；空雷达脉冲也推进 CPI，移除虚构地面命中、虚构扫描
  速度与重复速度相位。此处为旧 CPU 链纠错，不能代替新 `RadarChain` 或辐射标定。

验证记录：顶层全量构建通过；CTest 4/4、sky/cloud/ocean smoke 均通过。新增
`simulation_world_truth` 覆盖姿态/质心/挂载、浮动原点、外力保持、稳定地面接触、
刚体复用与释放、运动学历史、PRF、场景时钟、无窗口初始化与连续推进、空回波/
静止 Doppler 和波形缓存失效；该测试通过 ASan/UBSan（关闭 LeakSanitizer，
未宣称完成泄漏检查）。生产 raygen 编译通过，push constant 布局有静态断言。
当前机器无 DISPLAY 和 NVIDIA 设备，GUI 实际截图、FFT 海况视觉、硬件 RT
以及跨设备同步仍需目标机器验收。默认打开现有
物理效果并不意味着已达到商业海洋/天气引擎精度；海岸缺瓦片策略、FFT 与舰船采样
同源、反射闭环、动态地形和光谱标定仍在路线图中。

## 13. 共享测量编排与历史 CPU 求交（2026-11-06，第三阶段）

- `simcore::SensorRuntime` 为纯 C++ 的多实例测量事件编排层：有理时轴、曝光/CPI
  起止、独立帧号、稳定时间顺序和有限批次积压保留。环境 `SensorScheduler` 与
  主程序调度器均适配它，`env_viewer` 的噪声种子改用测量帧号。此类不创建 GPU
  设备、不提交 shader，也不持有产品资源；不能据此宣称完成无窗口 GPU 后端。
- 主程序不再按发射功率排除被动 VIS/IR，成像周期不再由 PRF 后节流推导；曝光
  起止均校验孔径历史。CPU 产品通过 `completedFrames()` 逐测量交付给
  `EquipmentDataSource`，同一步内多个实例、多次曝光不被最新帧缓存覆盖。
  Broker 拒绝的帧不录制；512 MiB 完成队列上限会显式报错，尚无持久化背压恢复。
- CPU `RayTracer` 的局部 BVH 配合每次测量的历史刚体姿态，删除无静态网格时
  查询 Bullet 当前状态的回退；支持迟到/删除对象、静态切运动。命中原始三角
  编号用于查询 `PhysParamManager`，修复跨对象局部面号冲突、动态参数串改。
  这是主程序参考链，未改 `SharedSceneResources` 的静态 GPU TLAS 为动态 TLAS。
- 旧原生 `RayTracingPipelineWrap` 明确声明缺少历史场景能力，主程序选择 CPU
  参考后端，避免消费占位 camera/sensor/TLAS 结果；查看器的新 GPU 链继续保留。
- CPU VIS/IR 投影使用孔径位置、视轴和上向量，输出正确时间戳/实例内帧号；
  局部层状大气修复高度取值、短程越界和被动双程问题。主程序 IR 温度/AGC、
  雷达接收及帧末滑动 RD 仍为待替换旧模型，不能称为已标定物理产品。
- 射线并行池修复晚唤醒线程访问已结束批次任务数组的竞态。批次函数在锁内
  领取任务副本，最后任务结束时先清空来源再唤醒调用方；新增短批次压力回归。

下一步仍是从 `SensorPipeline`/`RadarChain` 拆离 Window/显示，建立可离屏执行的
GPU 后端和实例输出所有权，再接动态场景/材质历史及完整世界快照。曝光运动积分、
地形缺瓦片策略、海面 FFT 采样同源、辐射/相干精度标定以及硬件视觉验收均未完成。

验证记录：全量构建通过，CTest 4/4、三套环境 smoke 通过。集成验证历史动态目标、
原始面材质与跨对象隔离、几何增删/权限切换、射线分批一致性、完整相机投影、噪声
重现、0.1 s 内两台被动相机 9 帧逐测量发布及短路径单/双程透射解析值。完整集成
连续 30 次通过，每次包含 300 个短并发批次；新增集成通过 ASan/UBSan，关闭
LeakSanitizer，未做 ThreadSanitizer 和无 DISPLAY/NVIDIA 条件下的视觉/硬件验收。

## 14. 无窗口 GPU 执行与动态场景（2026-11-06，第四阶段）

严格 GPU 初始化同时拒绝尚无 shader 实现的交叉耦合和无历史真值的云台扫描；
auto 模式对此报告原因并选择 CPU 参考。不得忽略已配置效果后宣称物理效果有效。
环境输入兼容场景设计器 `atmosphere` 的 cloud_cover/cloud_base/rain/visibility，
`environment` 中同名字段可覆盖；显式单位别名优先。

- `GpuContext` 创建无 surface/swapchain 的 Device，或接受宿主已有 Device；负责
  编译、动态数据上传、提交与完成 fence。`configureDevice` 在 GUI 创建设备前
  按支持情况启用 RT/AS/BDA 和雷达共享浮点原子特性。业务代码与 Window 不进入
  此执行契约，原 Window build/readback 接口只保留适配；共享设备仍由 ref_ptr 保活。
- `GpuSensorRuntime` 用共享 `simcore::SensorRuntime` 驱动所有实例，每个实例独占
  G-buffer 与成像/雷达产物。成像采样曝光中点，雷达在每个实际脉冲更新场景再执行
  scatter/echo。CPI 清零、逐脉冲写入和 FFT 后处理拆成独立命令图，不得每脉冲清掉
  已完成的回波行，也不得把同一份静态 CPI 场景反复标为不同脉冲。
- `SharedSceneResources::buildInstances` 接收局部网格和实例矩阵；硬件路径为每网格
  BLAS + `DynamicTlas`（ALLOW_UPDATE、持久 scratch、原位 Update），无硬件 RT 的
  设备执行 `gbuffer_compute.comp` 的局部 BVH 求交，产物布局一致。不同时间/视角
  不能共享已计算的 G-buffer。拓扑重建前清除编译任务/等待完成，保留实例测量时钟。
- 主程序的 `SimulationGpuAdapter` 属于宿主，绑定权威物理网格、面材质、孔径/刚体
  历史、显式地面与测量时刻环境。`Simulation.completedFrames` 逐测量交付现有
  EquipmentDataSource/Broker。产品持有主机数据，不声称 Vulkan 跨进程零拷贝。
- `simulation.sensor_backend=auto|cpu|gpu`；严格 gpu 不支持的传感器/初始化失败
  直接报错，auto 只在初始化允许 CPU 回退。运行后历史缺失或 GPU 故障不会用最新
  状态替换。旧 `RayTracingPipelineWrap` 仍不能承担历史测量，不重新启用占位链。
- IR Broker 接收 P0 积分辐亮度；VIS 为电子学输出；RADAR 为 RD，SAR 为未聚焦 IQ。
  不能把 SAR IQ 标记为已聚焦图像；激光飞行时间 shader 仍未实现。显式零光谱系数
  是物理值，负值才表示未指定；雷达发射功率以幅度平方根缩放，0 W 输出零散射。
- `EnvPhysicsService::update(EnvFrame)` 在无窗口情况下使用同一 Rocky 星历和大气
  光照函数，消费锚点/日期/测量时间/气象，避免默认太阳方向替代真实环境。

已验证：实际 llvmpipe Vulkan 执行完整生产成像链和雷达链；动态实例移出视野后
命中改变；同一步两相机 5+4 帧交付 Broker；逐脉冲目标移动后第一行非零、第二行
零回波；场景重建保留测量帧序，传感器门控不外发。全量/验证层记录在设计文档。

仍需完成：硬件 RT 实机、GUI 视觉/性能验收，历史拓扑/材质/天气版本，Rocky 瓦片
与物理网格及 FFT 海面变形统一，曝光运动积分，雷达面积/距离/接收机定量标定，
远距离相位精度、异步提交/读回和资源句柄产品。本阶段是主链集成，非工业级精度
认证；当前 compute BVH 和命中属性仍为 float，提交为串行 fence。

第四阶段最终验证：全量构建通过；SDK Vulkan 验证层下 CTest 5/5 与三套
环境 smoke 通过。GPU 主程序实际覆盖 VIS/IR/RADAR/SAR 原始 IQ 发布、非零 RD、
零功率 IQ、P0/P2/P4 标签、共享设备及垂直俯视。世界真值/内核测试通过
ASan/UBSan（detect_leaks=0），录制回归覆盖 v2 级别保留和 v1/seek 兼容。
测试为 llvmpipe 软件 Vulkan；未完成硬件 RT、GUI 视觉和实时性能验收。

## 15. 地球基座 P0/P1 与环境同源修复（2026-11-06）

本节覆盖此前“已生成 LUT/已同源”等过宽描述；以实际执行路径和测试为准。

- 地球导航使用当地切平面和真北屏幕方向，修复俯视视线与 up 平行；随距离调整
  裁剪面。云/海局部效果的 `view_proj * model` 在 CPU double 中预合成，再传
  float UBO（model 为单位矩阵），避免 GPU 上两次 ECEF float 变换相消抖动。
- 天空实际使用 `sky_dome_lut.frag` 和三个有效 LUT 描述符，三维表的 ImageView
  类型、分离顶点数组布局已修正。CPU 表有界共享，各视图 GPU 图像独立；仍是
  约化三维散射模型，不能称为完整四维散射或已完成统一 HDR。
- 大气路径用 WGS84 椭球高，太阳与法线在相同 ECEF 坐标中运算。GUI 与离屏
  共用谱天空函数，云雨/能见度修正不再依赖窗口栈是否存在；不再用 UTC 小时
  判断当地昼夜。太阳方位统一局部北(-Z)起算，顺时针。
- `CloudField` 与生产体积 shader 共用确定性连续密度、层高整形和 m/s 风漂移；
  CPU 云影沿太阳路径积分，包含体积云。显示仍受积分距离/步数预算限制；
  共享云影纹理、完整辐射标定和天气历史快照尚未完成。
- FFT 海面与 CPU 查询共用 h0、色散、相位及 IFFT。修复 h0 RG32F 格式、
  FFT A/B 布局屏障、逐 compute shader 编译、归一化/checkerboard、法线及
  雅可比行列式。无 Window 可生成并查询同一波场；曲率使用固定锚点近域球面，
  不随观察相机改变。旧谐波分支通过独立 set1/binding2 UBO 传完整参数。
- `OceanParams.fft_patch_size`（尾部追加，米，默认3200）独立于显示覆盖 size；
  CPU/GPU 共用同一周期长度。结构参数变更在宿主等待设备空闲后重挂海面。
  图形/FFT 初始化失败抛出明确错误，不再打印假成功或静默更换波形模型。
- `sensor_vis.frag` 和 `band_shade.comp` 的体积密度已统一到 CloudField：
  相同整数噪声、600m尺度、五级频率、m/s平流和层形。生产shader真实UBO
  各5组4096点 GPU 对照，最大误差2.15e-6（含远距离不改变密度）。这不代表
  多层云、二维云片显示和各传感器辐射积分已完成统一标定。
- 海岸掩膜按地表 revision、海平面和位置失效；NoData 保留 Unknown 并有界
  重试，不再当作深水。宿主优先 O(1) 局部 snapshot 查询，避免反复坐标变换。
- 宿主新增 `EarthDataService`（位于业务 core，不进入环境模块依赖）：离线
  GeoTIFF/TMS/旧高度图经有界 ROI 预加载为不可变网格/有效掩膜。GUI 拾取、海岸、
  Bullet 和 CPU/GPU RT 消费相同地表快照；Rocky 显示同一数据包的图层。
  可视 LOD 与物理/测量网格允许不同采样，需按数据分辨率验收误差。
- 地表配置包含 map、coverage、grid_size、strict、垂向基准及来源假设。
  revision 根据有效来源、材料、几何与掩膜生成，等价配置跨进程一致；未知
  地表不伪造零米平面。显式旧无地表的 core 夹具保留本地参考平面语义。
- 项目默认离线 manifest 位于 `config/earth/default.map.json`；数据根由
  `VSGRAYSIM_EARTH_DATA_ROOT` 指定。默认全球数据基准未核实，明确标为概略参考；
  GUI 兼容现有本机数据目录，core 不猜机器路径。高精度作业需绑定已声明数据包。
- 场景交付保留地球配置，地形导出按完整 WGS84/heading 重采样，缺数带掩膜、
  包内路径相对化。编辑成果按内容 hash 保存为独立包，避免覆盖旧场景的地形。
  显控 earth 面板复用共同视口，通过 Broker 心跳同步锚点、来源、时钟和版本。
- 原始 TMS TIFF 的配准由显式 global-geodetic profile 和 tile key 提供，
  取样按 Rocky 的 width−1 网格端点约定；裸 GDAL 源仍必须有配准和 CRS。
  strict 下未声明垂向基准且未显式允许参考假设会失败。实际默认数据已验
  4225有效节点/8192三角；原始约2.46km采样，不能以加密网格冒充更高源精度。
- 自定义传感器相机/太阳参数改用动态 UBO，避免 VSG 自动矩阵 push 覆盖；
  只读贴图表显式 readonly，暖身pass不再读取尚未生成的图像。GUI 与 viewer
  共用 Rocky 所需设备能力检查。GUI view 安装并注册 ViewDependentStateEx，
  地形分页只由首次真实视图编译之后的 viewer update 驱动；禁止预先创建
  不含 View/VDS/renderpass 的 device-only 分页编译上下文。
- 修复视觉验收缺图/失败仍通过；清理旧截图，要求完整矩阵，默认预热64帧。
  固定双视口下只统计左观察口，冻结日期推进、指定海域与ENU测试机位，
  Vulkan validation 错误直接使矩阵失败。
- `EnvPhysicsService::update` 仅刷新环境元数据；完整 `answer` 首次按需精确
  采样原有16点海洋白沫，并在同一 update 批次/时刻内供并发查询复用。
  `answerAtmosphere` 只提供大气/光照，省略海洋扩展字段；viewer 每帧大气
  查询走此入口，避免无海洋消费者时触发512² CPU IFFT。bind/update/栈变更
  须在查询批次之间执行；未改变 FFT 分辨率、采样位置、时刻或 EnvApi 布局。

独立验证：生产云密度4096点 CPU/GPU 最大误差3.82e-6；FFT N32两时刻的
位移/法线/J 最大误差1.33e-6，SDK Vulkan 验证层未报错。Earth真实GPU产品对照
260K/320K地表辐亮度有预期变化；统一构建与全套结果记录在最终日志。

性能边界：512² CPU 新时刻 IFFT 首次约183ms，同时间缓存10万点约18.6ms
（本机独立O3测量）。后续实时物理应采用固定 tick 共享快照、异步预计算或 GPU
批量采样；不可把 GUI 模块直接跨线程接舰船每个子步。本轮未完成全球流式地形、
动态地形历史、舰船浮力、动态海面 TLAS、真实场景反射、完整 HDR/云影合成与
硬件 RT/目标帧率验收。

## 本地体素环境的当前路径（2026-11）

- `rocky_env/VoxelWeather.h` 与 `env_modules/src/VoxelWeather.cpp` 持有整球海洋、
  离散三维云体及同源 CPU 云密度/透射率查询。云、海使用同一
  `VoxelWeatherFrame` 的仿真时间、风、太阳和月亮方向；原始海岸/月球数据及
  再生成脚本位于 `env_modules/data`。`lib/voxel_earth` 只通过兼容别名接入环境。
- `planet_cloud.frag` 沿 WGS84 云壳作体积积分，厚度决定 Beer–Lambert 消光，
  太阳/月亮光沿体积方向积分遮蔽；地球、海和局部地表的云影共用同一三维场。
  光线单位方向与相机视深的换算是轨道云层深度测试的必要条件，图像验收
  `--cloud-volume-capture` 覆盖云下、云内、云上、高空及轨道五种机位。
- 天空大气 LUT 的高度坐标为 0–100 km 平方映射；轨道视角取大气入射点，
  不把航天器高度直接截成地表高度。三套软件共用视口配置路径。
- 当前传感器的 `EnvPhysicsService` 只接收云量、云底和仰角，尚缺每条测量
  光线的 ECEF 起点/方向/路径长度；它的标量云消光不能称为与体素云逐射线
  一致。下一阶段需扩展请求契约并按冻结的 `VoxelWeatherFrame` 采样云透射率。
- 全球天气仍缺少观测气象驱动；云团动力学缺少 GPU compute、压力投影和
  时空重投影。海洋近场 FFT 与全球海面仍待统一光学与几何验证。不要把
  这些阶段性功能描述为已达到工业级物理精度。
- CPU 云透射率先裁剪 WGS84 内外云壳，再对有效交段积分；长距离轨道
  射线不会因固定全程采样步数而跳过薄云。近场 FFT 海面绘制节点与其
  预渲染计算同开同关，轨道视角不读取未初始化的位移/白沫图像。
- 2026-11-14：积云默认几何厚度改为 2.4 km；96³ 周期细节纹理预烘焙
  三维占据率，使 GPU mip 平均云水含量而不再平均原始噪声后重新阈值化。
  着色器采用高度可变云顶、近远分段体积步进、地表遮挡裁剪和低阶散射
  补光。`--cloud-descent-capture PREFIX PRESET` 在同一时间和天气下生成
  太空至地表的 97 帧连续轨迹、13 张关键帧；三套 PRESET 已各自通过。
  当前近景云侧面仍偏平滑，云内漫射仍是近似值。一次离散球状云团尝试
  产生规则圆点云，已撤回；后续应以观测天气、分层云型、适应性细节和
  可验证的多次散射改进，而不是把周期球团当成最终体素云。
- 2026-11-14 后续修正：默认积云包络扩为 4.2 km；新增
  `SparseCloudVolume`，以 32 m 体素和 8³ 活动砖保存液态水含量（kg/m³）、
  水汽混合比（kg/kg）、温度扰动及三维速度，执行平流、浮力、潜热、
  凝结/蒸发、水负载及涡量约束。随后引入
  `VoxelCloudCluster`：每团云有独立 ID、热力学初态、生命周期和稀疏砖集合；
  多个云团只在 GPU 上传阶段合并到共享的 40×20×40 页表及 R16 图集。页表 0
  在局部权威域内表示晴空，不再回退为程序化云壳。全球 LOD 同样由天气湿度
  与 CAPE 触发的离散粗体素云团构成，1024×512×40 粗场使用无规则点过程，
  不再暴露经纬播种网格。局部初态由独立上升热泡、侧向子涡和干空气卷吸泡
  直接写入状态体素，不在 shader 中求程序化云壳。地球云、海面、地形
  云影和 CPU 透射率共享相同密度版本。后台工作集准备与帧边界提交已接入；
  当前云团演化/上传仍为 CPU 10 Hz，下一阶段必须迁移为 Vulkan compute
  ping-pong、压力投影和气象同化。R16 图集的 1.0 对应 0.003 kg/m³ 液态水，
  可见光消光由液态水、12 µm 有效滴半径和 Qext≈2 推导。旧 160³ 周期纹理
  不再参与云形生成。
- 2026-11-14 GPU 单云实验场：新增公开的 `GpuVoxelCloud`，固定 ENU 三维域内以
  RGBA32F 保存水汽混合比、液态水、温度扰动、雨水和三维速度。每个固定步在
  Vulkan compute 中依次执行半拉格朗日平流、凝结/蒸发、潜热、浮力、水负载、
  风松弛、涡量约束、散度、Jacobi 压力求解和无散投影；输出总是回到固定纹理槽，
  便于显示、传感器和碰撞共享版本。外物以最多 8 个球形浸入边界输入位置、半径、
  速度和耦合强度，内部排开水物质并向尾迹注入动量和旋转，不是 fragment 遮罩。
  初始化布局转换与重复计算命令分离，禁止每帧重复 UNDEFINED→GENERAL。
  RTX 无窗口回读夹具在 32×24×32、30 m 体素上验证了凝结、有限速度、飞机穿云
  后的液态水变化和尾流速度变化。该类目前是单云动力学实验场，尚未替换产品默认
  CPU 稀疏砖，也尚未接入现有体积光学 ray-march、传感器逐射线采样、真实机体
  SDF/网格占据和异步计算队列；不得把本阶段描述为完整产品集成。

## 2026-11-18 场景显示细化

- 普通影像/高程地图与原生地球包复用同一个 PlanetWeather 体素场。
- 中景 CPU 与 GPU 共同按实际凝结水插值。空体素允许保持清空，域边缘连续混合到宏场。
- 云色通道按物理距离分配近、远采样。直射、自阴影、天空漫射分开计算，深度取第一段可见凝结水。
- 天空 LUT 按相对太阳方位分块。CPU/GPU 使用相同角度参数，按实际 Rayleigh 和 Mie 相函数采样。
- 可用地球程序 --scene-detail-capture 观察实测 DEM、影像、云和太阳方位，验收入口见场景设计指南。

### 远云过渡

- 中景 atlas 的水平定位仍使用其驻留地理图表。垂直采样改用与宏云壳体一致的椭球径向高度，
  CPU 和 GPU 使用相同的高度边缘衰减。近场求解器、体素存储和描述符布局沿用原实现。
- 云色通道对宽射线段执行连续垂直过滤。步长随离观察者的距离增长，
  射线终点的变化不再重新分布整条射线的采样点。
- 远云按密度积分加权估计可见距离，并按到观察者的空气柱衰减对比度，与实际天空合成。

### 低仰角云线与固定飞行验收

- 原生云色通道使用双精度侧上传的球壳常量及稳定根公式。射线绕过内层清空区时分别积分
  前后两个云区，只在遇到实体地球时截断。距离采样和光学前缘在各段连续。
- 稀疏图集的高度权重同时约束径向与页边缘渐变。CPU 和 GPU 的页边界均采用半体素偏移。
  局部驻留域仍采用原有布局，观察者平移不再每 384 m 重置局部云团。
- 地球测试程序通过 --scene-flight-capture 用固定时钟保存近地到高空的每帧画面。

### 地理驻留云团与薄云照明

- 地球视口向天气帧传入现有地理图表的锚点。同一区域的中景图集、稀疏页表、局部 GPU
  液态水场以该图表定位。天气配置或地理区域变化时异步发布完整的新驻留版本。
- 云颜色的射线在稀疏域内按体素尺寸限制步长，区域和全球图集在驻留边界羽化。
  CPU 点查询和共享 GPU 消光路径使用同一驻留权重。CPU 点查询不读取 GPU 动力学纹理。
- 热泡沿连续路径叠合，云底凝结平滑增长，卷入干空气的细节写入已有液态水／水汽体素。
  颜色通道从同一密度场积分太阳光程，并探测上方云水对天空散射的遮蔽。
  稀疏域每段就地估计受光，避免相机移动时的缓存光照条带。
- 地球程序 --scene-cloud-pass-capture 固定时钟和相机轨迹，导出原生帧、稀疏截面、
  云边穿越和 GPU 场版本。航线说明见场景设计指南。

### 云丝与稀疏页边缘

- 外侧热泡沿风向释放弯曲的低密度凝结水云丝。片段在原有体素格中存储水汽、液态水、
  温度和速度，尾段逐渐变细和减淡。局部水汽按求解器的饱和公式初始化，随后参与平流与蒸发。
- 采样跨到空页时，从相邻驻留页的一格填充中重建整个三线性足迹。CPU、可见云、
  云影和传感器消光使用同一方式，移除按缺页位置抑制三个体素的衰减。
- 稀疏推进优先保留已驻留页，然后使用剩余预算扩展邻页。云侧航线在固定截面统计
  稀薄云水柱的面积和首个动力学更新后仍有水的列。

### 近景云絮与带风验收

- 近场热泡的含水量保留不同尺度的干空气间隙。云色通道在贴近处缩短积分步长，
  用太阳和上方的实际光学厚度估计周围凝结水提供的多次散射。
- 稀疏步进从原有六邻格计算中心差分旋度和液态水的局部曲率。在回溯位移不超过一格时，
  以采样值和上风邻格限制液态水校正的幅度，减少云边随步进变软。空格在背景风下参与回溯。
- --scene-cloud-wind-capture 沿原航线推进仿真。靠近薄边时固定相机 16 帧，
  并记录帧的天气时间、云图表、GPU 版本和固定姿态下的亮度变化。

### 移动薄边的穿越拍摄

- 地球测试程序的 --scene-cloud-edge-capture 从最近提交的稀疏场测量连续薄边，
  按归一化水柱积分选段，使用原带风航线的时间序列。每帧推进后重新测量相机位置的
  云水，验收要求清空、进入薄云和离开三个阶段。GPU 覆盖域外的采样可由 CPU 直接验证。
- 近场云色漫射增加朝向亮侧天空的斜向探测，以同一体素消光计算透过率并与天顶探测加权。
  斜向探测随距离连续减弱，保留薄边受光与较厚云体遮蔽的层次。

### 体素格线和区域云

- 云团初始化先估计完整凝结水支撑所需的砖页。超预算时把热泡、薄边和干空气口袋
  一同缩放至可驻留范围，常规场景保留完整渐隐轮廓。
- CPU 稀疏查询和 GPU 共用中心二次 B 样条重建光学密度。驻留页使用既有一格填充；
  空页边界从相邻驻留页累加有效支撑。GPU 动力学使用原网格，可见光学查询对其液态水同样重建。
- 地球测试程序提供 white、storm 和 sunset 区域快照。云量及厚度进入同一体素生成和消光路径；
  彩霞使用实际低太阳高度与大气光照。

### 多云区域预设

- 地球视口的 `dense_regional_clouds` 让既有中景图集以 500 m 间距覆盖 80 km 区域，
  在湿度较高处写入多组积云和层积云，并保留云隙。近景稀疏体素和局部 GPU 动力学沿用原布局。
- 地球测试程序的 `many` 预设保存区域俯视、低空云海、云间视角和同机位的清晰背景参照。

### 单云对象模式

- 地球视口参数 `cloud_objects` 选择有限区域的参数化云场。每个对象用中心、三轴尺度、
  形状种子和归一化液态水描述连续密度。各对象按各自的 80–230 秒周期生成和消散，
  随风平移并逐渐生长。云影和共享 GPU 传感器使用同一密度函数。
- 水平分箱只保存对象索引。渲染射线跳过空箱，在候选云包围范围内积分；
  拥挤分箱完整回查对象，避免丢失重叠云。局部流体、尾流和微物理求解使用原体素模式。
- --scene-cloud-object-capture 在同机位录制两种模式，并在同步 GPU 完成后计量整帧耗时。
- 近景云絮和云丝由原对象的外缘密度、种子和出生后时长决定。小云絮和薄丝沿风向以不同速度
  平移，原对象继续决定整体轮廓和生命周期。近景细节随距离和采样覆盖范围衰减。
- --scene-cloud-object-wind-flight 在实际密度场选取薄云边，并可用 route.json 复拍相同航线。

### 昼夜天空与云光照

- EphemerisBridge 提供统一的地球自转角，天空 UBO 加观测经度得到本地恒星时。原生 LUT 与解析穹顶共用
  HYG 星图、银河背景、月晕和暮光星点显隐。星图由内置 J2000 星表生成，数据许可见 data/README.md。
- 行星云渲染在低太阳仰角根据云高计算地球遮挡和掠射消光，夜间按月相、月高和云内遮挡显示月光。
- 地球 GUI 的逐时快照推进完整仿真时钟。固定云形延时通过测试入口单独推进日期和光照，保留逐帧原始回读。
- 天空散射表在日光和视线接近地平线时分配更密集的角度样本。日月盘使用视直径、低空折射和色消光，
  通过地球 GUI 的 --scene-sky-disc-capture 在固定视场复核。
- 月面反照率和 LOLA 高程共用一张双通道纹理。月面直接光按日照坡度和近处地形遮挡计算，
  光晕采用大气消光控制的近盘散射和宽角散射。
- 原生天空、区域地形与行星云的太阳消光引用标准大气透射表。地形和云的显示包括有限视线路径的
  分波段消光与日月入散射。月光夜景增益属于显示标定，沿用独立的传感器辐照量。
- 地形的显示入散射按俯角连续压低亮度和蓝色饱和度，以保留高空地表色彩。低仰角恢复完整空气路径。
  --scene-atmosphere-light-capture 为高空和低空白昼机位分别报告 15 帧同步耗时。
- `overcast` 使用同一单云对象表示形成衔接层积云。云下天光及地面漫射光单独作阴天显示标定，
  云顶保留日照。--scene-overcast-capture 输出四个实拍机位与区域透光检查。
