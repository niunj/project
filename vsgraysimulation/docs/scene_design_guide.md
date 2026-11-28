# vsgraysimulation 场景设计指南

## 1. 场景设计原则

### 1.1 核心定位
本系统是**多光谱传感器灰度仿真平台**,主要用于:
- 军事装备的多光谱特性分析
- 传感器性能评估与测试
- 大气环境影响研究
- 传感器互扰现象验证

### 1.2 设计层次
```
场景设计层次结构:
├── 地形环境层 (Terrain/Environment)
│   ├── 基础地形 (Ground/Desert/Mountain)
│   ├── 大气环境 (Atmosphere Layers)
│   └── 天气效应 (Weather Effects)
├── 对象层 (Objects)
│   ├── 目标对象 (Tank/Missile/Vehicle)
│   ├── 背景对象 (Buildings/Terrain Features)
│   └── 动态参数 (Temperature/RCS Updates)
├── 传感器层 (Sensors)
│   ├── 主动传感器 (Laser/Radar/SAR)
│   ├── 被动传感器 (IR Camera/Visual)
│   └── 互扰配置 (Cross-Induction)
└── 任务层 (Mission)
    ├── 时序调度 (Scheduling)
    ├── 平台运动 (Platform Dynamics)
    └── 数据采集 (Data Collection)
```

## 2. 场景类型设计

### 2.1 单目标测试场景
**用途**: 传感器标定、物理参数验证

**配置要点**:
- 单一目标对象
- 简单地面网格
- 标准大气条件
- 少量传感器

**示例**: `tank_physics_demo.json`
- 目的: 验证坦克物理运动与多普勒效应
- 对象: M1A2 坦克
- 传感器: X波段雷达
- 环境: 标准大气, 地面 y=0

### 2.2 多目标战术场景
**用途**: 战术仿真、多目标检测

**配置要点**:
- 多个目标对象(3-5个)
- 地形要素(山地/城市)
- 复杂大气(云层/湍流)
- 多传感器协同

**示例场景**: "坦克编队侦察"
```
对象配置:
- tank_001: M1A2坦克 (主动, 物理驱动)
- tank_002: M1A2坦克 (主动, 物理驱动)
- agm_001: AGM-114导弹 (被动, 静态)
- terrain_001: 山地地形 (静态背景)

传感器配置:
- radar_01: X波段雷达 (tank_001平台)
- radar_02: X波段雷达 (tank_002平台)
- sar_01: C波段SAR (空中平台)
- ir_01: LWIR红外 (tank_001平台)
```

### 2.3 复杂环境测试场景
**用途**: 大气影响研究、环境适应性测试

**配置要点**:
- 多层大气配置
- 天气变化(云/雨/雾)
- 湍流与折射
- 背景杂波

**示例场景**: "沙漠环境雷达测试"
```
大气配置:
- 地面层: 沙尘气溶胶增强
- 云层: 2km高度增加云密度
- 湍流: 沙漠热对流

对象配置:
- tank_001: 坦克目标
- terrain_001: 沙漠地形(大范围)
- decoy_001: 雷达诱饵

传感器配置:
- radar_x: X波段雷达(测试杂波抑制)
- sar_c: C波段SAR(测试穿透性)
- ir_lwir: LWIR红外(测试热对比度)
```

### 2.4 传感器互扰研究场景
**用途**: 验证跨频段互扰模型

**配置要点**:
- 多频段传感器并发
- 高耦合因子配置
- 详细的互扰路由
- 分析模式运行

**示例**: `sensor_cross_induction.json` 基础上增强

## 3. 场景配置组件库

### 3.1 地形模板

> 三套软件通过场景的 `earth.map` 选择地球数据包。体素包使用同一份地表快照生成显示网格、碰撞面和传感器几何。启用 Rocky 兼容后端的构建也可以打开 Rocky 地图。场景的 WGS84 `geo_anchor` 定义作业坐标原点。附加城区或地面模型可放入 `objects[]` 并标记为 `"type": "terrain"`，用于指定模型的热学和散射材质。

#### 基础地面网格
```json
{
  "object_id": "ground_plane",
  "mesh_path": "models/ground_grid.gltf",
  "face_count": 2,
  "type": "terrain",
  "physics": {
    "mass": 0,
    "initial_position": [0.0, 0.0, 0.0]
  }
}
```

#### 沙漠地形
```json
{
  "object_id": "desert_terrain",
  "mesh_path": "models/Desert/Desert.obj",
  "face_count": 100000,
  "type": "terrain",
  "physics": { "mass": 0 },
  "face_groups": [
    {
      "group_id": "sand_surface",
      "face_range": [0, 100000],
      "vis_params": { "brdf_type": "sand", "albedo": 0.35 },
      "lwir_params": { "emissivity": 0.92, "base_temp": 323.15 },
      "radar_x_params": { "sigma0_db": -20.0, "surface_rough": 0.5 }
    }
  ]
}
```

#### 山地地形
```json
{
  "object_id": "mountain_terrain",
  "mesh_path": "models/Mountain/mountain.obj",
  "type": "terrain",
  "physics": { "mass": 0 },
  "face_groups": [
    {
      "group_id": "rock_surface",
      "face_range": [0, 50000],
      "vis_params": { "brdf_type": "rock", "albedo": 0.25 },
      "lwir_params": { "emissivity": 0.88 },
      "radar_x_params": { "sigma0_db": -15.0 }
    }
  ]
}
```

### 3.2 目标对象模板

#### M1A2 坦克
```json
{
  "object_id": "tank_001",
  "mesh_path": "models/M1A2/M1A2.obj",
  "face_count": 12560,
  "physics": {
    "mass": 61500,
    "initial_position": [0.0, 0.5, 0.0],
    "initial_velocity": [10.0, 0.0, 0.0],
    "friction": 0.8,
    "convex_hull": true
  },
  "face_groups": [
    {
      "group_id": "turret",
      "face_range": [0, 3000],
      "vis_params": { "brdf_type": "roughmetal", "albedo": 0.3, "roughness": 0.5 },
      "lwir_params": { "emissivity": 0.9, "base_temp": 340.15, "temp_tex_id": 1 },
      "radar_x_params": { "rcs_lut_id": 1, "dielectric_real": 18.0, "dielectric_imag": 5.0 }
    },
    {
      "group_id": "hull",
      "face_range": [3001, 9000],
      "vis_params": { "brdf_type": "diffuse", "albedo": 0.2 },
      "lwir_params": { "emissivity": 0.88, "base_temp": 320.15 }
    },
    {
      "group_id": "tracks",
      "face_range": [9001, 12560],
      "vis_params": { "brdf_type": "rubber", "albedo": 0.1 },
      "lwir_params": { "emissivity": 0.85, "base_temp": 330.15 }
    }
  ]
}
```

#### AGM-114 导弹
```json
{
  "object_id": "missile_001",
  "mesh_path": "models/AGM-114/AGM-114.obj",
  "face_count": 2400,
  "physics": {
    "mass": 45,
    "initial_position": [50.0, 100.0, 0.0],
    "initial_velocity": [0.0, 0.0, 200.0]
  },
  "face_groups": [
    {
      "group_id": "body",
      "face_range": [0, 2000],
      "vis_params": { "brdf_type": "metal", "albedo": 0.6 },
      "lwir_params": { "emissivity": 0.2, "base_temp": 290.15 }
    },
    {
      "group_id": "exhaust",
      "face_range": [2001, 2400],
      "lwir_params": { "emissivity": 0.95, "base_temp": 800.15 }
    }
  ]
}
```

### 3.3 传感器模板

#### X波段雷达(地面平台)
```json
{
  "sensor_id": 1,
  "type": "radar",
  "band": "X",
  "platform_object": "tank_001",
  "tx_params": {
    "power": 5000.0,
    "beam_width": 0.05,
    "wavelength": 0.03,
    "pulse_rate": 3000.0,
    "duty_cycle": 0.05
  },
  "aperture": {
    "position_offset": [0.0, 2.0, 1.0],
    "direction": [0.0, 0.0, 1.0],
    "fov_horizontal": 60.0,
    "fov_vertical": 30.0
  }
}
```

#### C波段SAR(空中平台)
```json
{
  "sensor_id": 2,
  "type": "sar",
  "band": "C",
  "platform_type": "airborne",
  "tx_params": {
    "power": 10000.0,
    "wavelength": 0.055,
    "pulse_rate": 1500.0,
    "duty_cycle": 0.08
  },
  "aperture": {
    "position": [0.0, 5000.0, 0.0],
    "direction": [1.0, -0.5, 0.0],
    "swath_width": 10000.0
  }
}
```

#### LWIR红外相机
```json
{
  "sensor_id": 3,
  "type": "ir",
  "band": "LWIR",
  "tx_params": {
    "wavelength": 10.0e-6
  },
  "aperture": {
    "position": [100.0, 2.0, 50.0],
    "direction": [-1.0, 0.0, -0.5],
    "fov_horizontal": 40.0,
    "fov_vertical": 30.0
  }
}
```

### 3.4 大气环境模板

原生体素联合场景的统一 UTC、天气时间线、雨雪与保存/重开入口见 [场景时间与区域天气](scene_time_weather.md)。可直接打开 `config/topics/taipei-weather.vtopic.json`；它使用台北新数据。

#### 标准大气
```json
{
  "layers": [
    {
      "height_min": 0, "height_max": 11000,
      "temp": 288.15, "pressure": 101325.0,
      "humidity": 0.5, "aerosol": 1.0e-5,
      "cloud_density": 0.0
    }
  ]
}
```

#### 沙尘环境
```json
{
  "layers": [
    {
      "height_min": 0, "height_max": 5000,
      "aerosol": 5.0e-4,
      "ext_coeff": [2.0e-3, 5.0e-3, 1.0e-4, 1.0e-4],
      "visibility_km": 5.0
    }
  ]
}
```

#### 云层环境
```json
{
  "layers": [
    {
      "height_min": 2000, "height_max": 4000,
      "cloud_density": 0.5,
      "ext_coeff": [1.0e-2, 2.0e-2, 5.0e-4, 5.0e-4]
    }
  ]
}
```

## 4. 场景设计工作流

### 4.1 设计流程
```
1. 确定场景目标
   ├── 仿真目的(测试/训练/研究)
   ├── 关键指标(检测概率/定位精度/互扰抑制)
   └── 约束条件(计算资源/时间限制)

2. 选择场景类型
   ├── 单目标测试 → 简单配置
   ├── 多目标战术 → 复杂对象配置
   ├── 环境测试 → 强化大气配置
   └── 互扰研究 → 强化传感器配置

3. 配置地形环境
   ├── 选择地形模板
   ├── 配置大气分层
   ├── 设置天气参数
   └── 调整湍流/折射

4. 配置目标对象
   ├── 选择对象模型
   ├── 配置物理参数
   ├── 设置面组属性
   └── 规划运动轨迹

5. 配置传感器系统
   ├── 选择传感器类型
   ├── 配置发射参数
   ├── 设置孔径位置
   └── 配置互扰路由

6. 验证与调试
   ├── 运行验证模式
   ├── 检查参数健全性
   ├── 单传感器追踪
   └── 小帧数测试

7. 正式运行
   ├── 选择运行模式
   ├── 设置日志级别
   ├── 监控运行状态
   └── 收集结果数据
```

### 4.2 配置文件组织
```
config/
├── scenarios/
│   ├── tank_platoon_recon.json        # 坦克编队侦察场景
│   ├── missile_tracking_test.json     # 导弹跟踪测试场景
│   ├── desert_radar_test.json         # 沙漠雷达测试场景
│   └── urban_multi_sensor.json        # 城市多传感器场景
├── templates/
│   ├── terrain/
│   │   ├── ground_plane.json
│   │   ├── desert.json
│   │   └── mountain.json
│   ├── objects/
│   │   ├── m1a2_tank.json
│   │   ├── agm114_missile.json
│   │   └── civilian_vehicle.json
│   ├── sensors/
│   │   ├── radar_x_band.json
│   │   ├── sar_c_band.json
│   │   └── ir_lwir.json
│   └── atmosphere/
│       ├── standard.json
│       ├── dusty.json
│       └── cloudy.json
└── active/
    ├── object_config.json
    ├── sensor_config.json
    └── atmosphere_config.json
```

## 5. 高级场景设计技巧

### 5.1 动态温度更新
为对象配置 `temp_update_rate_hz` 实现引擎温度动态变化:
```json
{
  "temp_update_rate": 10,
  "face_groups": [{
    "group_id": "engine",
    "lwir_params": {
      "base_temp": 380.15,
      "temp_tex_id": 1
    }
  }]
}
```

### 5.2 传感器平台挂载
使用物理引擎的真实平台速度:
```cpp
sim.mountSensorOnObject(sensor_id, object_id,
                         local_offset, local_direction);
```

### 5.3 多普勒效应验证
配置平台初速度观察多普勒频移:
```json
{
  "physics": {
    "initial_velocity": [15.0, 0.0, 0.0]  // 54 km/h
  }
}
```

### 5.4 传感器互扰路由
设计复杂的跨频段互扰场景:
```json
{
  "rx_cross_sensors": [
    {
      "sensor_id": 2,
      "receive_range": 0.15,
      "coupling_factor": 0.05,
      "band_match": 0.8,
      "is_process": true
    }
  ]
}
```

### 5.5 大气动态更新
运行时更新大气湍流/云密度:
```cpp
atmosphere->updateDynamic(layer_id, turbulence, aerosol, cloud_density);
```

## 6. 场景设计检查清单

### 6.1 基础检查
- [ ] 场景目标明确
- [ ] 地形配置完整
- [ ] 对象模型路径正确
- [ ] 物理参数合理
- [ ] 传感器配置有效

### 6.2 参数验证
- [ ] 对象质量 > 0 (动态对象)
- [ ] 传感器功率 > 0 (主动传感器)
- [ ] 面组范围 ≤ face_count
- [ ] 大气高度层连续
- [ ] 互扰路由有效

### 6.3 运行验证
- [ ] `--mode validate` 通过
- [ ] `--mode list-sensors` 输出正确
- [ ] `--mode trace-sensor` 单传感器追踪正常
- [ ] `--mode headless --frames 10` 无错误

### 6.4 性能优化
- [ ] 面组划分合理(避免过多小组)
- [ ] 传感器数量适中(建议 ≤ 10)
- [ ] 帧数控制(测试场景 ≤ 100帧)
- [ ] 日志级别适当(info/warn)

## 7. 常见场景设计案例

### 7.1 案例一: 坦克热特征测试
**目标**: 测试坦克在不同环境温度下的红外特征

**配置**:
- 对象: M1A2 坦克
- 传感器: LWIR 红外相机
- 环境: 多种大气温度配置
- 方法: 对比分析

### 7.2 案例二: 雷达多目标跟踪
**目标**: 验证X波段雷达的多目标分辨能力

**配置**:
- 对象: 3辆坦克(不同距离)
- 传感器: X波段雷达
- 环境: 标准大气
- 方法: 运动轨迹分析

### 7.3 案例三: SAR成像质量评估
**目标**: 评估C波段SAR在云层环境下的成像质量

**配置**:
- 对象: 坦克编队
- 传感器: C波段SAR
- 环境: 云层大气(2-4km)
- 方法: 图像对比度分析

### 7.4 案例四: 激光-红外互扰测试
**目标**: 验证激光雷达对红外成像的干扰模型

**配置**:
- 对象: 静态坦克
- 传感器: 激光雷达 + LWIR红外
- 环境: 标准大气
- 方法: 互扰功率测量

---

**文档版本**: v1.0
**更新日期**: 2026-07-31
**维护者**: vsgraysimulation 开发团队
## 共用地球基座与体素地形

在场景设计软件的「文件 → 加载地球地形包」中选择 `config/earth/voxel-terraces.map.json`，或直接打开配套的 `config/earth/voxel-terraces.scene.json`。保存后的场景可交给仿真主控；显控工作台从主控接收地理锚点、地形包引用和版本。跨进程使用时，各进程需要能够读取同一份地形包。

场景使用 `earth.map` 引用地形包，相对路径以场景文件为基准。可用 `earth.backend` 显式选择 `rocky` 或 `voxel`。省略时按数据格式识别，旧 DEM、TMS 和地形工程仍按原方式加载。显式选择与包格式冲突会导致加载失败。Rocky 保留全球影像和分级地形，体素模式显示锚点处的局部体块。

体素包格式为 `voxel-terrain-v1`。`origin_m` 指定体素格最小角的局部米制坐标，`cell_size_m` 是立方体边长，`dimensions` 按 X、Y、Z 给出格数。`occupied` 列出实心格的零基整数坐标，未列出的格为空气。局部坐标 Y 向上，锚点航向为零时 X 向东、Z 向南。相邻实心格的内部面剔除，悬空块的底面保留。高度查询返回该列最高表面；拾取、碰撞和传感器使用外露三角面。严格模式要求每个水平列至少有一个实心格。

首版按场景整块预载。每轴上限为 256 格，总格数上限为 1,048,576，实心格上限为 32,768。更改包后重新装订场景会生成对应版本，已发布的快照保持有效。示例为合成台地；标记 `provenance_assumed` 可让工具注明参考数据。

### 全球分块 DEM 与影像

在场景设计或仿真主控打开 `config/earth/global-voxel.scene.json`。示例直接读取部署在 `VSGRAYSIM_EARTH_DATA_ROOT` 下的 `worldHeight` 与 `worldTexture_tiff`。批处理和显控进程也需要能读取地形包和对应的数据目录。可以把各端的数据根设成不同路径，传递的地形包路径须在各端可解析。

`voxel-geodetic-v1` 使用 WGS84 经纬度，两列一行根瓦片；瓦片键为 `[lod,x,y]`，行从北向南。`{y}` 可接 XYZ 行，`{-y}` 可接 TMS 行。经度在日期变更线回绕，极区按有效纬度截断。包分别声明高程和 8 位 RGB 图层的本地 URI 模板及最大可用级别。缺少子瓦片时读取可用父瓦片的对应窗口。缺少整个来源、遇到 NoData 或读到中途改写的文件会使候选版本加载失败。

中心瓦片与周围一圈组成一次提交的工作集，通常有 9 块，极区为 6 块。示例中心每块 12 × 12 个高程柱，外围每块 6 × 6 个，中心和外围之间共用边界节点。高程按 5 米量化，立面、顶面和跨块接缝都进入碰撞及测量网格。影像通过 UV 贴到同一批顶点。源栅格间距、网格间距和垂向量化步长是三个独立量。提高几何 LOD 只能细化对原始数据的采样。卫星 RGB 用于可见外观，物理材质和热参数读取地形包的材质声明。本地示例源未注明垂直基准，已显式标记为参考数据。

设计器在后台按相机目标和视距加载、编译瓦片。仿真可以用 `earth.follow_body` 指定刚体，省略时跟随第一个可运动刚体，显式空串固定工作区。主控按平台高度、速度、其他刚体和传感器视场选级。当当前位置已有覆盖时，使用 `earth.prefetch_seconds`（默认 0.25 个仿真秒）预测下一个位置与 LOD。每个仿真实例最多有一个在途候选，转向后过期的结果会被丢弃。

工作线程从不可变地表生成显示网格与影像、Bullet 三角网格和 BVH、测量用 GPU 场景。在主控连接视口时也预编译其绘制节点。仿真步开始时检查实际需要的瓦片键、活动刚体、传感器覆盖、几何与材质版本。候选就绪后安装碰撞体、交换 GPU 场景并发布地表版本与生效时刻。准备期间继续使用旧版本，严格覆盖检查对每个物理步和测量仍然生效。旧设备资源在后续后台任务或停机时释放。手工调用 `requestEarthRegion` 可在后续步边界提交，显式同步接口 `selectEarthRegion` 供停步选区和离线工具使用。

主控视口在绘制前接收同一地表与仿真时刻。独立显控进程按广播的瓦片键后台装载和编译，并校验地表版本；可见视口与产品版本匹配后才展示测量。跨版本的曝光被跳过，传感器时钟保持连续。录制格式 v3 保存地表版本，播放器兼容 v1、v2。海面按新地表刷新岸线和代表水深，水深参与波浪色散；风与海况决定的 FFT 初始振幅谱可以跨地形版本复用。

局部物理坐标系保持场景锚点。单一场景的活动窗口要求其地表法线与锚点法线的夹角小于约 26°。可在全球不同地点建立场景；跨洲连续运动仍需作业坐标系重定位。DEM 瓦片描述量化地表柱；带悬洞的占据数据可使用局部 `voxel-terrain-v1` 包或下文的分块体素包。

在仓库根目录以 Release 构建复测。第一条命令统计显式同步 CPU 切换。后两条使用同一条往返、连续升降的压力轨迹，统计真正发生提交的 100 个仿真步。每步为 20 ms 仿真时间，挂载的 64 × 48 MWIR 每步输出一帧。原生窗口测试需可用的 X11 显示，运行 960 × 640 地球视口、天空、云和 FFT 海洋，同时检查共享设备上的绘制提交与完成栅栏。

```bash
export VSGRAYSIM_EARTH_DATA_ROOT=/home/mte/EarthData/World.earth.tms_repo
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json
build/source-release/tests/sim_earth_surface_tests --tile-benchmark
build/source-release/tests/sim_earth_surface_tests --tile-gpu-benchmark
QT_QPA_PLATFORM=xcb build/source-release/gui/sim_earth_gui_tests --native-stream-benchmark
```

GPU 基准按最近秩计算 P95，超过 33 ms 返回失败。每步检查曝光时间、帧号、地表与物理版本。每次切换另在平台下方比对几何采样与 Bullet 碰撞高程，容差 2 cm。瓦片解码、桌面合成器和存储缓存状态会影响复测结果。台北样例高程源南北间距约 2.46 km，影像约 9.78 km，计时采用本机离线数据。

2026-11-12，RTX 4060 Laptop、驱动 580.178.04、i7-13650HX，Release 构建。原生窗口每步串行执行仿真、红外测量与回读、视口绘制和 present，并等待绘制完成栅栏。计时包括原有视口限帧等待。桌面合成与屏幕扫描输出在计时范围之外。

| 负载 | 切换样本 | 切换 P50 | 切换 P95 | 最慢切换 | 超过 33 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 无窗口仿真与测量 | 100 | 14.13 ms | 17.81 ms | 24.00 ms | 0 |
| 960 × 640 原生地球视口与同设备测量 | 100 | 17.59 ms | 22.07 ms | 27.90 ms | 0 |

原生窗口共推进 1,558 步，收到 1,558 帧连续的 MWIR 产品。55 次跨 LOD 的 P95 为 21.98 ms，45 次同级移动的 P95 为 22.73 ms。已提交候选的后台准备 P95 为 17.22 ms。100 个切换点的 Bullet 与地表采样最大高程差为 3.34 mm。该差值衡量引擎内部的一致性，原始 DEM 的测绘精度另由数据源决定。


### 独立体素地球

可在三套软件间共享 `voxel-geodetic-volume-v1` 工程。打开 `config/earth/native-volume.scene.json` 可观察合成的岩棚，场景中同一经纬度上有地面、空腔和顶板。地图仍按全球经纬度瓦片寻址。区域包的覆盖由实际发布的瓦片和可回退的父级决定。

原生瓦片以 `tile` 指明键，`side` 指明方形网格边长。`columns` 自北向南按行记录每列的剖面索引，行内从西向东。`profiles[].runs` 记录互相分离的有符号整数区间，每段包含下端点，上端点属于上方格。两端乘地图的 `vertical_step_m` 得到垂直基准下的高度米数。空区间列表表示整列为空。`profiles[].rgb` 提供基本颜色，瓦片还可以携带独立分辨率的 RGB 影像。选择较低工作区分辨率时，取格心位置的完整剖面。窄于格距的结构可能在远景消失。

查看地形高程时取列顶；查询指定高度的实体和空气时使用 `EarthSurfaceSnapshot::occupiedGeodetic`，覆盖之外返回未知。碰撞与测量取同一版本的外露几何，保留可从下方命中的顶板。海洋在每个水位查询相应海床，例如水下空腔使用空腔底面估算水深。波浪由 FFT 海面模块推进。天空照明使用 VSG、UTC 和 WGS84 锚点，云层由环境模块的程序化密度场或层片渲染。

从本地 DEM 和影像生成台北区域的独立工程可执行以下命令。输出目录须为新目录。最后六个参数是中心纬度、经度、最低和最高 LOD、每级瓦片半径以及填实体的底部高度米数。底部应低于区域内的最低量化地表。转换保留源数据间距与垂直基准声明。DEM 转换产生的地下实体是工程填充，地质洞穴需要单独的数据源或手工编制的多段剖面。

```bash
./build_all.sh --build-dir build/voxel-release --test -- -DVSGRAYSIM_WITH_ROCKY=OFF
VSGRAYSIM_EARTH_DATA_ROOT=/home/mte/EarthData/World.earth.tms_repo \
  build/voxel-release/earth_voxel_bake config/earth/global-voxel.map.json \
  build/earth-packages/taipei-native 25.078 121.772 9 12 5 -12000
```

将输出目录整体复制到目标机后打开其中的 `earth.scene.json`。该工程在运行时直接读取体素瓦片。三套软件的无 Rocky 构建仍包含用于其他格式和导入的 GDAL、PROJ 支持。复测独立包切换帧可设置 `VSGRAYSIM_BENCH_EARTH_SCENE` 为生成的场景绝对路径，运行无 Rocky 构建下的 `gui/sim_earth_gui_tests --native-stream-benchmark`。

#### 台北原生体素包实测

2026-11-12 使用上述命令生成了 484 块瓦片，LOD 9–12，共约 14 MiB。RTX 4060 Laptop、驱动 580.178.04、Release 无 Rocky 构建，运行时取消原始栅格数据根。960 × 640 视口与 64 × 48 MWIR 共用设备，每步校验显示、物理、测量的地表版本和产品时刻。下表的 100 个样本均为真实提交帧，包含物理、着色测量、回读、绘制、呈现提交和绘制完成栅栏。

| GPU 运行条件 | 切换 P50 | 切换 P95 | 最慢切换 | 超过 33 ms |
| --- | ---: | ---: | ---: | ---: |
| 107 次 GPU 进程采样均未观察到其他计算进程 | 19.76 ms | 26.75 ms | 37.07 ms | 2 / 100 |
| 采样观察到其他计算进程共享 GPU | 17.15 ms | 203.93 ms | 248.68 ms | 22 / 100 |

首行运行中，54 次 LOD 切换的 P95 为 26.75 ms，46 次同级跨块的 P95 为 27.23 ms。1,558 个仿真步的产品帧号连续。各切换点地形与 Bullet 的最大高程差为 3.12 mm。首行满足 33 ms 的 P95 指标，共享 GPU 的运行超出该指标。设置 `VSGRAYSIM_BENCH_SWITCH_CSV` 可导出各次切换的单调时钟起止纳秒数、地表版本和 LOD 变化标志，以便与 GPU 采样对齐。

三套软件的截图从交换链颜色附件回读。仓库管理的 VSG 在 Vulkan 报告 `VK_SUBOPTIMAL_KHR` 时保留已取得的图像并完成呈现，避免连续取图耗尽交换链。此改动的源码摘要和原因记录在 `third_party/upstream.lock.json`。

### 轨道视图与跨洲浏览

全局体素工程在非空的两个半球根瓦片上生成 WGS84 整球表面，颜色读取同一数据包的 RGB 影像，昼夜由场景的 UTC 太阳方向决定。场景设计、仿真主控和显控的三维视口都使用 `EarthViewFeature`。在视口内按 **Home** 从当前关注点拉到轨道，滚轮连续缩放，平移可跨越经度换日线。**End** 恢复场景初始视角。

在仓库根目录运行下列命令生成全局工程。源数据须能覆盖两个半球，输出目录须为新目录。

```bash
VSGRAYSIM_EARTH_DATA_ROOT=/home/mte/EarthData/World.earth.tms_repo \
  build/voxel-release/earth_voxel_bake config/earth/global-voxel.map.json \
  build/earth-packages/voxel-world 25.078 121.772 9 12 5 -12000 --global-roots
```

打开生成目录中的 `earth.scene.json`。本例含 654 块原生体素瓦片，约 36 MiB。第 0–3 级覆盖全球，第 9–12 级覆盖台北区域。跨洲降到地表时，视口优先读取当地已有级别，缺少精细数据时回退到包内的粗级别。独立区域包保持原来的覆盖范围。

仿真工作区继续由仿真步边界发布。其余区域先显示根体素表面，相机靠近远方地点后，后台线程用当地瓦片中心建立新的 ENU 观察坐标，加载并编译近景。绘制时用地理窗口遮掉被近景替换的整球表面，经度按环形计算。场景对象仍保存在原场景坐标中。相机跨洲浏览不会搬动正在运行的刚体或传感器。运动平台跨洲飞行时的动力学坐标系迁移仍需单独实现。

`orbital_catalog` 列出全局工程中每个瓦片的键和文件内容指纹。整球和观察区使用同一地球数据集标识，仿真工作区另带物理和测量共同使用的地表修订号。已收录的瓦片缺失或内容变化会使候选加载失败，已安装的快照继续有效。可通过重新加载场景切换到新的数据集。

原生 RTX 压测可用下列命令复现。轨迹从东亚移至欧洲，降低视距，在阿尔卑斯区域平移，再返回东亚。测试同时推进台北仿真工作区的碰撞、地形提交和测量，并分别统计仿真提交帧和观察区更换帧。三个视口预设也可分别运行 `--orbital-capture 图像路径 预设名`，预设名为 `scene_designer`、`simulation_display`、`earth_display`。

```bash
export VSGRAYSIM_BENCH_EARTH_SCENE="$PWD/build/earth-packages/voxel-world/earth.scene.json"
export QT_QPA_PLATFORM=xcb
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json
build/voxel-release/gui/sim_earth_gui_tests --orbital-stream-benchmark
```

### 2026-11-18：现有全球数据的场景细化

默认参考地图仍使用本机 `VSGRAYSIM_EARTH_DATA_ROOT` 下的真实影像和高程。
`config/earth/default.map.json` 的有界工作区扩大为 60×60 km，使用 129×129 个
高程顶点和 512×512 的同版本影像。该范围用于显示山脉、海岸及区域高差；网格和
纹理的输出尺寸不代表源数据精度。当前源数据在台北锚点的 DEM 间距约 2455.6 m，
影像像素约 9784.0 m，区域实际局部高差约 1968 m。近地画面仍受这些源分辨率限制。
没有加入虚构的微地形或把程序噪声当作正射影像。

影像/高程地图现在从已发布的 `EarthSurfaceSnapshot` 绘制，与地面查询、碰撞使用
相同顶点和三角形；不再另外显示一个独立分页的 Rocky 地形。陆地使用无镜面高光的
漫反射材质，法线按三角形面积加权。参考地图在单次绘制中计算真实影像、太阳/月光、
云影和距离雾，地面没有额外的共面云影层。该着色路径接收体素云影，场景物体的动态
投影仍走原有物体渲染路径。地图包和体素数据格式不变，可破坏地形仍由原有
体素场景负责。此默认参考地图是有界区域；全球轨道浏览继续使用带全球根瓦片的原生
地球包，不以有界区域冒充完整地球。

默认参考地图的 `preview_context_scale: 3` 用同一 DEM 与影像将显示范围从 60×60 km
延伸到 180×180 km。两层使用对齐的网格，发布前逐个核对重叠区高程与位置，在一个连续
网格上绘制。作业区内沿用原版影像，接缝外约 2.7 km 混合到外围影像，最远边缘渐变到天空
的地平线雾色。外圈仅供显示，地面查询、碰撞和传感器仍使用
原 60×60 km 的地表版本。源文件缺失、外圈没有完整覆盖或中心网格不一致时，保留原有
工作区。原生体素地球包仍负责全球轨道视图。

普通 DEM 场景与原生体素地图现在都启用已有 `PlanetWeather` 体素云场，旧的局部
云片显示不会叠加。云团自阴影跨空域后重新采样，并按照实际射线距离插值。中景体素里的空白保留为清空，云边缘按实际的前沿光学深度
接受地形遮挡。中景和远云采用同一椭球高度，云层顶部和底部平滑过渡。近场使用不超过
26 m 的分段采样，远场步长随距离平滑增大，并在宽采样区间内过滤垂直体素层。
空气路径消光使远云自然融入实际天空颜色。天空散射
表保留太阳与视线的相对方位，分别保存 Rayleigh、Mie 与多次散射积分，恢复迎日与
背日天空的区别；大气开关也作用于实际穹顶着色。密度场、扰动求解、传感器绑定与
体素架构保持原有实现。

地球程序提供实际数据精度和区域、近地、云侧视、迎日/背日视角：

```bash
export VSGRAYSIM_EARTH_DATA_ROOT=/home/mte/EarthData/World.earth.tms_repo
build/gui/sim_earth_gui_tests --scene-detail-view
# 保存地球程序实际帧和统计；必须有可用的 X11/Vulkan 图形环境
build/gui/sim_earth_gui_tests --scene-detail-capture build/earth-transitions/verified
# 无窗口检查实际地图数据，而非检查插值后的网格尺寸
build/earth_voxel_bake --inspect config/earth/default.map.json 25.078 121.772
```

`SurfaceQuality` 暴露源间距、有效顶面高差、坡度、影像颜色范围、对比度及地表版本，
地球视口属性名为 `earthSurfaceQuality`。高度统计使用局部切平面向上坐标，不能当作
正高或气压高度。源垂向基准尚未核实的情况通过 `provenance_assumed` 单独标明。

仍需后续细化的部分：约 40 km 水平宏云体素在近景放大时仍可能显出分层，局部
稀疏云驻留范围有限；本轮未用额外假云掩盖该限制。当前美术与数据精度尚不足以达到
DCS 级近地画面。后续高清数据可以通过已有 `GDALElevation`/`GDALImage` 图层接入，
并继续沿同一地表版本传给体素地形、碰撞和传感器。

### 固定爬升航线

运行 `build/gui/sim_earth_gui_tests --scene-detail-view`，点击「近地到高空」即可播放固定航线。
相机从地面上方约 180 m 开始，移动约 2 km，连续穿过云层高度并爬升到局部高度 120 km。
点选其他预设视角可结束回放。

```bash
build/gui/sim_earth_gui_tests --scene-flight-capture build/earth-cloud-flight/review
```

导出保留完整的 49 个渲染帧。相机沿同一轨迹前进，天气仿真时刻固定，便于前后对照。
低仰角射线会穿过内外两个可能的云区，受地面遮挡时缩短光程。
近远采样步长连续变化。在同一地理天气区域内，稀疏云和局部动力学场随场景驻留。

### 云侧穿越航线

在同一地球程序点击「云侧穿越」，由距主云团约 20 km 处接近，掠过云底并穿过稀薄云边，
最后从局部高度 7.1 km 俯视云顶。使用默认地球地图的本地 DEM 和影像。

```bash
build/gui/sim_earth_gui_tests --scene-cloud-pass-capture build/earth-cloud-pass/review
```

原生 X11 窗口导出 65 帧及 `pass-acceptance.json`。固定天气仿真时刻，
检查相机飞行期间的稀疏云图表、GPU 液态水场版本，以及五个距离和高度处同一截面的凝结水柱。
报告同时记录相机进入低密度云水的帧。32 m 稀疏体素负责区域主云团，
公里级和全球图集补充周边天气。GPU 风场和飞机尾流的作用范围取决于局部求解网格的覆盖范围。

对比外围细节时可查看 `pass-020.png` 的侧视和 `pass-064.png` 的云顶。
`wisps-after-dynamics.png` 在侧视相机固定时记录推进 0.12 秒后的同一云团，
报告同时列出薄云水柱在推进前后的统计。

带风验收复用同一路径，以 8 m/s 的场景风和 0.125 秒间隔推进仿真，导出 80 帧。
进入云边前固定相机 16 帧，便于观察气流作用下的变化。

```bash
build/gui/sim_earth_gui_tests --scene-cloud-wind-capture build/earth-cloud-near/wind-review
```

要按实际移动后的云边验证穿越，可运行同一测试程序的云边追踪拍摄。
它沿原带风时间轴推进到 4.5 秒，在已提交的稀疏云水场中寻找光学较薄且两端通往清空的截面，
之后每个仿真步复测入口和出口。拍摄相机依次接近、短暂停住观察风中变化、穿过薄边并离开。

```bash
build/gui/sim_earth_gui_tests --scene-cloud-edge-capture build/earth-cloud-tracked/review
```

`edge-acceptance.json` 把 44 个原生帧与原航线帧号、仿真时刻和云版本对应。
截面由上一个 0.125 秒的已提交状态估算，进入薄云与离开的条件由当前帧的云水重新验证。
采样段在 GPU 云覆盖域之外，使用和显示消光共享的稀疏体素云水。

### 区域云和天气预设

区域快照入口接收 `white`、`storm` 或 `sunset`。白云和厚云使用相同的区域和侧视机位；
厚云另有逆光仰视图，彩霞从朝向晚霞的机位拍摄。区域图同时保存隐藏云色通道的同机位参照，
验收报告记录云的可见面积比例、太阳高度和云层参数。

```bash
build/gui/sim_earth_gui_tests --scene-cloud-region-capture build/earth-cloud-region/white white
build/gui/sim_earth_gui_tests --scene-cloud-region-capture build/earth-cloud-region/storm storm
build/gui/sim_earth_gui_tests --scene-cloud-region-capture build/earth-cloud-region/sunset sunset
```

`many` 预设使用现有区域云图集，在 80 km 范围内分布多个云群。依次保存俯视区域、
从云顶附近看云海和云层高度的视角。

```bash
build/gui/sim_earth_gui_tests --scene-cloud-region-capture build/earth-cloud-many/review many
```

### 单云对象预览

在 Earth 特性的参数中设置 `"cloud_objects": true`，重新应用视口配置即可启用。该模式使用独立的连续密度云团，
每个云团具有自己的形态、含水量、风致位移和生成消散时间。它适用于区域云景和宏观风运动，
需要局部流体扰动的场景使用原体素模式。

地球测试程序在相同机位、分辨率、云量和太阳位置下采集并测量两种模式。
每个机位预热 5 帧，随后记录 15 个同步渲染帧。截图回读在计时段之后完成。

```bash
build/gui/sim_earth_gui_tests --scene-cloud-object-capture build/earth-cloud-objects/objects objects
build/gui/sim_earth_gui_tests --scene-cloud-object-capture build/earth-cloud-objects/voxels voxels
```

单云对象的近景密度增加了随风剪切移动的小云絮和细丝。云层的外围混合区随观察距离平滑过渡，
近距离用更短的光线积分步长解析薄云，远处沿用原形态与步长。

带风飞行测试先从当前密度场选取一段可穿越的薄云边，记录接近、固定机位观察和穿越共 52 帧。
首轮保存 `route.json`，后续传入同一文件便可复用相机坐标与模拟时间。在每段预热后记录实际提交并
同步完成的整帧耗时，随后回读截图。报告同时记录相机处密度，以验证确实经过了薄云边。

```bash
build/gui/sim_earth_gui_tests --scene-cloud-object-wind-flight build/earth-object-wind-flight/before
build/gui/sim_earth_gui_tests --scene-cloud-object-wind-flight build/earth-object-wind-flight/after build/earth-object-wind-flight/before/route.json
```

### 24 小时天空与云光照

太阳、月亮和恒星读取同一 UTC 时钟。穹顶按观测经度和恒星时旋转离线 HYG 星图，
星点根据曙暮光、视线消光和月光逐渐淡入淡出。云层在太阳接近地平线时按云高度计算直射光，
并叠加适合昼夜过渡的天光和月光。银河背景和夜间显示亮度采用视觉近似。

作为地球测试的固定机位场景，2026 年 9 月 26 日以 UTC+08:00 为地方时。快照命令同时推进
天空和云，保存东西向机位的逐时画面，并在日出日落附近加密取样。延时命令固定云的模拟时刻，
以十分钟为间隔推进星历，便于观察同一朵云的变色过程。

```bash
build/gui/sim_earth_gui_tests --scene-sky-day-cycle-capture build/earth-sky-day-cycle/snapshots
build/gui/sim_earth_gui_tests --scene-sky-light-timelapse build/earth-sky-day-cycle/timelapse
```

日月盘按观察者到天体的距离估算视直径。低空盘面加入近地折射和垂直压缩，太阳盘使用临边昏暗，
日月盘周围的散射随大气消光变化。月面结合 IAU 平均极轴、LROC 反照率和 LOLA 高程，
由局部地形坡度和掠射方向决定明暗。测试使用 3.5° 机位复拍高低太阳与满月，并用 1.2°
视场观察上弦附近和娥眉月的明暗交界。

```bash
build/gui/sim_earth_gui_tests --scene-sky-disc-capture build/earth-sky-optics/discs
```

日月光照使用同一套标准大气透射表。区域地形和云沿观察射线积分分子、气溶胶与臭氧的消光，
并叠加太阳和月亮的方向散射。天空 LUT 同样为月亮采样散射，夜景显示使用单独的低照度增益。
云层的路径以可见密度的加权距离估计。长程路径和暮光多次散射属于绘制近似。
地形显示对俯视方向的空气入散射作亮度与饱和度映射，改善现有低动态范围影像在高空的蓝白遮蔽。
校正按视线俯角连续变化，接近地平线时恢复完整雾层。测试在 1.25 km 和 12.6 km 两个固定机位
分别预热 5 帧并计量 15 个同步提交帧，截图回读在计时后完成。

```bash
build/gui/sim_earth_gui_tests --scene-atmosphere-light-capture build/earth-atmosphere-light/compare
```

多云阴天示例在 Earth 和 Sky 特性上同时启用 `overcast`，Earth 采用 `cloud_objects`。
用较大的单云对象形成相互衔接的层积云，云下调整为较暗的中性漫射光，云顶保留太阳照明。
这是区域合成天气预设。地球测试程序采集地面、云底、云下平视和云顶画面，
并检查区域内 25 根垂直光学采样柱至少有 20 根的透过率低于 0.15。

```bash
build/gui/sim_earth_gui_tests --scene-overcast-capture build/earth-overcast/review
```

### 模型表面与毁伤碎块

OSG/IVE 和标准 VSG 模型导入时，将基础颜色、顶点色与 UV 纹理采样到体素外表面。
表面图集沿原模型坐标保存，断裂裁剪后的碎块继续读取各自区域；新暴露的内部切面使用中性材料色。
占用网格、碰撞和断裂机制保持一致，显示与传感器共同读取图集，传感器在滤波前将 sRGB 解码为线性反射率。
外表面还支持曲面贴合、源顶点法线，以及标准 VSG 法线贴图和金属度/粗糙度通道。
自定义源着色器不自动迁移；遗留 OSG 材质没有金属参数时使用涂层默认值。

工坊的“导入体素目标”可以设置自动体素化最长轴格数（默认 512，上限请求会自动适配 400 万格预算），表面颜色默认白色以保留贴图。
“安排损伤”新增碎块径向总冲量，对应 damage 事件的可选 `blast_impulse_ns`（牛顿秒）。
默认 0；非零值按质量分配给此次损伤后的动态碎块，方向从损伤中心向外，固定支撑保留。
这是可控的刚体毁伤冲量，不是流体爆炸压力求解。

地球测试读取现有影像、高程和 MQ-9 OSGB，保存完整、毁伤、飞散、落地的真实 Vulkan 画面，
同时检查每帧 49 条几何/碰撞射线、显示/传感器变换和剩余体素数量。

```bash
build/gui/sim_earth_gui_tests --scene-target-destruction-capture build/earth-target-destruction/textured
```

针对 MQ-9 的最细对比可请求最长轴 512 格，自动适配为 415 格（约 4.83 cm，单体 400 万格预算）；
更细的格数需要结合模型长宽高计算，不能把最长轴格数当作整个体积预算。
输出 `original-detail.png` 和 `intact-detail.png` 采用同一 OSGB 三角形与 UV 来源、相同机位及漫反射灯光，
前者保留原网格，后者是可破坏的体素外表面。曲面阶梯与薄结构仍会有差异。
验证显示直接复用实际视口的对象变换，避免每帧重复创建显示图集；测量仍读取同一表面图集。
帧耗时只统计提交到 GPU 完成，排除截图、射线核验及检查点写盘。

```bash
build/gui/sim_earth_gui_tests --scene-target-destruction-capture build/earth-target-destruction/auto-finest 512
```


### 曲面薄翼与 PBR 表面重建

占用网格、体素数量、固定步长和面键断裂保持不变。导入时将原始外表面网格角点投影到邻域源三角形，
并保留共享角点和源法线。薄翼测试要求重建顶点回到原始倾斜平面，避免用一个整格厚度替代翼面。
损伤后的内切面继续由剩余占用格生成，与原有外皮使用相同角点连接。
显示和传感器读取相同三角网格。刚体边界使用这些角点的局部凸体，内部使用合并盒体，整个目标不会被凸化。
碰撞体允许沿平直方向合并相邻实心格，每个原始角点与合并插值的偏差限为 0.25 mm。
这项局部合并准则不代表总碰撞表面误差；曲面格的凸体近似仍需要单独验证。
接触求解按对象对及法向簇保留实际接触中的最深点和各方向支撑点，避免密集冗余约束。
质量和惯量仍基于体素占用体积，不因外皮重建而重新估计。

VSG PBR 导入支持源顶点法线、normalMap，以及 mrMap 的 G 粗糙度/B 金属度，乘以源 PbrMaterial 因子。
基础色图集使用 sRGB，法线与 MR 图集使用线性 UNORM；法线贴图转换到重建面的切线基。
OSG/IVE 导入保留顶点法线并将传统 shininess 近似转换为粗糙度；没有数据的参数使用默认值。
现有 MQ-9 没有独立法线/MR 贴图，本次使用源顶点法线、涂层默认金属度 0、粗糙度约 0.65。
新暴露切面暂用金属度 0.7、粗糙度 0.8 的外观默认值，不代表实测材料参数。
检查点使用 surface_appearances 表共享外皮数据，并对法线/MR 通道进行游程编码；兼容旧的无外皮检查点。
已有旧检查点继续显示原表面，重新从源模型导入后启用重建。

2026-11-19 的地球测试以同一机位、数据、光照和 415 格/约 4.83 cm 精度复拍，输出目录为
`build/earth-target-surface/accepted`。70 帧覆盖 5.54 秒仿真，每帧验证 49 条射线、显示/测量变换和体素数量守恒。
碰撞与几何测距的观测最大差为 0.01009 m，低于该测试的 0.03 m 容差；碰撞凸体并非三角网格的逐点精确替代。
82123 个初始占用体素毁伤去除 35966 个，剩余 46157 个，形成 4 个目标刚体并发生地面接触。
检查点恢复、倾斜薄面、PBR 通道、毁伤开孔和曲面刚体稳定落地均有回归测试。

同步提交到 GPU 完成的渲染帧中位为 8.07 ms、P95 为 8.54 ms；上一版相同机位渲染中位为 7.03 ms。
推进 80 ms 仿真时间的 CPU 中位为 197.23 ms，包含固定步推进和状态发布，排除截图与射线验收。
该高精度毁伤场景尚未达到实时，不能用单独的渲染耗时推导完整仿真帧率。
纯三角面接触方案中位约 758 ms，最终保留原复合刚体机制；这两个数字用于本轮内部方案评估。

```bash
build/gui/sim_earth_gui_tests --scene-target-destruction-capture build/earth-target-surface/accepted 512
```

### 台北 80 × 80 km 高程体素地形：历史粗数据基线

`config/earth/taipei-80km-coarse.vscene.json` 保留初版台北周边粗高程与影像，地理锚点为
25.078°N、121.772°E。可破坏工作区为东西、南北各 80 km，体素边长 312.5 m，
高度采样为 257 × 257；底部 -8000 m 是仿真实体范围，不是源数据提供的地下地质。
配套 `taipei-coarse.map.json` 提供 120 × 120 km 的显示背景，边界使用相同 312.5 m
节点间距。工作区以外的背景不参加刚体和传感器地形查询，不应被当作已加载的物理区域。

新导入 Earth 对象默认 `source.voxel_surface: "smooth"`，保留既有占用格、破坏与断裂机制，
在剩余实体范围内沿源 DEM 三角面重建地表。原始高程不做平均或夸张；地表法线沿高度场梯度连续，
挖掘切面、坑底和侧壁保留几何法线。静态地形的显示、几何测距及 Bullet 碰撞使用同一外露网格。
移动碎块继续使用既有体素复合刚体和对应外形。显式选择 `"blocks"` 可查看整格顶面；
旧检查点缺少该字段时继续使用 blocks，重新导入才采用新默认值。HD/voxel 切换始终保留当前毁伤占用，
不会恢复已移除的地表。

静态地表与周边背景共用现有地形光照、大气及云影绘制路径，避免普通物体阴影在大面积地面上产生条纹。
地球测试保留正常大气截图，同时用 `VoxelSceneFeature` 的 `terrain_atmosphere: false`
提供关闭地表空气透视的检查视图，以便直接检查台阶和法线；产品默认仍为 true。

现有文件实际最高为 DEM 第 5 级、影像第 3 级，源采样间距约 2455.6 m 和 9784.0 m。
312.5 m 体素/网格是在这些数据之间插值，不会产生真实道路、建筑或细小山沟。
源数据垂直基准缺少独立核验，配置的 `provenance_assumed` 保留这一限制。
局部切平面高度还包含海床与地球曲率，不能直接当作海拔。

原生地球测试保存总览、山脊、平缓区、工作区边界的同机位前后截图，另有 24 帧山地到平缓地表的
相机巡视和毁伤后两种模式的截图。64 个 DEM 节点检查确认地表高度保留；25 条几何/Bullet 射线
检查碰撞一致性，另检查显示背景不会进入物理查询。1500 m 半径的挖掘事件仅用于粗网格毁伤压力验证，
不表示真实爆炸威力。

```bash
VSGRAYSIM_EARTH_DATA_ROOT=/home/mte/EarthData/World.earth.tms_repo \
  VSGRAYSIM_TAIPEI_DATA_ROOT=/home/mte/EarthData/Taipei80km \
  DISPLAY=:1 QT_QPA_PLATFORM=xcb VSGRAYSIM_SOAK_SECONDS=240 build/gui/sim_earth_gui_tests \
  --scene-terrain-hires-capture build/earth-terrain-hires/accepted

ctest --test-dir build --output-on-failure \
  -R '^(voxel_earth_sensor_consistency|voxel_earth_unified_scene|voxel_structural_fracture|earth_voxel_physics|earth_voxel_debris)$'
```

初版粗数据阶段的 RTX 4060 原生验收结果保存在 `build/earth-terrain-80km/accepted`；
上面的当前命令采集粗数据与高清数据对照，当前常规入口已升级为下文的新数据。
整格顶面相对源 DEM 的节点最大偏高约 328.03 m；重建后节点误差小于 5 mm，实际为浮点舍入量级。
几何与 Bullet 射线的内部最大差为 2.47 mm。这些数值衡量系统内部一致性，不代表源 DEM 的地理精度。
毁伤移除 250 格，模式切换前后测试坑底均为局部高度 -500 m。
关闭地表空气透视的检查机位，同步提交到 GPU 完成的渲染中位为 2.56 ms、P95 为 3.24 ms，
原整格版本为 2.11 ms、P95 为 2.52 ms；不含截图回读、几何查询、毁伤重建或检查点写盘，
不能据此推导完整仿真帧率。五项地球传感器、统一场景、断裂、物理与碎块回归均通过。

### 台北公开高清数据包与同机位复拍

`config/earth/taipei-80km-hires.vscene.json` 保留上述 80 × 80 km 范围、锚点、UTC 和
312.5 m 体素占用格，改用公开的 Copernicus GLO-30 DSM 与 Sentinel-2B L2A 真彩影像。
原粗数据配置仍可打开，用于对照。高精度配置使用 1025 × 1025 高度节点，地表间距
78.125 m，3072 × 3072 显示贴图约 26.04 m/像素；外围显示背景使用较低分辨率。
原始 DSM 约 30 m、影像 10 m，显示精度受整个工作区的网格和贴图预算约束，
不能将源分辨率等同于最终近景精度。破坏的最小占用单元仍为 312.5 m。

高程来源是 [Copernicus GLO-30 公开数据](https://registry.opendata.aws/copernicus-dem/)，
使用该镜像的 2021 发布版。它是包含植被、建筑影响的 DSM，不是裸地 DTM。
原数据采用 EGM2008 正高；准备程序通过 [PROJ 的 EGM2008 格网](https://cdn.proj.org/)
计算 `h = H + N`，转为引擎使用的 WGS84 椭球高。本区 N 约 17.66–23.78 m。
海洋取源数据的零正高水面，不含水深，不能用于海床测量。
影像来源为 [Sentinel-2 COG 公开数据](https://registry.opendata.aws/sentinel-2-l2a-cogs/)，
锁定 2024-02-14 的同一次 Sentinel-2B 过境，拼接 UH/UJ/VH/VJ 的七个有效条带。
13 个孤立的全黑无效像元用周围观测颜色插值，并在清单中记录数量。存在大片缺失时程序会失败，
不会用程序噪声、生成图像或低清影像冒充高清纹理。

数据准备工具 `tools/earth/prepare_taipei_hires.py` 将数据存入用户指定目录，不提交大型栅格到源码。
`provenance.json` 保存原始 URL、SHA-256、字节数、日期、源分辨率、垂直转换和署名。
数据集的具体生产日期、影像已有阴影与 DSM 植被影响，仍需结合仿真用途判断。

```bash
# 使用独立的 Python 环境安装准备工具的依赖：numpy requests rasterio pyproj
python3 tools/earth/prepare_taipei_hires.py /home/mte/EarthData/Taipei80km

VSGRAYSIM_EARTH_DATA_ROOT=/home/mte/EarthData/World.earth.tms_repo \
  VSGRAYSIM_TAIPEI_DATA_ROOT=/home/mte/EarthData/Taipei80km \
  DISPLAY=:1 QT_QPA_PLATFORM=xcb VSGRAYSIM_SOAK_SECONDS=240 \
  build/gui/sim_earth_gui_tests --scene-terrain-hires-capture build/earth-terrain-hires/accepted
```

复拍使用粗数据计算出的相同机位、相同时间和光照，before 为原来的平滑体素地形，after 为新数据，
保留山脊、平缓区、总览、边界和巡视截图；另保存正常大气画面与毁伤后 voxel/HD 画面。
计时同时记录渲染帧，以及挖掘事件推进、地表重建和显示准备的单次耗时。
后者是一次性同步提交开销，不能由稳定渲染帧耗时推断，也不等同于单独的 GPU 时间。

地表网格按相同位置和表面角色共享顶点；完整实体内部不再生成不可见的细分顶面，
平直坑底保留精确平面。DEM 外皮与切面分开保存法线，避免共享顶点把坑壁抹圆。
影像加载采用有上限的局部像块缓存，保留线性颜色插值、alpha/NoData 检查和图层回退。
显示、碰撞、传感器、检查点和毁伤仍沿用原有体素场景链路；单体 400 万格、
场景 128 MiB 高程/影像预算保持不变。提高采样上限不会绕过这些资源约束。

2026-11-19 的 RTX 4060 原生复拍结果位于 `build/earth-terrain-hires/accepted`。
相机参数与上一轮 `earth-terrain-80km/accepted` 逐项一致。相同检查机位，粗数据的渲染中位
1.93 ms、P95 2.05 ms，新数据为 4.68 ms、P95 4.89 ms。25 条几何/Bullet 射线最大差
2.82 mm，64 个高度节点均保留，误差为浮点舍入量级；这里衡量内部一致性，不代表 DSM 的绝对测量精度。
新场景约 261 万个外露三角形，包含坑底/底面和侧壁。一次 1500 m 半径压力挖掘移除 254 格，
voxel 与 HD 模式下坑底均为局部高度 -500 m。
首次源导入约 9.73 s；上述毁伤推进和准备约 2897 ms，仍有明显同步卡顿。
稳定渲染耗时排除截图、查询、检查点写盘和毁伤更新。数据细节增加后的同步全区域重建尚未达到实时毁伤要求。
本轮七项回归通过，包含高分辨率影像跨像块插值/透明孔回退、细分地表共享顶点、
挖掘后显示/传感器/碰撞一致性、GPU 地表传感器、断裂、物理和碎块测试。

### 后续默认输入与全球对应区域

场景搭建与地球测试默认使用新数据：`taipei-80km.vscene.json` 和 `taipei-region.map.json`
现已指向与 hires 配置相同的 DSM/RGB；保留 hires 文件名以兼容已有命令。
`default.map.json` 在原全球底图之上优先读取台北新栅格，因此普通地球视图、场景设计、
目标验收和云天空验收也使用新数据。显式粗数据文件只用于旧/新对照，不是日常默认入口。
GUI 会检测本机 `/home/mte/EarthData/Taipei80km`，也可用 `VSGRAYSIM_TAIPEI_DATA_ROOT`
或视口 `taipei_data_root` 覆盖；命令行程序仍应明确设置环境变量。

全球体素地球的 `global-voxel.map.json` 使用同一新数据生成的区域 TMS 覆盖。
`build_taipei_global_tiles.py` 保留原有全球父级瓦片的文件链接，在独立的 `global-tiles`
目录生成 972 个区域高程瓦片和 4032 个区域影像瓦片，不改写粗数据源。
高程采用边界共用的 257 节点格式，影像为 256 像素 RGB，沿用现有全球体素读取器。
默认每瓦片 32 个地形柱、128 个显示纹理像素，初始 LOD 12、最高 LOD 14，
全球模式沿用 5 m 高度量化。台北以外继续使用原全球数据和原基准假定。
区域物理场景的连续 DEM 外皮与全球柱体显示的精度由各自 LOD 决定，不能把两者当作同样的几何精度。

```bash
python3 tools/earth/build_taipei_global_tiles.py \
  /home/mte/EarthData/Taipei80km /home/mte/EarthData/World.earth.tms_repo

VSGRAYSIM_EARTH_DATA_ROOT=/home/mte/EarthData/World.earth.tms_repo \
  VSGRAYSIM_TAIPEI_DATA_ROOT=/home/mte/EarthData/Taipei80km \
  build/tests/sim_earth_surface_tests --taipei-defaults
```

默认源验收要求常规区域地图与新栅格地图的高程、RGB 一致，全球台北 LOD 12/14
确实读取新覆盖，并保留欧洲等区域的原全球数据。实际地表射线与区域高程在对应柱中心的
最大差约 1.94 m，处于全球原有 5 m 量化范围内。它不是统一物理场景的碰撞误差。
地球测试另保存 `build/earth-terrain-hires/default-earth.png` 与 `global-taipei.png`。
小范围可破坏地形示例 `config/voxel/earth-workshop.vscene.json` 也已校正到新高程，
使用 2 m 占用格；45 步验证移除 109 格并完成 HD/voxel 切换。源地形信息仍为约 30 m。

最细 MQ-9 新数据初验曾失败：原测试位置（25.078°N、121.772°E）在新 DSM 中为陡坡，
密集接触使原生测试超时，记录为 `build/earth-terrain-hires/new-data-target.log`。
曾在较平坦的实测地段（25.068°N、121.548°E）诊断，随后触发 256 碰撞子步预算保护，
记录为 `level-site-target.log`。没有保留迁移测试点的改动，正式测试仍使用原机位；
没有修改真实高程、降低目标精度或关闭碰撞来获得通过。这是已知接触稳定性/性能问题，
不能将数据源统一、地形破坏通过视作最细目标刚体已经验收。接触修复及后续原机位复验见下文。

### 建筑、山地与道路进一步精细化的边界

高清 OBJ/IVE 目标批量导入，以及建筑、道路和树木的低人工生成流程，见[高清体素目标与台北地表建模方案](voxel_asset_build_workflow.md)。

在现有体素架构内，可以采用分区、分对象的不同精度，背景继续使用大范围地球，近景使用独立的物理工作区。
下列是后续实现的精度目标，不表示已从卫星影像自动生成这些几何：

| 类别 | 局部体素建议 | 需要的几何来源与处理 |
| --- | --- | --- |
| 建筑 | 0.25–1 m，按建筑尺寸适配预算 | 建筑轮廓与高度，或 OSG/VSG/IVE/BIM 导出的模型；保留墙、楼板、孔洞和支撑，而非整栋实心填充 |
| 山地 | 2–8 m 的局部破坏格 | 当前 30 m DSM 可支撑山脊河谷；真实细小沟坎还需要更细的 DTM/实测数据，插值不产生新地貌 |
| 道路 | 0.25–1 m，按路段装载 | 道路中心线/边界、宽度、标高和路面厚度；路基随 DEM，桥梁须保留独立高程和下方空间 |

建筑和路面可以复用现有模型导入、表面贴图/PBR、占用、断裂、刚体、碰撞和传感器接口。
几何、材质外观、物理参数和数据来源分别记录；缺失的楼高、路宽或材料强度需要标为推定。
已查到[台北都发局官方 I3S 模型介接说明](https://historygis.udd.gov.taipei/urban/map/doc/%E8%87%BA%E5%8C%97%E5%B8%82%E6%94%BF%E5%BA%9C%E9%83%BD%E5%B8%82%E7%99%BC%E5%B1%95%E5%B1%803D%E6%A8%A1%E5%9E%8B%E4%BB%8B%E6%8E%A5%E8%AA%AA%E6%98%8E.pdf)，
其中包含南港软件园区等局部 3D Mesh 和全市 LOD1 建物服务，可作为后续几何来源候选。
说明中的全市积木模型是白模，部分仿真版本使用随机贴图，不能将其称为真实立面纹理。
已实际读取南港 `mesh_202503` 与全市 `LOD1_2025` 的服务元数据，分别为 IntegratedMesh 与 3DObject。
它们声明水平 EPSG:4326、垂直 EPSG:5773（EGM96）；接入时须另做正高到椭球高转换，
不能直接套用地形包的 EGM2008 转换，也不能把服务包围盒直接当成有效建筑高度。
当前引擎已有的模型入口是 OSG/VSG/IVE 等，I3S 还需要单独的转换/分栋与授权范围核查流程，尚未接入。
建筑轮廓与路网尚未接入自动生成流程，影像里看见屋顶或道路不代表已有可破坏的三维实体。
近景必须裁出自己的物理工作区，避免在背景地面上叠加第二套可碰撞地面。
例如 256 × 256 × 128 m、2 m 占用格为 1048576 格，适合现有单体预算；
整片 80 km 区域不能统一细化到建筑精度。细节验收继续使用地球测试程序，检查毁伤后的
显示、碰撞、测距与检查点，并同时计量重建开销和稳定渲染耗时。

### 最细目标接触：质心与惯量修正

2026-11-20 修复保持占用格、表面角点、复合刚体、断裂规则、固定步长和 256 碰撞子步上限。
原问题发生在重建表面凸体的局部坐标：凸体顶点携带了相对格网原点的偏移，子形状变换却为单位矩阵，
Bullet 用子变换计算出的质心因而偏离占用体素质心。施加在体素质心的径向冲量被当成偏心冲量，
碎块尚在空中即出现约 27 rad/s 的异常自转，导致约 240 个细碰撞子步和大量无接触流形处理。

凸体顶点现以所属占用格组中心为原点，子变换补偿相同位移，接触几何位置保持一致。
质量与完整惯量张量从占用格组体积、固有惯量和平行轴项计算，避免近似平面外皮的 AABB 改写材料惯量。
冲量力臂先在双精度模型坐标中减去质心，再转给 Bullet，避免高处或偏移坐标下的浮点消减误差。
局部物理坐标按最近网格原点重定位；接触筛选跳过空流形。
没有增加碰撞外壳厚度、强行限制速度、删掉碎块或放宽子步上限。

回归覆盖旋转、偏移后的重建模型质心冲量无附加转矩，偏心冲量的线/角动量，
自由运动轨迹以及检查点确定性续算。实际目标仍为原 MQ-9，最长轴请求 512 格，适配后约 4.83 cm。
原生验收仍用原台北机位、原毁伤点、半径和总冲量，源 DSM/RGB 与上一轮相同。

只读测距新增有内存上限的三角面 BVH 缓存，命中仍使用原三角面，并保持共边等距时的三角形选择。
缓存按对象寿命、几何修订和重建代号失效，不进入检查点。回归将索引命中与穷举比较，
包含上下表面、共边、原位几何更新和并发读取。
地球验收复用同一内容版本的传感器几何/图集，仅更新刚体变换，并增加真实 Vulkan IR/TV 采集；
IR 测距及温度与相应曝光时刻的历史场景核对。同步渲染计时只采纳实际提交了原生帧的调用。
同时修正独立近景地表的光照输入：隐藏背景天气时，从当前相机与日月提供者更新地表帧，
避免使用未推进的零相机位置而把正确影像渲染成泛白地面。该更新不推进被隐藏的天气场。

```bash
VSGRAYSIM_EARTH_DATA_ROOT=/home/mte/EarthData/World.earth.tms_repo \
  VSGRAYSIM_TAIPEI_DATA_ROOT=/home/mte/EarthData/Taipei80km \
  DISPLAY=:1 QT_QPA_PLATFORM=xcb VSGRAYSIM_SOAK_SECONDS=120 \
  build/gui/sim_earth_gui_tests --scene-target-destruction-capture build/earth-contact-fix/accepted 512
```

`VSGRAYSIM_VOXEL_CONTACT_TRACE=1` 可在离线固定步回放中记录每步子步数、接触生成/筛选/求解耗时、
流形数量、刚体速度、动能和穿透深度。诊断日志不属于场景状态，也不替代原生显示与 GPU 传感器验收。

本轮最终验收记录在 `build/earth-contact-fix/fix-status.json`，原生逐帧结果为
`accepted/target-acceptance.json`，实拍对比为 `accepted/before-after.png`，
录像为 `accepted/destruction-and-landing.mp4`（均相对于 `build/earth-contact-fix/`）。
原机位完成 70 帧、5.54 s 的破坏与接触过程：初始 82123 格，移除 35966 格，
46157 格保留在原目标残部及三个分离碎块中。未超时，未触发子步预算错误。
原生场景与离线固定步回放的最终检查点 JSON 完全一致；后者共 277 个固定步，
最大碰撞子步为 73，最深接触约 6.91 mm。另一处原诊断位置使用真实新 DSM，
完成 500 步、10 s 回放，最大子步为 50，最深接触约 2.06 mm，未修改成水平地面。
这两项均保留 256 子步保护。残部可在真实坡面继续缓慢滑动，不以强制静止作为落地条件。

原生验收逐内容版本比较显示与传感器顶点/索引，逐帧比较刚体变换；
3430 条几何/刚体碰撞射线的最大测距差约 4.86 mm。
Vulkan 实际输出 IR、TV 各 69 帧，IR 命中、距离和温度通过曝光中点历史场景核对，
GPU IR 与对应几何测距最大差约 0.0252 mm。上述均为采样位置的系统内部一致性，
不代表 30 m DSM 或 10 m 影像具有毫米级绝对精度。

| 本轮原生计时 | 中位数 | P95 |
| --- | --- | --- |
| 实际提交帧的同步渲染调用 | 8.13 ms | 15.32 ms |
| 推进 80 ms 仿真所需时间 | 230.81 ms | 682.81 ms |

最细档接触超时与异常子步问题已在上述用例中修复，但该档物理推进仍未实时；
不能用约 8 ms 的渲染时间代替完整仿真帧耗时。录像按 0.08 s 采样的仿真时间播放，
不表示机器能以该速度实时求解。接触生成仍是后续性能优化重点。
最终相关 12 项 CTest 全部通过，覆盖导入、断裂、统一场景、CPU/GPU 地表与传感器、
物理碎块、地球 GUI 和显控上下文，完整输出见 `regressions.log`。

### 最细目标碎块：接触生成与求解性能

后续性能修复沿用约 4.83 cm 的最细 MQ-9、原台北 DSM/RGB、原机位与毁伤事件。
体素占用、重建角点、凸体、三角地表、质量/惯量、0.02 s 固定步、
自适应碰撞步长、256 子步保护和 20 次求解迭代保持一致。

`VoxelTerrainCollision.h` 在当前世界的 dispatcher 中注册复合目标/三角地表候选筛选，
先从地形 BVH 查询目标范围内的真实三角面，再查询目标既有的子形状 DBVT。
同时在地形坐标中检查原凸体/三角面算法相同的 AABB 条件，才创建或调用子碰撞算法。
候选扩展只用于容纳原接触阈值和浮点舍入，不改变任何碰撞形状或 margin。
窄阶段仍是原 Bullet 凸体/凹网格算法。没有三角候选时，已有非空流形仍按原规则刷新；
仅释放无候选且已空的子算法。该缓存只活在当前固定步，检查点仍不依赖隐藏物理状态。

接触筛选改用对象整数序号和逐流形位掩码，避免每个接触反复复制字符串和分配树集合。
原主轴法线分组进一步保留不同法线的支撑面，防止坡面与开挖面被并入同一组。
支撑点与非空约束按稳定规则排序，排除空流形数量及分配顺序对求解排序的影响。
无关节的现有体素世界将有效流形直接交给同一个 Bullet 顺序冲量求解器，
保留原迭代次数、摩擦、恢复系数和 split impulse；有关节时仍走原世界求解入口。
自由飞行也必须调用求解器处理外力冲量，不能因没有接触而省略重力积分。

自动回归将空间筛选与原 Bullet 候选路径在 400 个姿态下逐接触比较，
包含正距离接触、旋转/偏移坐标、地表边界、开挖壁与洞顶；另比较 150 步下落及持续支撑。
原生最细目标验收也在多个时刻比较原始接触集合，继续验证逐版本显示/传感器网格、
逐帧变换、几何/碰撞射线和实际 Vulkan IR/TV 曝光结果。

本轮原生输出在 `build/earth-contact-perf/before/` 与 `accepted/`，运行命令沿用上节，
仅将输出目录替换为 `build/earth-contact-perf/accepted`。
`target-acceptance.json` 的每帧分别记录推进、同步渲染、实际传感器采集及其合计。
合计包括场景发布和毁伤时的 GPU 资源准备，排除截图编码、测试射线校核与人工节拍等待；
不包括启动时的模型导入及初始 GPU 会话创建。IR/TV 验收各为 9 × 7，不能据此推断高清传感器吞吐。
首帧推进 20 ms 并执行毁伤；后续每帧推进 80 ms，另行统计中位数、P95、最大值和预算通过帧数，
避免把首帧拓扑重建或纯渲染时间误称为稳定实时仿真性能。

2026-11-20 最终原生复验完成 70 帧、5.54 s 的破坏/落地过程。
初始 82123 格、移除 35966 格及三个分离碎块均与基线相同；
7 个原生场景时刻共 3986 条原始接触与未加空间筛选的 Bullet 路径一致。
3430 条几何/碰撞射线的最大距离差仍为 4.86 mm，实际 IR/TV 各 69 帧，
GPU IR 与曝光时刻几何测距最大差约 0.0203 mm。
最终原生检查点与独立固定步回放完全一致；原陡坡最大子步为 83，仍低于 256。
另一诊断位置在新 DSM 上完成 10 s 回放，最大子步 50。

| 每推进 80 ms（首帧毁伤另计） | 优化前中位 | 优化后中位 | 优化后 P95 |
| --- | --- | --- | --- |
| 场景推进与发布 | 254.86 ms | 37.08 ms | 156.99 ms |
| 同步渲染提交/等待 | 1.31 ms | 8.14 ms | 8.25 ms |
| 推进＋渲染＋实际 IR/TV 采集 | 260.75 ms | 60.02 ms | 179.64 ms |

上述渲染列如实保留各次运行的同步调用实测值，本轮未修改渲染器或着色器。
完整链路中位约改善 4.3 倍；69 段中 39 段在 80 ms 内完成。
排除首帧毁伤后，推进 5.52 s 仿真合计需约 6.08 s 活跃处理，实时倍率约 0.908，
仍不能称为全程实时。首次毁伤/发布含资源重建约 1.773 s，后段接触（4 s 起）
完整处理中位约 54.69 ms、P95 约 86.24 ms。模型导入、窗口启动和初始传感器会话创建另计。

接触生成/筛选/求解的分段诊断记录在 `accepted-replay.log`；
原固定步回放的接触生成中位约 51.64 ms、求解约 4.10 ms，
优化后分别约 7.38 ms、0.137 ms，流形数量中位由 8561 降为约 892。
这些分段诊断计时不替代上表的原生完整链路统计，详细数值见 `performance-comparison.json`。
原陡坡逐固定步最大接触穿透由基线约 6.91 mm 变为约 10.20 mm；
接触与约束排序、法线支撑保留使轨迹发生变化，没有修改体素或碰撞形状精度。
此残余穿透仍需后续改进，不能只报告平均提速而省略稳定性边界。
从原生最终检查点另续算原陡坡至 10 s，后续最大子步为 3，末态最大质心速度约
0.353 m/s、角速度约 0.064 rad/s，仍有碎块沿真实坡面缓滑；记录为 `mountain-10s.log`。
实拍对比在 `accepted/before-after.png`，双画面对比录像为 `accepted/destruction-comparison.mp4`，
均按相同仿真时刻对齐，不表示录像播放速度就是实时求解速度。
最终相关 12 项 CTest 全部通过，完整输出在 `build/earth-contact-perf/regressions.log`；
地球测试程序、显控、场景编辑器和体素工作台均已重新构建。

## 体素海洋、漂浮与水下观察

入口为 `config/earth/taipei-ocean.vscene.json`，仍使用既有 VoxelScene、占用体素、Bullet、断裂、检查点和地球视口。海水是有密度、流速和 RGB 消光系数的隐式体积，按需查询体素水占比，不为水体生成立方体刚体。固体模型的体素精度保持原样。

`VoxelOcean` 将有效波高、风浪/涌浪、方向和水深转换为确定性谐波；CPU 浮力、水面网格和介质查询共享同一张谐波表。谐波高度、法线和轨道速度满足一致的相位关系，采用有限水深色散。风浪及涌浪分别按有效波高归一化。检查点保存完整波谱，支持重放；GPU 每 256 秒重置相位基准，避免长时间单精度漂移。

固体通过不跨越空洞的占用体素立方长块计算排水量，近水线按波面曲率细分积分，求浸没体积与浮心。浮力和扭矩进入原有 Bullet 子步，外露面产生隐式水阻冲量，因此会自然升沉、横摇、纵摇和随流漂移，而不是直接播放位置动画。细化的是积分区域，未改变占用或碰撞精度。断裂后的刚体沿用各自剩余占用体积；高于水密度的物体下沉并与真实固体海底碰撞。水面本身不是硬碰撞平面。每个模型的物性必须使用合适的体积平均密度，不能直接把钢材密度当作整船平均密度。

水面显示包含随波法线、Fresnel、天空反射、太阳高光与透射。水下显示包含随深度变化的太阳照明、RGB Beer–Lambert 消光、散射、焦散近似、斯涅尔窗口和全反射。光源接入已有天空系统；海洋开启时替换旧平面海洋，避免重复绘制。水下背景也经过水体传输，避免将普通天空/地平线直接画在水中。对象保留原有颜色、法线、金属度和粗糙度图集。

台北样例使用引擎默认的 RGB 吸收与散射，由水面渲染和介质光学查询共同读取。默认系数用于通用模拟，台北当地水体光学参数暂无实测来源。项目有对应水体数据时，可在场景的 `ocean` 中提供系数。

这是面向交互场景的近似：视觉折射使用平面界面的表观深度投影，天空反射不包含船体的完整倒影，焦散为波面曲率近似。目前提供局部船体波包与泡沫尾迹近似；未实现破碎浪流体拓扑、自由飞行喷溅、完整船体流场求解、舱室进水和经过船型标定的附加质量。当前波谱限制为非破碎波；这不是经过实船验证的航海动力学求解器。

`traceVoxelOceanScene` 是介质感知 CPU 光学几何查询：波面交点、瞬时法线、Snell 折射/全反射、逐段几何距离、水中距离、光程与 RGB 透过率。其查询对象与刚体/显示使用同一发布快照。超过水域边界或界面追踪预算会显式报错。画面使用的表观深度近似与 CPU 分段折射不构成逐像素光线追踪一致性。原有 GPU 红外/雷达等传感器尚无水下介质模型，因此附带旧 GPU 传感器的场景会明确拒绝启用海洋，避免输出看似正常的错误测量；本次没有声称完成水下多波段辐射标定或声呐。

工作区锚点在台北东北近海（25.22°N，121.8°E）。现有新 DSM 不含实测水深或潮位；局部零水位使用该 DSM 样点对应椭球高 19.8389225 m，仅作局部基准。`assumed-bed` 是明确标注的人工海底，绝不能称为新数据恢复的真实海床。为避免 DSM 海面成为实心地面，海洋场景的工作区以发布的场景固体为碰撞/测量权威。普通陆地场景仍使用原有台北数据链路。

主要配置位于场景顶层 `ocean`：`sea_level_m`、`bottom_m`、`half_extent_m`、`cell_size_m`、`density_kg_m3`、`wave_height_m`、`wave_length_m`、`swell_height_m`、`swell_length_m`、`wind_speed_mps`、`wind_azimuth_deg`、`current_mps`、`absorption_rgb_per_m`、`scattering_rgb_per_m`、`drag_coefficient`。水域边界是有限工作区，应让漂浮对象留在内部；目前跨边界浸没体积不是精确裁剪。暂停后可通过 `kind: ocean` 的场景编辑原子更新参数。

原生地球测试程序：

```bash
cmake --build build --target sim_earth_gui_tests sim_voxel_ocean_tests sim_voxel_ocean_gpu_tests -j 4
ctest --test-dir build -R '^voxel_ocean_' --output-on-failure
# 交互查看：1 水面，2 水线，3 水下，4 仰视，空格暂停
build/gui/sim_earth_gui_tests --scene-ocean-demo
# 固定路线：水面接近、穿过水线、潜航器、海底，再拍傍晚
DISPLAY=:1 QT_QPA_PLATFORM=xcb VSGRAYSIM_SOAK_SECONDS=240 \
  build/gui/sim_earth_gui_tests --scene-ocean-capture build/voxel-ocean/accepted
```

默认日期 2026-09-10，白天 UTC 04:00；额外的傍晚照片覆盖为 UTC 10:00，保持同一物理波浪时刻。路线共 80 帧，仿真时间 3.02–10.92 秒；影片按仿真时间采样播放，不代表实时时钟帧率。`ocean-acceptance.json` 分别记录每次推进、同步渲染及其合计。第一帧推进 20 ms，其余各推进 100 ms；统计不包含截图编码、验证射线和旧 GPU 传感器采集。完整仿真是否实时，必须用推进对应的仿真时长判断，不能将纯渲染帧率当作完整仿真帧率。

单元测试覆盖截切排水体积与浮心、静浮平衡、入水稳定、恢复力矩、破坏后质量/浮力、水底接触、检查点重放、原子失败回滚以及折射/全反射。真实 Vulkan 计算测试在 0、27.375 和 86400.125 秒共比较 3072 个生产着色器高度/法线样点；这是数值链路一致性检验，不是海况实测精度声明。

2026-11-20 本机原生验收保存在 `build/voxel-ocean/accepted`。80 帧、2000 条水面射线通过，546 步独立重放与最终完整检查点相同，14 项相关回归通过。生产 GLSL 与 CPU 的最大波高差 1.25238×10⁻⁵ m，法线差 4.99546×10⁻⁶。渲染中位/P95 为 27.62/40.25 ms；推进中位/P95 为 112.76/157.88 ms（通常推进 100 ms 仿真）；合计中位/P95 为 137.04/189.85 ms。因此当前案例的完整仿真尚未稳定实时。汇总见 `build/voxel-ocean/validation-summary.json`，实拍拼图为 `accepted/ocean-overview.jpg`，路线影片为 `accepted/ocean-route.mp4`。


### 海洋近浪、船体扰动与浮力优化（2026-11-20 后续）

本轮保留台北锚点、原始体素尺寸、占用、断裂、碰撞子步、Bullet 求解器和检查点架构。风浪改为 13 个对数尺度、较宽方向分布的确定性分量，加 3 个涌浪分量，分别保持原有效波高；不再将大部分能量集中于两条近似平行长波。水面光学法线按像素足迹过滤不可分辨频率，近处保留小浪；微纹理仍只是光学粗糙度，不冒充新增物理体素。

靠近水面的运动模型每约 0.4 秒生成一次紧支撑波包，最多保留 24 个，默认寿命 8 秒。波包记录出生位置、时间、振幅、宽度、传播速度、方向和模型速度；强度由水线处的运动、升沉与船宽近似估算。它们随水流平移、向外传播并平滑衰减，加入同一 CPU/GPU 水面高度、法线、浮力和水面光学查询。近船泡沫使用这些已发布波包的方向与年龄，在船后沉积和消散；浪峰泡沫则随局部陡度与风速间歇出现。泡沫是光学覆盖近似，不是单独的刚体或海水密度洞。深水模型不直接生成表面白沫。

这是有反馈的局部扰动近似，尚不是经过船型标定的 Kelvin 尾流、螺旋桨流场或完整 CFD。波包寿命/数量有预算限制；大量模型同时入水时旧波包可能提前被淘汰。新生成的场景采用新风浪分布；旧检查点保留原谐波表，因此重放旧检查点不会擅自更换海况。

浮力降耗包括：

- 只计算水面高度/法线的路径，避免在排水量积分时计算完整流速和速度梯度。
- 精确的长方体截切体积/一阶矩闭式解；病态近轴情况回退到原凸多面体裁切。1000 组随机截切与独立多面体参考比较，测试还覆盖近轴和对称情形。
- 子步内缓存波相位和波包时间系数，使用有界局部相位多项式求压力水面。相位增量小于 0.5，基波高度误差上界为 `sum(amplitude) × 0.5^6 / 720`；波包仍直接求值，正式光学查询和 GPU 波面保持解析值。测试要求缓存高度误差小于 0.05 mm、法线差小于 0.001。
- 水线细分只依赖水平投影的曲率，避免无意义的竖向细分；曲率界使用波包当前剩余振幅，避免已衰减尾浪继续触发高密度积分。切平面误差阈值保留为 2.5 mm，固体占用/接触几何没有降精度。

渲染增加水面深度预写，减少半透明水面自叠加。水下看向水面的路径直接使用完整水中段，避免逐像素重复做界面二分。相机所在点的水位每帧从同一 CPU 场查询一次传给 GPU。近区水面网格更密；以上改动不替换物理碰撞网格。

验收目录 `build/ocean-refinement/before-moving` 与 `build/ocean-refinement/accepted` 使用同一初始体素模型、初速度、80 帧相机路线和 UTC。海况分布变化后刚体运动自然会变化，不能把船姿差异当作机位变化。新增尾流固定机位段为 11.02–14.92 秒：11 秒在质心施加沿模型 −Z 的 1.5 m/s 等效推进脉冲，作为可重放的验证输入，不代表真实发动机或螺旋桨模型。前 80 帧不受该脉冲影响。影片按仿真采样间隔播放，不代表实测帧率。数据和海底来源约束沿用前节。

如需定位 CPU 浮力瓶颈，可在无截图的检查点重放中设置 `VSGRAYSIM_OCEAN_PROFILE=1`；日志分别输出压力积分、水阻和总水动力时间。正常运行仅保留粗粒度耗时返回值，关闭额外剖析日志。完整验收时不应同时运行剖析、构建或其他重负载。

本轮最终原生验收完成 80 帧原路线和 40 帧尾流段，2000 条水面射线通过，746 步独立重放与完整最终检查点一致。14 项相关回归通过。含活跃波包及 24 小时相位重置的 3072 个 GPU 样点：最大高度差 1.90674×10⁻⁵ m，法线差 6.65221×10⁻⁶。

同路线渲染中位/P95 从 17.13/22.02 ms 变为 25.61/38.30 ms；推进中位/P95 从 109.51/155.68 ms 降到 81.74/120.21 ms；推进加同步渲染从 125.85/179.76 ms 降到 107.35/149.24 ms。新增尾流航行段推进中位 107.39 ms、渲染中位 40.53 ms、合计中位 152.17 ms。每行通常推进 100 ms 仿真，故完整仿真仍未稳定实时；渲染统计包含原生同步和显示等待，不是 GPU 单通道时间戳。

在同一份优化前检查点上单独剖析 60 步，本轮曲率衰减优化使船体单次压力积分平均从 6.44 ms 降到 3.54 ms（该剖析对照是本轮中间版本，不能冒充初版海洋的剖析）。完整数字与范围见 `build/ocean-refinement/validation-summary.json`；同机位实拍拼图为 `accepted/before-after.jpg`，尾流照片为 `accepted/wake-overview.jpg`，连续影片为 `accepted/ocean-route-and-wake.mp4`。

### 地球海岸统一场景与海洋实时性验收

海洋测试的默认入口现为 `config/earth/taipei-coast-ocean.vscene.json`：20×20 km 台北东北海岸陆地工作区、32×32 km 局部海洋与真尺寸CG-57军舰。原测试船/潜航器/人工验证海底仅保留于显式reference-boat场景。按 `5` 可切到地球海岸俯视机位。普通 80×80 km 陆地场景入口保持兼容；海岸场景可像其他 `.vscene.json` 一样经 VoxelSceneFeature 加载。`VSGRAYSIM_OCEAN_ISOLATED=1` 显式选择旧独立海洋场景。

陆地继续采用台北新 GLO-30 椭球高 DSM 和 Sentinel-2B 10 m 影像。海岸工作区使用 80 m 占用单元和 40 m HD 曲面采样，已有目标体素尺寸、Bullet 接触几何、子步和求解器未降精度。影像不代表建筑几何。海岸支持既有 HD/体素切换、占用破坏和检查点。

`config/earth/taipei-sea-level.map.json` 引用同一数据准备流程保留的 `raw/us_nga_egm08_25.tif`，给出零正高对应的 EGM2008 椭球高参考。`ocean.reference_surface` 将它解析为不可变局部高度网格，CPU 水位/法线、浮力、介质射线和 GPU 水面使用同一网格；检查点保存解析数据，恢复时不重新读取原始地形。陆地源的 `sea_surface_mask` 删除 DSM 与该基准相差不超过 0.25 m 的零水位列，防止海面成为固体碰撞平面。该分类是由 DSM 推断的水域边界，不是实测岸线、水深或潮位；低于该阈值的低地也受分类限制。旧参考场景的人工验证海底仍有显式标签；默认军舰场景已移除该人工方形海床。水体工作体积的 `bottom_m` 不是实测海床。

外围地球地形按同一水位基准裁去零水面，并保留权威地形工作区的占用孔洞。近岸陆地沿用地球的大气、地形照明和影像链路。局部海洋与地球远海显示衔接，工作区内旧海洋不重复绘制；远海只是显示背景，不代表全球都已加载动态海洋刚体仿真。海面透明通道后使用统一深水辐射背景，避免混入天空形成灰色平板，也避免按水下几何包围盒切换透明度造成矩形色差；水下物体仍保留原始几何与水体消光，不改变碰撞或固体占用。显式旧效果对照保留原包围盒切换方式。

本轮水动力加速保留 2.5 mm 切平面误差阈值。占用 cuboid 的层级树仅汇总完全湿/干的区域；水线叶子继续积分。模型局部的海平面上下界从参考网格的双线性片段精确求得。局部曲率取“全局幅值界”与“Hessian 算子范数＋三阶导数 Lipschitz 界”中较小的有效界，避免无关波相位触发细分。流速查询复用子步内波相位缓存。`Advance` 返回临时浮力积分、水阻和总水动力计时；它们不进入物理快照/检查点。

显示优化包括波包时间系数预计算、仅高度查询、陆地高度范围拒绝、复用水下像素的高度/焦散采样，以及带括区间的 Newton 显示介质交点求解。CPU 测量射线仍使用原精确求交与折射路径。6144 个真实 Vulkan 样点覆盖平水位/地理参考、活跃尾浪和 24 小时相位重置；显示路径交点另外对照 CPU 射线。泡沫在存储的排水方向上形成逐渐扩散的细带，随水流平移、衰减；仍是实时光学近似，不是完整 Kelvin/螺旋桨 CFD。

原生显控限帧改为帧开始到帧开始计时；显式 Vulkan 截图提交独立的带 fence 帧，不再被连续显示中的 32 ms 截图门限饿死。固定路线仍保留 80 帧，随后 40 帧固定机位推进尾流段。新增 60 帧、30 Hz 目标节拍的连续运行：不截图、不丢物理步，单独记录推进 2 秒仿真的实际墙钟时间。该结果与截图视频按仿真时间播放的速度分开报告。

```bash
# 默认加载地球海岸；1/2/3/4/5 切换机位，空格暂停
build/gui/sim_earth_gui_tests --scene-ocean-demo
# 同场景参考路径：关闭层级/局部界/流速缓存，保留参考显示计算
VSGRAYSIM_OCEAN_REFERENCE_HYDRO=1 DISPLAY=:1 QT_QPA_PLATFORM=xcb \
  build/gui/sim_earth_gui_tests --scene-ocean-capture build/ocean-coast/reference-final
# 优化路径
DISPLAY=:1 QT_QPA_PLATFORM=xcb \
  build/gui/sim_earth_gui_tests --scene-ocean-capture build/ocean-coast/accepted-final
```

计时中的“完整”是物理推进与同步原生渲染的合计；不包括截图编码、测试校验射线或尚不支持的 GPU 水下多波段传感器采集。报告分别保留浮力积分与含水阻的总水动力，不能将渲染时间当成仿真时间。最初受并行构建干扰的 `build/ocean-coast/before` 与失败截图运行不用于性能结论。

### 海岸海洋：浪花、入水飞沫、海况与日夜（本轮）

继续使用隐式水体与连续波面；没有改为三维水格流体。目标占用、断裂、刚体、精确碰撞子步和传感器几何链路保持原实现。新增内容如下：

- 船体近水面区域生成贴着船边的泡沫覆盖，尾流泡沫保留发射方向并随流衰减，增加断续细丝；不把这些泡沫当作新的实体体素。
- 碎块包围盒从空气跨入水面时触发带固定种子的短时飞沫和扩散波包；飞沫采用重力下的弹道和风漂移，视觉粒子不增加刚体碰撞，也不声称求解飞溅水量守恒、舱室进水或翻卷破浪。
- 修正太阳镜面高光重复乘 Fresnel 的问题，增加月光镜面分量；细浪法线按屏幕足迹过滤，网格位移按可表达的波长过滤。近场测量与浮力仍查询原始统一波场，光学细浪和远场绘制LOD不提高物理分辨率。
- `ocean.sea_fog` 可配置 `visibility_m`（0关闭，否则至少50m）和 `height_m`（1–1000m，默认45m）。近海海面与地形工作区/外围地形使用同一近海雾层设定，随离海高度指数衰减；岸边湿润色带只作显示，不产生伪造海床或修改固体占用。介质感知CPU查询的`transmission`同时包含海雾消光，并单独返回`marineFogOpticalDepth`；尚未将海雾散射标定为多波段传感器辐射产品。
- 浮力保留2.5mm水线曲率积分阈值。压力积分按固定BVH分区并行，最后固定顺序累加；水流采样也可并行，隐式阻力脉冲与Bullet求解仍按原顺序执行。`VSGRAYSIM_OCEAN_SERIAL_HYDRO=1` 用于同算法串行对照，`VSGRAYSIM_OCEAN_PROFILE=1` 仅用于定位热点，性能验收关闭剖析日志。

原生演示增加按键：`1–5`切换机位，`6`循环平静/普通/较强风浪，`7`切换海雾，`8`切换日间/傍晚/夜晚，空格暂停。海况改变会重新生成统一波谱，浮力、测量和显示均从同一海洋状态取波浪；这组预设是演示海况，不是台北实时天气。

默认演示仍为 `config/earth/taipei-coast-ocean.vscene.json`。新增 `config/earth/taipei-80km-ocean.vscene.json` 将原台北80×80km新数据地形与海洋组合，原陆地入口不受影响。海岸由同一新DSM与EGM2008零正高基准推定，不是实测海岸线。`bottom_m` 为计算水域下界，不代表实测水深。其他区域可沿用相同 `ocean.reference_surface` 和地形 `sea_surface_mask` 机制，前提是两者来自同一垂直基准和场景锚点；不能把台北高程偏移直接套用到其他地区。

```bash
DISPLAY=:1 QT_QPA_PLATFORM=xcb VSGRAYSIM_TAIPEI_DATA_ROOT=/home/mte/EarthData/Taipei80km \
  build/gui/sim_earth_gui_tests --scene-ocean-capture build/ocean-natural/accepted

# 80km海陆混合，实际截图检查HD/体素两种模式
VSGRAYSIM_OCEAN_SCENE=config/earth/taipei-80km-ocean.vscene.json \
  DISPLAY=:1 QT_QPA_PLATFORM=xcb \
  build/gui/sim_earth_gui_tests --scene-ocean-capture build/ocean-natural/taipei80
```

验收新增太阳反光、日落反光、月夜、三种风浪、海雾和破坏后碎块入水机位。天气样例保持冻结几何以检查显示，交互模式可连续推进海况。JSON分别保存浮力压力积分、水阻、总水动力、场景更新和渲染耗时；不能用渲染耗时替代完整仿真耗时，100ms更新统计也不能冒充单个20ms物理步。

### 真尺寸CG-57与近景细节验收

默认 `taipei-coast-ocean.vscene.json` / `taipei-ocean.vscene.json` 已替换为FlightGear公开高细节CG-57巡洋舰；源文件、原贴图、许可与固定提交保存在 `models/CG-57/`。舰体约171.9m长，保留真实米单位。旧小艇只用于显式 `*-reference-boat.vscene.json` 固定负载对照。默认巡洋舰场景没有人工方形海床；计算水域下界不能当作台北海深。

默认 `max_dimension=512`，在每模型4M体素预算内自动得到58×144×478网格、单格约0.3597343m、占用811670格。源角点重建目标使用Bullet GImpact，直接以体素系统发布的规范三角面生成接触皮肤，避免逐格凸包与显示三角面间的差异；质量、惯量和断裂仍由体素占用决定，普通体素盒目标保留原复合盒接触路径。没有提高占用预算、放宽碰撞子步或把原模型另挂为碰撞代理。

旧复合凸体后端的452/453网格边界探测仅作历史诊断，不再限制当前重建表面的接触实现。不能将真尺寸大舰的物理精度称为旧12m小艇约0.094m的精度，也不能把不同模型负载的时间差当作优化收益。整舰密度167.30299kg/m³是按源名义水线配平的体积平均假设，不是实舰钢材密度或经过认证的排水量/稳性数据。

新增原模型自发光材质导入与无损检查点保存，用于夜间航行灯、甲板灯等显示。它不代表已完成灯具照度、聚光投影或多波段传感器辐射标定。近景仍继承原始舰号、甲板标线、窗框与材质；源贴图没有通过插值新增实测细节。`9`在舰艏、舰桥、雷达和飞行甲板机位间切换。

巡洋舰的原生验收单独输出 `naval-acceptance.json`：960×540统计渲染、浮力、水阻与完整更新；1920×1080的五组细节截图另行采集，保持几何状态和UTC冻结，不混入性能数据。新增碎块伴随落水验证；重型军舰与旧小艇的性能分别报告。

大型材质检查点使用紧凑JSON，字段、浮点值、占用、源角点和贴图保持不变，文档预算仍为256MiB。水阻新增保守BVH筛选与原顺序施加；`VSGRAYSIM_OCEAN_EXHAUSTIVE_DRAG=1` 可强制完整遍历对照。详细模型导入时将每个贴图块的四个源角点查询移出逐像素循环，保留相同采样结果。

CG-57稳性限制：均匀密度只配平名义水线，不能校准重心与横摇恢复力。动态路线出现明显侧倾，尚未达到实舰稳性；不锁定横摇来掩盖。高分辨率外观验收因此冻结在初始姿态（detail_tick=0），与动态帧耗时和姿态测试分开。

### 地球联合专题与大模型表面细化

新的统一入口是 `config/topics/taipei-integrated.vtopic.json`，场景设计器“场景专题”菜单和地球测试程序 `--scene-topic-demo` / `--scene-topic-capture` 共用同一文件。源三角片按占用单元裁剪，分类后台任务、数据准备进程隔离、GPU资源复用与预算边界详见 [场景专题运行机制](scene_topic_runtime.md)。占用破坏精度保持原值；当前高细节表面不等于更小的破坏单元。

## 正式体素专题编辑与场景包

原场景设计器现有“体素地形与多波段属性”面板，可编辑地形工作区、导入目标并设置对象/局部体素的光学、红外及雷达参数。新 `.vsimscene` 清单与同名 `.data` 二进制资源目录共同交付正式仿真软件，旧专题仍兼容。格式、预算和大区域驻留边界见 [场景包说明](scene_package.md)。

### 台北红外专题

在场景设计器打开 `config/topics/taipei-thermal.vtopic.json`。新建地形时可勾选“按影像推定红外地表分类”，生成裸土/混合地面、林地、低植被、亮色建成区/矿物和推定水面五类。目标可按对象或局部体素设置 MWIR/LWIR 发射率，以及昼夜、湿润和工作热响应。局部热源支持启动时刻、工作时长和升温/冷却时间常数。保存 `.vsimscene` 可随场景发布这些参数。目标的对象级参数也可随 USD 编辑层往返。

仿真主控打开发布包，沿用显控的装备链接流程订阅 7102（MWIR）和 7103（LWIR）。红外日照使用已有的 DSM 平滑法线，陡直断面保留几何朝向。原始 P0 的四个通道依次为带内辐亮度、模型表面温度（K）、发射率和距离（km）。显控自动拉伸适合观察细节，关闭后使用 265–335 K 黑体等效辐亮度窗，便于同一波段对照。显示处理保留原始数据。

专题使用台北新影像与 DSM，地表类别、热参数和目标散热区均为估算。RGB 影像无法提供实测温度、材料光谱或被植被遮挡的地表细节。样例目标的热循环在数秒内完成，用于联调。地球测试程序可用 `--scene-thermal-capture <专题或场景包> <输出目录>` 生成 Vulkan 图像、原始 P0 和 `thermal-report.json`。

### 显控平台操作台

在大显控选择“平台操作台”，绑定在线平台后，顶部可在“Qt 动态仪表”和“三维座舱 / HUD”间切换。F-16C、AH-64D、A320 和 MQ-9 使用各自的 `config/cockpits` 配置。Qt 按配置绘制姿态、航向、仪表量程和操纵控件，缺失的指示空速、气压高度及发动机数据以空读数显示。MQ-9 的 EO/IR 窗口继续从现有设备链路取得图像。

三维模式默认使用驾驶员眼点。F-16C 的 `photo_panel.mount: fighter` 使用项目自行建模的独立 Vulkan 座舱。显示器边框、按键、仪表外圈、侧台旋钮、右侧操纵杆和左侧油门为三维几何；干净版底图仍提供印字及部分面板细节。侧台区分喷漆、金属和橡胶表面；夜间压低屏幕发光，面板字标的背光与舱内照明分开。ImGui 仪表读取在线遥测，操纵杆和油门按实际执行器值运动，未提供的空速、气压高度和发动机数据保持空读数。

左键可点双 MFD 下方前三个按键，切换 FLT、CTRL 和 ENG。主面板灯光区域的两个实体旋钮分别控制仪表照明和 HUD；右侧 ALT / HDG HOLD 开关需要先用工具栏取得当前平台的飞控租约。保持模式以当前速度、局部高度和航向建立设定；其开关位置取自仿真端回报。各照明档位和 MFD 页是本地显控设置。左侧新加 `LDG GEAR`、`POS` 和 `LDG`，分别连接仿真端的起落架、航行灯和着陆灯状态，同样需要已有飞控租约。起落架手柄命令确认后由仿真时间驱动 3 秒行程，暂停时不会移动。座舱区分收起、行程中与放下锁定；着陆灯仅在选择开启且起落架放下锁定时回报为点亮。这里是简化子系统，未改变地面接触、碰撞体或传感器；外部灯具投光、发动机和电气系统仍未建模。

右键拖动可转头，F1 回正，滚轮调整视场。关闭 HUD 后仪表台和窗外场景继续显示；“外部跟随”切回机外观察。AH-64D、A320 仍使用原有面板，MQ-9 使用机载前视和地面控制台。EO/IR 图像仍在原 Qt 显控。若平台来自体素场景，本机外壳仅在当前驾驶员视口隐藏，体素快照、碰撞及传感器仍使用原数据。

键盘、手柄和 Qt 操纵控件沿用原平台控制租约。舰船和地面车辆仍使用外部跟随，分别显示节和 km/h。

## 单云散射、自阴影、雾与时间重投影

现有单云对象新增沿光路自阴影、三阶近似散射、可选附加雾及半分辨率积分/原尺寸时间重建。密度、风、物理体素与碰撞精度保持原实现。实现、实际地球程序对照、分辨率和性能边界见 [天气渲染说明](cloud_weather_rendering.md)。

海面看天空时的异常云白边，以及反射缓存的透明度修复和原始 Vulkan 截图检查，见 [云透明合成说明](cloud_alpha_compositing.md)。

云团纵向发展、随机鼓包、密度侵蚀与受光层次的后续改进，见 [云形态与日光层次](cloud_natural_shape.md)。这轮同步改变了 CPU/GPU 的云形态密度，保留现有单云对象架构。

近景采样、每云光照密度缓存、相机相对坐标、单云运动重投影与海面尖点处理，见 [近云与海面验证](cloud_edge_performance.md)。其中风航线按固定相机和 UTC 比较，另用 30 Hz 连续采样检查正常时间重投影。

### 海洋天气倒影与持久泡沫

实现、检查点字段、实际 Vulkan 验收及性能计时范围见 [海洋天气与泡沫说明](ocean_weather_rendering.md)。

### 军舰稳性与连续海面真实感

当前军舰默认预设采用明确标注为假定的压载质量分布；连续海面材质、保持精度的水动力优化及原生实拍范围见 [军舰稳性与海面说明](ocean_stability_and_realism.md)。旧检查点保留其原质量属性，重新打开更新后的场景声明才采用新默认值。

### 三维尾焰与烟尘

F-16C 的机外视角在仿真上报较高的实际油门时显示尾焰，喷口按自带 OBJ 的几何位置校准。场景中其他飞机可为对象填写 `visual_jet_exhaust.nozzle_local_m` 来指定显示挂点，不使用 F-16 的挂点作为通用默认值。白色尾烟根据已有飞行位置采样；发射源结束后，已产生的烟仍会随场景风漂移、扩散及淡出。仿真预览对检测到的飞机存活状态变化播放一次闪光、火球和黑烟。粒子仅用于视口，既不生成物理事件，也不改变体素、碰撞或传感器数据。

新版将视口粒子视为局部椭球体积：投影包围面仅限定着色范围，片元按视线与椭球的交点在内部采样。所有粒子复用一张 48³ 的周期三维扰动纹理，并按投影尺寸降低远处烟团采样数。烟与火分别积分消光和发光，烟的向阳衰减按局部密度近似；这套体积数据不进入场景的物理体素、传感器或检查点。

同机位对照可用 `sim_earth_gui_tests --aviation-fx-capture <输出目录>`，通过 `legacy_effects` 对比旧版线框效果。测试用台北区域影像与 DEM，并使用纯视觉合成事件验证；统计的围栏帧耗时不含仿真步进。

体素飞机发布断裂碎块后，视口从同一份仿真快照读取各块的位置、姿态和速度，在各碎块上持续绘制火焰。生成的烟脱离碎块后继续随风漂移。自定义飞机可在模型的 `source` 对象中填写 `"visual_burning_debris": true`。效果使用预设燃烧时长，红外传感器仍按现有材料温度计算。可运行 `sim_earth_gui_tests --aircraft-debris-capture <输出目录>`，查看台北场景的完整机体、空中断裂、翻滚和落地的原生截图，以及分别统计的渲染和物理耗时。

三维航迹使用仿真时间和已发布的位置、速度。相邻观测点之间的曲线保留原始端点，并限制在两点的范围内。缺测或位置突变时从新位置另起一段。相机朝向变化时，航迹条带保持可见宽度，主要用于显示俯冲和垂直上升。持续尾烟沿已观测路径按距离采样，生成后随风漂移并逐渐扩散。体素专题中的自定义飞行模型可在其 `source` 对象中填写 `"visual_flight_trail": true`。运行 `sim_earth_gui_tests --aviation-fx-capture <输出目录>` 可得到使用同一批观测点绘制的直线、曲线原生截图和独立渲染计时。

运行 `sim_earth_gui_tests --aviation-video-frames <输出目录>` 可按 24 帧/秒导出三段 720p 的 Vulkan PNG 连续帧，依次展示飞机尾焰、导弹模型的烟焰和火箭的尾焰烟柱。三段均使用台北新数据和固定 UTC，飞行路径用于画面演示，可从帧序列查看起焰、持续飞行、熄火和残留烟迹。

### 直升机的体素旋翼

运行 `python3 tools/prepare_ah64_voxel.py <场景目录>`，可从自带 AH-64 的原始三角网格生成带贴图的机身、主旋翼和尾旋翼，以及台北场景的低速和高速定义。物体的 `rotor_drive` 按机身局部坐标记录 `carrier`、`pivot_m`、`axis` 和有正负方向的 `rpm`，可选 `phase_deg`。旋翼网格以自身轴心为原点。固定步、分数时刻传感器采样及 Bullet 碰撞子步均按仿真时间计算旋翼相位。当前安装驱动适用于静止或规定运动的机身，受力飞行动力学和断轴脱落属于后续模型。

运行 `sim_earth_gui_tests --ah64-rotor-capture <场景目录> <输出目录>` 可输出同机位原生 Vulkan 对照。高速视频以 60 帧/秒、1/240 秒快门采样。尾桨特写使用 1/500 秒快门，各自附有同一时刻的 1/40 秒长曝光对照。曝光由多个仿真时刻的原生帧合成，另提供瞬时画面。场景转速和轴心取自演示设置与资产几何。

航迹动画可以给各个 `rotor_drive` 配置 `start_seconds` 和 `spin_up_seconds`，两者均以场景仿真秒计。旋翼由静止平滑加速到设置转速，角度按瞬时转速积分。渲染、碰撞、转速回报和挂点声音共享这一时间基准。

运行 `python3 tools/prepare_cg57_deck_takeoff.py build/cg57-deck-takeoff/scene` 生成台北外海的 CG-57 甲板起飞主题，在现有场景入口打开生成的 `cg57-deck-takeoff.vtopic.json`。舰艇和 AH-64 使用 `models` 中的原始资产。14.4 秒的规定航迹包含停机、升速、离舰、悬停和侧向飞离，舰艇甲板及起落架位置以模型几何为准。用 `sim_earth_gui_tests --earth-deck-takeoff <主题文件> <输出目录>` 导出 6 张 Vulkan 截图和验收数据。设置 `VSGRAYSIM_DECK_TAKEOFF_FRAMES=1` 可同时导出 50 帧/秒的连续画面。

### 平台动作预览

`platform_motion_preview` 视口特性提供履带车辆、CG‑57 和 F‑16 三种独立演示。履带环和负重轮按同一段位移更新，军舰天线使用源文件拆出的网格。后坐、开盖、闪光和演示件离架由预览时间线控制，复用体积烟尘与尾焰渲染。履带车辆演示统一使用 M1A2 原模型，CG‑57 的甲板开盖件使用视觉演示件。

舰艇资产先用 `python3 tools/earth/split_cg57_view.py <资产目录>` 拆分。用离线转换工具将目录中的 `hull.ac`、`antenna.ac` 分别导出为 `hull-split.obj`、`antenna-split.obj`，保留源坐标和贴图。可使用 `osgconv -O noRotation` 完成这一步。运行 `sim_earth_gui_tests --platform-motion-capture <M1A2 拆件目录> <CG-57 拆件目录> <输出目录>` 可导出三段原生 PNG 帧和各部件状态的验收数据。运行预览使用已有的模型加载和渲染组件。

M1A2 专用预览使用 `python3 tools/prepare_m1a2_motion.py <资产目录>` 拆出原模型的炮塔、炮管和 18 个轮件，保留原始贴图、UV 和法线。负重轮按网格半径转动，移动的履齿沿原始履带网格提取的带面轮廓循环。运行 `sim_earth_gui_tests --m1a2-motion-capture <资产目录> <输出目录>` 可查看行驶、炮筒回缩和停车的原生帧。可通过 `platform_motion_preview` 的 `tank_assets` 指定生成的资产目录。

视觉参考：[DVIDS 的 F-16 加力实拍](https://www.dvidshub.net/image/9900718/thunderbird-knifes-vertical-afterburner-lit-climb)、[NASA 探空火箭飞行影像](https://svs.gsfc.nasa.gov/13932)、[NASA B-720 受控撞击照片](https://www.nasa.gov/gallery/b-720-controlled-impact-demonstration-cid/)。仅参考可见外观，不把试验画面或数据作为燃烧、推进或毁伤模型。

### 型号动画、座舱信号与体素快照

- `gui/platform_animation` 是纯 C++ 模块。型号动画类以场景时刻和平台状态采样部件姿态、挂点特效，型号在 `PlatformAnimation.cpp` 登记。F-16C 还有座舱型号类，定义座舱硬件的动作曲线和点击对应的信号。
- `gui/viewport3d` 绑定 VSG 节点并绘制模型。可用 `model_id` 选择型号曲线，用 `parts_manifest` 绑定其他模型。绑定文件含 `version: 1`、`body`、`joints`、`attachments`。关节按父节点在前排列，字段为 `name`、可选 `parent`、`pivot_m`、可选 `mesh`。挂点字段为 `name`、可选 `joint`、`position_m`。坐标采用米、Y 向上，各网格和枢轴使用同一绑定姿态坐标。空父节点表示车体根节点，网格路径相对绑定文件。现有 M1A2 履带和轮组继续用 `tank_assets`。
- `CockpitControlSignals` 把型号座舱的系统动作转发给公共飞控通路，由已有租约、平台能力、仿真回报处理控制状态。灯光和舵面动画可以随正式遥测更新。
- 尾烟按平台实例和挂点提交，型号可指定色调预设、烟雾不透明度、寿命及扩散，统一由现有 GPU 粒子路径绘制。体素碎块和天气输入由视口适配器从快照转换。装配、启动、碰撞和传感器以原有体素场景及仿真状态为准。
- 重新运行 M1A2 拆件脚本可生成独立炮塔与炮管。地球测试程序的 `--m1a2-motion-capture` 会核对两侧炮塔角度、炮管俯仰、回缩复位及 18 个轮件。

### 正式 M1A2 状态联调

生成原模型部件后，在仿真主控打开 `config/topics/taipei-m1a2-motion.vtopic.json`。

```bash
python3 tools/prepare_m1a2_motion.py build/platform-motion-assets/m1a2
```

显控连接台北观察相机，再在「地球态势」中选择 M1A2。可调整显示姿态并查看后台确认的修订号、横向角、俯仰角与履带行程。暂停时可设置显示姿态，履带行程从正式世界步的车体位移求得。两端视口采用同一快照的 tick、体素内容修订和显示姿态数据。关节属于显示层，碰撞与传感器仍以原有体素体为准。模型发生结构变化时，视口恢复显示正式体素几何。

配置中 `m1a2_display_rig` 为相对主题文件的拆件目录。正式场景配置的 AH-64 转子和舰艇由已有世界状态产生转速、相位、航速及姿态遥测。F-16C 使用现有飞控通路。

用 `python3 tools/prepare_display_vehicle.py build/platform-motion-assets/display-vehicle` 转换车辆贴图后，`sim_earth_gui_tests --vehicle-effects-capture <输出目录>` 在台北新数据视口显示车辆烟尘和短程体素道具动画。该演示按视口时间轴驱动。

分开验收控制回报与 Vulkan 画面时，可先保存后台确认的四帧状态，再用地球测试程序逐帧读回。保存的描述符引用同一份体素几何。

```bash
QT_QPA_PLATFORM=offscreen build/source-refresh/gui/sim_earth_gui_tests \
  --formal-platform-motion-broker build/source-refresh/app/vsgraysim_sim_control \
  config/topics/taipei-m1a2-motion.vtopic.json build/formal-platform-motion/recorded-broker

QT_QPA_PLATFORM=xcb build/source-refresh/gui/sim_earth_gui_tests \
  --published-platform-motion-capture build/formal-platform-motion/recorded-broker \
  build/formal-platform-motion/recorded-vulkan
```

### 平台运动与海浪、阵风

按航线运动的体素对象可在 `pose_track` 旁设置 `"motion_response": "waves"` 或 `"motion_response": "gusts"`。船体从同一海浪谱采样船底四点的波高，经短时平滑产生升沉、横摇和纵摇。应将基准吃水写入航线。动态船体由现有浮力和水阻积分求解。飞机的阵风以场景天气的风向、风速和降水为输入，根据平台经过的位置和仿真时刻计算。显示、碰撞和传感器共用带响应的刚体位姿。装配的部件也随载体运动。

水下轴件可在 `rotor_drive` 中设置 `"water_jet": true`。轴心浸没且转动时，桨后出现连续的透明旋流。渲染器根据轴转速历史估算排水距离，并叠加载体相对海流的位移。旋流离桨后扩散、减弱，颜色沿用水深和光照参数。尾流系数用于显示近似。

设置 `"motion_response": "waves"` 的船体，迎流侧肩部压入水面时会发出短时浪花。发射点用当前体素船体轮廓和波面求交，发射强度随船体和水沿波面法线的相对速度变化。浪花沿船体外侧扩散，并随水面运动淡出。设置 `VSGRAYSIM_CAPTURE_WITHOUT_HULL_SPRAY=1` 可在地球测试程序中拍摄只关闭这层浪花的对照，保留同航线的船体摇摆和原有泡沫。

海洋专题复用场景声音系统。打开显控「地球态势」的「音效」，或在地球海洋演示中按 M，可听到随风速、浪高、距离和地形遮挡变化的海面声。船舷浪花的拍水声取自同一波面接触事件，根据声源到听者的距离延迟播放。相机或声压测点进入水下后，播放水下环境声，按 1 µPa 显示声压级。空气声压级继续按 20 µPa 显示。海洋声频谱和传播参数为演示估算设置。

船舶使用水上发动机声和船体入水声两条声路，音高和强度按相对水速与海况估算。带 `water_jet` 的潜艇螺旋桨直接以转轴作为声源，用仿真 RPM 驱动周期和强度。水面附近约 1.25 m 的过渡带随局部波面移动，逐步混合空气、水中声路并降低空气声高频。距离、传播时延和遮挡由原有声学链路处理。

```bash
python3 tools/prepare_sonar_crossing.py --marine-audio build/marine-audio-dive/topic
QT_QPA_PLATFORM=xcb build/source-refresh/gui/sim_earth_gui_tests \
  --earth-marine-audio build/marine-audio-dive/topic/taipei-marine-audio.vtopic.json \
  build/marine-audio-dive/capture
```

这条航线从船边穿过波面至潜艇桨后。输出每秒 25 张 Vulkan 帧和同钟采样的 48 kHz 立体声 PCM，以及各时刻的水下混合比例、转速与声路距离。

### 动态目标的撞击压扁

在场景设计器选中体素目标，打开「目标撞击压扁」设置影响半径、起始速度和变形强度。场景文件也可在目标的 `physics` 下添加 `"crush": {"radius_m": 3, "yield_speed_mps": 4, "max_strain": 0.65}`。设置会随检查点保存，在目标进入动态刚体阶段后生效。

首次高于阈值的有效接触从 Bullet 读取冲击位置、法向和冲量。受压区的占用沿法向压缩，并将体素分配到附近有支撑的空位。体素数量、质量和分区传感器材质随重排保留。变形网格与碰撞形状同时发布，符合条件的未变形单元保留原始贴图三角面。配置的参数表示塑性近似强度，适合场景观感及刚体交互。台北演示在机身实际触地并压扁后，沿预设翼根平面分离碎块。

```bash
QT_QPA_PLATFORM=xcb build/source-refresh/gui/sim_earth_gui_tests \
  --aircraft-impact-capture build/impact-crush/taipei
```

用以下命令生成同航线、同 UTC 的开关对照专题，并在原生 Vulkan 地球视口拍摄。

```bash
python3 tools/prepare_environment_motion.py build/environment-motion/topics
python3 tools/prepare_sonar_crossing.py --diesel build/open-submarine/scene
QT_QPA_PLATFORM=xcb build/source-refresh/gui/sim_earth_gui_tests \
  --earth-environment-motion build/environment-motion/topics/ship-response.vtopic.json \
  build/environment-motion/ship-response
QT_QPA_PLATFORM=xcb build/source-refresh/gui/sim_earth_gui_tests \
  --earth-diesel-propeller build/open-submarine/scene/taipei-diesel-submarine.vtopic.json \
  build/environment-motion/propeller
```
